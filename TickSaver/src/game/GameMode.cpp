#include "GameMode.hpp"

#include <Geode/binding/PlayerObject.hpp>

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
    // Valores iniciales razonables; afinarlos en juego con el setting
    // "debug-log" (registra cada simulación y su resultado).
    //                         window  landing  margin  hold
    static constexpr ModeProfile kProfiles[kModeCount] = {
        /* Cube   */ {120, true, 6, 8},
        /* Ship   */ {48, false, 0, 14},
        /* Ball   */ {90, true, 6, 4},
        /* Ufo    */ {60, false, 0, 6},
        /* Wave   */ {36, false, 0, 10},
        /* Robot  */ {120, true, 6, 12},
        /* Spider */ {60, true, 6, 4},
        /* Swing  */ {48, false, 0, 6},
    };
    return kProfiles[idx(mode)];
}

} // namespace ts
