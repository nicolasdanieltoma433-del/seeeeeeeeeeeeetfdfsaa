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
//  Costo: cada paso es un checkCollisions completo. En niveles pesados
//  (Slaughterhouse y similares) eso es caro, así que hay un presupuesto de
//  TIEMPO por frame (setting "Presupuesto de simulacion", en ms). Si se
//  agota, la simulación devuelve Skipped y el juego no se traba.
//
//  Limitación conocida: durante la simulación el nivel está "congelado"
//  (no corren triggers ni objetos móviles).
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

    // Crea los dos fantasmas. Se llama al terminar de cargar el nivel: crear
    // PlayerObjects en medio de un paso de física no es seguro.
    void prepare();

    // Una vez por tick: dt real del paso y presupuesto (ms por frame).
    void beginTick(float stepDt, float budgetMs);

    // Los chequeos de un click recién apretado tienen su propio presupuesto
    // (no compiten con las reevaluaciones de clicks retenidos).
    void setPriority(bool priority) { m_priority = priority; }

    SimOutcome simulatePress(Slot slot, int holdTicks) override;
    SimOutcome simulateIdle(Slot slot, int ticks) override;

    // Jugadores REALES que mueve un click de este slot (para reinyectar).
    // Devuelve cuántos escribió en `out`.
    int realTargets(Slot slot, bool forInjection, std::array<PlayerObject*, 2>& out) const;

    // --- Validación (setting "validate-sim") -------------------------------
    void validationBefore();
    void validationAfter();

    void setDebugLog(bool v) { m_debug = v; }
    // Dual de un jugador: si es false sólo se exige que sobreviva el P1.
    void setCorrectBoth(bool v) { m_correctBoth = v; }
    int stepsUsedThisTick() const { return m_stepsUsed; }
    double usedMsThisFrame() const { return m_usedUs / 1000.0; }

private:
    struct Target {
        PlayerObject* real = nullptr;
        PlayerObject* ghost = nullptr;
    };

    int collectTargets(Slot slot, std::array<Target, 2>& out);
    PlayerObject* ghostAt(std::size_t index);
    void refreshFrame();
    void sync(PlayerObject* ghost, PlayerObject* real);
    void step(PlayerObject* ghost);
    SimOutcome run(Slot slot, bool press, int holdTicks, int idleTicks);

    PlayLayer* m_layer;
    std::array<geode::Ref<PlayerObject>, 2> m_ghosts{};
    float m_dt = 0.25f;  // se reemplaza por el dt real en el primer tick

    // presupuesto
    unsigned int m_frame = ~0u;
    double m_usedUs = 0.0;
    double m_budgetUs = 2000.0;
    bool m_priority = false;
    int m_stepsUsed = 0;

    bool m_debug = false;
    bool m_correctBoth = true;

    // validación
    bool m_validating = false;
    float m_predictedX = 0.f, m_predictedY = 0.f;
};

} // namespace ts
