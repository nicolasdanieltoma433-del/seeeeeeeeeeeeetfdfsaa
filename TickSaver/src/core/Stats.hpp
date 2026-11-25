#pragma once
// ============================================================================
//  TickSaver · core/Stats.hpp
//  Racha / último click del intento actual (Session) y estadísticas
//  persistentes por nivel (LevelStats).
//
//  LevelStats guarda histogramas con resolución de 1% (100 celdas). La UI
//  los agrupa en N barras (por defecto 20 de 5%) con `bucketed()`.
// ============================================================================

#include "InputScheduler.hpp"
#include "Types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ts {

// Descripción del último click, para el HUD y el resumen al morir.
struct LastClick {
    bool valid = false;
    ClickKind kind = ClickKind::Unchecked;
    int earlyTicks = 0;
    Mode mode = Mode::Cube;
    bool release = false;  // fue un SOLTAR (nave/wave)
};

// "temprano por 3t: salvado", "justo", ... (sin tildes: fuentes de GD).
std::string describeClick(LastClick const& c);

// Estado de UN intento (se reinicia al morir / reiniciar).
class Session {
public:
    void startAttempt();
    // Devuelve true si la racha subió (para animar el HUD).
    bool onClick(ClickEvent const& ev);

    int streak() const { return m_streak; }
    int bestStreakThisAttempt() const { return m_bestStreak; }
    int savesThisAttempt() const { return m_saves; }
    bool assisted() const { return m_saves > 0 || m_unsalvageable > 0; }
    LastClick const& lastClick() const { return m_last; }

private:
    int m_streak = 0;
    int m_bestStreak = 0;
    int m_saves = 0;
    int m_unsalvageable = 0;
    LastClick m_last{};
};

inline constexpr std::size_t kHistogramCells = 100;  // 1 celda = 1%
using Histogram = std::array<std::uint32_t, kHistogramCells>;

struct LevelStats {
    static constexpr int kSchemaVersion = 1;

    std::string key;   // "id-12345" o "local-xxxxxxxx"
    std::string name;  // nombre del nivel (informativo)

    std::uint32_t attempts = 0;
    std::uint32_t deaths = 0;
    std::uint32_t clicks = 0;
    std::uint32_t saves = 0;
    std::uint32_t unsalvageable = 0;
    std::uint32_t ignored = 0;  // clicks que mataban y no hacían falta (descartados)
    std::uint32_t bestStreak = 0;
    std::uint64_t savedTicksSum = 0;  // para el promedio de ticks salvados
    std::uint32_t maxSavedTicks = 0;
    float bestPercent = 0.f;  // mejor % alcanzado CON el mod (no es el récord del juego)

    Histogram savesByPct{};
    Histogram deathsByPct{};
    std::array<std::uint32_t, kModeCount> savesByMode{};

    void recordClick(ClickEvent const& ev, float percent);
    void recordDeath(float percent);
    void recordStreak(int streak);
    void recordProgress(float percent);

    double averageSavedTicks() const;

    // Agrupa un histograma de 100 celdas en `buckets` barras.
    static std::vector<std::uint32_t> bucketed(Histogram const& h, int buckets);
    // Celda (0..99) para un porcentaje cualquiera.
    static std::size_t cellFor(float percent);
};

} // namespace ts
