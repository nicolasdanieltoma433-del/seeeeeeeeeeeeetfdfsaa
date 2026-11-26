#pragma once
// ============================================================================
//  TickSaver · game/Engine.hpp
//  Orquestador: existe uno por PlayLayer (vive en los m_fields del hook de
//  PlayLayer) y conecta todas las piezas:
//
//   hooks de input ──► Engine::onButton / onRealPlayerUpdate
//                          │
//                          ├─► InputScheduler (core)   decide pasar/retener
//                          │        └─► GhostSimulator  predice con fantasmas
//                          ├─► Session + LevelStats     racha, saves, muertes
//                          ├─► AdaptiveModel            aprende de vos
//                          ├─► LiveLog                  pestaña "Log" del menú
//                          └─► HudNode / DeathSummary   feedback visual
//
//  Reloj (desde v0.4, como Afterflow): m_tick avanza al EMPEZAR cada paso de
//  física del jugador 1 (hook de PlayerObject::update, antes del original).
//  Ahí mismo se reinyectan los clicks retenidos, así entran exactamente al
//  principio del paso que el fantasma simuló. Un click que llega entre pasos
//  se aplica en el paso siguiente, por eso se registra como m_tick + 1.
//  Con click-between-steps el juego puede partir un paso en dos updates más
//  cortos: se suman y cuentan como un solo tick.
// ============================================================================

#include "../core/InputScheduler.hpp"
#include "../core/Stats.hpp"
#include "../ui/DeathSummaryNode.hpp"  // Ref necesita el tipo completo
#include "../ui/FloatingButton.hpp"
#include "../ui/HudNode.hpp"
#include "GhostSimulator.hpp"

#include <Geode/utils/cocos.hpp>

#include <array>
#include <chrono>
#include <string>
#include <vector>

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
    // ¿Es el jugador 1 real de este nivel y el juego no está en pausa?
    bool isRealPlayer1(PlayerObject const* player) const;

    // --- input / física (hooks/InputHooks.cpp) -----------------------------
    // Devuelve true si el input original debe llegar al juego ahora.
    bool onButton(bool down, int button, bool isPlayer1);
    // Antes de cada PlayerObject::update del jugador 1 (no en pausa).
    void onRealPlayerUpdate(float dt);
    // Alrededor del update original del jugador 1: sirve para saber si un
    // click llega en medio del paso (click-between-steps).
    void beginRealUpdate(float x);
    void endRealUpdate(float x);

    // --- ciclo de vida (hooks/PlayLayerHooks.cpp) ---------------------------
    void onLevelReady();
    void onReset();
    void onRealDeath(PlayerObject* player);
    void onLevelComplete();
    void onQuit();
    void onConfigChanged();

    LevelStats const& stats() const { return m_stats; }
    void resetStats();
    int injectLatency() const { return m_sim.injectLatency(); }
    float tickDt() const { return m_avgDt; }

private:
    void tick();
    Slot slotFor(bool isPlayer1) const;
    SlotParams paramsFor(Slot slot) const;
    float percent() const;
    bool playing() const;
    void applySimConfig();
    void noteInput(Slot slot);

    void inject(Slot slot, bool push);
    void processEvents();
    void drawPath(std::vector<cocos2d::CCPoint> const& points);
    void markAssisted();
    void restoreTestMode();
    void showDeathSummary(float pct);
    void updateFloatingButton();
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
    geode::Ref<ui::FloatingButton> m_floatingButton;
    std::uint64_t m_id = 0;  // identifica a este Engine en callbacks diferidos

    Tick m_tick = 0;
    // dt del paso: promedio (EMA) + acumulador para pasos partidos.
    float m_avgDt = 0.f;
    float m_stepAccum = 0.f;
    int m_dtOutliers = 0;
    // Click en medio de un paso: dónde empezó el update y cuánto avanza un paso.
    bool m_inRealUpdate = false;
    float m_updateStartX = 0.f;
    float m_stepDeltaX = 0.f;

    std::array<bool, kSlotCount> m_lastIsPlayer1{true, false};
    std::array<Tick, kSlotCount> m_physDownTick{};
    std::array<Mode, kSlotCount> m_physDownMode{};
    // Spam: ticks desde el último input y cuántos inputs seguidos rápidos.
    std::array<int, kSlotCount> m_ticksSinceInput{999, 999};
    std::array<int, kSlotCount> m_rapidInputs{};

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
