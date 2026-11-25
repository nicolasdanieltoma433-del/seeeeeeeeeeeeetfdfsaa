// ============================================================================
//  TickSaver · hooks/GhostGuardHooks.cpp
//  Aislamiento del fantasma: todo lo que checkCollisions() puede hacerle al
//  nivel real cuando el que colisiona es un fantasma.
//
//  Regla general de cada hook:
//      si (simulando && el jugador es fantasma) -> bloquear / registrar
//      si no                                    -> comportamiento original
//  Fuera de una simulación el costo es un if sobre un bool global.
//
//  Tabla de decisiones (ampliar si aparece un efecto colateral nuevo):
//    orbes, pads              -> se ejecutan (física) y se registra el flag
//                                de "activado" para restaurarlo (SimGuard)
//    gravedad                 -> se ejecuta, pero SIN efectos visuales
//    portal de modo/dual/tp   -> se corta la simulación como "inconclusa"
//    triggers por contacto,
//    monedas/items, eventos,
//    checkpoints (también los
//    automáticos de práctica),
//    partículas,
//    estelas, contador saltos -> bloqueados
// ============================================================================

#include "../game/SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

namespace {
bool isSimGhost(PlayerObject* p) { return ts::sim::active() && ts::sim::isGhost(p); }
} // namespace

class $modify(TSGuardLayer, GJBaseGameLayer) {
    // --- se cortan: cambiarían cámara, suelo o crearían jugadores ----------
    void playerWillSwitchMode(PlayerObject* player, GameObject* object) {
        if (isSimGhost(player)) return ts::sim::markInconclusive("portal de modo");
        GJBaseGameLayer::playerWillSwitchMode(player, object);
    }

    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        if (isSimGhost(player)) return ts::sim::markInconclusive("portal dual");
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
    }

    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        if (isSimGhost(player)) return ts::sim::markInconclusive("teleport");
        GJBaseGameLayer::teleportPlayer(object, player);
    }

    // --- se ejecutan con cuidado -------------------------------------------
    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        GJBaseGameLayer::flipGravity(player, flip, isSimGhost(player) ? true : noEffects);
    }

    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        if (isSimGhost(player)) ts::sim::recordActivation(object);
        GJBaseGameLayer::bumpPlayer(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* object) {
        if (isSimGhost(player)) ts::sim::recordActivation(object);
        GJBaseGameLayer::playerTouchedRing(player, object);
    }

    // --- bloqueados durante cualquier simulación ---------------------------
    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (isSimGhost(player)) return;  // triggers "touch" dispararían grupos reales
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }

    void pickupItem(EffectGameObject* object) {
        if (ts::sim::active()) return;  // llaves, items, contadores
        GJBaseGameLayer::pickupItem(object);
    }

    void destroyObject(GameObject* object) {
        if (ts::sim::active()) return;  // monedas y objetos que se rompen al tocar
        GJBaseGameLayer::destroyObject(object);
    }

    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (ts::sim::active()) return;  // "al saltar", "al aterrizar"... -> triggers de evento
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }
};

class $modify(TSGuardPlayLayer, PlayLayer) {
    void checkpointActivated(CheckpointGameObject* object) {
        if (ts::sim::active()) return;  // checkpoints de práctica fantasma
        PlayLayer::checkpointActivated(object);
    }

    void playGravityEffect(bool flip) {
        if (ts::sim::active()) return;
        PlayLayer::playGravityEffect(flip);
    }

    void playEndAnimationToPos(CCPoint position) {
        if (ts::sim::active()) return ts::sim::markInconclusive("final del nivel");
        PlayLayer::playEndAnimationToPos(position);
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        if (ts::sim::active()) return;  // nunca guardar un checkpoint durante una simulación
        PlayLayer::storeCheckpoint(checkpoint);
    }
};

class $modify(TSGuardPlayer, PlayerObject) {
    void ringJump(RingObject* object, bool skipCheck) {
        if (isSimGhost(this)) ts::sim::recordActivation(object);
        PlayerObject::ringJump(object, skipCheck);
    }

    void incrementJumps() {
        if (isSimGhost(this)) return;  // estadísticas de saltos del perfil
        PlayerObject::incrementJumps();
    }

    void tryPlaceCheckpoint() {
        if (ts::sim::isGhost(this)) return;  // checkpoints automáticos de práctica
        PlayerObject::tryPlaceCheckpoint();
    }

    void playerDestroyed(bool noEffects) {
        if (isSimGhost(this)) return;
        PlayerObject::playerDestroyed(noEffects);
    }

    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (isSimGhost(this)) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void spawnPortalCircle(ccColor3B color, float startRadius) {
        if (isSimGhost(this)) return;
        PlayerObject::spawnPortalCircle(color, startRadius);
    }

    void activateStreak() {
        if (isSimGhost(this)) return;
        PlayerObject::activateStreak();
    }
};

class $modify(TSGuardObject, EnhancedGameObject) {
    // Orbes, pads y demás objetos "de un uso por jugador" se marcan acá.
    void activatedByPlayer(PlayerObject* player) {
        if (isSimGhost(player)) ts::sim::recordActivation(this);
        EnhancedGameObject::activatedByPlayer(player);
    }
};
