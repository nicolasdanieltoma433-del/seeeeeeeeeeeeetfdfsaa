#pragma once
// ============================================================================
//  TickSaver · game/Settings.hpp
//  Puente entre los settings de Geode (definidos en mod.json) y ts::Config.
//
//  * get()      -> Config cacheada (barata, se puede llamar cada tick).
//  * install()  -> registra listeners: cualquier cambio en el menú de
//                  settings recarga la cache y avisa al motor.
//  * Presets: elegir un preset escribe sus valores en los settings de ticks.
//    Si después tocás un valor a mano, el preset pasa a "Personalizado".
// ============================================================================

#include "../core/Config.hpp"

#include <functional>

namespace ts::settings {

Config const& get();
void reload();
void install();

// Escribe una Config completa en los settings (importar código TS2).
void write(Config const& cfg);

// Callback opcional que se dispara tras cada recarga (lo usa el motor para
// refrescar colores/HUD en vivo).
void setOnChanged(std::function<void()> callback);

} // namespace ts::settings
