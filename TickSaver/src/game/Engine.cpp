#include "Engine.hpp"

#include "../ui/DeathSummaryNode.hpp"
#include "../ui/HudNode.hpp"
#include "GameMode.hpp"
#include "Settings.hpp"
#include "SimGuard.hpp"
#include "StatsStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/UILayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace ts {
namespace {
Engine* g_current = nullptr;
bool g_injecting = false;
std::uint64_t g_nextEngineId = 1;

// RAII para marcar "este handleButton lo mandamos nosotros".
struct InjectGuard {
    InjectGuard() { g_injecting = true; }
    ~InjectGuard() { g_injecting = false; }
};

char const* kindName(ClickKind k) {
    switch (k) {
        case ClickKind::Clean: return "justo";
        case ClickKind::Saved: return "salvado";
        case ClickKind::Unsalvageable: return "sin salvacion";
        case ClickKind::Unchecked: return "sin evaluar";
    }
    return "?";
}
} // namespace

Engine* Engine::current() { return g_current; }
bool Engine::isInjecting() { return g_injecting; }

Engine::Engine(PlayLayer* layer) : m_layer(layer), m_sim(layer) {
    g_current = this;
    m_id = g_nextEngineId++;
    m_stats = store::loadLevel(layer->m_level);
    m_scheduler.setPreserveHold(settings::get().preserveHold);
    m_session.startAttempt();
    m_lastSave = std::chrono::steady_clock::now();
}

Engine::~Engine() {
    saveStats(true);
    store::saveAdaptive();
    if (g_current == this) g_current = nullptr;
}

bool Engine::owns(GJBaseGameLayer const* layer) const { return layer == m_layer; }

// ---------------------------------------------------------------- helpers ---

Slot Engine::slotFor(bool isPlayer1) const {
    bool twoPlayer = m_layer->m_level && m_layer->m_level->m_twoPlayerMode;
    return (twoPlayer && !isPlayer1) ? Slot::P2 : Slot::P1;
}

float Engine::percent() const { return m_layer->getCurrentPercent(); }

bool Engine::playing() const {
    return m_layer->m_started && m_layer->m_player1 && !m_layer->m_player1->m_isDead && !m_deadThisAttempt &&
           !m_layer->m_hasCompletedLevel && !m_layer->m_levelEndAnimationStarted;
}

SlotParams Engine::paramsFor(Slot slot) const {
    auto const& cfg = settings::get();
    SlotParams p;

    PlayerObject* ref = m_layer->m_player1;
    bool dual = m_layer->m_gameState.m_isDualMode && m_layer->m_player2;
    if (slot == Slot::P2 && dual) ref = m_layer->m_player2;
    p.mode = detectMode(ref);

    auto const& prof = profileFor(p.mode);
    auto const& model = store::adaptive();
    p.expectedHold = cfg.adaptive ? model.expectedHold(p.mode, prof.defaultHold) : prof.defaultHold;

    if (!cfg.enabled || (slot == Slot::P2 && !cfg.correctBoth)) {
        p.maxDelay = 0;  // pasa directo (y si había algo retenido se suelta)
        return p;
    }

    // El adaptativo achica el máximo BASE del modo; el extra del tramo se
    // suma después, porque es una decisión explícita del jugador.
    int base = cfg.baseTicksFor(p.mode);
    if (cfg.adaptive) base = model.adaptiveMax(p.mode, base);
    p.maxDelay = std::clamp(base + cfg.regionExtraAt(percent()), 0, kMaxSaveTicks);
    return p;
}

void Engine::inject(Slot slot, bool push) {
    auto const& cfg = settings::get();
    bool twoPlayer = m_layer->m_level && m_layer->m_level->m_twoPlayerMode;

    // Reinyección directa (por defecto): pushButton/releaseButton sobre los
    // jugadores, exactamente lo que hizo el fantasma en la simulación. Se
    // aplica en ESTE paso, sin depender de cómo 2.2091 procesa la cola de
    // inputs con click-between-steps.
    if (cfg.directInject && !twoPlayer) {
        std::array<PlayerObject*, 2> players{};
        int n = m_sim.realTargets(slot, true, players);
        for (int i = 0; i < n; ++i) {
            auto* p = players[static_cast<std::size_t>(i)];
            if (push) p->pushButton(PlayerButton::Jump);
            else p->releaseButton(PlayerButton::Jump);
        }
        if (cfg.debugLog) log::debug("[TickSaver] reinyeccion directa {} en tick {}", push ? "press" : "release", m_tick);
        return;
    }

    // Ruta del juego: handleButton original (2 jugadores, o si el usuario
    // desactivó la reinyección directa). Nuestro hook la deja pasar.
    InjectGuard guard;
    m_layer->GJBaseGameLayer::handleButton(push, static_cast<int>(PlayerButton::Jump), m_lastIsPlayer1[idx(slot)]);
    if (cfg.debugLog) log::debug("[TickSaver] reinyeccion por handleButton {} en tick {}", push ? "press" : "release", m_tick);
}

// ------------------------------------------------------------------ input ---

bool Engine::onButton(bool down, int button, bool isPlayer1) {
    // Izquierda/derecha del plataformero nunca se tocan: sólo el salto.
    if (button != static_cast<int>(PlayerButton::Jump)) return true;
    if (!playing()) return true;

    auto const& cfg = settings::get();
    auto slot = slotFor(isPlayer1);
    m_lastIsPlayer1[idx(slot)] = isPlayer1;
    m_inputThisTick = true;

    // El click llega entre pasos de física: el juego lo aplicaría en el próximo.
    Tick applyTick = m_tick + 1;

    if (down) {
        auto params = paramsFor(slot);
        m_physDownTick[idx(slot)] = applyTick;
        m_physDownMode[idx(slot)] = params.mode;

        m_scheduler.setPreserveHold(cfg.preserveHold);
        m_sim.setDebugLog(cfg.debugLog);
        m_sim.setCorrectBoth(cfg.correctBoth);
        m_sim.setFullSync(cfg.fullSync);

        // El chequeo del click recién apretado usa su propio presupuesto.
        m_sim.setPriority(true);
        auto result = m_scheduler.onPhysicalPress(slot, applyTick, params, m_sim);
        m_sim.setPriority(false);
        if (result.releaseFirst) inject(slot, false);
        processEvents();
        return result.forwardNow;
    }

    // Release: aprendemos tu duración de hold (aunque el adaptativo esté
    // apagado, así ya hay datos cuando lo prendas).
    auto held = static_cast<int>(applyTick - m_physDownTick[idx(slot)]);
    if (held > 0) store::adaptive().recordHold(m_physDownMode[idx(slot)], held);
    return m_scheduler.onPhysicalRelease(slot, applyTick);
}

void Engine::noteRealUpdate(PlayerObject* player, float dt) {
    if (player == m_layer->m_player1 && dt > m_tickDt) m_tickDt = dt;
}

void Engine::beforeStep(bool isHalfTick) {
    // >>> PUNTO DE CALIBRACIÓN #2 <<< En 2.2091 los "half ticks" existen por el
    // click-between-steps oficial. Contamos sólo pasos completos; verificar
    // con "debug-log" que m_tick avanza ~240 veces por segundo.
    if (isHalfTick) return;
    ++m_tick;

    auto const& cfg = settings::get();
    if (m_tickDt > 0.f) m_lastDt = m_tickDt;
    m_tickDt = 0.f;

    // Validación del fantasma: comparamos la predicción del tick anterior con
    // donde quedó realmente el jugador.
    if (m_validatePending) {
        m_sim.validationAfter();
        m_validatePending = false;
    }

    m_sim.beginTick(m_lastDt, cfg.simBudgetMs);
    m_sim.setDebugLog(cfg.debugLog);
    m_sim.setCorrectBoth(cfg.correctBoth);
    m_sim.setFullSync(cfg.fullSync);
    m_scheduler.setPreserveHold(cfg.preserveHold);

    if (playing()) {
        std::array<SlotParams, kSlotCount> params{paramsFor(Slot::P1), paramsFor(Slot::P2)};
        std::vector<InjectCmd> cmds;
        m_scheduler.onTick(m_tick, params, m_sim, cmds);
        for (auto const& cmd : cmds) inject(cmd.slot, cmd.push);
        processEvents();

        if (m_hud) {
            m_hud->setHolding(std::max(m_scheduler.heldTicks(Slot::P1, m_tick), m_scheduler.heldTicks(Slot::P2, m_tick)));
        }

        if (cfg.validateSim && cmds.empty() && !m_inputThisTick && !m_scheduler.anyHolding()) {
            m_sim.validationBefore();
            m_validatePending = true;
        }
    }
    m_inputThisTick = false;

    if (cfg.debugLog && m_tick % kTicksPerSecond == 0) {
        log::debug("[TickSaver] tick {} dt={:.4f} pasos fantasma={} cpu frame={:.2f} ms", m_tick, m_lastDt,
                   m_sim.stepsUsedThisTick(), m_sim.usedMsThisFrame());
    }
}

void Engine::processEvents() {
    auto const& cfg = settings::get();
    for (auto const& ev : m_scheduler.drainEvents()) {
        float pct = percent();
        bool increased = m_session.onClick(ev);
        m_stats.recordClick(ev, pct);
        m_stats.recordStreak(m_session.streak());

        if (ev.kind == ClickKind::Saved) store::adaptive().recordSave(ev.mode, ev.earlyTicks);
        if (ev.kind == ClickKind::Saved || ev.kind == ClickKind::Unsalvageable) markAssisted();

        if (m_hud) m_hud->onClick(ev, m_session.streak(), increased);
        if (cfg.debugLog) {
            log::info("[TickSaver] click P{} {} {} +{}t ({:.1f}%)", static_cast<int>(ev.slot) + 1,
                      kModeNames[idx(ev.mode)], kindName(ev.kind), ev.earlyTicks, static_cast<double>(pct));
        }
        m_dirty = true;
    }
}

// --------------------------------------------------------------- modo seguro

void Engine::markAssisted() {
    if (!settings::get().safeMode || m_forcedTestMode) return;
    // Mismo truco que una start position: con m_isTestMode GD no guarda
    // progreso, récords ni completados de este intento.
    m_savedTestMode = m_layer->m_isTestMode;
    m_layer->m_isTestMode = true;
    m_forcedTestMode = true;
    if (m_hud) m_hud->setAssisted(true);
    if (settings::get().debugLog) log::info("[TickSaver] modo seguro: este intento no guarda progreso");
}

void Engine::restoreTestMode() {
    if (!m_forcedTestMode) return;
    m_layer->m_isTestMode = m_savedTestMode;
    m_forcedTestMode = false;
    if (m_hud) m_hud->setAssisted(false);
}

// ----------------------------------------------------------- ciclo de vida

void Engine::onLevelReady() {
    m_sim.prepare();  // fantasmas creados acá, nunca en medio de un paso de física

    auto hud = ui::HudNode::create();
    if (!hud) return;
    hud->applyConfig(settings::get());
    CCNode* parent = m_layer->m_uiLayer ? static_cast<CCNode*>(m_layer->m_uiLayer) : static_cast<CCNode*>(m_layer);
    parent->addChild(hud, 1000);
    m_hud = hud;
}

void Engine::onReset() {
    restoreTestMode();  // antes del resetLevel original (start pos usa el flag)
    m_scheduler.reset();
    m_session.startAttempt();
    m_deadThisAttempt = false;
    m_validatePending = false;
    ++m_stats.attempts;
    if (m_hud) m_hud->resetAttempt();
    m_dirty = true;
}

void Engine::onRealDeath(PlayerObject*) {
    if (m_deadThisAttempt) return;  // en dual mueren los dos: contamos una vez
    m_deadThisAttempt = true;

    float pct = percent();
    m_scheduler.reset();
    m_stats.recordDeath(pct);
    m_stats.recordProgress(pct);
    m_stats.recordStreak(m_session.bestStreakThisAttempt());
    m_dirty = true;

    // destroyPlayer corre en medio del paso de física: crear UI o escribir a
    // disco ahí es arriesgado. Lo hacemos en el próximo frame.
    // Capturamos un id y no punteros: si saliste del nivel antes del próximo
    // frame, el Engine ya no existe y el callback no hace nada.
    geode::queueInMainThread([id = m_id, pct] {
        auto* engine = Engine::current();
        if (!engine || engine->m_id != id) return;
        if (settings::get().showDeathSummary) engine->showDeathSummary(pct);
        engine->saveStats(false);
    });
}

void Engine::onLevelComplete() {
    m_stats.recordProgress(100.f);
    m_stats.recordStreak(m_session.bestStreakThisAttempt());
    m_dirty = true;
    saveStats(true);
}

void Engine::onQuit() {
    restoreTestMode();
    saveStats(true);
    store::saveAdaptive();
}

void Engine::onConfigChanged() {
    auto const& cfg = settings::get();
    if (m_hud) m_hud->applyConfig(cfg);
    // Si apagás el modo seguro en medio de un intento que ya fue asistido,
    // ese mismo intento vuelve a poder guardar progreso.
    if (!cfg.safeMode) restoreTestMode();
}

void Engine::resetStats() {
    auto key = m_stats.key;
    auto name = m_stats.name;
    m_stats = LevelStats{};
    m_stats.key = key;
    m_stats.name = name;
    store::deleteLevel(key);
    m_dirty = false;
}

void Engine::showDeathSummary(float pct) {
    if (m_summary && m_summary->getParent()) m_summary->removeFromParent();

    ui::DeathSummary s;
    s.percent = pct;
    s.savesThisAttempt = m_session.savesThisAttempt();
    s.bestStreakThisAttempt = m_session.bestStreakThisAttempt();
    s.bestStreakLevel = static_cast<int>(m_stats.bestStreak);
    s.totalSavesLevel = static_cast<int>(m_stats.saves);
    s.last = m_session.lastClick();

    auto const& cfg = settings::get();
    auto node = ui::DeathSummaryNode::create(s, cfg.palette, cfg.hudScale);
    if (!node) return;
    CCNode* parent = m_layer->m_uiLayer ? static_cast<CCNode*>(m_layer->m_uiLayer) : static_cast<CCNode*>(m_layer);
    parent->addChild(node, 1001);
    m_summary = node;
}

void Engine::saveStats(bool force) {
    if (!m_dirty) return;
    auto now = std::chrono::steady_clock::now();
    // En práctica se muere cada segundo: no escribimos a disco más de una
    // vez cada 3 s salvo al salir / completar.
    if (!force && now - m_lastSave < std::chrono::seconds(3)) return;
    store::saveLevel(m_stats);
    m_lastSave = now;
    m_dirty = false;
}

} // namespace ts
