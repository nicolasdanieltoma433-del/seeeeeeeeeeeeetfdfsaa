#include "StatsPopup.hpp"

#include "Palette.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Button.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace ts::ui {
namespace {
constexpr float kPopupW = 380.f;
constexpr float kPopupH = 260.f;
constexpr float kChartW = 320.f;
constexpr float kChartH = 90.f;
constexpr int kBuckets = 20;  // 20 barras de 5%
} // namespace

StatsPopup* StatsPopup::create(LevelStats const& stats, Config const& cfg, std::function<void()> onResetStats) {
    auto ret = new StatsPopup();
    if (ret->init(stats, cfg, std::move(onResetStats))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool StatsPopup::init(LevelStats const& stats, Config const& cfg, std::function<void()> onResetStats) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_stats = stats;
    m_cfg = cfg;
    m_onReset = std::move(onResetStats);

    this->setTitle(fmt::format("TickSaver - {}", m_stats.name.empty() ? std::string("Nivel") : m_stats.name));
    buildSummary();
    buildHistogram();
    buildButtons();
    return true;
}

void StatsPopup::buildSummary() {
    auto textColor = c3(role(m_cfg.palette, PaletteRole::Text));
    auto addLine = [&](std::string const& text, float y) {
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setColor(textColor);
        label->limitLabelWidth(kPopupW - 30.f, .32f, .1f);
        m_mainLayer->addChildAtPosition(label, Anchor::Top, {0.f, y});
    };

    auto const& s = m_stats;
    addLine(fmt::format("Salvados: {}   Mejor racha: x{}   Muertes: {}   Intentos: {}", s.saves, s.bestStreak, s.deaths,
                        s.attempts),
            -46.f);
    addLine(fmt::format("Promedio: {:.1f}t   Max: {}t   Ignorados: {}   Sin salvacion: {}   Mejor: {:.0f}%",
                        s.averageSavedTicks(), s.maxSavedTicks, s.ignored, s.unsalvageable,
                        static_cast<double>(s.bestPercent)),
            -62.f);
}

void StatsPopup::buildHistogram() {
    auto const& p = m_cfg.palette;

    auto chart = CCNode::create();
    chart->setContentSize({kChartW, kChartH});
    chart->setAnchorPoint({.5f, .5f});
    m_mainLayer->addChildAtPosition(chart, Anchor::Center, {0.f, -4.f});

    auto draw = CCDrawNode::create();
    chart->addChild(draw);
    draw->drawRect(CCPoint{0.f, 0.f}, CCPoint{kChartW, kChartH}, c4f(role(p, PaletteRole::BarBg)), 0.f, ccColor4F{0, 0, 0, 0});

    // Tramo con ticks extra (si está activado).
    if (m_cfg.region.enabled && m_cfg.region.endPct > m_cfg.region.startPct) {
        float x0 = kChartW * static_cast<float>(m_cfg.region.startPct) / 100.f;
        float x1 = kChartW * static_cast<float>(m_cfg.region.endPct) / 100.f;
        draw->drawRect(CCPoint{x0, 0.f}, CCPoint{x1, kChartH}, c4f(role(p, PaletteRole::Early), .18f), 0.f,
                       ccColor4F{0, 0, 0, 0});
    }

    auto saves = LevelStats::bucketed(m_stats.savesByPct, kBuckets);
    auto deaths = LevelStats::bucketed(m_stats.deathsByPct, kBuckets);
    auto maxSaves = std::max<std::uint32_t>(1, *std::max_element(saves.begin(), saves.end()));
    auto maxDeaths = std::max<std::uint32_t>(1, *std::max_element(deaths.begin(), deaths.end()));

    constexpr float kUsableH = kChartH - 10.f;
    float slot = kChartW / static_cast<float>(kBuckets);
    for (int i = 0; i < kBuckets; ++i) {
        auto ui = static_cast<std::size_t>(i);
        float x0 = slot * static_cast<float>(i) + 1.5f;
        float x1 = slot * static_cast<float>(i + 1) - 1.5f;
        if (saves[ui] > 0) {
            float h = kUsableH * static_cast<float>(saves[ui]) / static_cast<float>(maxSaves);
            draw->drawRect(CCPoint{x0, 0.f}, CCPoint{x1, std::max(h, 2.f)}, c4f(role(p, PaletteRole::Accent)), 0.f,
                           ccColor4F{0, 0, 0, 0});
        }
        if (deaths[ui] > 0) {
            float y = kUsableH * static_cast<float>(deaths[ui]) / static_cast<float>(maxDeaths) + 4.f;
            draw->drawDot(CCPoint{(x0 + x1) / 2.f, std::min(y, kChartH - 3.f)}, 2.2f, c4f(role(p, PaletteRole::Danger)));
        }
    }

    // Eje X
    auto textColor = c3(role(p, PaletteRole::Text));
    for (int pct : {0, 25, 50, 75, 100}) {
        auto label = CCLabelBMFont::create(fmt::format("{}%", pct).c_str(), "chatFont.fnt");
        label->setScale(.5f);
        label->setColor(textColor);
        label->setPosition({kChartW * static_cast<float>(pct) / 100.f, -8.f});
        chart->addChild(label);
    }

    auto legend = CCLabelBMFont::create(
        m_stats.saves == 0 && m_stats.deaths == 0 ? "Sin datos todavia: juga unos intentos" : "Barras: salvados   Puntos: muertes",
        "chatFont.fnt");
    legend->setScale(.55f);
    legend->setColor(textColor);
    legend->setPosition({kChartW / 2.f, -20.f});
    chart->addChild(legend);
}

void StatsPopup::buildButtons() {
    auto spr = ButtonSprite::create("Borrar stats", "goldFont.fnt", "GJ_button_06.png", .6f);
    auto btn = Button::createWithNode(spr, [this](Button*) { onResetStats(); });
    m_mainLayer->addChildAtPosition(btn, Anchor::Bottom, {0.f, 24.f});
}

void StatsPopup::onResetStats() {
    createQuickPopup("Borrar stats",
                     "Se borran las estadisticas de <cy>este nivel</c>. No se puede deshacer.",
                     "Cancelar", "Borrar", [this](FLAlertLayer*, bool confirmed) {
                         if (!confirmed) return;
                         if (m_onReset) m_onReset();
                         this->onClose(nullptr);
                     });
}

} // namespace ts::ui
