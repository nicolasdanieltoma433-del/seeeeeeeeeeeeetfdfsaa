#include "Stats.hpp"

#include <algorithm>
#include <cmath>

namespace ts {

std::string describeClick(LastClick const& c) {
    if (!c.valid) return "Sin clicks todavia";
    auto t = std::to_string(c.earlyTicks) + "t";
    std::string early = c.release ? "soltaste temprano por " : "temprano por ";
    switch (c.kind) {
        case ClickKind::Clean: return "justo";
        case ClickKind::Saved:
            if (c.orb) return "click al orbe (+" + t + "): salvado";
            return early + t + (c.failed ? ": salvado, pero fallo la prediccion" : ": salvado");
        case ClickKind::Unsalvageable: return early + t + ": sin salvacion";
        case ClickKind::Unchecked: return "sin evaluar";
        case ClickKind::Unnecessary: return "click sin efecto: no cuenta";
        case ClickKind::Ignored: return "click innecesario: ignorado";
    }
    return {};
}

// ---------------------------------------------------------------- Session ---

void Session::startAttempt() {
    m_streak = 0;
    m_bestStreak = 0;
    m_saves = 0;
    m_unsalvageable = 0;
    m_last = {};
}

bool Session::onClick(ClickEvent const& ev) {
    // Política de racha:
    //   Clean          -> +1   (click bueno)
    //   Saved          ->  0   (fue temprano: el mod tuvo que salvarte)
    //   Unsalvageable  ->  0
    //   Unchecked      -> sin cambio (no sabemos si fue bueno)
    //   Unnecessary    -> sin cambio (el click no hizo nada: no cuenta)
    //   Ignored        ->  0   (el click mataba y no hacía falta)
    bool increased = false;
    switch (ev.kind) {
        case ClickKind::Clean:
            ++m_streak;
            increased = true;
            break;
        case ClickKind::Saved:
        case ClickKind::Ignored:
            ++m_saves;
            m_streak = 0;
            break;
        case ClickKind::Unsalvageable:
            ++m_unsalvageable;
            m_streak = 0;
            break;
        case ClickKind::Unchecked:
        case ClickKind::Unnecessary:
            break;
    }
    m_bestStreak = std::max(m_bestStreak, m_streak);
    if (ev.kind != ClickKind::Unchecked && ev.kind != ClickKind::Unnecessary) {
        m_last = LastClick{true, ev.kind, ev.earlyTicks, ev.mode, ev.release, false, ev.orb};
    }
    return increased;
}

// ------------------------------------------------------------- LevelStats ---

std::size_t LevelStats::cellFor(float percent) {
    if (!(percent > 0.f)) return 0;  // también atrapa NaN
    auto cell = static_cast<std::size_t>(percent);
    return std::min<std::size_t>(cell, kHistogramCells - 1);
}

void LevelStats::recordClick(ClickEvent const& ev, float percent) {
    if (ev.kind == ClickKind::Unnecessary) return;  // clicks sin efecto no cuentan
    if (!ev.release) ++clicks;                       // los "soltar" no son clicks
    if (ev.kind == ClickKind::Ignored) {
        ++saves;
        ++ignored;
        ++savesByPct[cellFor(percent)];
        ++savesByMode[idx(ev.mode)];
    }
    else if (ev.kind == ClickKind::Saved) {
        ++saves;
        savedTicksSum += static_cast<std::uint64_t>(std::max(0, ev.earlyTicks));
        maxSavedTicks = std::max<std::uint32_t>(maxSavedTicks, static_cast<std::uint32_t>(std::max(0, ev.earlyTicks)));
        ++savesByPct[cellFor(percent)];
        ++savesByMode[idx(ev.mode)];
    }
    else if (ev.kind == ClickKind::Unsalvageable) {
        ++unsalvageable;
    }
}

void LevelStats::recordDeath(float percent) {
    ++deaths;
    ++deathsByPct[cellFor(percent)];
}

void LevelStats::recordStreak(int streak) {
    bestStreak = std::max<std::uint32_t>(bestStreak, static_cast<std::uint32_t>(std::max(0, streak)));
}

void LevelStats::recordProgress(float percent) {
    if (percent > bestPercent) bestPercent = std::min(percent, 100.f);
}

double LevelStats::averageSavedTicks() const {
    auto timed = saves - std::min(saves, ignored);  // los descartados no tienen "ticks salvados"
    return timed == 0 ? 0.0 : static_cast<double>(savedTicksSum) / static_cast<double>(timed);
}

std::vector<std::uint32_t> LevelStats::bucketed(Histogram const& h, int buckets) {
    buckets = std::clamp(buckets, 1, static_cast<int>(kHistogramCells));
    std::vector<std::uint32_t> out(static_cast<std::size_t>(buckets), 0);
    for (std::size_t i = 0; i < kHistogramCells; ++i) {
        auto b = i * static_cast<std::size_t>(buckets) / kHistogramCells;
        out[b] += h[i];
    }
    return out;
}

} // namespace ts
