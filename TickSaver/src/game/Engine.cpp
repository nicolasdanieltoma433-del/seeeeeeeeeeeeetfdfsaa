#include "Engine.hpp"

#include "../ui/DeathSummaryNode.hpp"
#include "../ui/FloatingButton.hpp"
#include "../ui/HudNode.hpp"
#include "../ui/Palette.hpp"
#include "GameMode.hpp"
#include "LiveLog.hpp"
#include "Settings.hpp"
#include "SimGuard.hpp"
#include "StatsStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/UILayer.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace ts {
namespace {
Engine* g_current = nullptr;
bool g_injecting = false;
std::uint64_t g_nextEngineId = 1;

constexpr float kDefaultDt = 0.25f;  // 240 pasos por segundo, en unidades de GD

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
        case ClickKind::Unnecessary: return "sin efecto";
        case ClickKind::Ignored: return "ignorado";
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

bool Engine::isRealPlayer1(PlayerObject const* player) const {
    return player && player == m_layer->m_player1 && !m_layer->m_isPaused;
}

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

void Engine::applySimConfig() {
    auto const& cfg = settings::get();
    m_scheduler.setPreserveHold(cfg.preserveHold);
    m_sim.setDebugLog(cfg.debugLog);
    m_sim.setCorrectBoth(cfg.correctBoth);
    m_sim.setFullSync(cfg.fullSync);
    m_sim.setRecordPath(cfg.showPath);
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
    p.dropUnneeded = cfg.dropUnneeded && prof.noPressWindow > 0;   // modos de toque
    p.saveReleases = cfg.saveReleases && prof.releaseWindow > 0;   // nave/wave
    p.safetyMargin = std::clamp(cfg.safetyMargin, 0, 3);
    // El buffer de orbes sólo tiene sentido donde un click puede "no hacer
    // nada" (cubo/robot/bola/araña en el aire). En nave/wave siempre hace algo.
    // Necesita "Ignorar clicks innecesarios": si no, un tap sin orbe no se puede descartar.
    p.orbBufferTicks = (cfg.orbBuffer && cfg.dropUnneeded && !prof.holdBased) ? std::clamp(cfg.orbBufferTicks, 4, 60) : 0;

    // Reglas de paso directo. maxDelay = 0 hace que el click pase tal cual (y
    // si había algo retenido se suelta sin juzgar), así el scheduler nunca
    // queda en un estado a medias.
    bool passThrough = !cfg.enabled || (slot == Slot::P2 && !cfg.correctBoth) || !m_sim.ready() ||
                       (cfg.practiceOnly && !m_layer->m_isPracticeMode) ||
                       (m_layer->m_isPlatformer && !cfg.platformer) ||
                       (cfg.ignoreSpam && isSpammyMode(p.mode) && m_rapidInputs[idx(slot)] >= kSpamStreak);
    if (passThrough) {
        p.maxDelay = 0;
        p.orbBufferTicks = 0;
        return p;
    }

    // El adaptativo achica el máximo BASE del modo; el extra del tramo se
    // suma después, porque es una decisión explícita del jugador.
    int base = cfg.baseTicksFor(p.mode);
    if (cfg.adaptive) base = model.adaptiveMax(p.mode, base);
    p.maxDelay = std::clamp(base + cfg.regionExtraAt(percent()), 0, kMaxSaveTicks);
    return p;
}

void Engine::noteInput(Slot slot) {
    // Spam: inputs (apretar o soltar) separados por menos de `gap` ticks.
    auto i = idx(slot);
    PlayerObject* ref = m_layer->m_player1;
    if (slot == Slot::P2 && m_layer->m_gameState.m_isDualMode && m_layer->m_player2) ref = m_layer->m_player2;
    int gap = spamGapTicks(detectMode(ref));
    m_rapidInputs[i] = m_ticksSinceInput[i] < gap ? std::min(m_rapidInputs[i] + 1, 100) : 0;
    m_ticksSinceInput[i] = 0;
}

void Engine::inject(Slot slot, bool push) {
    auto const& cfg = settings::get();
    bool twoPlayer = m_layer->m_level && m_layer->m_level->m_twoPlayerMode;

    // Reinyección directa (opcional): pushButton/releaseButton sobre los
    // jugadores, exactamente lo que hace el fantasma en la simulación.
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

    // Por defecto (como Afterflow): handleButton del juego, al principio del
    // paso. Maneja dual, 2 jugadores y plataformero igual que un click real.
    // Nuestro hook lo deja pasar.
    InjectGuard guard;
    m_layer->GJBaseGameLayer::handleButton(push, static_cast<int>(PlayerButton::Jump), m_lastIsPlayer1[idx(slot)]);
    if (cfg.debugLog) log::debug("[TickSaver] reinyeccion {} en tick {}", push ? "press" : "release", m_tick);
}

// ------------------------------------------------------------------ input ---

bool Engine::onButton(bool down, int button, bool isPlayer1) {
    // Izquierda/derecha del plataformero nunca se tocan: sólo el salto.
    if (button != static_cast<int>(PlayerButton::Jump)) return true;
    if (!playing()) return true;

    auto slot = slotFor(isPlayer1);
    m_lastIsPlayer1[idx(slot)] = isPlayer1;
    m_inputThisTick = true;
    noteInput(slot);
    applySimConfig();

    // ¿El click llega en medio de un paso del jugador real? (click-between-
    // steps). Entonces se aplica en ESTE paso y el fantasma arranca con lo
    // que falta de él. Si no, el juego lo aplica en el paso siguiente.
    float fraction = 0.f;
    bool midStep = m_inRealUpdate || m_stepAccum > 0.f;
    if (m_inRealUpdate && std::abs(m_stepDeltaX) > 0.01f && m_layer->m_player1) {
        fraction = std::clamp((m_layer->m_player1->getPositionX() - m_updateStartX) / m_stepDeltaX, 0.f, .999f);
    }
    else if (m_stepAccum > 0.f && m_avgDt > 0.f) {
        fraction = std::clamp(m_stepAccum / m_avgDt, 0.f, .999f);
    }
    Tick applyTick = midStep ? m_tick : m_tick + 1;

    m_sim.setFirstStepScale(1.f - fraction);
    m_sim.setTick(m_tick);
    m_sim.setPriority(true);  // el chequeo de un input recién hecho tiene su propio presupuesto

    bool forward = true;
    if (down) {
        auto params = paramsFor(slot);
        m_physDownTick[idx(slot)] = applyTick;
        m_physDownMode[idx(slot)] = params.mode;

        // Click sobre un orbe que ya estás tocando: es justo lo que querías
        // hacer, nunca se retrasa (idea de Afterflow).
        if (params.maxDelay > 0 && m_sim.touchingOrb(slot)) {
            params.maxDelay = 0;
            params.orbBufferTicks = 0;
        }

        auto result = m_scheduler.onPhysicalPress(slot, applyTick, params, m_sim);
        if (result.releaseFirst) inject(slot, false);
        forward = result.forwardNow;
    }
    else {
        // Release: aprendemos tu duración de hold (aunque el adaptativo esté
        // apagado, así ya hay datos cuando lo prendas).
        auto held = static_cast<int>(applyTick - m_physDownTick[idx(slot)]);
        if (held > 0) store::adaptive().recordHold(m_physDownMode[idx(slot)], held);

        // En nave/wave el soltar también se evalúa (soltar temprano mata igual
        // que apretar temprano).
        auto params = paramsFor(slot);
        forward = m_scheduler.onPhysicalRelease(slot, applyTick, params, m_sim);
    }

    m_sim.setPriority(false);
    m_sim.setFirstStepScale(1.f);
    processEvents();
    return forward;
}

void Engine::beginRealUpdate(float x) {
    m_inRealUpdate = true;
    m_updateStartX = x;
}

void Engine::endRealUpdate(float x) {
    m_inRealUpdate = false;
    float dx = x - m_updateStartX;
    if (std::abs(dx) > 0.01f) m_stepDeltaX = dx;
}

void Engine::onRealPlayerUpdate(float dt) {
    // Un tick por paso de física. Con click-between-steps el juego puede
    // partir un paso en dos updates más cortos: el primero abre el tick y el
    // resto se acumula hasta completar el dt normal.
    bool startsTick = m_stepAccum <= 0.f;
    if (dt > 0.f) {
        // dt promedio (EMA, como Afterflow). Un dt muy distinto (paso partido)
        // no lo mueve; si se repite 4 veces seguidas es un cambio real (time
        // warp, speedhack) y se adopta.
        if (m_avgDt <= 0.f) m_avgDt = dt;
        if (std::abs(dt - m_avgDt) < m_avgDt * .3f) {
            m_avgDt = m_avgDt * .9f + dt * .1f;
            m_dtOutliers = 0;
        }
        else if (++m_dtOutliers >= 4) {
            m_avgDt = dt;
            m_dtOutliers = 0;
        }
        m_stepAccum += dt;
        if (m_stepAccum >= m_avgDt * .98f) m_stepAccum = 0.f;
    }
    if (startsTick) tick();
}

void Engine::tick() {
    ++m_tick;
    auto const& cfg = settings::get();
    for (auto& t : m_ticksSinceInput) t = std::min(t + 1, 999);

    // Validación del fantasma: comparamos la predicción del tick anterior con
    // donde quedó realmente el jugador.
    if (m_validatePending) {
        m_sim.validationAfter();
        m_validatePending = false;
    }

    m_sim.beginTick(m_avgDt > 0.f ? m_avgDt : kDefaultDt, cfg.simBudgetMs);
    m_sim.setTick(m_tick);
    // Autocalibración: ¿dónde quedó el jugador K ticks después de la última
    // reinyección? (Se anula si hubo otro input en el medio.)
    m_sim.calibrateCheck(m_tick, m_inputThisTick);
    applySimConfig();

    if (playing()) {
        std::array<SlotParams, kSlotCount> params{paramsFor(Slot::P1), paramsFor(Slot::P2)};
        std::vector<InjectCmd> cmds;
        m_scheduler.onTick(m_tick, params, m_sim, cmds);
        for (auto const& cmd : cmds) {
            if (cmd.push) m_sim.calibrateBefore(cmd.slot, m_tick, params[idx(cmd.slot)].expectedHold);
            inject(cmd.slot, cmd.push);
        }
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
        log::debug("[TickSaver] tick {} dt={:.4f} pasos fantasma={} cpu frame={:.2f} ms", m_tick, m_avgDt,
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

        if (ev.kind == ClickKind::Saved && !ev.release) {
            // El adaptativo aprende cuán temprano clickeás; esperar a un orbe
            // es otra cosa y no se mezcla.
            if (!ev.orb) store::adaptive().recordSave(ev.mode, ev.earlyTicks);
            m_lastSaveValid = true;
            m_lastSaveTick = ev.applyTick;
            m_lastSaveMode = ev.mode;
            m_lastSaveEarly = ev.earlyTicks;
            if (cfg.showPath) {
                if (auto const* path = m_sim.lastPathFor(m_tick)) drawPath(*path);
            }
        }
        if (ev.kind == ClickKind::Saved && cfg.saveSound) {
            if (auto* audio = FMODAudioEngine::get()) audio->playEffect("counter003.ogg");
        }
        if (ev.kind == ClickKind::Saved || ev.kind == ClickKind::Unsalvageable || ev.kind == ClickKind::Ignored) {
            markAssisted();
        }

        if (m_hud) m_hud->onClick(ev, m_session.streak(), increased);

        if (ev.kind != ClickKind::Unchecked) {
            LastClick last{true, ev.kind, ev.earlyTicks, ev.mode, ev.release, false, ev.orb};
            livelog::push(fmt::format("{:5.1f}% {}{}: {}", static_cast<double>(pct), kModeNames[idx(ev.mode)],
                                      ev.slot == Slot::P2 ? " (P2)" : "", describeClick(last)));
        }
        if (cfg.debugLog) {
            log::info("[TickSaver] {} P{} {} {}{} +{}t ({:.1f}%)", ev.release ? "soltar" : "click",
                      static_cast<int>(ev.slot) + 1, kModeNames[idx(ev.mode)], kindName(ev.kind), ev.orb ? " (orbe)" : "",
                      ev.earlyTicks, static_cast<double>(pct));
        }
        m_dirty = true;
    }
}

void Engine::drawPath(std::vector<CCPoint> const& points) {
    auto* parent = m_layer->m_objectLayer;
    if (!parent || points.size() < 2) return;
    auto node = CCDrawNode::create();
    if (!node) return;
    auto col = ui::role(settings::get().palette, PaletteRole::Saved);
    ccColor4F color{col.r / 255.f, col.g / 255.f, col.b / 255.f, .85f};
    for (std::size_t i = 1; i < points.size(); ++i) node->drawSegment(points[i - 1], points[i], 1.4f, color);
    node->drawDot(points.back(), 3.f, color);
    parent->addChild(node, 9999);
    node->runAction(CCSequence::create(CCDelayTime::create(1.f), CCRemoveSelf::create(), nullptr));
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
    if (hud) {
        hud->applyConfig(settings::get());
        CCNode* parent = m_layer->m_uiLayer ? static_cast<CCNode*>(m_layer->m_uiLayer) : static_cast<CCNode*>(m_layer);
        parent->addChild(hud, 1000);
        m_hud = hud;
    }
    updateFloatingButton();
    livelog::push(fmt::format("--- {} ---", m_stats.name.empty() ? std::string("nivel") : m_stats.name));
}

void Engine::updateFloatingButton() {
    bool want = settings::get().floatingButton;
    if (want && !m_floatingButton) {
        CCNode* parent = m_layer->m_uiLayer ? static_cast<CCNode*>(m_layer->m_uiLayer) : static_cast<CCNode*>(m_layer);
        if (auto* button = ui::FloatingButton::create()) {
            parent->addChild(button, 1002);
            m_floatingButton = button;
        }
    }
    else if (!want && m_floatingButton) {
        m_floatingButton->removeFromParent();
        m_floatingButton.swap(nullptr);
    }
}

void Engine::onReset() {
    restoreTestMode();  // antes del resetLevel original (start pos usa el flag)
    m_scheduler.reset();
    m_session.startAttempt();
    m_deadThisAttempt = false;
    m_validatePending = false;
    m_lastSaveValid = false;
    m_stepAccum = 0.f;
    m_inRealUpdate = false;
    m_ticksSinceInput.fill(999);
    m_rapidInputs.fill(0);
    ++m_stats.attempts;
    if (m_hud) m_hud->resetAttempt();
    m_dirty = true;
}

void Engine::onRealDeath(PlayerObject*) {
    if (m_deadThisAttempt) return;  // en dual mueren los dos: contamos una vez
    m_deadThisAttempt = true;

    float pct = percent();

    // ¿Dijo "salvado" y moriste dentro de lo que el fantasma daba por seguro?
    // Entonces la predicción falló: lo decimos en vez de esconderlo.
    bool failedSave = false;
    if (m_lastSaveValid) {
        auto since = m_tick - m_lastSaveTick;
        if (since >= 0 && since <= profileFor(m_lastSaveMode).window) {
            failedSave = true;
            ++m_stats.failedSaves;
            m_session.markLastFailed();
            log::warn("[TickSaver] prediccion fallida: salvado +{}t en tick {} ({}), muerte {} ticks despues al {:.1f}%; "
                      "latencia calibrada={}",
                      m_lastSaveEarly, m_lastSaveTick, kModeNames[idx(m_lastSaveMode)], since,
                      static_cast<double>(pct), m_sim.injectLatency());
            livelog::push(fmt::format("{:5.1f}% PREDICCION FALLIDA: salvado +{}t ({}) y muerte {} ticks despues",
                                      static_cast<double>(pct), m_lastSaveEarly, kModeNames[idx(m_lastSaveMode)], since));
        }
        m_lastSaveValid = false;
    }
    if (!failedSave) livelog::push(fmt::format("{:5.1f}% muerte", static_cast<double>(pct)));

    m_scheduler.reset();
    m_ticksSinceInput.fill(999);
    m_rapidInputs.fill(0);
    m_stats.recordDeath(pct);
    m_stats.recordProgress(pct);
    m_stats.recordStreak(m_session.bestStreakThisAttempt());
    m_dirty = true;

    // destroyPlayer corre en medio del paso de física: crear UI o escribir a
    // disco ahí es arriesgado. Lo hacemos en el próximo frame.
    // Capturamos un id y no punteros: si saliste del nivel antes del próximo
    // frame, el Engine ya no existe y el callback no hace nada.
    geode::queueInMainThread([id = m_id, pct, failedSave] {
        auto* engine = Engine::current();
        if (!engine || engine->m_id != id) return;
        if (failedSave && engine->m_hud) engine->m_hud->showFailedSave();
        if (settings::get().showDeathSummary) engine->showDeathSummary(pct);
        engine->saveStats(false);
    });
}

void Engine::onLevelComplete() {
    m_stats.recordProgress(100.f);
    m_stats.recordStreak(m_session.bestStreakThisAttempt());
    m_dirty = true;
    livelog::push("100.0% nivel completado");
    saveStats(true);
}

void Engine::onQuit() {
    restoreTestMode();
    saveStats(true);
    store::saveAdaptive();
    // Con el PlayLayer todavía vivo: los fantasmas apuntan a él.
    m_sim.release();
    if (m_floatingButton) {
        m_floatingButton->removeFromParent();
        m_floatingButton.swap(nullptr);
    }
}

void Engine::onConfigChanged() {
    auto const& cfg = settings::get();
    if (m_hud) m_hud->applyConfig(cfg);
    // Si apagás el modo seguro en medio de un intento que ya fue asistido,
    // ese mismo intento vuelve a poder guardar progreso.
    if (!cfg.safeMode) restoreTestMode();
    updateFloatingButton();
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
