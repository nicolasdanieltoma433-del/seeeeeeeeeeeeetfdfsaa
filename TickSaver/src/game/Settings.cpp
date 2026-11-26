#include "Settings.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace ts::settings {
namespace {

Config g_cache;
bool g_writing = false;  // evita bucles: nuestras propias escrituras disparan eventos
std::function<void()> g_onChanged;

std::string modeKey(std::size_t i) { return fmt::format("ticks-{}", kModeKeys[i]); }

std::uint8_t toU8(std::int64_t v, int max) { return static_cast<std::uint8_t>(std::clamp<std::int64_t>(v, 0, max)); }
std::uint16_t toU16(std::int64_t v, int max) { return static_cast<std::uint16_t>(std::clamp<std::int64_t>(v, 0, max)); }

template <class T>
T value(std::string_view key) {
    return Mod::get()->getSettingValue<T>(key);
}

Config readAll() {
    Config c;
    c.enabled = value<bool>("enabled");
    c.preset = presetFromName(value<std::string>("preset")).value_or(Preset::Custom);

    c.globalMaxTicks = toU16(value<std::int64_t>("max-ticks"), kMaxSaveTicks);
    for (std::size_t i = 0; i < kModeCount; ++i) {
        c.modeMaxTicks[i] = toU16(value<std::int64_t>(modeKey(i)), kMaxSaveTicks);
    }

    c.region.enabled = value<bool>("region-enabled");
    c.region.startPct = toU8(value<std::int64_t>("region-start"), 100);
    c.region.endPct = toU8(value<std::int64_t>("region-end"), 100);
    c.region.extraTicks = toU16(value<std::int64_t>("region-extra"), kMaxSaveTicks);

    c.correctBoth = value<bool>("correct-both");
    c.adaptive = value<bool>("adaptive");
    c.preserveHold = value<bool>("preserve-hold");
    c.safeMode = value<bool>("safe-mode");
    c.dropUnneeded = value<bool>("drop-unneeded");
    c.saveReleases = value<bool>("save-releases");
    c.safetyMargin = static_cast<int>(value<std::int64_t>("safety-margin"));

    c.orbBuffer = value<bool>("orb-buffer");
    c.orbBufferTicks = static_cast<int>(std::clamp<std::int64_t>(value<std::int64_t>("orb-buffer-ticks"), 4, 60));
    c.ignoreSpam = value<bool>("ignore-spam");
    c.practiceOnly = value<bool>("practice-only");
    c.platformer = value<bool>("platformer");
    c.saveSound = value<bool>("save-sound");
    c.showPath = value<bool>("show-path");
    c.floatingButton = value<bool>("floating-button");

    c.showHud = value<bool>("show-hud");
    c.showTimingBar = value<bool>("show-timing-bar");
    c.showDeathSummary = value<bool>("show-death-summary");
    c.hudScale = static_cast<float>(value<double>("hud-scale"));

    for (std::size_t i = 0; i < kPaletteSize; ++i) {
        auto col = value<ccColor4B>(kPaletteKeys[i]);
        c.palette[i] = RGBA{col.r, col.g, col.b, col.a};
    }

    c.simBudgetMs = static_cast<float>(value<double>("sim-budget-ms"));
    c.directInject = value<bool>("direct-inject");
    c.fullSync = value<bool>("full-sync");
    c.debugLog = value<bool>("debug-log");
    c.validateSim = value<bool>("validate-sim");
    return c;
}

// Escribe sólo los campos que maneja un preset.
void writePresetFields(Config const& c) {
    auto* mod = Mod::get();
    mod->setSettingValue<std::int64_t>("max-ticks", c.globalMaxTicks);
    for (std::size_t i = 0; i < kModeCount; ++i) {
        mod->setSettingValue<std::int64_t>(modeKey(i), c.modeMaxTicks[i]);
    }
    mod->setSettingValue<bool>("adaptive", c.adaptive);
}

bool isPresetControlled(std::string_view key) {
    return key == "max-ticks" || key == "adaptive" || key.starts_with("ticks-");
}

void onSettingChanged(std::string_view key) {
    if (g_writing) return;

    if (key == "preset") {
        auto preset = presetFromName(value<std::string>("preset"));
        if (preset && *preset != Preset::Custom) {
            Config tmp = g_cache;
            applyPreset(tmp, *preset);
            g_writing = true;
            writePresetFields(tmp);
            g_writing = false;
        }
    }

    g_cache = readAll();

    // Edición manual de un valor de preset -> "Personalizado" (o el preset
    // que casualmente coincida).
    if (isPresetControlled(key)) {
        auto detected = detectPreset(g_cache);
        if (detected != g_cache.preset) {
            g_writing = true;
            Mod::get()->setSettingValue<std::string>("preset", std::string(presetName(detected)));
            g_writing = false;
            g_cache.preset = detected;
        }
    }

    if (g_onChanged) g_onChanged();
}

// Registra un listener tipado por cada setting. (Usamos listeners por clave
// en vez de uno global para que cada tipo se lea con su SettingV3 exacto.)
template <class T>
void listen(std::string key) {
    // El handle queda "leakeado" a propósito: el listener vive lo que vive el mod.
    (void)geode::listenForSettingChanges<T>(key, [key](T) { onSettingChanged(key); });
}

} // namespace

Config const& get() { return g_cache; }

void reload() { g_cache = readAll(); }

void setOnChanged(std::function<void()> callback) { g_onChanged = std::move(callback); }

void install() {
    reload();

    for (auto key : {"enabled", "region-enabled", "correct-both", "adaptive", "preserve-hold", "safe-mode",
                     "drop-unneeded", "save-releases", "orb-buffer", "ignore-spam", "practice-only", "platformer",
                     "save-sound", "show-path", "floating-button",
                     "show-hud", "show-timing-bar", "show-death-summary", "debug-log", "validate-sim", "direct-inject", "full-sync"}) {
        listen<bool>(key);
    }
    for (auto key : {"max-ticks", "region-start", "region-end", "region-extra", "safety-margin", "orb-buffer-ticks"}) {
        listen<std::int64_t>(key);
    }
    for (std::size_t i = 0; i < kModeCount; ++i) listen<std::int64_t>(modeKey(i));
    listen<double>("hud-scale");
    listen<double>("sim-budget-ms");
    listen<std::string>("preset");
    for (auto key : kPaletteKeys) listen<ccColor4B>(std::string(key));
}

void write(Config const& c) {
    auto* mod = Mod::get();
    g_writing = true;
    mod->setSettingValue<bool>("enabled", c.enabled);
    mod->setSettingValue<std::string>("preset", std::string(presetName(c.preset)));
    writePresetFields(c);
    mod->setSettingValue<bool>("region-enabled", c.region.enabled);
    mod->setSettingValue<std::int64_t>("region-start", c.region.startPct);
    mod->setSettingValue<std::int64_t>("region-end", c.region.endPct);
    mod->setSettingValue<std::int64_t>("region-extra", c.region.extraTicks);
    mod->setSettingValue<bool>("correct-both", c.correctBoth);
    mod->setSettingValue<bool>("preserve-hold", c.preserveHold);
    mod->setSettingValue<bool>("safe-mode", c.safeMode);
    mod->setSettingValue<bool>("drop-unneeded", c.dropUnneeded);
    mod->setSettingValue<bool>("save-releases", c.saveReleases);
    mod->setSettingValue<std::int64_t>("safety-margin", c.safetyMargin);
    mod->setSettingValue<bool>("orb-buffer", c.orbBuffer);
    mod->setSettingValue<std::int64_t>("orb-buffer-ticks", c.orbBufferTicks);
    mod->setSettingValue<bool>("ignore-spam", c.ignoreSpam);
    mod->setSettingValue<bool>("practice-only", c.practiceOnly);
    mod->setSettingValue<bool>("platformer", c.platformer);
    mod->setSettingValue<bool>("save-sound", c.saveSound);
    mod->setSettingValue<bool>("show-path", c.showPath);
    mod->setSettingValue<bool>("floating-button", c.floatingButton);
    mod->setSettingValue<bool>("show-hud", c.showHud);
    mod->setSettingValue<bool>("show-timing-bar", c.showTimingBar);
    mod->setSettingValue<bool>("show-death-summary", c.showDeathSummary);
    mod->setSettingValue<double>("hud-scale", static_cast<double>(c.hudScale));
    for (std::size_t i = 0; i < kPaletteSize; ++i) {
        auto const& p = c.palette[i];
        mod->setSettingValue<ccColor4B>(kPaletteKeys[i], ccColor4B{p.r, p.g, p.b, p.a});
    }
    mod->setSettingValue<double>("sim-budget-ms", static_cast<double>(c.simBudgetMs));
    mod->setSettingValue<bool>("direct-inject", c.directInject);
    mod->setSettingValue<bool>("full-sync", c.fullSync);
    mod->setSettingValue<bool>("debug-log", c.debugLog);
    mod->setSettingValue<bool>("validate-sim", c.validateSim);
    g_writing = false;

    g_cache = readAll();
    if (g_onChanged) g_onChanged();
}

void update(std::function<void(Config&)> const& mutator) {
    Config c = g_cache;
    mutator(c);
    // El preset siempre refleja los valores reales: si el menú tocó un tick
    // a mano queda "Personalizado"; si eligió un preset, coincide con él.
    c.preset = detectPreset(c);
    write(c);
}

} // namespace ts::settings
