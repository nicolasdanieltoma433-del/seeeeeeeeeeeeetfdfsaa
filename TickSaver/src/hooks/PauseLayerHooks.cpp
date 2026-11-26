// ============================================================================
//  TickSaver · hooks/PauseLayerHooks.cpp
//  Botón "TS" que abre el menú del mod:
//    * en el menú de pausa de cualquier nivel
//    * en el menú principal (para configurar antes de jugar)
// ============================================================================

#include "../ui/ModMenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

namespace {

CCMenuItemSpriteExtra* makeMenuButton(CCObject* target, SEL_MenuHandler callback, CircleBaseSize size) {
    auto label = CCLabelBMFont::create("TS", "bigFont.fnt");
    label->setScale(.55f);
    auto sprite = CircleButtonSprite::create(label, CircleBaseColor::Cyan, size);
    auto button = CCMenuItemSpriteExtra::create(sprite, target, callback);
    button->setID("ticksaver-button"_spr);
    return button;
}

// Lo agrega al menú con ese ID; si otro mod lo movió o no existe, crea uno propio.
void addToMenu(CCNode* layer, char const* menuID, CCMenuItemSpriteExtra* button, CCPoint fallbackPos) {
    if (auto menu = layer->getChildByID(menuID)) {
        menu->addChild(button);
        menu->updateLayout();
        return;
    }
    auto fallback = CCMenu::create();
    fallback->setPosition(fallbackPos);
    fallback->addChild(button);
    layer->addChild(fallback);
}

} // namespace

class $modify(TSPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto win = CCDirector::get()->getWinSize();
        addToMenu(this, "right-button-menu", makeMenuButton(this, menu_selector(TSPauseLayer::onTickSaver), CircleBaseSize::Small),
                  {win.width - 32.f, 32.f});
    }

    void onTickSaver(CCObject*) { ts::ui::openModMenu(); }
};

class $modify(TSMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        auto win = CCDirector::get()->getWinSize();
        addToMenu(this, "bottom-menu", makeMenuButton(this, menu_selector(TSMenuLayer::onTickSaver), CircleBaseSize::Medium),
                  {win.width / 2.f, 45.f});
        return true;
    }

    void onTickSaver(CCObject*) { ts::ui::openModMenu(); }
};
