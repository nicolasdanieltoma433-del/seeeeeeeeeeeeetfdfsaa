#include "AdaptiveModel.hpp"

#include <algorithm>
#include <cmath>

namespace ts {

void Ema::add(double x, double alpha) {
    if (samples == 0) {
        mean = x;
        var = 0.0;
    }
    else {
        // EMA de media y varianza (forma incremental de West).
        double diff = x - mean;
        double incr = alpha * diff;
        mean += incr;
        var = (1.0 - alpha) * (var + diff * incr);
    }
    ++samples;
}

double Ema::stddev() const { return std::sqrt(std::max(0.0, var)); }

void AdaptiveModel::recordSave(Mode mode, int earlyTicks) {
    if (earlyTicks <= 0) return;
    m_earliness[idx(mode)].add(earlyTicks, kAlpha);
}

void AdaptiveModel::recordHold(Mode mode, int holdTicks) {
    // Holds larguísimos (mantener en nave varios segundos) distorsionan la
    // media del "tap típico"; los recortamos a 2 segundos.
    holdTicks = std::clamp(holdTicks, 1, kTicksPerSecond * 2);
    m_hold[idx(mode)].add(holdTicks, kAlpha);
}

int AdaptiveModel::expectedHold(Mode mode, int fallback) const {
    auto const& e = m_hold[idx(mode)];
    if (e.samples < kMinSamples) return fallback;
    return std::max(1, static_cast<int>(std::lround(e.mean)));
}

int AdaptiveModel::adaptiveMax(Mode mode, int configuredMax) const {
    auto const& e = m_earliness[idx(mode)];
    if (configuredMax <= 0 || e.samples < kMinSamples) return configuredMax;
    int learned = static_cast<int>(std::ceil(e.mean + 2.5 * e.stddev())) + 1;
    return std::clamp(learned, std::min(2, configuredMax), configuredMax);
}

void AdaptiveModel::clear() {
    m_earliness = {};
    m_hold = {};
}

std::vector<double> AdaptiveModel::toFlat() const {
    std::vector<double> out;
    out.reserve(kModeCount * 6);
    for (std::size_t i = 0; i < kModeCount; ++i) {
        for (auto const* e : {&m_earliness[i], &m_hold[i]}) {
            out.push_back(e->mean);
            out.push_back(e->var);
            out.push_back(e->samples);
        }
    }
    return out;
}

bool AdaptiveModel::fromFlat(std::vector<double> const& data) {
    if (data.size() != kModeCount * 6) return false;
    std::size_t k = 0;
    for (std::size_t i = 0; i < kModeCount; ++i) {
        for (auto* e : {&m_earliness[i], &m_hold[i]}) {
            e->mean = data[k++];
            e->var = data[k++];
            e->samples = static_cast<int>(data[k++]);
        }
    }
    return true;
}

} // namespace ts
