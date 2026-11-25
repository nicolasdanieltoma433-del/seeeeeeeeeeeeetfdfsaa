#include "ModMenuPopup.hpp"

#include "../core/Config.hpp"
#include "../core/ConfigCode.hpp"
#include "../game/Engine.hpp"
#include "../game/Settings.hpp"
#include "Palette.hpp"
#include "StatsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/ColorChannelSprite.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/SliderNode.hpp>
#include <Geode/ui/Toggler.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

using namespace geode::prelude;

namespace ts::ui {
namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 300.f;
constexpr float kContentW = 410.f;
constexpr float kContentH = 165.f;
constexpr float kRowLabelX = 14.f;
constexpr float kRowControlX = 392.f;

constexpr std::array<char const*, static_cast<std::size_t>(5)> kTabNames = {
    "General", "Modos", "Tramo", "Opciones", "HUD",
};

// Nombres cortos de los 8 colores (mismo orden que PaletteRole).
constexpr std::array<char const*, kPaletteSize> kColorNames = {
    "Racha", "Justo", "Temprano", "Salvado", "Peligro", "Fondo", "Texto", "Histog.",
};

Config const& cfg() { return settings::get(); }
RGBA textColor() { return role(cfg().palette, PaletteRole::Text); }
RGBA dimColor() { return RGBA{170, 170, 185, 255}; }

std::string ticksText(int t) { return fmt::format("{}t", t); }

} // namespace

ModMenuPopup* ModMenuPopup::create() {
    auto ret = new ModMenuPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ModMenuPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    this->setTitle("TickSaver");

    m_tabRow = CCNode::create();
    m_tabRow->setContentSize({kContentW, 26.f});
    m_tabRow->setAnchorPoint({.5f, .5f});
    m_mainLayer->addChildAtPosition(m_tabRow, Anchor::Top, {0.f, -50.f});

    m_content = CCNode::create();
    m_content->setContentSize({kContentW, kContentH});
    m_content->setAnchorPoint({.5f, .5f});
    m_mainLayer->addChildAtPosition(m_content, Anchor::Center, {0.f, -6.f});

    buildFooter();
    rebuild();
    return true;
}

// ------------------------------------------------------------- reconstruir

void ModMenuPopup::requestRebuild() {
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    // Ref fuerte (no WeakRef): mantiene vivo el popup hasta el próximo
    // frame. Si para entonces ya se cerró, no reconstruimos nada.
    Ref<ModMenuPopup> self(this);
    geode::queueInMainThread([self] {
        self->m_rebuildPending = false;
        if (self->getParent()) self->rebuild();
    });
}

void ModMenuPopup::rebuild() {
    m_building = true;
    m_tabRow->removeAllChildren();
    m_content->removeAllChildren();
    buildTabs();
    switch (m_tab) {
        case Tab::General: buildGeneral(); break;
        case Tab::Modes: buildModes(); break;
        case Tab::Region: buildRegion(); break;
        case Tab::Options: buildOptions(); break;
        case Tab::Hud: buildHud(); break;
        case Tab::Count: break;
    }
    m_building = false;
}

// ----------------------------------------------------------------- helpers

CCLabelBMFont* ModMenuPopup::addLabel(CCNode* parent, std::string const& text, CCPoint pos, float scale,
                                      CCPoint anchor, RGBA color, char const* font) {
    auto label = CCLabelBMFont::create(text.c_str(), font);
    label->setScale(scale);
    label->setAnchorPoint(anchor);
    label->setColor(c3(color));
    label->setPosition(pos);
    parent->addChild(label);
    return label;
}

void ModMenuPopup::addButton(CCNode* parent, char const* text, CCPoint pos, float scale, bool highlighted,
                             std::function<void()> callback) {
    auto sprite = ButtonSprite::create(text, "bigFont.fnt", highlighted ? "GJ_button_01.png" : "GJ_button_04.png", .6f);
    auto button = Button::createWithNode(sprite, [callback = std::move(callback)](Button*) { callback(); });
    button->setScale(scale);
    button->setPosition(pos);
    parent->addChild(button);
}

void ModMenuPopup::addToggleRow(char const* label, float y, bool value, std::function<void(bool)> onChange) {
    addLabel(m_content, label, {kRowLabelX, y}, .42f, {0.f, .5f}, textColor());
    auto toggler = Toggler::createWithStandardSprites(.6f, [this, onChange = std::move(onChange)](Toggler*, bool on) {
        if (!m_building) onChange(on);
    });
    toggler->setToggled(value);
    toggler->setPosition({kRowControlX - 8.f, y});
    m_content->addChild(toggler);
}

void ModMenuPopup::addSliderRow(char const* label, float y, float min, float max, float step, float value,
                                std::function<std::string(int)> format, std::function<void(int)> onChange,
                                bool rebuildOnRelease) {
    addLabel(m_content, label, {kRowLabelX, y}, .38f, {0.f, .5f}, textColor());
    auto valueLabel = addLabel(m_content, format(static_cast<int>(std::lround(value))), {kRowControlX + 6.f, y}, .4f,
                               {1.f, .5f}, role(cfg().palette, PaletteRole::Streak));

    // Callback compartido por el slider y los botones -/+.
    auto apply = [this, valueLabel, format, onChange](int v) {
        if (m_building) return;
        valueLabel->setString(format(v).c_str());
        onChange(v);
    };

    auto slider = SliderNode::create([apply](SliderNode*, float v) { apply(static_cast<int>(std::lround(v))); });
    slider->setContentSize({220.f, 10.f});
    slider->setScale(.7f);
    slider->setAnchorPoint({.5f, .5f});
    slider->setMin(min);
    slider->setMax(max);
    slider->setSnapStep(step);
    slider->setValue(value);
    slider->setPosition({252.f, y});
    if (rebuildOnRelease) {
        slider->setReleaseCallback([this](SliderNode*, float) { requestRebuild(); });
    }
    m_content->addChild(slider);

    // -/+ para ajustes finos (en celular arrastrar el slider de a 1 es incómodo).
    // Puntero crudo seguro: el slider y estos botones son hermanos en
    // m_content y se destruyen juntos en cada rebuild.
    auto nudge = [this, slider, apply, min, max, step](int dir) {
        auto* s = slider;
        float v = std::clamp(s->getValue() + static_cast<float>(dir) * step, min, max);
        s->setValue(v);
        apply(static_cast<int>(std::lround(v)));
        requestRebuild();
    };
    addButton(m_content, "-", {162.f, y}, .55f, false, [nudge] { nudge(-1); });
    addButton(m_content, "+", {342.f, y}, .55f, false, [nudge] { nudge(+1); });
}

// ------------------------------------------------------------------- tabs

void ModMenuPopup::buildTabs() {
    float slotW = kContentW / static_cast<float>(kTabNames.size());
    for (std::size_t i = 0; i < kTabNames.size(); ++i) {
        auto tab = static_cast<Tab>(i);
        addButton(m_tabRow, kTabNames[i], {slotW * (static_cast<float>(i) + .5f), 13.f}, .62f, tab == m_tab, [this, tab] {
            m_tab = tab;
            requestRebuild();
        });
    }
}

void ModMenuPopup::buildGeneral() {
    auto const& c = cfg();

    addToggleRow("Mod activado", 148.f, c.enabled, [](bool on) {
        settings::update([on](Config& x) { x.enabled = on; });
    });

    // Preset con flechas (sin "Personalizado": a ese se llega tocando valores).
    addLabel(m_content, "Preset", {kRowLabelX, 116.f}, .42f, {0.f, .5f}, textColor());
    addLabel(m_content, std::string(presetName(c.preset)), {300.f, 116.f}, .45f, {.5f, .5f},
             role(c.palette, PaletteRole::Streak));
    auto cycle = [this](int dir) {
        constexpr int kSelectable = static_cast<int>(kPresetCount) - 1;  // sin Custom
        int cur = static_cast<int>(cfg().preset);
        int next = cur >= kSelectable ? (dir > 0 ? 0 : kSelectable - 1) : (cur + dir + kSelectable) % kSelectable;
        settings::update([next](Config& x) { applyPreset(x, static_cast<Preset>(next)); });
        requestRebuild();
    };
    addButton(m_content, "<", {214.f, 116.f}, .55f, false, [cycle] { cycle(-1); });
    addButton(m_content, ">", {386.f, 116.f}, .55f, false, [cycle] { cycle(+1); });

    addSliderRow("Ticks maximos", 84.f, 0.f, static_cast<float>(kMaxSaveTicks), 1.f, c.globalMaxTicks, ticksText,
                 [](int v) { settings::update([v](Config& x) { x.globalMaxTicks = static_cast<std::uint8_t>(v); }); },
                 true);

    addToggleRow("Modo seguro", 50.f, c.safeMode, [this](bool on) { onSafeModeToggled(on); });
    addLabel(m_content,
             c.safeMode ? "Los intentos en los que te salva no guardan progreso"
                        : "DESACTIVADO: los intentos asistidos SI guardan progreso",
             {kRowLabelX, 32.f}, .28f, {0.f, .5f},
             c.safeMode ? dimColor() : role(c.palette, PaletteRole::Danger));

    addLabel(m_content, "Los ticks de cada personaje se ajustan en la pestana Modos", {kContentW / 2.f, 8.f}, .27f,
             {.5f, .5f}, dimColor());
}

void ModMenuPopup::buildModes() {
    auto const& c = cfg();

    // Grilla 4x2 con un botón por personaje: "Wave 24t" / "Cubo G6".
    for (std::size_t i = 0; i < kModeCount; ++i) {
        auto mode = static_cast<Mode>(i);
        int own = c.modeMaxTicks[i];
        auto text = own == 0 ? fmt::format("{} G{}", kModeNames[i], c.globalMaxTicks)
                             : fmt::format("{} {}t", kModeNames[i], own);
        float x = 55.f + 100.f * static_cast<float>(i % 4);
        float y = i < 4 ? 146.f : 116.f;
        addButton(m_content, text.c_str(), {x, y}, .55f, mode == m_selectedMode, [this, mode] {
            m_selectedMode = mode;
            requestRebuild();
        });
    }

    auto mi = idx(m_selectedMode);
    int own = c.modeMaxTicks[mi];
    addLabel(m_content,
             own == 0 ? fmt::format("{}: usa el maximo general ({}t)", kModeNames[mi], c.globalMaxTicks)
                      : fmt::format("{}: {} ticks propios", kModeNames[mi], own),
             {kContentW / 2.f, 84.f}, .4f, {.5f, .5f}, role(c.palette, PaletteRole::Streak));

    auto selected = m_selectedMode;
    addSliderRow("Ticks", 54.f, 0.f, static_cast<float>(kMaxSaveTicks), 1.f, own,
                 [](int v) { return v == 0 ? std::string("global") : fmt::format("{}t", v); },
                 [selected](int v) {
                     settings::update([selected, v](Config& x) { x.modeMaxTicks[idx(selected)] = static_cast<std::uint8_t>(v); });
                 },
                 true);

    addButton(m_content, "Usar el general", {kContentW / 2.f, 22.f}, .5f, false, [this, selected] {
        settings::update([selected](Config& x) { x.modeMaxTicks[idx(selected)] = 0; });
        requestRebuild();
    });
    addLabel(m_content, "G = usa el maximo general", {kContentW - 6.f, 4.f}, .25f, {1.f, .5f}, dimColor());
}

void ModMenuPopup::buildRegion() {
    auto const& c = cfg();
    addToggleRow("Ticks extra en un tramo del nivel", 148.f, c.region.enabled, [](bool on) {
        settings::update([on](Config& x) { x.region.enabled = on; });
    });

    auto pct = [](int v) { return fmt::format("{}%", v); };
    addSliderRow("Desde", 114.f, 0.f, 100.f, 1.f, c.region.startPct, pct,
                 [](int v) { settings::update([v](Config& x) { x.region.startPct = static_cast<std::uint8_t>(v); }); },
                 true);
    addSliderRow("Hasta", 82.f, 0.f, 100.f, 1.f, c.region.endPct, pct,
                 [](int v) { settings::update([v](Config& x) { x.region.endPct = static_cast<std::uint8_t>(v); }); },
                 true);
    addSliderRow("Ticks extra", 50.f, 0.f, static_cast<float>(kMaxSaveTicks), 1.f, c.region.extraTicks,
                 [](int v) { return fmt::format("+{}t", v); },
                 [](int v) { settings::update([v](Config& x) { x.region.extraTicks = static_cast<std::uint8_t>(v); }); });

    bool invalid = c.region.enabled && c.region.endPct <= c.region.startPct;
    addLabel(m_content,
             invalid ? "\"Hasta\" tiene que ser mayor que \"Desde\""
                     : "Ej: 40% a 60% para la parte mas dificil del nivel",
             {kContentW / 2.f, 16.f}, .3f, {.5f, .5f},
             invalid ? role(c.palette, PaletteRole::Danger) : dimColor());
}

void ModMenuPopup::buildOptions() {
    auto const& c = cfg();
    struct Row {
        char const* label;
        bool value;
        bool Config::*field;
    };
    Row const rows[] = {
        {"Modo adaptativo (aprende de vos)", c.adaptive, &Config::adaptive},
        {"Corregir a los dos jugadores", c.correctBoth, &Config::correctBoth},
        {"Conservar duracion del hold", c.preserveHold, &Config::preserveHold},
        {"Copia completa del jugador (recomendado)", c.fullSync, &Config::fullSync},
        {"Reinyeccion directa (recomendado)", c.directInject, &Config::directInject},
        {"Log de depuracion", c.debugLog, &Config::debugLog},
        {"Validar fantasma", c.validateSim, &Config::validateSim},
    };
    float y = 158.f;
    for (auto const& row : rows) {
        auto field = row.field;
        addToggleRow(row.label, y, row.value, [field](bool on) {
            settings::update([field, on](Config& x) { x.*field = on; });
        });
        y -= 20.f;
    }
    // En décimas de ms para que el slider sea entero: 5..100 = 0.5..10 ms.
    addSliderRow("CPU por frame", 12.f, 5.f, 100.f, 5.f, std::round(c.simBudgetMs * 10.f),
                 [](int v) { return fmt::format("{:.1f} ms", static_cast<double>(v) / 10.0); },
                 [](int v) { settings::update([v](Config& x) { x.simBudgetMs = static_cast<float>(v) / 10.f; }); });
}

void ModMenuPopup::buildHud() {
    auto const& c = cfg();
    addToggleRow("Mostrar HUD (racha)", 150.f, c.showHud, [](bool on) {
        settings::update([on](Config& x) { x.showHud = on; });
    });
    addToggleRow("Barra de timing", 126.f, c.showTimingBar, [](bool on) {
        settings::update([on](Config& x) { x.showTimingBar = on; });
    });
    addToggleRow("Resumen al morir", 102.f, c.showDeathSummary, [](bool on) {
        settings::update([on](Config& x) { x.showDeathSummary = on; });
    });
    addSliderRow("Escala", 76.f, 50.f, 150.f, 5.f, std::round(c.hudScale * 100.f),
                 [](int v) { return fmt::format("{}%", v); },
                 [](int v) { settings::update([v](Config& x) { x.hudScale = static_cast<float>(v) / 100.f; }); });

    // Paleta: 8 muestras; tocar una abre el selector de color de Geode.
    for (std::size_t i = 0; i < kPaletteSize; ++i) {
        float x = 30.f + 50.f * static_cast<float>(i);
        auto swatch = ColorChannelSprite::create();
        swatch->setColor(c3(c.palette[i]));
        auto button = Button::createWithNode(swatch, [this, i](Button*) {
            auto const& col = cfg().palette[i];
            auto picker = ColorPickPopup::create(ccColor4B{col.r, col.g, col.b, col.a});
            Ref<ModMenuPopup> self(this);
            picker->setCallback([self, i](ccColor4B const& picked) {
                settings::update([&](Config& x) { x.palette[i] = RGBA{picked.r, picked.g, picked.b, picked.a}; });
                self->requestRebuild();
            });
            picker->show();
        });
        button->setScale(.5f);
        button->setPosition({x, 40.f});
        m_content->addChild(button);
        addLabel(m_content, kColorNames[i], {x, 18.f}, .45f, {.5f, .5f}, textColor(), "chatFont.fnt");
    }
}

// ---------------------------------------------------------------- footer

void ModMenuPopup::buildFooter() {
    auto footer = CCNode::create();
    footer->setContentSize({kContentW, 60.f});
    footer->setAnchorPoint({.5f, 0.f});
    m_mainLayer->addChildAtPosition(footer, Anchor::Bottom, {0.f, 4.f});

    addButton(footer, "Estadisticas", {62.f, 46.f}, .55f, false, [this] { onStats(); });
    addButton(footer, "Exportar", {166.f, 46.f}, .55f, false, [this] { onExport(); });
    addButton(footer, "Importar", {258.f, 46.f}, .55f, false, [this] { onImport(); });
    addButton(footer, "Restablecer", {356.f, 46.f}, .55f, false, [this] { onRestoreDefaults(); });

    // Aviso de responsabilidad, siempre visible al pie del menú.
    addLabel(footer, "AVISO: usalo bajo tu propia conciencia y responsabilidad.", {kContentW / 2.f, 22.f}, .5f,
             {.5f, .5f}, role(cfg().palette, PaletteRole::Danger), "chatFont.fnt");
    addLabel(footer, "El creador no se hace cargo de baneos ni de perdida de progreso o cuentas.",
             {kContentW / 2.f, 9.f}, .45f, {.5f, .5f}, dimColor(), "chatFont.fnt");
}

// --------------------------------------------------------------- acciones

void ModMenuPopup::onSafeModeToggled(bool enabled) {
    if (enabled) {
        settings::update([](Config& x) { x.safeMode = true; });
        requestRebuild();
        return;
    }
    Ref<ModMenuPopup> self(this);
    createQuickPopup(
        "Desactivar modo seguro",
        "Sin modo seguro, los intentos en los que <cr>TickSaver</c> te ayudo <cy>guardan progreso y completados</c>.\n"
        "Subir records o completados asi va contra las reglas de GD y puede terminar en <cr>baneo</c>.\n"
        "Lo usas bajo tu propia responsabilidad.",
        "Cancelar", "Desactivar",
        [self](FLAlertLayer*, bool confirmed) {
            if (confirmed) settings::update([](Config& x) { x.safeMode = false; });
            // Si canceló, el toggle volvió a quedar marcado: redibujamos igual.
            self->requestRebuild();
        });
}

void ModMenuPopup::onStats() {
    auto* engine = Engine::current();
    if (!engine) {
        Notification::create("Entra a un nivel para ver sus estadisticas", NotificationIcon::Info)->show();
        return;
    }
    auto popup = StatsPopup::create(engine->stats(), cfg(), [] {
        if (auto* e = Engine::current()) e->resetStats();
    });
    if (popup) popup->show();
}

void ModMenuPopup::onExport() {
    auto code = encodeConfig(cfg());
    if (clipboard::write(code)) {
        Notification::create(fmt::format("Codigo copiado: {}", code), NotificationIcon::Success, 2.5f)->show();
    }
    else {
        Notification::create("No se pudo copiar al portapapeles", NotificationIcon::Error)->show();
    }
}

void ModMenuPopup::onImport() {
    auto result = decodeConfig(clipboard::read(), cfg());
    if (auto* err = std::get_if<CodeError>(&result)) {
        Notification::create(fmt::format("Importar: {}", err->message), NotificationIcon::Error, 2.5f)->show();
        return;
    }
    auto const& imported = std::get<Config>(result);
    settings::write(imported);
    Notification::create(fmt::format("Config importada (preset {})", presetName(imported.preset)),
                         NotificationIcon::Success)
        ->show();
    requestRebuild();
}

void ModMenuPopup::onRestoreDefaults() {
    Ref<ModMenuPopup> self(this);
    createQuickPopup("Restablecer", "Volver toda la configuracion de <cy>TickSaver</c> a los valores de fabrica?",
                     "Cancelar", "Restablecer", [self](FLAlertLayer*, bool confirmed) {
                         if (!confirmed) return;
                         Config defaults;
                         applyPreset(defaults, Preset::Normal);
                         settings::write(defaults);
                         self->requestRebuild();
                     });
}

} // namespace ts::ui
