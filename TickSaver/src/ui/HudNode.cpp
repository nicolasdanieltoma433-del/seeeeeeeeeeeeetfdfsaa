#include "HudNode.hpp"

#include "../core/Stats.hpp"
#include "Palette.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace ts::ui {
namespace {

// Geometría de la barra (unidades de cocos, antes de hudScale).
constexpr float kBarW = 150.f;
constexpr float kBarH = 6.f;
constexpr float kGoodW = 22.f;                       // zona verde "justo" a la derecha
constexpr float kLeft = -kBarW / 2.f;                // -75
constexpr float kGoodStart = kBarW / 2.f - kGoodW;   // 53
constexpr float kBarY = -20.f;

RGBA lerp(RGBA a, RGBA b, float t) {
    auto mix = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

} // namespace

HudNode* HudNode::create() {
    auto ret = new HudNode();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool HudNode::init() {
    if (!CCNode::init()) return false;
    this->setID("ticksaver-hud"_spr);

    m_streakLabel = CCLabelBMFont::create("x0", "bigFont.fnt");
    m_streakLabel->setScale(.55f);
    this->addChild(m_streakLabel);

    m_barRoot = CCNode::create();
    this->addChild(m_barRoot);

    m_bar = CCDrawNode::create();
    m_barRoot->addChild(m_bar);

    m_caption = CCLabelBMFont::create("", "bigFont.fnt");
    m_caption->setScale(.3f);
    m_caption->setPosition({0.f, kBarY - 13.f});
    m_barRoot->addChild(m_caption);

    m_holdLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_holdLabel->setScale(.28f);
    m_holdLabel->setPosition({0.f, kBarY - 25.f});
    m_holdLabel->setVisible(false);
    this->addChild(m_holdLabel);

    m_assistedLabel = CCLabelBMFont::create("INTENTO ASISTIDO: NO GUARDA", "bigFont.fnt");
    m_assistedLabel->setScale(.22f);
    m_assistedLabel->setPosition({0.f, 12.f});
    m_assistedLabel->setVisible(false);
    this->addChild(m_assistedLabel);

    return true;
}

void HudNode::applyConfig(Config const& cfg) {
    m_cfg = cfg;
    auto win = CCDirector::get()->getWinSize();
    this->setScale(cfg.hudScale);
    this->setPosition({win.width / 2.f, win.height - 22.f * cfg.hudScale});
    this->setVisible(cfg.showHud);
    m_barRoot->setVisible(cfg.showTimingBar);

    auto const& p = cfg.palette;
    m_streakLabel->setColor(c3(role(p, PaletteRole::Streak)));
    m_caption->setColor(c3(role(p, PaletteRole::Text)));
    m_holdLabel->setColor(c3(role(p, PaletteRole::Early)));
    m_assistedLabel->setColor(c3(role(p, PaletteRole::Danger)));
    redrawBar();
}

float HudNode::markerX(int earlyTicks) const {
    if (earlyTicks <= 0) return kGoodStart + kGoodW / 2.f;
    // Escala raíz cuadrada: 1-10 ticks se distinguen bien aunque el máximo
    // configurado sea 240.
    float maxShown = static_cast<float>(std::max<int>(8, m_cfg.globalMaxTicks));
    float t = std::sqrt(std::min(static_cast<float>(earlyTicks), maxShown) / maxShown);
    return kGoodStart - (kGoodStart - kLeft) * t;
}

void HudNode::redrawBar() {
    auto const& p = m_cfg.palette;
    m_bar->clear();

    // Fondo
    m_bar->drawRect(CCPoint{kLeft - 2.f, kBarY - kBarH / 2.f - 2.f}, CCPoint{-kLeft + 2.f, kBarY + kBarH / 2.f + 2.f},
                    c4f(role(p, PaletteRole::BarBg)), 0.f, ccColor4F{0, 0, 0, 0});

    // Zona "temprano": degradé de peligro (izquierda) a temprano (derecha).
    constexpr int kSegments = 8;
    float segW = (kGoodStart - kLeft) / kSegments;
    for (int i = 0; i < kSegments; ++i) {
        float t = static_cast<float>(i) / (kSegments - 1);
        auto col = lerp(role(p, PaletteRole::Danger), role(p, PaletteRole::Early), t);
        float x0 = kLeft + segW * static_cast<float>(i);
        m_bar->drawRect(CCPoint{x0, kBarY - kBarH / 2.f}, CCPoint{x0 + segW, kBarY + kBarH / 2.f}, c4f(col, .8f), 0.f,
                        ccColor4F{0, 0, 0, 0});
    }

    // Zona "justo"
    m_bar->drawRect(CCPoint{kGoodStart, kBarY - kBarH / 2.f}, CCPoint{-kLeft, kBarY + kBarH / 2.f},
                    c4f(role(p, PaletteRole::Good), .9f), 0.f, ccColor4F{0, 0, 0, 0});

    // Marcador del último click
    if (m_hasMarker) {
        m_bar->drawRect(CCPoint{m_markerPos - 1.5f, kBarY - kBarH / 2.f - 4.f},
                        CCPoint{m_markerPos + 1.5f, kBarY + kBarH / 2.f + 4.f}, c4f(m_markerColor), .6f,
                        ccColor4F{0, 0, 0, .8f});
    }
}

void HudNode::onClick(ClickEvent const& ev, int streak, bool streakIncreased) {
    // Sin evaluar o sin efecto (cubo en el aire): no tocan la racha ni la barra.
    if (ev.kind == ClickKind::Unchecked || ev.kind == ClickKind::Unnecessary) return;
    auto const& p = m_cfg.palette;

    // Racha
    m_streakLabel->setString(fmt::format("x{}", streak).c_str());
    m_streakLabel->setOpacity(streak > 0 ? 255 : 120);
    if (streakIncreased) {
        m_streakLabel->stopAllActions();
        m_streakLabel->setScale(.55f * 1.25f);
        m_streakLabel->runAction(CCEaseBackOut::create(CCScaleTo::create(.18f, .55f)));
    }

    // Barra
    m_hasMarker = ev.kind != ClickKind::Ignored;  // un click descartado no tiene "timing"
    m_markerPos = markerX(ev.earlyTicks);
    m_markerColor = colorForKind(p, ev.kind);
    m_caption->setString(describeClick(LastClick{true, ev.kind, ev.earlyTicks, ev.mode, ev.release, false, ev.orb}).c_str());
    m_caption->setColor(c3(ev.kind == ClickKind::Clean ? role(p, PaletteRole::Good) : m_markerColor));
    redrawBar();

    if (ev.kind == ClickKind::Saved) {
        auto text = ev.orb       ? fmt::format("ORBE +{}t", ev.earlyTicks)
                    : ev.release ? fmt::format("SALVADO +{}t (soltar)", ev.earlyTicks)
                                 : fmt::format("SALVADO +{}t", ev.earlyTicks);
        spawnToast(text, role(p, PaletteRole::Saved));
    }
    else if (ev.kind == ClickKind::Ignored) {
        spawnToast("CLICK IGNORADO", role(p, PaletteRole::Saved));
    }
}

void HudNode::setHolding(int ticksHeld) {
    if (ticksHeld == m_lastHolding) return;
    m_lastHolding = ticksHeld;
    m_holdLabel->setVisible(ticksHeld > 0);
    if (ticksHeld > 0) m_holdLabel->setString(fmt::format("esperando +{}t", ticksHeld).c_str());
}

void HudNode::setAssisted(bool assisted) { m_assistedLabel->setVisible(assisted); }

void HudNode::showFailedSave() {
    spawnToast("PREDICCION FALLIDA", role(m_cfg.palette, PaletteRole::Danger));
}

void HudNode::resetAttempt() {
    m_streakLabel->setString("x0");
    m_streakLabel->setOpacity(120);
    setHolding(0);
    setAssisted(false);
}

void HudNode::spawnToast(std::string const& text, RGBA color) {
    auto toast = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
    toast->setScale(.5f);
    toast->setColor(c3(color));
    toast->setPosition({0.f, kBarY - 42.f});
    this->addChild(toast, 10);
    toast->runAction(CCSequence::create(
        CCSpawn::create(CCMoveBy::create(.6f, {0.f, 12.f}),
                        CCSequence::create(CCDelayTime::create(.25f), CCFadeOut::create(.35f), nullptr), nullptr),
        CCRemoveSelf::create(), nullptr));
}

} // namespace ts::ui
