#include "FloatingButton.hpp"

#include "ModMenuPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace ts::ui {
namespace {
constexpr char const* kSaveX = "floating-button-x";
constexpr char const* kSaveY = "floating-button-y";
constexpr float kTouchPad = 8.f;   // margen extra para acertarle con el dedo
constexpr float kDragSlop = 6.f;   // cuánto hay que mover para que sea arrastre
} // namespace

FloatingButton* FloatingButton::create() {
    auto ret = new FloatingButton();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool FloatingButton::init() {
    if (!CCLayer::init()) return false;
    auto win = CCDirector::get()->getWinSize();
    this->setContentSize(win);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition({0.f, 0.f});
    this->setID("floating-button"_spr);
    // Prioridad alta: un toque sobre el botón no llega al juego (no salta).
    // Los toques fuera del botón devuelven false y siguen de largo.
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-1001);
    this->setTouchEnabled(true);

    auto label = CCLabelBMFont::create("TS", "bigFont.fnt");
    label->setScale(.5f);
    auto sprite = CircleButtonSprite::create(label, CircleBaseColor::Cyan, CircleBaseSize::Small);
    if (!sprite) return false;
    float width = std::max(1.f, sprite->getContentSize().width);
    sprite->setScale(m_size / width);
    sprite->setOpacity(210);
    m_face = sprite;
    this->addChild(sprite, 1);

    CCPoint home(win.width - 30.f, win.height * .62f);
    double sx = Mod::get()->getSavedValue<double>(kSaveX, -1.0);
    double sy = Mod::get()->getSavedValue<double>(kSaveY, -1.0);
    if (sx >= 0.0 && sx <= 1.0 && sy >= 0.0 && sy <= 1.0) {
        home = CCPoint(static_cast<float>(sx) * win.width, static_cast<float>(sy) * win.height);
    }
    setHome(home);
    return true;
}

bool FloatingButton::acceptsTouches() {
    if (!this->isVisible()) return false;
    auto* pl = PlayLayer::get();
    // En pausa (o con el menú abierto) no tapamos los botones de la pausa.
    return pl && !pl->m_isPaused;
}

void FloatingButton::setHome(CCPoint position) {
    auto win = CCDirector::get()->getWinSize();
    float margin = m_size * .5f + 2.f;
    m_home = CCPoint(std::clamp(position.x, margin, std::max(margin, win.width - margin)),
                     std::clamp(position.y, margin, std::max(margin, win.height - margin)));
    if (m_face) m_face->setPosition(m_home);
}

void FloatingButton::savePosition() {
    auto win = CCDirector::get()->getWinSize();
    Mod::get()->setSavedValue<double>(kSaveX, std::clamp(m_home.x / std::max(1.f, win.width), 0.f, 1.f));
    Mod::get()->setSavedValue<double>(kSaveY, std::clamp(m_home.y / std::max(1.f, win.height), 0.f, 1.f));
}

bool FloatingButton::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!touch || !m_face || !acceptsTouches()) return false;
    CCPoint local = this->convertTouchToNodeSpace(touch);
    float reach = m_size * .5f + kTouchPad;
    float dx = local.x - m_home.x;
    float dy = local.y - m_home.y;
    if (dx * dx + dy * dy > reach * reach) return false;  // no es para nosotros

    m_dragging = true;
    m_moved = false;
    m_dragStart = local;
    m_dragOffset = CCPoint(m_home.x - local.x, m_home.y - local.y);
    float base = m_size / std::max(1.f, m_face->getContentSize().width);
    m_face->stopAllActions();
    m_face->runAction(CCScaleTo::create(.08f, base * .9f));
    return true;
}

void FloatingButton::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_dragging || !m_face || !touch) return;
    CCPoint local = this->convertTouchToNodeSpace(touch);
    float dx = local.x - m_dragStart.x;
    float dy = local.y - m_dragStart.y;
    if (!m_moved && dx * dx + dy * dy > kDragSlop * kDragSlop) {
        m_moved = true;
        float base = m_size / std::max(1.f, m_face->getContentSize().width);
        m_face->stopAllActions();
        m_face->runAction(CCScaleTo::create(.08f, base * 1.08f));
    }
    if (m_moved) setHome(CCPoint(local.x + m_dragOffset.x, local.y + m_dragOffset.y));
}

void FloatingButton::ccTouchEnded(CCTouch*, CCEvent*) {
    if (!m_dragging) return;
    m_dragging = false;
    if (m_face) {
        float base = m_size / std::max(1.f, m_face->getContentSize().width);
        m_face->stopAllActions();
        m_face->runAction(CCScaleTo::create(.1f, base));
    }
    if (m_moved) {
        m_moved = false;
        savePosition();
        return;
    }
    // Abrir el menú fuera del manejo del toque (pausa + popup).
    geode::queueInMainThread([] { openModMenu(); });
}

void FloatingButton::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_dragging = false;
    m_moved = false;
    if (m_face) {
        m_face->stopAllActions();
        m_face->setScale(m_size / std::max(1.f, m_face->getContentSize().width));
    }
}

} // namespace ts::ui
