#pragma once
// ============================================================================
//  TickSaver · core/Config.hpp
//  Configuración completa del mod + presets.
//
//  Flujo:
//    settings de Geode (mod.json)  --Settings.cpp-->  ts::Config  --> motor
//    código "TS2-..."              --ConfigCode.cpp-> ts::Config  --> settings
//
//  Config es un struct "tonto" (sólo datos + helpers puros) para que sea
//  trivial de copiar, comparar, serializar y testear.
// ============================================================================

#include "Types.hpp"

#include <optional>
#include <string_view>

namespace ts {

enum class Preset : std::uint8_t {
    Soft = 0,   // "Suave"
    Normal,     // "Normal"
    Strong,     // "Fuerte"
    Max,        // "Maximo"
    Wave,       // "Wave"   (pensado para tramos de wave/nave)
    Train,      // "Entrenar" (poca ayuda + aprende de vos)
    Custom,     // "Personalizado" (el usuario tocó algún valor a mano)
    Count
};
inline constexpr std::size_t kPresetCount = static_cast<std::size_t>(Preset::Count);

// Nombres EXACTOS usados en el "one-of" del setting "preset" de mod.json.
inline constexpr std::array<std::string_view, kPresetCount> kPresetNames = {
    "Suave", "Normal", "Fuerte", "Maximo", "Wave", "Entrenar", "Personalizado",
};

std::string_view presetName(Preset p);
std::optional<Preset> presetFromName(std::string_view name);

// Tramo del nivel con ticks extra (ej. 40%-60% de una parte difícil).
struct Region {
    bool enabled = false;
    std::uint8_t startPct = 40;   // 0..100
    std::uint8_t endPct = 60;     // 0..100, > startPct
    std::uint8_t extraTicks = 6;  // se SUMAN al máximo del modo (tope 240)

    constexpr bool operator==(Region const&) const = default;
    bool contains(float percent) const;
};

struct Config {
    // --- General -----------------------------------------------------------
    bool enabled = true;
    Preset preset = Preset::Normal;

    // --- Ticks ---------------------------------------------------------------
    // Máximo de ticks que un click puede retrasarse (0..240).
    std::uint8_t globalMaxTicks = 6;
    // Override por modo: 0 = usar globalMaxTicks. Así "swing aparte con sus
    // propios ticks" es simplemente modeMaxTicks[Swing] != 0.
    std::array<std::uint8_t, kModeCount> modeMaxTicks{};
    Region region{};

    // --- Comportamiento ------------------------------------------------------
    bool correctBoth = true;    // 2 jugadores: corregir también al P2
    bool adaptive = false;      // modo adaptativo "aprende de vos"
    bool preserveHold = true;   // al retrasar, conservar cuánto mantuviste el click
    bool safeMode = true;       // intentos asistidos no guardan progreso

    // --- HUD -----------------------------------------------------------------
    bool showHud = true;
    bool showTimingBar = true;
    bool showDeathSummary = true;
    float hudScale = 1.f;

    Palette palette = defaultPalette();

    // --- Avanzado (no se exporta en el código TS2) ---------------------------
    // Tiempo de CPU por frame para simular fantasmas. Con niveles pesados
    // (miles de objetos) cada paso de colisión cuesta más, por eso el límite
    // es en tiempo y no en cantidad de pasos.
    float simBudgetMs = 2.f;
    // Reinyectar el click retrasado con PlayerObject::pushButton (igual que
    // hace el fantasma) en vez de pasar por handleButton.
    bool directInject = true;
    bool debugLog = false;
    bool validateSim = false;

    // Máximo del modo sin el tramo extra (override de modo -> global).
    int baseTicksFor(Mode mode) const;
    // Ticks extra del tramo en este % (0 si está fuera o desactivado).
    int regionExtraAt(float percent) const;
    // Máximo efectivo = base + extra del tramo, con tope en 240.
    int maxTicksFor(Mode mode, float percent) const;

    static Palette defaultPalette();
};

// Escribe en `cfg` SOLO los campos que controla un preset (ticks y
// adaptativo). No toca región, HUD, colores ni avanzado.
void applyPreset(Config& cfg, Preset preset);

// ¿Los valores actuales de ticks/adaptativo coinciden con algún preset?
// Se usa para mostrar "Personalizado" cuando el usuario edita a mano.
Preset detectPreset(Config const& cfg);

} // namespace ts
