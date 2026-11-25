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
    //                         window landing margin hold  holdBased afterRelease
    static constexpr ModeProfile kProfiles[kModeCount] = {
        /* Cube   */ {120, true, 6, 8, false, 0},
        /* Ship   */ {40, false, 0, 14, true, 6},
        /* Ball   */ {60, true, 6, 4, false, 0},
        /* Ufo    */ {20, false, 0, 6, false, 0},
        /* Wave   */ {30, false, 0, 10, true, 4},
        /* Robot  */ {120, true, 6, 12, false, 0},
        /* Spider */ {40, true, 4, 4, false, 0},
        /* Swing  */ {20, false, 0, 6, false, 0},
    };
    return kProfiles[idx(mode)];
}

int windowFor(ModeProfile const& profile, int holdTicks) {
    if (!profile.holdBased) return profile.window;
    return std::clamp(holdTicks + profile.afterRelease, 6, profile.window);
}

} // namespace ts
