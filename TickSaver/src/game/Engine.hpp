#pragma once
// ============================================================================
//  TickSaver · game/Engine.hpp
//  Orquestador: existe uno por PlayLayer (vive en los m_fields del hook de
//  PlayLayer) y conecta todas las piezas:
//
//   hooks de input ──► Engine::onButton / beforeStep
//                          │
//                          ├─► InputScheduler (core)   decide pasar/retener
//                          │        └─► GhostSimulator  predice con fantasmas
//                          ├─► Session + LevelStats     racha, saves, muertes
//                          ├─► AdaptiveModel            aprende de vos
//                          └─► HudNode / DeathSummary   feedback visual
//
//  Reloj: m_tick cuenta pasos de física completos (processCommands con
//  isHalfTick == false). Un click que llega entre pasos se aplica en el
//  paso siguiente, por eso se registra como m_tick + 1.
// ============================================================================

#include "../core/InputScheduler.hpp"
#include "../core/Stats.hpp"
#include "../ui/DeathSummaryNode.hpp"  // Ref necesita el tipo completo
#include "../ui/HudNode.hpp"
#include "GhostSimulator.hpp"

#include <Geode/utils/cocos.hpp>

#include <array>
#include <chrono>

class PlayLayer;
class PlayerObject;
class GJBaseGameLayer;

namespace ts {

class Engine {
public:
    static Engine* current();
    // true mientras el propio mod reinyecta un click (el hook de handleButton
    // debe dejarlo pasar sin volver a evaluarlo).
    static bool isInjecting();

    explicit Engine(PlayLayer* layer);
    ~Engine();
    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;

    bool owns(GJBaseGameLayer const* layer) const;

    // --- input / física (hooks/InputHooks.cpp) -----------------------------
    // Devuelve true si el input original debe llegar al juego ahora.
    bool onButton(bool down, int button, bool isPlayer1);
    void beforeStep(bool isHalfTick);
    void noteRealUpdate(PlayerObject* player, float dt);

    // --- ciclo de vida (hooks/PlayLayerHooks.cpp) ---------------------------
    void onLevelReady();
    void onReset();
    void onRealDeath(PlayerObject* player);
    void onLevelComplete();
    void onQuit();
    void onConfigChanged();

    LevelStats const& stats() const { return m_stats; }
    void resetStats();

private:
    Slot slotFor(bool isPlayer1) const;
    SlotParams paramsFor(Slot slot) const;
    float percent() const;
    bool playing() const;

    void inject(Slot slot, bool push);
    void processEvents();
    void markAssisted();
    void restoreTestMode();
    void showDeathSummary(float pct);
    void saveStats(bool force);

    PlayLayer* m_layer;
    GhostSimulator m_sim;
    InputScheduler m_scheduler;
    Session m_session;
    LevelStats m_stats;

    // Refs fuertes a propósito: NO usar geode::WeakRef acá. Reasignar un
    // WeakRef (operator= -> swap) deja el objeto nuevo sin retener, y al
    // borrarse solo el resumen quedaba un puntero colgado (crash al morir
    // por tercera vez, visto en v0.2.0).
    geode::Ref<ui::HudNode> m_hud;
    geode::Ref<ui::DeathSummaryNode> m_summary;
    std::uint64_t m_id = 0;  // identifica a este Engine en callbacks diferidos

    Tick m_tick = 0;
    float m_tickDt = 0.f;    // mayor dt visto en el tick (paso completo)
    float m_lastDt = 0.25f;  // dt del último tick completo
    std::array<bool, kSlotCount> m_lastIsPlayer1{true, false};
    std::array<Tick, kSlotCount> m_physDownTick{};
    std::array<Mode, kSlotCount> m_physDownMode{};

    bool m_inputThisTick = false;
    // Última salvada de un press, para detectar predicciones fallidas.
    bool m_lastSaveValid = false;
    Tick m_lastSaveTick = 0;
    Mode m_lastSaveMode = Mode::Cube;
    int m_lastSaveEarly = 0;
    bool m_validatePending = false;
    bool m_deadThisAttempt = false;

    bool m_forcedTestMode = false;
    bool m_savedTestMode = false;

    bool m_dirty = false;
    std::chrono::steady_clock::time_point m_lastSave{};
};

} // namespace ts
