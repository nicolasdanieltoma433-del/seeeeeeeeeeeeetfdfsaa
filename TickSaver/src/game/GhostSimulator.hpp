#pragma once
// ============================================================================
//  TickSaver · game/GhostSimulator.hpp
//  Implementación real de IPredictor: simula el futuro con "fantasmas".
//
//  Un fantasma es un PlayerObject invisible creado por nosotros. Para cada
//  pregunta ("¿si apreto ahora, vivo?"):
//    1. sync: copyAttributes() del jugador real al fantasma (+ campos extra);
//    2. pushButton(Jump) en el fantasma (si es una simulación de press);
//    3. por cada tick de la ventana:  checkCollisions -> update -> rotación,
//       con las MISMAS funciones del juego, así la física es idéntica;
//    4. si PlayLayer::destroyPlayer recibe al fantasma -> Dies.
//  Los hooks de GhostGuardHooks.cpp impiden que eso toque el nivel real.
//
//  Limitación conocida: durante la simulación el nivel está "congelado"
//  (no corren triggers ni objetos móviles). Con ventanas cortas y la
//  reevaluación en cada tick (InputScheduler) el error es pequeño, pero en
//  zonas con muchos move triggers la predicción puede fallar.
// ============================================================================

#include "../core/InputScheduler.hpp"

#include <Geode/binding/PlayerObject.hpp>  // Ref<PlayerObject> necesita el tipo completo
#include <Geode/utils/cocos.hpp>

#include <array>

class PlayLayer;

namespace ts {

class GhostSimulator final : public IPredictor {
public:
    explicit GhostSimulator(PlayLayer* layer);
    ~GhostSimulator() override;
    GhostSimulator(GhostSimulator const&) = delete;
    GhostSimulator& operator=(GhostSimulator const&) = delete;

    // Una vez por tick: dt real del paso y presupuesto de pasos de fantasma.
    void beginTick(float stepDt, int budget);
    // Cuando el press llega entre ticks (desde handleButton) el presupuesto
    // del tick en curso puede no haberse renovado todavía.
    void ensureBudget(int minimum);

    SimOutcome simulatePress(Slot slot, int holdTicks) override;
    SimOutcome simulateIdle(Slot slot, int ticks) override;

    // --- Validación (setting "validate-sim") -------------------------------
    // Antes del paso: predice 1 tick sin input. Después: compara con el real.
    void validationBefore();
    void validationAfter();

    void setDebugLog(bool v) { m_debug = v; }
    // Dual de un jugador: si es false sólo se exige que sobreviva el P1.
    void setCorrectBoth(bool v) { m_correctBoth = v; }
    int stepsUsedThisTick() const { return m_stepsUsed; }

private:
    struct Target {
        PlayerObject* real = nullptr;
        PlayerObject* ghost = nullptr;
    };

    int collectTargets(Slot slot, std::array<Target, 2>& out);
    PlayerObject* ghostAt(std::size_t index);
    void sync(PlayerObject* ghost, PlayerObject* real);
    void step(PlayerObject* ghost);
    SimOutcome run(Slot slot, bool press, int holdTicks, int idleTicks);

    PlayLayer* m_layer;
    std::array<geode::Ref<PlayerObject>, 2> m_ghosts{};
    float m_dt = 0.25f;  // se reemplaza por el dt real en el primer tick
    int m_budget = 0;
    int m_stepsUsed = 0;
    bool m_debug = false;
    bool m_correctBoth = true;

    // validación
    bool m_validating = false;
    float m_predictedX = 0.f, m_predictedY = 0.f;
};

} // namespace ts
