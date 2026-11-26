#pragma once
// ============================================================================
//  TickSaver · game/GameMode.hpp
//  Detección del modo de juego de un PlayerObject y "perfil" de simulación
//  de cada modo (cuánto mirar hacia adelante, cuándo dar por bueno un click).
//
//  Regla clave: la ventana sólo debe cubrir lo que depende de ESTE click.
//  Si mira demasiado lejos, el fantasma "muere" por inputs futuros que no
//  conocemos y el mod retiene clicks que estaban bien (se siente como que
//  el click no entra).
// ============================================================================

#include "../core/Types.hpp"

class PlayerObject;

namespace ts {

Mode detectMode(PlayerObject* player);

struct ModeProfile {
    // Ventana máxima de simulación en ticks (240 = 1 s).
    int window;
    // Modos de salto (cubo, robot, bola, araña): el click es bueno cuando el
    // fantasma vuelve a apoyarse vivo + `landingMargin` ticks.
    bool endOnLanding;
    int landingMargin;
    // Hold supuesto mientras el modo adaptativo no tenga datos.
    int defaultHold;
    // Modos de mantener (nave, wave): la ventana termina `afterRelease`
    // ticks después del soltar supuesto. Lo que pase después depende de tu
    // próximo click, no de este.
    bool holdBased;
    int afterRelease;
    // Ventana para "¿y si NO apreto?" (decidir si un click hace falta). Más
    // larga que la del press para no descartar un salto que se necesita un
    // poco más adelante. 0 = el modo no descarta clicks (nave/wave).
    int noPressWindow;
    // Ventana para "¿y si suelto ahora?" (nave/wave). 0 = no se corrige el soltar.
    int releaseWindow;
};

ModeProfile const& profileFor(Mode mode);

// Ventana efectiva para un hold dado.
int windowFor(ModeProfile const& profile, int holdTicks);

// Modos donde es normal spamear (wave, nave, UFO, bola, swing). Ahí, 3 inputs
// muy seguidos pasan directo: retenerlos se siente como lag (idea de Afterflow).
bool isSpammyMode(Mode mode);
// Separación máxima (en ticks) entre dos inputs para contarlos como spam.
int spamGapTicks(Mode mode);
inline constexpr int kSpamStreak = 3;

} // namespace ts
