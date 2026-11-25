#include "GhostSimulator.hpp"

#include "GameMode.hpp"
#include "SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace ts {
namespace {

void hide(CCNode* node) {
    if (node) node->setVisible(false);
}

// El fantasma no debe verse ni generar partículas/estelas.
void silence(PlayerObject* g) {
    g->setVisible(false);
    g->m_playEffects = false;
    hide(g->m_regularTrail);
    hide(g->m_waveTrail);
    hide(g->m_shipStreak);
    for (CCNode* p : {static_cast<CCNode*>(g->m_playerGroundParticles), static_cast<CCNode*>(g->m_trailingParticles),
                      static_cast<CCNode*>(g->m_shipClickParticles), static_cast<CCNode*>(g->m_vehicleGroundParticles),
                      static_cast<CCNode*>(g->m_ufoClickParticles), static_cast<CCNode*>(g->m_robotBurstParticles),
                      static_cast<CCNode*>(g->m_dashParticles), static_cast<CCNode*>(g->m_swingBurstParticles1),
                      static_cast<CCNode*>(g->m_swingBurstParticles2)}) {
        hide(p);
    }
}

char const* outcomeName(SimOutcome o) {
    switch (o) {
        case SimOutcome::Survives: return "vive";
        case SimOutcome::Dies: return "muere";
        case SimOutcome::Inconclusive: return "inconcluso";
        case SimOutcome::Skipped: return "sin presupuesto";
    }
    return "?";
}

} // namespace

GhostSimulator::GhostSimulator(PlayLayer* layer) : m_layer(layer) {}

GhostSimulator::~GhostSimulator() {
    for (auto& ghost : m_ghosts) {
        if (!ghost) continue;
        sim::unregisterGhost(ghost.data());
        ghost->removeFromParent();
        ghost.swap(nullptr);
    }
}

void GhostSimulator::beginTick(float stepDt, int budget) {
    if (stepDt > 0.f) m_dt = stepDt;
    m_budget = budget;
    m_stepsUsed = 0;
}

void GhostSimulator::ensureBudget(int minimum) { m_budget = std::max(m_budget, minimum); }

PlayerObject* GhostSimulator::ghostAt(std::size_t index) {
    if (index >= m_ghosts.size()) return nullptr;
    if (!m_ghosts[index]) {
        // Mismos parámetros que usa GD para sus jugadores, pero con
        // playLayer = false: no se registra como jugador del nivel.
        auto* ghost = PlayerObject::create(1, 1, m_layer, m_layer->m_objectLayer, false);
        if (!ghost) return nullptr;
        m_layer->m_objectLayer->addChild(ghost, -100);
        silence(ghost);
        sim::registerGhost(ghost);
        m_ghosts[index] = ghost;
    }
    return m_ghosts[index].data();
}

int GhostSimulator::collectTargets(Slot slot, std::array<Target, 2>& out) {
    auto* p1 = m_layer->m_player1;
    auto* p2 = m_layer->m_player2;
    bool dual = m_layer->m_gameState.m_isDualMode && p2;
    bool twoPlayer = m_layer->m_level && m_layer->m_level->m_twoPlayerMode;

    int n = 0;
    auto add = [&](PlayerObject* real, std::size_t ghostIndex) {
        if (!real || real->m_isDead) return;
        if (auto* g = ghostAt(ghostIndex)) out[static_cast<std::size_t>(n++)] = {real, g};
    };

    if (!dual) {
        add(p1, 0);  // un solo jugador: cualquier botón mueve al P1
    }
    else if (twoPlayer) {
        // 2 jugadores: cada botón controla a su jugador
        if (slot == Slot::P1) add(p1, 0);
        else add(p2, 1);
    }
    else {
        // dual con un solo jugador humano: el click mueve a los dos, así que
        // el click sólo es "bueno" si sobreviven ambos.
        add(p1, 0);
        if (m_correctBoth) add(p2, 1);
    }
    return n;
}

void GhostSimulator::sync(PlayerObject* ghost, PlayerObject* real) {
    // copyAttributes es lo que usa GD para clonar jugadores (dual portal).
    ghost->copyAttributes(real);

    // "Cinturón y tirantes": campos que, si copyAttributes no copiara en
    // alguna versión, romperían la predicción. Si el fantasma deriva en un
    // modo (ver "validate-sim"), el campo que falta casi seguro va acá.
    ghost->setPosition(real->getPosition());
    ghost->m_position = real->m_position;
    ghost->setRotation(real->getRotation());
    ghost->m_yVelocity = real->m_yVelocity;
    ghost->m_gravity = real->m_gravity;
    ghost->m_gravityMod = real->m_gravityMod;
    ghost->m_isShip = real->m_isShip;
    ghost->m_isBird = real->m_isBird;
    ghost->m_isBall = real->m_isBall;
    ghost->m_isDart = real->m_isDart;
    ghost->m_isRobot = real->m_isRobot;
    ghost->m_isSpider = real->m_isSpider;
    ghost->m_isSwing = real->m_isSwing;
    ghost->m_isUpsideDown = real->m_isUpsideDown;
    ghost->m_isOnGround = real->m_isOnGround;
    ghost->m_vehicleSize = real->m_vehicleSize;
    ghost->m_playerSpeed = real->m_playerSpeed;
    ghost->m_isGoingLeft = real->m_isGoingLeft;
    ghost->m_isPlatformer = real->m_isPlatformer;
    ghost->m_isSecondPlayer = real->m_isSecondPlayer;
    ghost->m_isLocked = real->m_isLocked;
    ghost->m_holdingButtons = real->m_holdingButtons;
    ghost->m_isDead = false;

    silence(ghost);  // copyAttributes puede haber copiado visibilidad/efectos
}

void GhostSimulator::step(PlayerObject* ghost) {
    // Orden de un paso de física, igual que los mods de "show trajectory"
    // validados en 2.2x: colisiones -> movimiento -> rotación.
    // >>> PUNTO DE CALIBRACIÓN #1 <<< si "validate-sim" reporta deriva
    // constante, probar update() antes de checkCollisions().
    m_layer->checkCollisions(ghost, m_dt, false);
    if (sim::died(ghost) || sim::inconclusive()) return;
    ghost->update(m_dt);
    ghost->updateRotation(m_dt);
    ++m_stepsUsed;
    --m_budget;
}

SimOutcome GhostSimulator::run(Slot slot, bool press, int holdTicks, int idleTicks) {
    std::array<Target, 2> targets{};
    int n = collectTargets(slot, targets);
    if (n == 0) return SimOutcome::Inconclusive;

    // Ventana por fantasma (en dual cada jugador puede estar en otro modo).
    std::array<ModeProfile, 2> prof{};
    std::array<int, 2> window{};
    int maxWindow = 0;
    for (int i = 0; i < n; ++i) {
        auto ui = static_cast<std::size_t>(i);
        prof[ui] = profileFor(detectMode(targets[ui].real));
        window[ui] = press ? prof[ui].window : idleTicks;
        maxWindow = std::max(maxWindow, window[ui]);
    }
    if (m_budget < maxWindow * n) return SimOutcome::Skipped;

    for (int i = 0; i < n; ++i) sync(targets[static_cast<std::size_t>(i)].ghost, targets[static_cast<std::size_t>(i)].real);

    SimOutcome result = SimOutcome::Survives;
    int endedAt = maxWindow;
    {
        sim::Scope scope;  // al salir de este bloque se restauran orbes/pads
        if (press) {
            for (int i = 0; i < n; ++i) targets[static_cast<std::size_t>(i)].ghost->pushButton(PlayerButton::Jump);
        }

        std::array<bool, 2> airborne{}, done{};
        std::array<int, 2> landedAt{-1, -1};

        for (int k = 0; k < maxWindow && result == SimOutcome::Survives; ++k) {
            bool allDone = true;
            for (int i = 0; i < n; ++i) {
                auto ui = static_cast<std::size_t>(i);
                if (done[ui]) continue;
                auto* g = targets[ui].ghost;

                if (press && k == holdTicks) g->releaseButton(PlayerButton::Jump);
                step(g);

                if (sim::died(g)) {
                    result = SimOutcome::Dies;
                    endedAt = k;
                    break;
                }
                if (sim::inconclusive()) {
                    result = SimOutcome::Inconclusive;
                    endedAt = k;
                    break;
                }

                // Modos de salto: el click es bueno si aterriza vivo.
                if (press && prof[ui].endOnLanding) {
                    if (!g->m_isOnGround) airborne[ui] = true;
                    else if (airborne[ui] && landedAt[ui] < 0) landedAt[ui] = k;
                    if (landedAt[ui] >= 0 && k - landedAt[ui] >= prof[ui].landingMargin) done[ui] = true;
                }
                if (k + 1 >= window[ui]) done[ui] = true;
                allDone = allDone && done[ui];
            }
            if (allDone) {
                endedAt = k;
                break;
            }
        }
    }

    if (m_debug) {
        log::debug("[TickSaver] sim {} slot={} hold={} -> {} (paso {}{}{})", press ? "press" : "idle",
                   static_cast<int>(slot) + 1, holdTicks, outcomeName(result), endedAt,
                   result == SimOutcome::Inconclusive ? ", motivo: " : "",
                   result == SimOutcome::Inconclusive ? sim::inconclusiveReason() : "");
    }
    return result;
}

SimOutcome GhostSimulator::simulatePress(Slot slot, int holdTicks) { return run(slot, true, holdTicks, 0); }

SimOutcome GhostSimulator::simulateIdle(Slot slot, int ticks) { return run(slot, false, 0, ticks); }

void GhostSimulator::validationBefore() {
    m_validating = false;
    auto* real = m_layer->m_player1;
    if (!real || real->m_isDead) return;
    auto* g = ghostAt(0);
    if (!g) return;
    sync(g, real);
    {
        sim::Scope scope;
        step(g);
        if (sim::died(g) || sim::inconclusive()) return;
    }
    m_predictedX = g->getPositionX();
    m_predictedY = g->getPositionY();
    m_validating = true;
}

void GhostSimulator::validationAfter() {
    if (!m_validating) return;
    m_validating = false;
    auto* real = m_layer->m_player1;
    if (!real || real->m_isDead) return;
    float dx = real->getPositionX() - m_predictedX;
    float dy = real->getPositionY() - m_predictedY;
    if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
        log::warn("[TickSaver] deriva del fantasma en 1 tick: dx={:.4f} dy={:.4f} modo={}", dx, dy,
                  kModeNames[idx(detectMode(real))]);
    }
}

} // namespace ts
