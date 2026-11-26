#include "GameMode.hpp"

#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>

namespace ts {

Mode detectMode(PlayerObject* p) {
    if (!p) return Mode::Cube;
    // Flags de PlayerObject en GD 2.2091 (bindings: m_isShip, m_isBird = UFO,
    // m_isBall, m_isDart = wave, m_isRobot, m_isSpider, m_isSwing).
    if (p->m_isShip) return Mode::Ship;
    if (p->m_isBird) return Mode::Ufo;
    if (p->m_isBall) return Mode::Ball;
    if (p->m_isDart) return Mode::Wave;
    if (p->m_isRobot) return Mode::Robot;
    if (p->m_isSpider) return Mode::Spider;
    if (p->m_isSwing) return Mode::Swing;
    return Mode::Cube;
}

ModeProfile const& profileFor(Mode mode) {
    //              window landing margin hold holdBased afterRel noPress release
    static constexpr ModeProfile kProfiles[kModeCount] = {
        /* Cube   */ {120, true, 6, 8, false, 0, 180, 0},
        /* Ship   */ {48, false, 0, 14, true, 10, 0, 20},
        /* Ball   */ {60, true, 6, 4, false, 0, 120, 0},
        /* Ufo    */ {24, false, 0, 6, false, 0, 60, 0},
        /* Wave   */ {40, false, 0, 10, true, 10, 0, 16},
        /* Robot  */ {120, true, 6, 12, false, 0, 180, 0},
        /* Spider */ {40, true, 4, 4, false, 0, 90, 0},
        /* Swing  */ {24, false, 0, 6, false, 0, 60, 0},
    };
    return kProfiles[idx(mode)];
}

int windowFor(ModeProfile const& profile, int holdTicks) {
    if (!profile.holdBased) return profile.window;
    return std::clamp(holdTicks + profile.afterRelease, 6, profile.window);
}

bool isSpammyMode(Mode mode) {
    switch (mode) {
        case Mode::Ship:
        case Mode::Ufo:
        case Mode::Ball:
        case Mode::Wave:
        case Mode::Swing: return true;
        default: return false;
    }
}

int spamGapTicks(Mode mode) { return mode == Mode::Ship ? 6 : 16; }

} // namespace ts
