#pragma once
// ============================================================================
//  TickSaver · core/InputScheduler.hpp
//  Máquina de estados que decide, tick a tick, si un click de salto se deja
//  pasar, se retiene o se reinyecta más tarde.
//
//  No sabe nada de Geometry Dash: le pregunta a un IPredictor ("si apreto
//  ahora, ¿vivo?") y devuelve comandos a inyectar en la cola del juego.
//  El IPredictor real es game/GhostSimulator; en tests es un mock.
//
//  Algoritmo ("evaluación perezosa tick a tick"):
//
//   tick T: llega un PRESS físico
//     └─ simulatePress(ahora) ─ vive ─────────────► pasa tal cual (Clean)
//                             └ muere ─► se RETIENE (pending)
//   tick T+1, T+2, ... mientras esté retenido:
//     ├─ simulatePress(ahora) vive ───────────────► se inyecta (Saved, +N t)
//     ├─ muere, y no apretar 1 tick más TAMBIÉN
//     │  mata ─────────────────────────────────► se inyecta (Unsalvageable)
//     ├─ N >= máximo de ticks ─────────────────► se inyecta (Unsalvageable)
//     └─ si no, sigue retenido
//
//  Ventajas frente a "buscar el offset ideal de una vez":
//   * cada evaluación usa el estado REAL del nivel en ese tick (objetos que
//     se mueven, triggers ya disparados), no una extrapolación;
//   * el costo se reparte: una simulación corta por tick retenido;
//   * nunca añade latencia a los clicks buenos (se evalúan y pasan en T).
//
//  RELEASE: si soltás mientras el press está retenido, el release también
//  se retiene. Con `preserveHold` se reinyecta para que dure lo mismo que
//  tu hold original (importante en nave/wave y en el salto del robot).
//
//  SOLTAR TEMPRANO (nave/wave, `saveReleases`): si soltar ahora mata, se
//  sigue "manteniendo" y se suelta en el primer tick en que soltar es seguro.
//
//  CLICKS INNECESARIOS (modos de toque, `dropUnneeded`): si el click mata y
//  NO apretar también sobrevive, el click no hacía falta: se descarta en vez
//  de dispararse tarde. Y un click que no cambia nada (cubo en el aire) se
//  marca Unnecessary y no cuenta para la racha ni las estadísticas.
// ============================================================================

#include "Types.hpp"

#include <array>
#include <optional>
#include <vector>

namespace ts {

// Lo que el scheduler necesita del juego. Implementado por GhostSimulator.
class IPredictor {
public:
    virtual ~IPredictor() = default;
    // Simula "el slot presiona en ESTE tick y mantiene `holdTicks` ticks".
    virtual SimOutcome simulatePress(Slot slot, int holdTicks) = 0;
    // Simula `ticks` ticks SIN presionar (chequeo de seguridad al retener).
    virtual SimOutcome simulateIdle(Slot slot, int ticks) = 0;

    // No apretar durante la ventana que tendría un press: ¿hace falta el click?
    // (Por defecto "muere" = el click hace falta, nunca se descarta.)
    virtual SimOutcome simulateNoPress(Slot, int /*holdTicks*/) { return SimOutcome::Dies; }
    // ¿Apretar cambia algo en los próximos ticks? (cubo en el aire -> no)
    virtual bool pressHasEffect(Slot, int /*holdTicks*/) { return true; }
    // Soltar en ESTE tick (el jugador venía manteniendo).
    virtual SimOutcome simulateRelease(Slot) { return SimOutcome::Survives; }
    // Press que REINYECTA el mod, aplicado `extraDelay` ticks después de ahora.
    // (Puede tener otra latencia que un click físico; el simulador la calibra.)
    virtual SimOutcome simulateInjectedPress(Slot slot, int holdTicks, int extraDelay) {
        return extraDelay == 0 ? simulatePress(slot, holdTicks) : SimOutcome::Dies;
    }
};

// Parámetros vigentes para un slot en el tick actual (los calcula el motor
// a partir de Config + modo actual + % del nivel + modelo adaptativo).
struct SlotParams {
    int maxDelay = 0;           // 0 = no evaluar este slot (pasa directo)
    int expectedHold = 8;       // hold supuesto si el jugador aún no soltó
    Mode mode = Mode::Cube;     // sólo para etiquetar eventos
    bool dropUnneeded = false;  // modos de toque: descartar clicks que no hacen falta
    bool saveReleases = false;  // nave/wave: corregir también el soltar
    // Ticks de margen: si el tick siguiente también es seguro, esperar para no
    // disparar justo en el borde de la ventana segura (frágil ante errores).
    int safetyMargin = 0;
};

// Comando que el motor debe meter en GJBaseGameLayer::m_queuedButtons.
struct InjectCmd {
    Slot slot;
    bool push;  // true = press, false = release
};

// Evento para HUD / racha / estadísticas.
struct ClickEvent {
    Slot slot = Slot::P1;
    ClickKind kind = ClickKind::Unchecked;
    int earlyTicks = 0;  // cuántos ticks se retrasó (0 si Clean)
    Mode mode = Mode::Cube;
    Tick pressTick = 0;  // tick del click físico
    Tick applyTick = 0;  // tick en que el juego lo recibió
    bool release = false;  // el evento es de un SOLTAR (nave/wave)
};

struct PressResult {
    bool forwardNow = true;     // dejar el press original en la cola
    bool releaseFirst = false;  // inyectar antes un release que estaba programado
};

class InputScheduler {
public:
    void setPreserveHold(bool v) { m_preserveHold = v; }

    // Press físico. `now` = tick en el que el juego lo APLICARÍA (el próximo
    // paso de física), no el último tick ya simulado.
    PressResult onPhysicalPress(Slot slot, Tick now, SlotParams const& params, IPredictor& predictor);

    // Release físico. true = dejarlo pasar; false = queda retenido.
    bool onPhysicalRelease(Slot slot, Tick now, SlotParams const& params, IPredictor& predictor);

    // Una vez por tick, ANTES de que el juego procese la cola. Agrega a `out`
    // los comandos a inyectar en este tick.
    void onTick(Tick now, std::array<SlotParams, kSlotCount> const& params, IPredictor& predictor,
                std::vector<InjectCmd>& out);

    // Eventos generados desde la última llamada.
    std::vector<ClickEvent> drainEvents();

    // Muerte / reinicio / salir: descarta todo lo retenido SIN inyectar.
    void reset();

    bool isHolding(Slot slot) const { return m_slots[idx(slot)].pending || m_slots[idx(slot)].pendingRelease; }
    bool anyHolding() const { return isHolding(Slot::P1) || isHolding(Slot::P2); }
    int heldTicks(Slot slot, Tick now) const;

private:
    struct SlotState {
        bool pending = false;  // hay un press retenido
        Tick pressTick = 0;
        Mode mode = Mode::Cube;
        bool released = false;  // el jugador ya soltó mientras estaba retenido
        Tick releaseTick = 0;
        std::optional<Tick> scheduledRelease;  // release a inyectar en ese tick
        bool pendingRelease = false;           // un SOLTAR retenido (nave/wave)
        Tick releaseHeldTick = 0;
        int viableTicks = 0;                   // ticks seguidos en que apretar ya sobrevivía
    };

    void apply(Slot slot, Tick now, ClickKind kind, int holdTicks, std::vector<InjectCmd>& out);
    void drop(Slot slot, Tick now);
    void applyRelease(Slot slot, Tick now, ClickKind kind, std::vector<InjectCmd>& out);
    void tickRelease(Slot slot, Tick now, SlotParams const& p, IPredictor& predictor, std::vector<InjectCmd>& out);

    std::array<SlotState, kSlotCount> m_slots{};
    std::vector<ClickEvent> m_events;
    bool m_preserveHold = true;
};

} // namespace ts
