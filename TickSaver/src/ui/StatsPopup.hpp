#pragma once
// ============================================================================
//  TickSaver · ui/StatsPopup.hpp
//  Popup (desde el menú de pausa) con las estadísticas del nivel:
//    * resumen: salvados, mejor racha, muertes, intentos, promedio de ticks
//    * histograma 0%..100% en 20 barras de 5%: dónde te salva más el mod
//      (barras) y dónde morís (puntos); el tramo con ticks extra se resalta
//    * botones: exportar config (TS2 al portapapeles), importar config
//      (desde el portapapeles) y borrar las stats del nivel
// ============================================================================

#include "../core/Config.hpp"
#include "../core/Stats.hpp"

#include <Geode/ui/Popup.hpp>

#include <functional>

namespace ts::ui {

class StatsPopup : public geode::Popup {
public:
    static StatsPopup* create(LevelStats const& stats, Config const& cfg, std::function<void()> onResetStats);

protected:
    bool init(LevelStats const& stats, Config const& cfg, std::function<void()> onResetStats);

private:
    void buildSummary();
    void buildHistogram();
    void buildButtons();

    void onExport();
    void onImport();
    void onResetStats();

    LevelStats m_stats;
    Config m_cfg;
    std::function<void()> m_onReset;
};

} // namespace ts::ui
