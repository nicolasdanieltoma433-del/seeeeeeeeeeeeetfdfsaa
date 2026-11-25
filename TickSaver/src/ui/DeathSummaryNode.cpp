#include "DeathSummaryNode.hpp"

#include "Palette.hpp"

#include <cmath>

using namespace geode::prelude;

namespace ts::ui {

DeathSummaryNode* DeathSummaryNode::create(DeathSummary const& summary, Palette const& palette, float scale) {
    auto ret = new DeathSummaryNode();
    if (ret->initSummary(summary, palette, scale)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool DeathSummaryNode::initSummary(DeathSummary const& s, Palette const& p, float scale) {
    constexpr float kW = 240.f;
    constexpr float kH = 62.f;
    if (!CCLayerColor::initWithColor(c4b(role(p, PaletteRole::BarBg)), kW, kH)) return false;
    this->setID("ticksaver-death-summary"_spr);

    // CCLayerColor se ancla abajo a la izquierda: lo centramos a mano.
    auto win = CCDirector::get()->getWinSize();
    this->ignoreAnchorPointForPosition(false);
    this->setAnchorPoint({.5f, .5f});
    this->setScale(scale);
    this->setPosition({win.width / 2.f, win.height * .62f});
    this->setCascadeOpacityEnabled(true);  // el fade del panel arrastra a los textos

    auto addLabel = [&](std::string const& text, char const* font, float labelScale, float y, RGBA color) {
        auto label = CCLabelBMFont::create(text.c_str(), font);
        label->setScale(labelScale);
        label->setColor(c3(color));
        label->setPosition({kW / 2.f, y});
        label->limitLabelWidth(kW - 16.f, labelScale, .1f);
        this->addChild(label);
    };

    addLabel(fmt::format("RESUMEN  -  {}%", static_cast<int>(std::floor(s.percent))), "goldFont.fnt", .5f, kH - 12.f,
             role(p, PaletteRole::Streak));
    addLabel(fmt::format("Salvados: {} (total {})   Mejor racha: x{} (nivel x{})", s.savesThisAttempt,
                         s.totalSavesLevel, s.bestStreakThisAttempt, s.bestStreakLevel),
             "bigFont.fnt", .32f, kH - 32.f, role(p, PaletteRole::Text));

    auto lastColor = s.last.kind == ClickKind::Clean ? role(p, PaletteRole::Good) : colorForKind(p, s.last.kind);
    addLabel("Ultimo clic: " + describeClick(s.last), "bigFont.fnt", .3f, kH - 49.f, lastColor);

    this->runAction(CCSequence::create(CCDelayTime::create(2.2f), CCFadeOut::create(.4f), CCRemoveSelf::create(), nullptr));
    return true;
}

} // namespace ts::ui
