#pragma once
// ============================================================================
//  TickSaver · core/AdaptiveModel.hpp
//  Modo adaptativo: "aprende de vos".
//
//  Aprende dos cosas por modo de juego, con medias móviles exponenciales
//  (EMA) que pesan más lo reciente:
//
//   1. Cuán temprano solés clickear cuando te salva (earliness).
//      -> el máximo efectivo se ACHICA a  media + 2.5·desvío  (+1 de margen),
//         para que el mod no "salve" clicks que en realidad son errores
//         gruesos. Nunca supera el máximo configurado.
//   2. Cuánto mantenés el botón (hold).
//      -> el fantasma simula tu hold típico en vez de un valor fijo, clave
//         en nave/wave/swing donde mantener cambia toda la trayectoria.
//
//  Hasta juntar kMinSamples muestras no cambia nada (usa los defaults).
// ============================================================================

#include "Types.hpp"

#include <array>
#include <vector>

namespace ts {

struct Ema {
    double mean = 0.0;
    double var = 0.0;
    int samples = 0;

    void add(double x, double alpha);
    double stddev() const;
};

class AdaptiveModel {
public:
    static constexpr int kMinSamples = 8;
    static constexpr double kAlpha = 0.15;

    void recordSave(Mode mode, int earlyTicks);
    void recordHold(Mode mode, int holdTicks);

    // Hold esperado para el fantasma (usa `fallback` si no hay datos).
    int expectedHold(Mode mode, int fallback) const;

    // Máximo de retraso ajustado a tu historial (<= configuredMax).
    int adaptiveMax(Mode mode, int configuredMax) const;

    Ema const& earliness(Mode m) const { return m_earliness[idx(m)]; }
    Ema const& hold(Mode m) const { return m_hold[idx(m)]; }

    void clear();

    // Serialización plana (la capa de juego la guarda como JSON).
    std::vector<double> toFlat() const;
    bool fromFlat(std::vector<double> const& data);

private:
    std::array<Ema, kModeCount> m_earliness{};
    std::array<Ema, kModeCount> m_hold{};
};

} // namespace ts
