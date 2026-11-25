#include "Config.hpp"

#include <algorithm>

namespace ts {

std::string_view presetName(Preset p) {
    auto i = static_cast<std::size_t>(p);
    return i < kPresetCount ? kPresetNames[i] : "Personalizado";
}

std::optional<Preset> presetFromName(std::string_view name) {
    for (std::size_t i = 0; i < kPresetCount; ++i) {
        if (kPresetNames[i] == name) return static_cast<Preset>(i);
    }
    return std::nullopt;
}

bool Region::contains(float percent) const {
    if (!enabled || endPct <= startPct) return false;
    return percent >= static_cast<float>(startPct) && percent <= static_cast<float>(endPct);
}

int Config::baseTicksFor(Mode mode) const {
    int base = modeMaxTicks[idx(mode)] != 0 ? modeMaxTicks[idx(mode)] : globalMaxTicks;
    return std::clamp(base, 0, kMaxSaveTicks);
}

int Config::regionExtraAt(float percent) const {
    return region.contains(percent) ? region.extraTicks : 0;
}

int Config::maxTicksFor(Mode mode, float percent) const {
    return std::clamp(baseTicksFor(mode) + regionExtraAt(percent), 0, kMaxSaveTicks);
}

Palette Config::defaultPalette() {
    return Palette{{
        {255, 200, 60, 255},   // Streak  : dorado
        {90, 230, 120, 255},   // Good    : verde
        {255, 170, 60, 255},   // Early   : naranja
        {80, 200, 255, 255},   // Saved   : celeste
        {255, 80, 80, 255},    // Danger  : rojo
        {15, 15, 25, 170},     // BarBg   : negro translúcido
        {230, 230, 240, 255},  // Text    : blanco suave
        {180, 120, 255, 255},  // Accent  : violeta
    }};
}

// ----------------------------------------------------------------------------
// Tabla de presets. Índices de modo: Cube, Ship, Ball, Ufo, Wave, Robot,
// Spider, Swing. 0 = "usar el global".
// ----------------------------------------------------------------------------
namespace {
struct PresetValues {
    std::uint8_t global;
    std::array<std::uint8_t, kModeCount> modes;
    bool adaptive;
};

//                                 global   Cu Sh Ba Uf Wa Ro Sp Sw   adapt
constexpr PresetValues kPresets[] = {
    /* Suave    */ {3,   {0, 0, 0, 0, 0,  0, 0, 2},   false},
    /* Normal   */ {6,   {0, 0, 0, 0, 5,  0, 0, 4},   false},
    /* Fuerte   */ {12,  {0, 0, 0, 0, 10, 0, 0, 8},   false},
    /* Maximo   */ {240, {0, 0, 0, 0, 0,  0, 0, 0},   false},
    /* Wave     */ {6,   {0, 12, 0, 0, 24, 0, 0, 12}, false},
    /* Entrenar */ {4,   {0, 0, 0, 0, 3,  0, 0, 3},   true},
};
static_assert(std::size(kPresets) == kPresetCount - 1, "falta un preset en la tabla");
} // namespace

void applyPreset(Config& cfg, Preset preset) {
    if (preset == Preset::Custom || preset >= Preset::Count) {
        cfg.preset = Preset::Custom;
        return;
    }
    auto const& v = kPresets[static_cast<std::size_t>(preset)];
    cfg.globalMaxTicks = v.global;
    cfg.modeMaxTicks = v.modes;
    cfg.adaptive = v.adaptive;
    cfg.preset = preset;
}

Preset detectPreset(Config const& cfg) {
    for (std::size_t i = 0; i + 1 < kPresetCount; ++i) {
        auto const& v = kPresets[i];
        if (cfg.globalMaxTicks == v.global && cfg.modeMaxTicks == v.modes && cfg.adaptive == v.adaptive) {
            return static_cast<Preset>(i);
        }
    }
    return Preset::Custom;
}

} // namespace ts
