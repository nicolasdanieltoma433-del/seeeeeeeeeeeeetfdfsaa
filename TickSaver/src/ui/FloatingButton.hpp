#pragma once
// ============================================================================
//  TickSaver · ui/FloatingButton.hpp
//  Botón "TS" flotante dentro del nivel (opción "Boton flotante en el nivel").
//    * tocarlo pausa el juego y abre el menú del mod;
//    * arrastrarlo lo mueve, y la posición queda guardada;
//    * los toques fuera del botón siguen llegando al juego normalmente;
//    * en pausa no recibe toques (no tapa los botones del menú de pausa).
//  Es una capa del tamaño de la pantalla con prioridad de toque alta, igual
//  que el botón de Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include <Geode/cocos/layers_scenes_transitions_nodes/CCLayer.h>

namespace ts::ui {

class FloatingButton : public cocos2d::CCLayer {
public:
    static FloatingButton* create();

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

protected:
    bool init() override;

private:
    void setHome(cocos2d::CCPoint position);
    void savePosition();
    bool acceptsTouches();

    float m_size = 34.f;
    cocos2d::CCPoint m_home;
    cocos2d::CCPoint m_dragStart;
    cocos2d::CCPoint m_dragOffset;
    bool m_dragging = false;
    bool m_moved = false;
    cocos2d::CCNode* m_face = nullptr;
};

} // namespace ts::ui
