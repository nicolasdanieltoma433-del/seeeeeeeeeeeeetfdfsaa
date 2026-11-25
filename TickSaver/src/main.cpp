// ============================================================================
//  TickSaver · main.cpp
//  Punto de entrada. Los hooks ($modify) se registran solos al cargar el
//  mod; acá sólo inicializamos settings y datos persistentes.
//
//  Mapa del código:
//    core/   lógica pura y testeable (sin Geode): scheduler, presets,
//            código TS2, modo adaptativo, estadísticas
//    game/   puente con GD: motor, fantasma, settings, guardado
//    ui/     HUD, resumen al morir, popup de estadísticas
//    hooks/  $modify de GJBaseGameLayer, PlayLayer, PlayerObject, PauseLayer
// ============================================================================

#include "game/Engine.hpp"
#include "game/Settings.hpp"
#include "game/StatsStore.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    ts::settings::install();
    ts::settings::setOnChanged([] {
        if (auto* engine = ts::Engine::current()) engine->onConfigChanged();
    });
    ts::store::loadAdaptive();

    log::info("TickSaver listo: preset {}, max {}t", ts::presetName(ts::settings::get().preset),
              static_cast<int>(ts::settings::get().globalMaxTicks));
}
