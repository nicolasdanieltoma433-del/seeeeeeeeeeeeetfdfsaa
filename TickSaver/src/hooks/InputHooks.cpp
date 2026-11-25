// ============================================================================
//  TickSaver · hooks/InputHooks.cpp
//  Dónde entra y sale cada click.
//
//  GJBaseGameLayer::handleButton(down, button, isPlayer1)
//      Punto de entrada de TODO input en vivo (teclado, mouse, táctil,
//      control). Si el motor decide retener el click, NO llamamos al
//      original: el juego nunca se entera hasta que lo reinyectamos.
//      Elegimos este hook (y no la cola m_queuedButtons) porque es estable
//      en todas las plataformas y no depende de cómo 2.2091 procesa la cola
//      con click-between-steps.
//
//  GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick)
//      Corre una vez por paso de física (y en medios pasos con CBS). Antes
//      de cada paso completo el motor reevalúa lo retenido y reinyecta lo
//      que ya es seguro, llamando al handleButton ORIGINAL.
//
//  PlayerObject::update(dt)
//      Sólo para leer el dt real del paso y que el fantasma use el mismo.
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

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (!ts::sim::active()) {
            auto* engine = ts::Engine::current();
            if (engine && engine->owns(this)) engine->beforeStep(isHalfTick);
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
};

class $modify(TSInputPlayer, PlayerObject) {
    void update(float dt) {
        if (!ts::sim::active()) {
            if (auto* engine = ts::Engine::current()) engine->noteRealUpdate(this, dt);
        }
        PlayerObject::update(dt);
    }
};
