// ============================================================================
//  TickSaver · main.cpp
//  Punto de entrada. Los hooks ($modify) se registran solos al cargar el
//  mod; acá sólo inicializamos settings, datos persistentes y teclas rápidas.
//
//  Mapa del código:
//    core/   lógica pura y testeable (sin Geode): scheduler, presets,
//            código TS2, modo adaptativo, estadísticas
//    game/   puente con GD: motor, fantasma, settings, guardado, log en vivo
//    ui/     HUD, resumen al morir, menú, estadísticas, botón flotante
//    hooks/  $modify de GJBaseGameLayer, PlayLayer, PlayerObject, PauseLayer
//
//  Varias técnicas del fantasma (capa aparte, orden del paso, copia de
//  campos, filtro de colisiones, buffer de orbes, reglas de spam) están
//  adaptadas de Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include "game/Engine.hpp"
#include "game/Settings.hpp"
#include "game/StatsStore.hpp"
#include "ui/ModMenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace {

void setupKeybinds() {
    // Las teclas sólo existen en PC (mod.json: "platforms": ["win"]). En
    // Android estos listeners nunca se disparan.
    (void)listenForKeybindSettingPresses("key-menu", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) ts::ui::openModMenu();
        return true;
    });
    (void)listenForKeybindSettingPresses("key-toggle", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) {
            bool on = !ts::settings::get().enabled;
            ts::settings::update([on](ts::Config& c) { c.enabled = on; });
            Notification::create(on ? "TickSaver activado" : "TickSaver desactivado",
                                 on ? NotificationIcon::Success : NotificationIcon::Info, 1.2f)
                ->show();
        }
        return true;
    });
    (void)listenForKeybindSettingPresses("key-preset", [](Keybind const&, bool down, bool repeat, double) {
        if (down && !repeat) {
            constexpr int kSelectable = static_cast<int>(ts::kPresetCount) - 1;  // sin Personalizado
            int cur = static_cast<int>(ts::settings::get().preset);
            int next = cur >= kSelectable - 1 ? 0 : cur + 1;
            ts::settings::update([next](ts::Config& c) { ts::applyPreset(c, static_cast<ts::Preset>(next)); });
            Notification::create(fmt::format("Preset: {}", ts::presetName(ts::settings::get().preset)),
                                 NotificationIcon::Info, 1.2f)
                ->show();
        }
        return true;
    });
}

} // namespace

$on_mod(Loaded) {
    // v0.4: la reinyección recomendada pasó a ser la ruta normal del juego.
    // Quien venía de v0.3 tenía guardado el valor viejo (true): lo cambiamos
    // una sola vez; si después lo vuelve a prender, se respeta.
    if (!Mod::get()->getSavedValue<bool>("migrated-v040", false)) {
        Mod::get()->setSettingValue<bool>("direct-inject", false);
        Mod::get()->setSavedValue<bool>("migrated-v040", true);
    }

    ts::settings::install();
    ts::settings::setOnChanged([] {
        if (auto* engine = ts::Engine::current()) engine->onConfigChanged();
    });
    ts::store::loadAdaptive();
    setupKeybinds();

    log::info("TickSaver listo: preset {}, max {}t", ts::presetName(ts::settings::get().preset),
              static_cast<int>(ts::settings::get().globalMaxTicks));
}
