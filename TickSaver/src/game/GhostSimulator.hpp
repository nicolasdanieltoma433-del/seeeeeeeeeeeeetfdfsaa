#pragma once
// ============================================================================
//  TickSaver · game/GhostSimulator.hpp
//  Implementación real de IPredictor: simula el futuro con "fantasmas".
//
//  Un fantasma es un PlayerObject invisible creado por nosotros, que vive en
//  una capa APARTE (no está en la escena, nunca se dibuja ni se actualiza
//  solo). Para cada pregunta ("¿si apreto ahora, vivo?"):
//    1. sync: resetTouchedRings + copyAttributes() del jugador real y una
//       copia extendida de ~110 campos (velocidad, gravedad, pendientes,
//       suelo, dash, orbes, tiempos internos);
//    2. pushButton(Jump) en el fantasma en el paso que corresponda;
//    3. por cada tick:  update -> updateRotation -> limpiar los logs de
//       colisión -> checkCollisions, con las MISMAS funciones del juego;
//    4. si el juego mata al fantasma (destroyPlayer / playerDestroyed) -> Dies.
//  Los hooks de GhostGuardHooks.cpp impiden que eso toque el nivel real y
//  aplican a mano los portales de velocidad y tamaño.
//
//  Costo: cada paso es un checkCollisions completo. En niveles pesados
//  (Slaughterhouse y similares) eso es caro, así que hay un presupuesto de
//  TIEMPO por frame (setting "Presupuesto de simulacion", en ms). Si se
//  agota, la simulación devuelve Skipped y el juego no se traba.
//
//  Limitación conocida: durante la simulación el nivel está "congelado"
//  (no corren triggers ni objetos móviles).
//
//  Capa aparte, orden del paso, copia de campos y filtro de colisiones
//  adaptados de Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include "../core/InputScheduler.hpp"

#include <Geode/binding/PlayerObject.hpp>  // Ref<PlayerObject> necesita el tipo completo
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <array>
#include <vector>

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
    // Libera los fantasmas (al salir del nivel, con el PlayLayer todavía vivo).
    void release();
    bool ready() const { return m_ghosts[0].data() != nullptr; }

    // Una vez por tick: dt del paso y presupuesto (ms por frame).
    void beginTick(float stepDt, float budgetMs);

    // Los chequeos de un click recién apretado tienen su propio presupuesto
    // (no compiten con las reevaluaciones de clicks retenidos).
    void setPriority(bool priority) { m_priority = priority; }

    // Click que llega en MEDIO de un paso del jugador real (click-between-
    // steps): el primer paso del fantasma dura sólo lo que falta del paso
    // real, así queda alineado con los pasos del juego (idea de Afterflow).
    void setFirstStepScale(float scale) { m_firstStepScale = std::clamp(scale, 0.001f, 1.f); }

    SimOutcome simulatePress(Slot slot, int holdTicks) override;
    SimOutcome simulateIdle(Slot slot, int ticks) override;
    SimOutcome simulateNoPress(Slot slot, int holdTicks) override;
    bool pressHasEffect(Slot slot, int holdTicks) override;
    SimOutcome simulateRelease(Slot slot) override;
    SimOutcome simulateInjectedPress(Slot slot, int holdTicks, int extraDelay) override;
    bool touchingOrb(Slot slot) override;

    // --- Autocalibración de la latencia de reinyección ----------------------
    // Justo antes de reinyectar un press del cubo, predice dónde estará el
    // jugador K ticks después si el press entra con 0, 1 o 2 ticks de retraso.
    // K ticks más tarde compara con el jugador real y vota. Con votos
    // consistentes ajusta la latencia que usan todas las predicciones.
    void calibrateBefore(Slot slot, Tick now, int holdTicks);
    void calibrateCheck(Tick now, bool cancel);
    int injectLatency() const { return m_injectLatency; }

    // Jugadores REALES que mueve un click de este slot (para reinyectar).
    // Devuelve cuántos escribió en `out`.
    int realTargets(Slot slot, bool forInjection, std::array<PlayerObject*, 2>& out) const;

    // --- Trayectoria ("Mostrar trayectoria") ---------------------------------
    // Si está activo, la simulación "apretar ahora" guarda el camino del
    // fantasma; el motor lo dibuja cuando un click se salva.
    void setRecordPath(bool v) { m_recordPath = v; }
    // Camino de la última simulación "apretar ahora" que sobrevivió en este tick.
    std::vector<cocos2d::CCPoint> const* lastPathFor(Tick now) const;
    void setTick(Tick now) { m_now = now; }

    // --- Validación (setting "validate-sim") -------------------------------
    void validationBefore();
    void validationAfter();

    void setDebugLog(bool v) { m_debug = v; }
    // Dual de un jugador: si es false sólo se exige que sobreviva el P1.
    void setCorrectBoth(bool v) { m_correctBoth = v; }
    // Copia extendida de campos (recomendado).
    void setFullSync(bool v) { m_fullSync = v; }
    int stepsUsedThisTick() const { return m_stepsUsed; }
    double usedMsThisFrame() const { return m_usedUs / 1000.0; }

private:
    struct Target {
        PlayerObject* real = nullptr;
        PlayerObject* ghost = nullptr;
    };

    enum class Kind { Press, Idle, NoPress, Release };

    // Posición/velocidad final del primer fantasma (para pressHasEffect).
    struct Trace {
        float x = 0.f, y = 0.f;
        double yVelocity = 0.0;
    };

    int collectTargets(Slot slot, std::array<Target, 2>& out);
    PlayerObject* makeGhost();
    void refreshFrame();
    void sync(PlayerObject* ghost, PlayerObject* real);
    void advance(PlayerObject* ghost, float dt);
    void collide(PlayerObject* ghost);
    void sweep();
    // windowOverride > 0: correr exactamente esa cantidad de pasos (sin fin anticipado).
    // pressAt: en qué paso se aprieta (Kind::Press); antes el fantasma no toca nada.
    SimOutcome run(Slot slot, Kind kind, int holdTicks, int windowOverride, Trace* trace = nullptr, int pressAt = 0,
                   std::vector<cocos2d::CCPoint>* path = nullptr);

    PlayLayer* m_layer;
    geode::Ref<cocos2d::CCLayer> m_ghostLayer;  // capa aparte, fuera de la escena
    unsigned int m_baseChildren = 0;            // hijos de la capa tras crear los fantasmas
    std::array<geode::Ref<PlayerObject>, 2> m_ghosts{};
    float m_dt = 0.25f;  // se reemplaza por el dt real en el primer tick
    float m_firstStepScale = 1.f;

    // presupuesto
    unsigned int m_frame = ~0u;
    double m_usedUs = 0.0;
    double m_budgetUs = 2000.0;
    bool m_priority = false;
    int m_stepsUsed = 0;

    bool m_debug = false;
    bool m_correctBoth = true;
    bool m_fullSync = true;

    // trayectoria
    bool m_recordPath = false;
    Tick m_now = 0;
    Tick m_pathTick = -1;
    std::vector<cocos2d::CCPoint> m_path;
    std::vector<cocos2d::CCPoint> m_scratchPath;

    // calibración
    static constexpr int kCalSteps = 6;
    static constexpr int kMaxLatency = 2;
    struct Calibration {
        bool active = false;
        Tick checkTick = 0;
        std::array<Trace, kMaxLatency + 1> traces{};
        std::array<bool, kMaxLatency + 1> valid{};
    };
    Calibration m_cal;
    std::array<int, kMaxLatency + 1> m_votes{};
    int m_injectLatency = 0;

    // validación
    bool m_validating = false;
    float m_predictedX = 0.f, m_predictedY = 0.f;
};

} // namespace ts
