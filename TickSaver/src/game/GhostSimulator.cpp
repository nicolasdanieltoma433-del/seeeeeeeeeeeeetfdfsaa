#include "GhostSimulator.hpp"

#include "GameMode.hpp"
#include "LiveLog.hpp"
#include "SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace geode::prelude;

namespace ts {
namespace {

char const* outcomeName(SimOutcome o) {
    switch (o) {
        case SimOutcome::Survives: return "vive";
        case SimOutcome::Dies: return "muere";
        case SimOutcome::Inconclusive: return "inconcluso";
        case SimOutcome::Skipped: return "sin presupuesto";
    }
    return "?";
}

char const* kindName(int kind) {
    switch (kind) {
        case 0: return "press";
        case 1: return "idle";
        case 2: return "sin-apretar";
        case 3: return "soltar";
    }
    return "?";
}

void clearLog(CCDictionary* log) {
    if (log) log->removeAllObjects();
}

} // namespace

GhostSimulator::GhostSimulator(PlayLayer* layer) : m_layer(layer) {}

GhostSimulator::~GhostSimulator() { release(); }

void GhostSimulator::release() {
    for (auto& ghost : m_ghosts) {
        if (ghost) sim::unregisterGhost(ghost.data());
    }
    if (m_ghostLayer) m_ghostLayer->removeAllChildrenWithCleanup(true);
    for (auto& ghost : m_ghosts) ghost.swap(nullptr);
    m_ghostLayer.swap(nullptr);
    m_baseChildren = 0;
}

PlayerObject* GhostSimulator::makeGhost() {
    if (!m_ghostLayer) {
        // Capa propia que nunca se agrega a la escena: lo que el juego le
        // cuelgue al fantasma (estelas, partículas, círculos) no se dibuja
        // y se barre después de cada simulación.
        m_ghostLayer = CCLayer::create();
        if (!m_ghostLayer) return nullptr;
    }
    // Mismos parámetros que usa GD para los jugadores de un nivel. El último
    // (playLayer) en true: con false se comporta como un jugador del editor.
    auto* ghost = PlayerObject::create(1, 1, m_layer, m_ghostLayer, true);
    if (!ghost) return nullptr;
    ghost->setPosition({0.f, 105.f});
    ghost->setVisible(false);
    ghost->m_playEffects = false;
    ghost->unscheduleUpdate();  // sólo se mueve cuando lo simulamos nosotros
    ghost->stopAllActions();
    m_ghostLayer->addChild(ghost);
    sim::registerGhost(ghost);
    return ghost;
}

void GhostSimulator::prepare() {
    for (auto& ghost : m_ghosts) {
        if (!ghost) {
            if (auto* g = makeGhost()) ghost = g;
        }
    }
    if (!m_ghosts[0] || !m_ghosts[1]) {
        log::warn("[TickSaver] no se pudieron crear los fantasmas: la correccion queda desactivada en este nivel");
        release();
        return;
    }
    m_baseChildren = m_ghostLayer->getChildrenCount();
}

void GhostSimulator::sweep() {
    if (!m_ghostLayer) return;
    auto* children = m_ghostLayer->getChildren();
    while (children && children->count() > m_baseChildren) {
        auto* child = static_cast<CCNode*>(children->lastObject());
        if (child == m_ghosts[0].data() || child == m_ghosts[1].data()) break;  // orden inesperado: no tocar
        m_ghostLayer->removeChild(child, true);
    }
}

void GhostSimulator::refreshFrame() {
    auto frame = CCDirector::get()->getTotalFrames();
    if (frame != m_frame) {
        m_frame = frame;
        m_usedUs = 0.0;
    }
}

void GhostSimulator::beginTick(float stepDt, float budgetMs) {
    if (stepDt > 0.001f && stepDt < 2.f) m_dt = stepDt;
    m_budgetUs = std::max(0.1, static_cast<double>(budgetMs)) * 1000.0;
    m_stepsUsed = 0;
    refreshFrame();
}

int GhostSimulator::realTargets(Slot slot, bool forInjection, std::array<PlayerObject*, 2>& out) const {
    auto* p1 = m_layer->m_player1;
    auto* p2 = m_layer->m_player2;
    bool dual = m_layer->m_gameState.m_isDualMode && p2;
    bool twoPlayer = m_layer->m_level && m_layer->m_level->m_twoPlayerMode;

    int n = 0;
    auto add = [&](PlayerObject* p) {
        if (p && !p->m_isDead) out[static_cast<std::size_t>(n++)] = p;
    };

    if (!dual) {
        add(p1);  // un solo jugador: cualquier botón mueve al P1
    }
    else if (twoPlayer) {
        add(slot == Slot::P1 ? p1 : p2);  // 2 jugadores: cada botón a su jugador
    }
    else {
        // dual con un solo jugador humano: el click mueve a los dos. Para
        // reinyectar siempre van los dos; para juzgar, P2 sólo si se pide.
        add(p1);
        if (forInjection || m_correctBoth) add(p2);
    }
    return n;
}

int GhostSimulator::collectTargets(Slot slot, std::array<Target, 2>& out) {
    if (!ready()) return 0;
    std::array<PlayerObject*, 2> reals{};
    int n = realTargets(slot, false, reals);
    int written = 0;
    for (int i = 0; i < n; ++i) {
        auto* real = reals[static_cast<std::size_t>(i)];
        std::size_t ghostIndex = real == m_layer->m_player2 ? 1 : 0;
        if (auto* g = m_ghosts[ghostIndex].data()) out[static_cast<std::size_t>(written++)] = {real, g};
    }
    return written;
}

void GhostSimulator::sync(PlayerObject* ghost, PlayerObject* real) {
    ghost->setVisible(false);
    ghost->resetTouchedRings(true);
    // copyAttributes es lo que usa GD para clonar jugadores (dual portal).
    ghost->copyAttributes(real);
    ghost->m_playEffects = false;

    // --- cuerpo, posición y modo -------------------------------------------
    ghost->setScale(real->getScale());
    ghost->setContentSize(real->getContentSize());
    ghost->m_width = real->m_width;
    ghost->m_height = real->m_height;
    ghost->setPosition(real->getPosition());
    ghost->m_position = real->m_position;
    ghost->setRotation(real->getRotation());
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
    ghost->m_gravityMod = real->m_gravityMod;
    ghost->m_isDashing = real->m_isDashing;
    ghost->m_isGoingLeft = real->m_isGoingLeft;
    ghost->m_isPlatformer = real->m_isPlatformer;
    ghost->m_isSecondPlayer = real->m_isSecondPlayer;
    ghost->m_isLocked = real->m_isLocked;
    ghost->m_lastActivatedPortal = real->m_lastActivatedPortal;
    ghost->m_yVelocity = real->m_yVelocity;
    ghost->m_gravity = real->m_gravity;
    ghost->m_fallSpeed = real->m_fallSpeed;
    ghost->m_playerSpeedAC = real->m_playerSpeedAC;

    // Flags de física según la versión del nivel (2.1 vs 2.2). Con otra física
    // el fantasma predeciría muertes que no existen.
    ghost->m_enable22Changes = real->m_enable22Changes;
    ghost->m_enableImpulseFix = real->m_enableImpulseFix;
    ghost->m_fixGravityBug = real->m_fixGravityBug;
    ghost->m_fixRobotJump = real->m_fixRobotJump;
    ghost->m_reverseSync = real->m_reverseSync;
    ghost->m_disablePlayerSqueeze = real->m_disablePlayerSqueeze;
    ghost->m_ignoreDamage = false;

    // --- botón y orbes --------------------------------------------------------
    ghost->m_holdingButtons = real->m_holdingButtons;
    ghost->m_jumpBuffered = real->m_jumpBuffered;
    ghost->m_wasJumpBuffered = real->m_wasJumpBuffered;
    ghost->m_stateJumpBuffered = real->m_stateJumpBuffered;
    ghost->m_stateRingJump = real->m_stateRingJump;
    ghost->m_statePreJump = real->m_statePreJump;
    ghost->m_wasRobotJump = real->m_wasRobotJump;
    ghost->m_touchedRing = real->m_touchedRing;
    ghost->m_touchedCustomRing = real->m_touchedCustomRing;
    ghost->m_touchedPad = real->m_touchedPad;
    ghost->m_touchedGravityPortal = real->m_touchedGravityPortal;
    ghost->m_ringJumpedSincePress = real->m_ringJumpedSincePress;
    ghost->m_usedRingThisStep = real->m_usedRingThisStep;
    ghost->m_touchedRings = real->m_touchedRings;
    ghost->m_usedRings = real->m_usedRings;
    // Qué orbes está tocando AHORA: sin esto el fantasma no puede activar el
    // orbe que estás tocando al clickear y el mod retendría un click perfecto.
    if (ghost->m_touchingRings) {
        ghost->m_touchingRings->removeAllObjects();
        if (real->m_touchingRings) ghost->m_touchingRings->addObjectsFromArray(real->m_touchingRings);
    }

    // --- tiempos internos ---------------------------------------------------------
    ghost->m_totalTime = real->m_totalTime;
    ghost->m_gameModeChangedTime = real->m_gameModeChangedTime;
    ghost->m_lastJumpTime = real->m_lastJumpTime;
    ghost->m_lastFlipTime = real->m_lastFlipTime;
    ghost->m_lastSpiderFlipTime = real->m_lastSpiderFlipTime;
    ghost->m_lastLandTime = real->m_lastLandTime;
    ghost->m_lastGroundedPos = real->m_lastGroundedPos;
    ghost->m_hasStartedFalling = real->m_hasStartedFalling;

    if (m_fullSync) {
        // --- copia extendida (la lista de Afterflow, con los nombres de 2.2091)
        ghost->m_isSideways = real->m_isSideways;
        ghost->m_reverseCooldown = real->m_reverseCooldown;
        ghost->m_maybeReverseSpeed = real->m_maybeReverseSpeed;
        ghost->m_reverseSyncStep = real->m_reverseSyncStep;
        ghost->m_speedMultiplier = real->m_speedMultiplier;
        ghost->m_yStart = real->m_yStart;

        ghost->m_lastPosition = real->m_lastPosition;
        ghost->m_yVelocityBeforeSlope = real->m_yVelocityBeforeSlope;
        ghost->m_yVelocityRelated = real->m_yVelocityRelated;
        ghost->m_diagonalYStep = real->m_diagonalYStep;
        ghost->m_groundYVelocity = real->m_groundYVelocity;
        ghost->m_accelerationOrSpeed = real->m_accelerationOrSpeed;
        ghost->m_isAccelerating = real->m_isAccelerating;
        ghost->m_touchingGround = real->m_touchingGround;
        ghost->m_landedSinceFlip = real->m_landedSinceFlip;
        ghost->m_wasTouchingGround = real->m_wasTouchingGround;
        ghost->m_stateOnGround = real->m_stateOnGround;
        ghost->m_lastSlopeSnapOffset = real->m_lastSlopeSnapOffset;

        ghost->m_isOnSlope = real->m_isOnSlope;
        ghost->m_wasOnSlope = real->m_wasOnSlope;
        ghost->m_slopeVelocity = real->m_slopeVelocity;
        ghost->m_slopeResnapped = real->m_slopeResnapped;
        ghost->m_currentSlope = real->m_currentSlope;
        ghost->m_currentSlope2 = real->m_currentSlope2;
        ghost->m_currentPotentialSlope = real->m_currentPotentialSlope;
        ghost->m_isCollidingWithSlope = real->m_isCollidingWithSlope;
        ghost->m_collidingWithSlopeId = real->m_collidingWithSlopeId;
        ghost->m_slopeIsDownhill = real->m_slopeIsDownhill;
        ghost->m_slopeAngle = real->m_slopeAngle;
        ghost->m_slopeAngleRadians = real->m_slopeAngleRadians;
        ghost->m_slopeRotation = real->m_slopeRotation;
        ghost->m_currentSlopeYVelocity = real->m_currentSlopeYVelocity;
        ghost->m_isCurrentSlopeTop = real->m_isCurrentSlopeTop;
        ghost->m_slopeStartTime = real->m_slopeStartTime;
        ghost->m_slopeEndTime = real->m_slopeEndTime;
        ghost->m_slopeSlidingMaybeRotated = real->m_slopeSlidingMaybeRotated;
        ghost->m_stateDartSlide = real->m_stateDartSlide;

        ghost->m_lastGroundObject = real->m_lastGroundObject;
        ghost->m_preLastGroundObject = real->m_preLastGroundObject;
        ghost->m_maybeLastGroundObject = real->m_maybeLastGroundObject;
        ghost->m_collidedObject = real->m_collidedObject;
        ghost->m_objectSnappedTo = real->m_objectSnappedTo;
        ghost->m_snapDistance = real->m_snapDistance;
        ghost->m_lastCollisionBottom = real->m_lastCollisionBottom;
        ghost->m_lastCollisionTop = real->m_lastCollisionTop;
        ghost->m_lastCollisionLeft = real->m_lastCollisionLeft;
        ghost->m_lastCollisionRight = real->m_lastCollisionRight;
        ghost->m_collidedTopMinY = real->m_collidedTopMinY;
        ghost->m_collidedBottomMaxY = real->m_collidedBottomMaxY;
        ghost->m_collidedLeftMaxX = real->m_collidedLeftMaxX;
        ghost->m_collidedRightMinX = real->m_collidedRightMinX;

        ghost->m_dashX = real->m_dashX;
        ghost->m_dashY = real->m_dashY;
        ghost->m_dashAngle = real->m_dashAngle;
        ghost->m_dashStartTime = real->m_dashStartTime;
        ghost->m_dashRing = real->m_dashRing;
        ghost->m_dashFireFrame = real->m_dashFireFrame;

        ghost->m_stateForce = real->m_stateForce;
        ghost->m_stateForceVector = real->m_stateForceVector;
        ghost->m_affectedByForces = real->m_affectedByForces;
        ghost->m_stateBoostX = real->m_stateBoostX;
        ghost->m_stateBoostY = real->m_stateBoostY;
        ghost->m_stateHitHead = real->m_stateHitHead;
        ghost->m_stateFlipGravity = real->m_stateFlipGravity;
        ghost->m_stateNoAutoJump = real->m_stateNoAutoJump;
        ghost->m_stateScale = real->m_stateScale;
        ghost->m_stateNoWallPush = real->m_stateNoWallPush;
    }

    ghost->m_isDead = false;
}

void GhostSimulator::advance(PlayerObject* ghost, float dt) {
    ghost->update(dt);
    ghost->updateRotation(dt);
}

void GhostSimulator::collide(PlayerObject* ghost) {
    // Los logs de colisión son "qué toqué en este paso": se vacían antes de
    // cada chequeo, igual que en el paso real del juego.
    clearLog(ghost->m_collisionLogTop);
    clearLog(ghost->m_collisionLogBottom);
    clearLog(ghost->m_collisionLogLeft);
    clearLog(ghost->m_collisionLogRight);
    m_layer->checkCollisions(ghost, m_dt, false);
}

SimOutcome GhostSimulator::run(Slot slot, Kind kind, int holdTicks, int windowOverride, Trace* trace, int pressAt,
                               std::vector<CCPoint>* path) {
    using Clock = std::chrono::steady_clock;

    refreshFrame();
    double limitUs = m_priority ? m_budgetUs : m_budgetUs - m_usedUs;
    if (limitUs <= 0.0) return SimOutcome::Skipped;

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
        int w = 0;
        switch (kind) {
            case Kind::Press: w = windowFor(prof[ui], holdTicks) + pressAt; break;
            case Kind::Idle: w = windowOverride; break;
            case Kind::NoPress: w = prof[ui].noPressWindow > 0 ? prof[ui].noPressWindow : windowFor(prof[ui], holdTicks); break;
            case Kind::Release: w = prof[ui].releaseWindow; break;
        }
        if (windowOverride > 0) w = windowOverride;
        window[ui] = w;
        maxWindow = std::max(maxWindow, w);
    }
    // Modo sin corrección de soltar (o ventana vacía): soltar no se juzga.
    if (maxWindow <= 0) return SimOutcome::Survives;

    bool landingExit = (kind == Kind::Press || kind == Kind::NoPress) && trace == nullptr;

    auto start = Clock::now();
    auto elapsedUs = [&] { return std::chrono::duration<double, std::micro>(Clock::now() - start).count(); };
    {
        // La copia de estado también va aislada (copyAttributes puede cambiar
        // de modo y eso no debe tocar la cámara ni el suelo del nivel real).
        sim::SyncScope syncing;
        for (int i = 0; i < n; ++i) {
            auto const& t = targets[static_cast<std::size_t>(i)];
            sync(t.ghost, t.real);
            sim::setCounterpart(t.ghost, t.real);
        }
    }

    SimOutcome result = SimOutcome::Survives;
    int endedAt = maxWindow;
    int steps = 0;
    char const* portal = nullptr;
    {
        sim::Scope scope;  // al salir de este bloque se restauran orbes/pads
        CCRect r1 = targets[0].real->getObjectRect();
        CCRect r2 = n > 1 ? targets[1].real->getObjectRect() : r1;
        sim::setRealRects(true, r1, r2);

        for (int i = 0; i < n; ++i) {
            auto* g = targets[static_cast<std::size_t>(i)].ghost;
            if (kind == Kind::Release) g->releaseButton(PlayerButton::Jump);
            else if (kind == Kind::NoPress && g->m_holdingButtons[static_cast<int>(PlayerButton::Jump)]) {
                g->releaseButton(PlayerButton::Jump);  // "no apretar" = botón suelto
            }
        }
        if (path) {
            path->clear();
            path->push_back(targets[0].ghost->getPosition());
        }

        std::array<bool, 2> airborne{}, done{};
        std::array<int, 2> landedAt{-1, -1};

        for (int k = 0; k < maxWindow; ++k) {
            sim::setStep(k);

            // 1) input y movimiento de todos los fantasmas
            for (int i = 0; i < n; ++i) {
                auto ui = static_cast<std::size_t>(i);
                if (done[ui]) continue;
                auto* g = targets[ui].ghost;
                if (kind == Kind::Press && k == pressAt) g->pushButton(PlayerButton::Jump);
                if (kind == Kind::Press && k == pressAt + holdTicks) g->releaseButton(PlayerButton::Jump);
                advance(g, k == 0 ? m_dt * m_firstStepScale : m_dt);
                ++steps;
            }
            if (sim::anyDied()) {
                result = SimOutcome::Dies;
                endedAt = k;
                break;
            }

            // 2) colisiones (después de moverse, como el paso real)
            for (int i = 0; i < n && !sim::anyDied(); ++i) {
                auto ui = static_cast<std::size_t>(i);
                if (!done[ui]) collide(targets[ui].ghost);
            }
            if (sim::anyDied()) {
                result = SimOutcome::Dies;
                endedAt = k;
                break;
            }
            if (sim::hitPortal()) {
                // Llegó vivo a un portal de modo / dual / teleport o al final.
                // Para "apretar" eso cuenta como que el click sirvió; para
                // decidir si un click sobra no alcanza (podés morir después).
                result = (kind == Kind::Press && k >= pressAt) ? SimOutcome::Survives : SimOutcome::Inconclusive;
                portal = sim::portalReason();
                endedAt = k;
                break;
            }
            if (path) path->push_back(targets[0].ghost->getPosition());

            // 3) ¿terminó? Modos de salto: el click es bueno si aterriza vivo.
            bool allDone = true;
            for (int i = 0; i < n; ++i) {
                auto ui = static_cast<std::size_t>(i);
                if (done[ui]) continue;
                auto* g = targets[ui].ghost;
                if (landingExit && prof[ui].endOnLanding && k >= pressAt) {
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
            // Presupuesto de tiempo: medir cada 4 pasos para no pagar el reloj siempre.
            if ((steps & 3) == 0 && elapsedUs() > limitUs) {
                result = SimOutcome::Skipped;
                endedAt = k;
                break;
            }
        }

        if (trace) {
            auto* g = targets[0].ghost;
            trace->x = g->getPositionX();
            trace->y = g->getPositionY();
            trace->yVelocity = g->m_yVelocity;
        }
    }
    for (int i = 0; i < n; ++i) targets[static_cast<std::size_t>(i)].ghost->stopAllActions();
    sweep();

    double usedUs = elapsedUs();
    m_usedUs += usedUs;
    m_stepsUsed += steps;

    if (m_debug) {
        log::debug("[TickSaver] sim {} slot={} hold={} -> {} (paso {}/{}, {:.2f} ms){}{}",
                   kindName(static_cast<int>(kind)), static_cast<int>(slot) + 1, holdTicks, outcomeName(result),
                   endedAt, maxWindow, usedUs / 1000.0, portal ? ", fin en: " : "", portal ? portal : "");
    }
    return result;
}

SimOutcome GhostSimulator::simulatePress(Slot slot, int holdTicks) { return run(slot, Kind::Press, holdTicks, 0); }

SimOutcome GhostSimulator::simulateIdle(Slot slot, int ticks) { return run(slot, Kind::Idle, 0, ticks); }

SimOutcome GhostSimulator::simulateNoPress(Slot slot, int holdTicks) { return run(slot, Kind::NoPress, holdTicks, 0); }

SimOutcome GhostSimulator::simulateRelease(Slot slot) { return run(slot, Kind::Release, 0, 0); }

SimOutcome GhostSimulator::simulateInjectedPress(Slot slot, int holdTicks, int extraDelay) {
    // "Apretar ahora" (extraDelay 0) es exactamente lo que se reinyecta si el
    // click se salva: si piden la trayectoria, la guardamos de esta.
    bool record = m_recordPath && extraDelay == 0;
    auto outcome = run(slot, Kind::Press, holdTicks, 0, nullptr, m_injectLatency + extraDelay,
                       record ? &m_scratchPath : nullptr);
    if (record && outcome == SimOutcome::Survives) {
        m_path.swap(m_scratchPath);
        m_pathTick = m_now;
    }
    return outcome;
}

std::vector<CCPoint> const* GhostSimulator::lastPathFor(Tick now) const {
    return (m_pathTick == now && m_path.size() >= 2) ? &m_path : nullptr;
}

bool GhostSimulator::touchingOrb(Slot slot) {
    std::array<PlayerObject*, 2> reals{};
    int n = realTargets(slot, false, reals);
    for (int i = 0; i < n; ++i) {
        auto* real = reals[static_cast<std::size_t>(i)];
        if (real && real->m_touchingRings && real->m_touchingRings->count() > 0) return true;
    }
    return false;
}

void GhostSimulator::calibrateBefore(Slot slot, Tick now, int holdTicks) {
    if (m_cal.active || slot != Slot::P1) return;
    std::array<PlayerObject*, 2> reals{};
    // Sólo cubo y un solo jugador: es el caso donde los primeros ticks de un
    // salto son deterministas y no dependen de cuándo sueltes.
    if (realTargets(slot, false, reals) != 1 || detectMode(reals[0]) != Mode::Cube) return;

    Calibration cal;
    bool any = false;
    for (int latency = 0; latency <= kMaxLatency; ++latency) {
        auto ul = static_cast<std::size_t>(latency);
        auto outcome = run(slot, Kind::Press, std::max(holdTicks, kCalSteps), kCalSteps, &cal.traces[ul], latency);
        cal.valid[ul] = outcome == SimOutcome::Survives;
        any = any || cal.valid[ul];
    }
    if (!any) return;
    cal.active = true;
    cal.checkTick = now + kCalSteps;
    m_cal = cal;
}

void GhostSimulator::calibrateCheck(Tick now, bool cancel) {
    if (!m_cal.active) return;
    if (cancel || now > m_cal.checkTick) {
        m_cal.active = false;  // hubo otro input en el medio: la medición no sirve
        return;
    }
    if (now < m_cal.checkTick) return;
    m_cal.active = false;

    auto* real = m_layer->m_player1;
    if (!real || real->m_isDead) return;
    float rx = real->getPositionX(), ry = real->getPositionY();

    std::array<float, kMaxLatency + 1> err{};
    int best = -1;
    for (int l = 0; l <= kMaxLatency; ++l) {
        auto ul = static_cast<std::size_t>(l);
        err[ul] = m_cal.valid[ul] ? std::hypot(rx - m_cal.traces[ul].x, ry - m_cal.traces[ul].y) : 1e9f;
        if (best < 0 || err[ul] < err[static_cast<std::size_t>(best)]) best = l;
    }
    float second = 1e9f;
    for (int l = 0; l <= kMaxLatency; ++l) {
        if (l != best) second = std::min(second, err[static_cast<std::size_t>(l)]);
    }
    float bestErr = err[static_cast<std::size_t>(best)];

    if (m_debug) {
        log::debug("[TickSaver] calibracion: error latencia0={:.3f} 1={:.3f} 2={:.3f}", err[0], err[1], err[2]);
    }
    if (bestErr > 0.5f) {
        // Ninguna latencia explica lo que pasó: el fantasma difiere por otra
        // razón (campos sin copiar, objetos móviles, triggers).
        log::warn("[TickSaver] calibracion: el jugador real no coincide con ninguna prediccion (error {:.2f})", bestErr);
        return;
    }
    if (second < std::max(1.5f, bestErr * 3.f)) return;  // no es concluyente

    ++m_votes[static_cast<std::size_t>(best)];
    int others = 0;
    for (int l = 0; l <= kMaxLatency; ++l) {
        if (l != best) others += m_votes[static_cast<std::size_t>(l)];
    }
    if (best != m_injectLatency && m_votes[static_cast<std::size_t>(best)] >= 3 &&
        m_votes[static_cast<std::size_t>(best)] >= 2 * others) {
        log::info("[TickSaver] calibracion: el click reinyectado entra {} tick(s) tarde; ajustando predicciones", best);
        livelog::push(fmt::format("calibracion: el click reinyectado entra {} tick(s) tarde", best));
        m_injectLatency = best;
    }
}

bool GhostSimulator::pressHasEffect(Slot slot, int holdTicks) {
    // Nave/wave: apretar siempre cambia la trayectoria.
    std::array<PlayerObject*, 2> reals{};
    if (realTargets(slot, false, reals) == 0) return true;
    if (profileFor(detectMode(reals[0])).holdBased) return true;

    // Comparamos 8 pasos apretando contra 8 pasos sin apretar. Si el fantasma
    // termina exactamente igual, el click no hizo nada (cubo en el aire, bola
    // sin suelo...). Ante cualquier duda, "tiene efecto".
    constexpr int kSteps = 8;
    Trace withPress, without;
    if (run(slot, Kind::Press, holdTicks, kSteps, &withPress) != SimOutcome::Survives) return true;
    if (run(slot, Kind::NoPress, holdTicks, kSteps, &without) != SimOutcome::Survives) return true;
    return std::abs(withPress.x - without.x) > 0.01f || std::abs(withPress.y - without.y) > 0.01f ||
           std::abs(withPress.yVelocity - without.yVelocity) > 0.0001;
}

void GhostSimulator::validationBefore() {
    m_validating = false;
    auto* real = m_layer->m_player1;
    if (!ready() || !real || real->m_isDead) return;
    auto* g = m_ghosts[0].data();
    {
        sim::SyncScope syncing;
        sync(g, real);
        sim::setCounterpart(g, real);
    }
    {
        sim::Scope scope;
        auto rect = real->getObjectRect();
        sim::setRealRects(true, rect, rect);
        advance(g, m_dt);
        if (!sim::anyDied()) collide(g);
        bool bad = sim::anyDied() || sim::hitPortal();
        m_predictedX = g->getPositionX();
        m_predictedY = g->getPositionY();
        m_validating = !bad;
    }
    g->stopAllActions();
    sweep();
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
