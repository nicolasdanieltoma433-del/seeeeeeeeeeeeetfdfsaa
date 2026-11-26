// ============================================================================
//  TickSaver · hooks/PlayLayerHooks.cpp
//  Ciclo de vida del nivel: crear/destruir el motor, reinicios, muertes,
//  completado y salida. El Engine vive en m_fields: Geode lo destruye junto
//  con el PlayLayer, así que no hay que acordarse de limpiarlo.
// ============================================================================

#include "../game/Engine.hpp"
#include "../game/SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <memory>

using namespace geode::prelude;

class $modify(TSPlayLayer, PlayLayer) {
    struct Fields {
        std::unique_ptr<ts::Engine> engine;
    };

    static void onModify(auto& self) {
        // PRIMERO que cualquier otro mod: un noclip que no llame al original
        // nos impediría ver morir al fantasma y todo click "salvaría".
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
    }

    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        m_fields->engine = std::make_unique<ts::Engine>(this);
        m_fields->engine->onLevelReady();
        return true;
    }

    void resetLevel() {
        if (auto& engine = m_fields->engine) engine->onReset();
        PlayLayer::resetLevel();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Muerte de un fantasma: se anota y se corta acá. El jugador real no
        // se entera y no hay explosión, sonido ni reinicio.
        // Durante una simulación sólo se mueven fantasmas: nada de lo que pase
        // ahí puede matar al jugador real.
        if (ts::sim::isGhost(player) || ts::sim::active()) {
            if (ts::sim::active() && ts::sim::isGhost(player)) ts::sim::markDeath(player);
            return;
        }

        bool wasDead = player && player->m_isDead;
        PlayLayer::destroyPlayer(player, object);

        // m_anticheatSpike: GD llama a destroyPlayer al iniciar como chequeo
        // anti-trampas; no es una muerte real.
        auto& engine = m_fields->engine;
        if (engine && player && !wasDead && player->m_isDead && object != m_anticheatSpike) {
            engine->onRealDeath(player);
        }
    }

    void levelComplete() {
        if (auto& engine = m_fields->engine) engine->onLevelComplete();
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (auto& engine = m_fields->engine) engine->onQuit();
        PlayLayer::onQuit();
    }
};
