#pragma once
// ============================================================================
//  TickSaver · ui/ModMenuPopup.hpp
//  Menú del mod dentro del juego (botón "TS" en pausa y en el menú principal).
//
//   [General] [Modos] [Tramo] [Opciones] [HUD]
//   ┌──────────────────────────────────────────┐
//   │  contenido de la pestaña                  │
//   └──────────────────────────────────────────┘
//   [Estadisticas] [Exportar] [Importar] [Restablecer]
//   AVISO: usalo bajo tu propia conciencia y responsabilidad...
//
//  Todo cambio se escribe en los settings de Geode (settings::update), así
//  el HUD y el motor lo aplican al instante y queda guardado.
//
//  Ojo con los callbacks: un botón no puede destruirse a sí mismo mientras
//  procesa su toque, por eso los cambios que redibujan el menú usan
//  requestRebuild(), que reconstruye en el próximo frame.
// ============================================================================

#include "../core/Types.hpp"

#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>

namespace ts::ui {

class ModMenuPopup : public geode::Popup {
public:
    static ModMenuPopup* create();

protected:
    bool init();

private:
    enum class Tab : int { General = 0, Modes, Region, Options, Hud, Count };

    void rebuild();
    void requestRebuild();

    void buildTabs();
    void buildGeneral();
    void buildModes();
    void buildRegion();
    void buildOptions();
    void buildHud();
    void buildFooter();

    // --- helpers de UI -------------------------------------------------------
    cocos2d::CCLabelBMFont* addLabel(cocos2d::CCNode* parent, std::string const& text, cocos2d::CCPoint pos,
                                     float scale, cocos2d::CCPoint anchor, RGBA color,
                                     char const* font = "bigFont.fnt");
    void addButton(cocos2d::CCNode* parent, char const* text, cocos2d::CCPoint pos, float scale, bool highlighted,
                   std::function<void()> callback);
    void addToggleRow(char const* label, float y, bool value, std::function<void(bool)> onChange);
    // Fila con etiqueta + slider + valor. `format` arma el texto del valor.
    void addSliderRow(char const* label, float y, float min, float max, float step, float value,
                      std::function<std::string(int)> format, std::function<void(int)> onChange,
                      bool rebuildOnRelease = false);

    // --- acciones -----------------------------------------------------------
    void onSafeModeToggled(bool enabled);
    void onStats();
    void onExport();
    void onImport();
    void onRestoreDefaults();

    cocos2d::CCNode* m_tabRow = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    Tab m_tab = Tab::General;
    Mode m_selectedMode = Mode::Cube;
    bool m_building = false;        // ignora callbacks disparados al armar la UI
    bool m_rebuildPending = false;
};

} // namespace ts::ui
