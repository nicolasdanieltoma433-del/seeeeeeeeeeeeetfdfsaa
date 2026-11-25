#pragma once
// ============================================================================
//  TickSaver · ui/HudNode.hpp
//  HUD en pantalla durante el nivel (se cuelga del UILayer, no se mueve con
//  la cámara):
//
//              x12              <- racha de clicks buenos (pulsa al subir)
//     [=========|=====|##]      <- barra de timing: izquierda = muy temprano,
//     temprano por 3t: salvado     derecha (verde) = justo. Marcador = último click
//          esperando +2t        <- mientras hay un click retenido
//          SALVADO +3t          <- aviso flotante cuando te salva
// ============================================================================

#include "../core/Config.hpp"
#include "../core/InputScheduler.hpp"

#include <Geode/Geode.hpp>

namespace ts::ui {

class HudNode : public cocos2d::CCNode {
public:
    static HudNode* create();

    void applyConfig(Config const& cfg);
    void onClick(ClickEvent const& ev, int streak, bool streakIncreased);
    void setHolding(int ticksHeld);
    // Cartel "INTENTO ASISTIDO: NO GUARDA" (modo seguro activo en este intento).
    void setAssisted(bool assisted);
    // "PREDICCION FALLIDA": dijo salvado y moriste igual enseguida.
    void showFailedSave();
    void resetAttempt();

protected:
    bool init() override;

private:
    void redrawBar();
    float markerX(int earlyTicks) const;
    void spawnToast(std::string const& text, RGBA color);

    cocos2d::CCLabelBMFont* m_streakLabel = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCLabelBMFont* m_holdLabel = nullptr;
    cocos2d::CCLabelBMFont* m_assistedLabel = nullptr;
    cocos2d::CCDrawNode* m_bar = nullptr;
    cocos2d::CCNode* m_barRoot = nullptr;

    Config m_cfg{};
    bool m_hasMarker = false;
    float m_markerPos = 0.f;
    RGBA m_markerColor{};
    int m_lastHolding = -1;
};

} // namespace ts::ui
