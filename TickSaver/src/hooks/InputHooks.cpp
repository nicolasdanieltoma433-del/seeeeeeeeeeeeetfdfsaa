// ============================================================================
//  TickSaver · hooks/InputHooks.cpp
//  Dónde entra y sale cada click.
//
//  GJBaseGameLayer::handleButton(down, button, isPlayer1)
//      Punto de entrada de TODO input en vivo (teclado, mouse, táctil,
//      control). Si el motor decide retener el click, NO llamamos al
//      original: el juego nunca se entera hasta que lo reinyectamos.
//
//  PlayerObject::update(dt)  (sólo el jugador 1 real, fuera de pausa)
//      Marca el reloj: cada paso de física del jugador 1 es un tick. ANTES
//      del update original el motor reevalúa lo retenido y reinyecta lo que
//      ya es seguro (con el handleButton original), así el click entra justo
//      al principio del paso que simuló el fantasma. Es el mismo punto que
//      usa Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include "../game/Engine.hpp"
#include "../game/SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

class $modify(TSInputLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // Temprano: así vemos el click antes que otros mods de input y
        // podemos retenerlo. (Prioridad más baja = corre antes.)
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", Priority::Early);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // Reinyección propia o llamada desde una simulación: pasar directo.
        if (!ts::Engine::isInjecting() && !ts::sim::active()) {
            auto* engine = ts::Engine::current();
            if (engine && engine->owns(this) && !engine->onButton(down, button, isPlayer1)) {
                return;  // retenido: el motor lo reinyecta en un tick posterior
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(TSInputPlayer, PlayerObject) {
    static void onModify(auto& self) {
        // Temprano: el reloj y la reinyección tienen que pasar antes de que
        // otros mods toquen el paso del jugador.
        (void)self.setHookPriority("PlayerObject::update", Priority::Early);
    }

    void update(float dt) {
        // Fantasmas (y cualquier update durante una simulación): original.
        if (ts::sim::active() || ts::sim::isGhost(this)) {
            PlayerObject::update(dt);
            return;
        }
        auto* engine = ts::Engine::current();
        if (!engine || !engine->isRealPlayer1(this)) {
            PlayerObject::update(dt);
            return;
        }
        engine->onRealPlayerUpdate(dt);  // tick + reinyección, ANTES del paso
        engine->beginRealUpdate(getPositionX());
        PlayerObject::update(dt);
        // El motor puede haberse destruido si el update terminó el nivel.
        if (auto* again = ts::Engine::current(); again == engine) engine->endRealUpdate(getPositionX());
    }
};
