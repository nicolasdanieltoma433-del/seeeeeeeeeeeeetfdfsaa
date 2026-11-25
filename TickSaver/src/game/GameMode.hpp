#pragma once
// ============================================================================
//  TickSaver · game/GameMode.hpp
//  Detección del modo de juego de un PlayerObject y "perfil" de simulación
//  de cada modo (cuánto mirar hacia adelante, cuándo dar por bueno un click).
// ============================================================================

#include "../core/Types.hpp"

class PlayerObject;

namespace ts {

Mode detectMode(PlayerObject* player);

// Cómo se evalúa un click en cada modo.
struct ModeProfile {
    // Ventana máxima de simulación en ticks (240 = 1 s).
    int window;
    // Modos "de salto" (cubo, robot, bola, araña): el click se da por bueno
    // cuando el fantasma vuelve a tocar suelo/techo + `landingMargin` ticks.
    // Así no penalizamos un click correcto por el obstáculo siguiente (que
    // depende del PRÓXIMO click, no de este).
    bool endOnLanding;
    int landingMargin;
    // Hold por defecto para el fantasma mientras el modo adaptativo no tiene
    // datos. Muy importante en nave/wave: mantener = subir.
    int defaultHold;
};

ModeProfile const& profileFor(Mode mode);

} // namespace ts
