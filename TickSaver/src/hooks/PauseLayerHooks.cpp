// ============================================================================
//  TickSaver · hooks/PauseLayerHooks.cpp
//  Botón "TS" en el menú de pausa -> popup de estadísticas / config.
// ============================================================================

#include "../game/Engine.hpp"
#include "../game/Settings.hpp"
#include "../ui/StatsPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(TSPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!ts::Engine::current()) return;

        auto label = CCLabelBMFont::create("TS", "bigFont.fnt");
        label->setScale(.55f);
        auto sprite = CircleButtonSprite::create(label, CircleBaseColor::Cyan, CircleBaseSize::Small);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(TSPauseLayer::onTickSaver));
        button->setID("ticksaver-button"_spr);

        // Los menús del PauseLayer tienen IDs gracias a Geode. Si otro mod
        // los reorganiza y no existe, creamos nuestro propio menú.
        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(button);
            menu->updateLayout();
        }
        else {
            auto win = CCDirector::get()->getWinSize();
            auto fallback = CCMenu::create();
            fallback->setPosition({win.width - 32.f, 32.f});
            fallback->addChild(button);
            this->addChild(fallback);
        }
    }

    void onTickSaver(CCObject*) {
        auto* engine = ts::Engine::current();
        if (!engine) return;
        auto popup = ts::ui::StatsPopup::create(engine->stats(), ts::settings::get(), [] {
            if (auto* e = ts::Engine::current()) e->resetStats();
        });
        if (popup) popup->show();
    }
};
