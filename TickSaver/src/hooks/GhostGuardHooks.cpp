// ============================================================================
//  TickSaver · hooks/GhostGuardHooks.cpp
//  Aislamiento del fantasma: todo lo que checkCollisions() y update() pueden
//  hacerle al nivel real cuando el que se mueve es un fantasma.
//
//  Regla general de cada hook:
//      si (simulando && el jugador es fantasma) -> filtrar / registrar / bloquear
//      si no                                    -> comportamiento original
//  Fuera de una simulación el costo es un if sobre un bool global.
//
//  Tabla de decisiones (ampliar si aparece un efecto colateral nuevo):
//    objetos que el fantasma revisa  -> sólo sólidos, pinchos, pendientes,
//                                       pads, orbes, dash y portales de
//                                       gravedad (collisionCheckObjects)
//    portal de velocidad             -> se aplica a mano (m_playerSpeed)
//    portal de tamaño                -> togglePlayerScale en el fantasma
//    portal de modo/dual/tp, final   -> fin de la simulación: llegar vivo
//                                       hasta ahí cuenta como "vive"
//    orbes, pads, dash               -> se ejecutan (física), una vez por
//                                       simulación, con foto y restauración
//    gravedad                        -> PlayerObject::flipGravity, sin
//                                       tocar la cámara ni el suelo del nivel
//    grupos que un orbe apaga        -> el fantasma deja de chocarlos
//    triggers, grupos, contadores,
//    items, monedas, eventos,
//    checkpoints, partículas,
//    estelas, sonidos, saltos        -> bloqueados
//
//  Técnica adaptada de Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include "../game/SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>

#include <array>
#include <vector>

using namespace geode::prelude;

namespace {

bool isSimGhost(PlayerObject* p) { return ts::sim::active() && ts::sim::isGhost(p); }

constexpr std::uint64_t typeBit(GameObjectType type) { return 1ull << static_cast<int>(type); }

// Objetos que chocan: el fantasma los necesita para morir o apoyarse.
constexpr std::uint64_t kColliding = typeBit(GameObjectType::Solid) | typeBit(GameObjectType::Hazard) |
                                     typeBit(GameObjectType::Slope) | typeBit(GameObjectType::CollisionObject) |
                                     typeBit(GameObjectType::AnimatedHazard);

constexpr std::uint64_t kHazards = typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::AnimatedHazard);

// Objetos que cambian la física del jugador y se pueden simular sin tocar el nivel.
constexpr std::uint64_t kInteractable =
    typeBit(GameObjectType::YellowJumpPad) | typeBit(GameObjectType::PinkJumpPad) |
    typeBit(GameObjectType::RedJumpPad) | typeBit(GameObjectType::GravityPad) | typeBit(GameObjectType::SpiderPad) |
    typeBit(GameObjectType::YellowJumpRing) | typeBit(GameObjectType::PinkJumpRing) |
    typeBit(GameObjectType::RedJumpRing) | typeBit(GameObjectType::GravityRing) |
    typeBit(GameObjectType::GreenRing) | typeBit(GameObjectType::DropRing) | typeBit(GameObjectType::CustomRing) |
    typeBit(GameObjectType::DashRing) | typeBit(GameObjectType::GravityDashRing) |
    typeBit(GameObjectType::SpiderOrb) | typeBit(GameObjectType::InverseGravityPortal) |
    typeBit(GameObjectType::NormalGravityPortal) | typeBit(GameObjectType::GravityTogglePortal);

// Portales que cambian de modo, de cantidad de jugadores o de lugar: el
// fantasma no sigue más allá (cambiarían cámara, suelo o crearían jugadores).
constexpr std::uint64_t kStopPortals =
    typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
    typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
    typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal) |
    typeBit(GameObjectType::DualPortal) | typeBit(GameObjectType::SoloPortal) |
    typeBit(GameObjectType::TeleportPortal) | typeBit(GameObjectType::TeleportOrb);

constexpr std::uint64_t kSizePortals = typeBit(GameObjectType::RegularSizePortal) | typeBit(GameObjectType::MiniSizePortal);

// Portales de velocidad (por ID de objeto): 0.5x, 1x, 2x, 3x, 4x.
float speedPortalValue(int objectID) {
    switch (objectID) {
        case 200: return 0.7f;
        case 201: return 0.9f;
        case 202: return 1.1f;
        case 203: return 1.3f;
        case 1334: return 1.6f;
        default: return 0.f;
    }
}

// Vectores reutilizados por nivel de anidamiento (sin alocar en cada paso).
std::array<std::vector<GameObject*>, 4> g_filtered;
int g_filterDepth = 0;

} // namespace

class $modify(TSGuardLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // Muy temprano: si otro mod modifica estas funciones, que el fantasma
        // ya llegue filtrado.
        (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::flipGravity", Priority::First);
    }

    // --- qué objetos revisa el fantasma ---------------------------------------
    void collisionCheckObjects(PlayerObject* player, std::vector<GameObject*>* objects, int count, float dt) {
        if (!isSimGhost(player) || !objects || g_filterDepth >= static_cast<int>(g_filtered.size())) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, count, dt);
            return;
        }
        auto& kept = g_filtered[static_cast<std::size_t>(g_filterDepth)];
        kept.clear();
        if (kept.capacity() < static_cast<std::size_t>(count)) kept.reserve(static_cast<std::size_t>(count));

        bool haveRect = false;
        CCRect ghostRect;
        int total = std::min<int>(count, static_cast<int>(objects->size()));
        for (int i = 0; i < total; ++i) {
            auto* object = (*objects)[static_cast<std::size_t>(i)];
            if (!object) continue;
            auto type = static_cast<unsigned>(object->m_objectType);
            std::uint64_t bit = type < 64 ? (1ull << type) : 0;

            if (bit & (kColliding | kInteractable)) {
                // Apagado por un orbe/trigger en esta simulación.
                if (ts::sim::disabled(object)) continue;
                if (bit & kHazards) {
                    if (ts::sim::harmless(object)) continue;
                    // Un pincho que el jugador real ya está tocando sin morir
                    // (hitbox que lo roza) no puede matar al fantasma recién
                    // copiado: lo ignoramos en toda la simulación.
                    if (ts::sim::step() <= 1 && ts::sim::overlapsRealPlayer(object->getObjectRect())) {
                        ts::sim::markHarmless(object);
                        continue;
                    }
                }
                kept.push_back(object);
                continue;
            }

            bool stop = (bit & kStopPortals) != 0;
            bool size = (bit & kSizePortals) != 0;
            float speed = (stop || size) ? 0.f : speedPortalValue(object->m_objectID);
            if (!stop && !size && speed <= 0.f) continue;  // decoración, triggers, monedas...

            if (!haveRect) {
                ghostRect = player->getObjectRect();
                haveRect = true;
            }
            if (!ghostRect.intersectsRect(object->getObjectRect())) continue;

            if (stop) {
                ts::sim::markPortal("portal");
            }
            else if (size) {
                bool mini = object->m_objectType == GameObjectType::MiniSizePortal;
                if ((player->m_vehicleSize < 1.f) != mini) player->togglePlayerScale(mini, true);
            }
            else {
                player->m_playerSpeed = speed;
            }
        }

        ++g_filterDepth;
        GJBaseGameLayer::collisionCheckObjects(player, &kept, static_cast<int>(kept.size()), dt);
        --g_filterDepth;
    }

    // --- orbes y pads: una vez por simulación, si el jugador real todavía puede
    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (!ts::sim::active()) return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        if (!ts::sim::isGhost(player) || !object) return false;
        if (ts::sim::used(object)) return false;
        // Los pads se activan una sola vez; un orbe sigue disponible hasta que
        // el fantasma realmente salta con él (ver ringJump).
        bool isRing = typeinfo_cast<RingObject*>(object) != nullptr;
        if (!isRing && ts::sim::seen(object)) return false;
        ts::sim::snapshot(object);
        // Si el jugador real ya lo usó, el fantasma (que es una copia suya)
        // tampoco puede. Le preguntamos al juego con el jugador real.
        auto* real = ts::sim::counterpart(player);
        return real ? GJBaseGameLayer::canBeActivatedByPlayer(real, object) : true;
    }

    void playerTouchedRing(PlayerObject* player, RingObject* object) {
        if (ts::sim::active()) {
            if (!ts::sim::isGhost(player)) return;
            ts::sim::snapshot(object);
        }
        GJBaseGameLayer::playerTouchedRing(player, object);
    }

    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        if (ts::sim::active()) {
            if (!ts::sim::isGhost(player)) return;
            ts::sim::snapshot(object);
            ts::sim::markUsed(object);
        }
        GJBaseGameLayer::bumpPlayer(player, object);
    }

    // --- gravedad: sólo el fantasma, sin efectos sobre el nivel ----------------
    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (ts::sim::active()) {
            if (ts::sim::isGhost(player)) player->flipGravity(flip, true);
            return;
        }
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    // --- grupos: lo que un orbe apaga deja de chocar ----------------------------
    void toggleGroupTriggered(int group, bool activate, std::vector<int> const& remapKeys, int triggerID, int controlID) {
        if (ts::sim::active()) {
            if (!activate) {
                if (auto* objects = this->getGroup(group)) {
                    for (auto* object : CCArrayExt<GameObject*>(objects)) ts::sim::disable(object);
                }
            }
            return;
        }
        GJBaseGameLayer::toggleGroupTriggered(group, activate, remapKeys, triggerID, controlID);
    }

    // --- fin de la simulación ----------------------------------------------------
    void playerWillSwitchMode(PlayerObject* player, GameObject* object) {
        if (ts::sim::active()) return ts::sim::markPortal("portal de modo");
        GJBaseGameLayer::playerWillSwitchMode(player, object);
    }

    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        if (ts::sim::active()) return ts::sim::markPortal("portal dual");
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
    }

    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        if (ts::sim::active()) return ts::sim::markPortal("teleport");
        GJBaseGameLayer::teleportPlayer(object, player);
    }

    // --- bloqueados durante cualquier simulación ----------------------------------
    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (ts::sim::active()) return;  // triggers "touch" dispararían grupos reales
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }

    void spawnGroup(int group, bool ordered, double delay, std::vector<int> const& remapKeys, int triggerID, int controlID) {
        if (ts::sim::active()) return;
        GJBaseGameLayer::spawnGroup(group, ordered, delay, remapKeys, triggerID, controlID);
    }

    void toggleGroup(int group, bool activate) {
        if (ts::sim::active()) return;
        GJBaseGameLayer::toggleGroup(group, activate);
    }

    void updateCounters(int itemID, int value) {
        if (ts::sim::active()) return;
        GJBaseGameLayer::updateCounters(itemID, value);
    }

    void activateItemEditTrigger(ItemTriggerGameObject* object) {
        if (ts::sim::active()) return;
        GJBaseGameLayer::activateItemEditTrigger(object);
    }

    void activateItemCompareTrigger(ItemTriggerGameObject* object, std::vector<int> const& remapKeys) {
        if (ts::sim::active()) return;
        GJBaseGameLayer::activateItemCompareTrigger(object, remapKeys);
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
        if (ts::sim::active()) return ts::sim::markPortal("final del nivel");
        PlayLayer::playEndAnimationToPos(position);
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        if (ts::sim::active()) return;  // nunca guardar un checkpoint durante una simulación
        PlayLayer::storeCheckpoint(checkpoint);
    }
};

class $modify(TSGuardPlayer, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::playerDestroyed", Priority::First);
        (void)self.setHookPriority("PlayerObject::ringJump", Priority::First);
    }

    bool ghostEffect() { return isSimGhost(this); }

    // --- muerte y orbes -------------------------------------------------------
    void playerDestroyed(bool noEffects) {
        if (ts::sim::isGhost(this)) {
            if (ts::sim::active()) ts::sim::markDeath(this);
            return;  // un fantasma nunca explota
        }
        if (ts::sim::active()) return;
        PlayerObject::playerDestroyed(noEffects);
    }

    void playDeathEffect() {
        if (ts::sim::active() || ts::sim::isGhost(this)) return;
        PlayerObject::playDeathEffect();
    }

    void ringJump(RingObject* object, bool skipCheck) {
        if (ts::sim::active()) {
            if (!ts::sim::isGhost(this) || ts::sim::used(object)) return;
            ts::sim::snapshot(object);
            auto yVelocity = m_yVelocity;
            bool upsideDown = m_isUpsideDown;
            bool dashing = m_isDashing;
            PlayerObject::ringJump(object, skipCheck);
            // Sólo cuenta como "usado" si el orbe realmente hizo algo.
            if (m_yVelocity != yVelocity || m_isUpsideDown != upsideDown || m_isDashing != dashing) {
                ts::sim::markUsed(object);
            }
            return;
        }
        PlayerObject::ringJump(object, skipCheck);
    }

    void startDashing(DashRingObject* object) {
        if (isSimGhost(this)) ts::sim::markUsed(object);
        PlayerObject::startDashing(object);
    }

    // --- estadísticas y checkpoints -------------------------------------------
    void incrementJumps() {
        if (ts::sim::active()) return;  // estadísticas de saltos del perfil
        PlayerObject::incrementJumps();
    }

    void tryPlaceCheckpoint() {
        if (ts::sim::isGhost(this)) return;  // checkpoints automáticos de práctica
        PlayerObject::tryPlaceCheckpoint();
    }

    // --- efectos visuales del fantasma -------------------------------------------
    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (ts::sim::active()) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void spawnPortalCircle(ccColor3B color, float startRadius) {
        if (!ghostEffect()) PlayerObject::spawnPortalCircle(color, startRadius);
    }

    void spawnCircle() {
        if (!ghostEffect()) PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (!ghostEffect()) PlayerObject::spawnDualCircle();
    }

    void spawnScaleCircle() {
        if (!ghostEffect()) PlayerObject::spawnScaleCircle();
    }

    void playBumpEffect(int objectType, GameObject* object) {
        if (!ghostEffect()) PlayerObject::playBumpEffect(objectType, object);
    }

    void playSpawnEffect() {
        if (!ghostEffect()) PlayerObject::playSpawnEffect();
    }

    void createFadeOutDartStreak() {
        if (!ghostEffect()) PlayerObject::createFadeOutDartStreak();
    }

    void fadeOutStreak2(float duration) {
        if (!ghostEffect()) PlayerObject::fadeOutStreak2(duration);
    }

    void playDynamicSpiderRun() {
        if (!ghostEffect()) PlayerObject::playDynamicSpiderRun();
    }

    void animatePlatformerJump(float scale) {
        if (!ghostEffect()) PlayerObject::animatePlatformerJump(scale);
    }

    void runBallRotation(float speed) {
        if (!ghostEffect()) PlayerObject::runBallRotation(speed);
    }

    void runBallRotation2() {
        if (!ghostEffect()) PlayerObject::runBallRotation2();
    }

    void runNormalRotation(bool notNormalMode, float speed) {
        if (!ghostEffect()) PlayerObject::runNormalRotation(notNormalMode, speed);
    }

    void toggleGhostEffect(GhostType type) {
        if (!ghostEffect()) PlayerObject::toggleGhostEffect(type);
    }

    void activateStreak() {
        if (!ghostEffect()) PlayerObject::activateStreak();
    }

    void playCompleteEffect(bool noEffects, bool instant) {
        if (!ghostEffect()) PlayerObject::playCompleteEffect(noEffects, instant);
    }
};

class $modify(TSGuardObject, EnhancedGameObject) {
    // Orbes, pads y demás objetos "de un uso por jugador" se marcan acá.
    void activatedByPlayer(PlayerObject* player) {
        if (ts::sim::active()) {
            if (!ts::sim::isGhost(player)) return;
            ts::sim::snapshot(this);
        }
        EnhancedGameObject::activatedByPlayer(player);
    }
};

class $modify(TSGuardEffect, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, std::vector<int> const* remapKeys) {
        if (ts::sim::active()) return;  // ningún trigger corre por culpa del fantasma
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(TSGuardGameObject, GameObject) {
    void playShineEffect() {
        if (ts::sim::active()) return;
        GameObject::playShineEffect();
    }
};

class $modify(TSGuardRing, RingObject) {
    void spawnCircle() {
        if (ts::sim::active()) return;
        RingObject::spawnCircle();
    }
};

class $modify(TSGuardStreak, HardStreak) {
    void addPoint(CCPoint point) {
        if (ts::sim::active()) return;  // estela de la wave del fantasma
        HardStreak::addPoint(point);
    }
};
