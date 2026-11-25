#include "InputScheduler.hpp"

#include <algorithm>
#include <utility>

namespace ts {

PressResult InputScheduler::onPhysicalPress(Slot slot, Tick now, SlotParams const& params, IPredictor& predictor) {
    auto& s = m_slots[idx(slot)];
    PressResult result;

    // Había un release programado (hold preservado de un click anterior) y el
    // jugador ya volvió a apretar: soltamos primero para no "fusionar" clicks.
    if (s.scheduledRelease) {
        s.scheduledRelease.reset();
        result.releaseFirst = true;
    }

    // Teníamos retenido un SOLTAR y el jugador volvió a apretar: en el juego el
    // botón sigue apretado, así que este press sobra. Cancelamos el soltar.
    if (s.pendingRelease) {
        s.pendingRelease = false;
        result.forwardNow = false;
        return result;
    }

    if (s.pending) {
        if (!s.released) {
            // Segundo dedo con el primero todavía apretado: sigue "mantenido".
            result.forwardNow = false;
            return result;
        }
        // El click retenido ya era un tap completo y el jugador volvió a
        // clickear: el tap viejo se descarta y el nuevo se evalúa desde cero.
        s = SlotState{};
    }

    ClickEvent ev;
    ev.slot = slot;
    ev.mode = params.mode;
    ev.pressTick = now;
    ev.applyTick = now;

    if (params.maxDelay <= 0) {
        ev.kind = ClickKind::Unchecked;
        m_events.push_back(ev);
        return result;  // forwardNow = true
    }

    // Primer chequeo: ¿el click en su tick original mata?
    switch (predictor.simulatePress(slot, params.expectedHold)) {
        case SimOutcome::Survives:
            // Pasa siempre (no cuesta nada), pero si no cambia nada no cuenta.
            ev.kind = predictor.pressHasEffect(slot, params.expectedHold) ? ClickKind::Clean : ClickKind::Unnecessary;
            m_events.push_back(ev);
            return result;
        case SimOutcome::Inconclusive:
        case SimOutcome::Skipped:
            // Sin certeza nunca agregamos latencia.
            ev.kind = ClickKind::Unchecked;
            m_events.push_back(ev);
            return result;
        case SimOutcome::Dies:
            break;
    }

    // Mata: lo retenemos y lo reevaluamos en los próximos ticks.
    s.pending = true;
    s.pressTick = now;
    s.mode = params.mode;
    s.released = false;
    s.releaseTick = 0;
    result.forwardNow = false;
    return result;
}

bool InputScheduler::onPhysicalRelease(Slot slot, Tick now, SlotParams const& params, IPredictor& predictor) {
    auto& s = m_slots[idx(slot)];
    if (s.pending) {
        s.released = true;
        s.releaseTick = now;
        return false;  // se reinyecta cuando se aplique el press
    }
    if (s.pendingRelease) return false;

    // Nave/wave: ¿soltar justo ahora mata? Entonces seguimos manteniendo.
    if (params.saveReleases && params.maxDelay > 0 && predictor.simulateRelease(slot) == SimOutcome::Dies) {
        s.pendingRelease = true;
        s.releaseHeldTick = now;
        s.mode = params.mode;
        return false;
    }
    return true;
}

void InputScheduler::onTick(Tick now, std::array<SlotParams, kSlotCount> const& params, IPredictor& predictor,
                            std::vector<InjectCmd>& out) {
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        auto slot = static_cast<Slot>(i);
        auto& s = m_slots[i];
        auto const& p = params[i];

        // 1) Releases programados por "preservar hold".
        if (s.scheduledRelease && now >= *s.scheduledRelease) {
            s.scheduledRelease.reset();
            out.push_back({slot, false});
        }

        // 2) Soltar retenido (nave/wave).
        if (s.pendingRelease) {
            tickRelease(slot, now, p, predictor, out);
            continue;
        }

        if (!s.pending) continue;

        // El press físico ya se evaluó para su propio tick (pressTick) en
        // onPhysicalPress; reevaluarlo ahí daría el mismo resultado.
        if (now <= s.pressTick) continue;

        int elapsed = static_cast<int>(now - s.pressTick);
        int hold = s.released ? std::max<int>(1, static_cast<int>(s.releaseTick - s.pressTick)) : p.expectedHold;

        // 3) Mod desactivado a mitad de una retención: soltar sin juzgar el click.
        if (p.maxDelay <= 0) {
            apply(slot, now, ClickKind::Unchecked, hold, out);
            continue;
        }

        // 4) Se acabó el margen permitido. Si el click ni siquiera hacía falta,
        //    se descarta (dispararlo ahora sólo te mataría); si no, se suelta.
        if (elapsed >= p.maxDelay) {
            if (p.dropUnneeded && predictor.simulateNoPress(slot, hold) == SimOutcome::Survives) {
                drop(slot, now);
            }
            else {
                apply(slot, now, ClickKind::Unsalvageable, hold, out);
            }
            continue;
        }

        // 5) ¿Apretar en ESTE tick sobrevive?
        auto outcome = predictor.simulatePress(slot, hold);
        if (outcome == SimOutcome::Survives || outcome == SimOutcome::Inconclusive) {
            // Si tampoco apretar mata, el click no hace falta: dispararlo tarde
            // sería un salto "fantasma" en cualquier momento. Se descarta.
            if (p.dropUnneeded && predictor.simulateNoPress(slot, hold) == SimOutcome::Survives) {
                drop(slot, now);
            }
            else {
                apply(slot, now, ClickKind::Saved, hold, out);
            }
            continue;
        }
        if (outcome == SimOutcome::Skipped) continue;  // sin CPU este tick: seguimos esperando

        // 6) Sigue matando. Si además esperar un tick más ya mata (por ejemplo
        //    un pincho justo delante del cubo), retener sólo empeora: aplicar.
        if (predictor.simulateIdle(slot, 1) == SimOutcome::Dies) {
            apply(slot, now, ClickKind::Unsalvageable, hold, out);
        }
    }
}

void InputScheduler::tickRelease(Slot slot, Tick now, SlotParams const& p, IPredictor& predictor,
                                 std::vector<InjectCmd>& out) {
    auto& s = m_slots[idx(slot)];
    if (now <= s.releaseHeldTick) return;
    int elapsed = static_cast<int>(now - s.releaseHeldTick);

    if (p.maxDelay <= 0 || !p.saveReleases) {
        applyRelease(slot, now, ClickKind::Unchecked, out);
        return;
    }
    if (elapsed >= p.maxDelay) {
        applyRelease(slot, now, ClickKind::Unsalvageable, out);
        return;
    }
    auto outcome = predictor.simulateRelease(slot);
    if (outcome == SimOutcome::Survives || outcome == SimOutcome::Inconclusive) {
        applyRelease(slot, now, ClickKind::Saved, out);
        return;
    }
    if (outcome == SimOutcome::Skipped) return;
    // Seguir manteniendo un tick más también mata: soltar ya.
    if (predictor.simulateIdle(slot, 1) == SimOutcome::Dies) {
        applyRelease(slot, now, ClickKind::Unsalvageable, out);
    }
}

void InputScheduler::apply(Slot slot, Tick now, ClickKind kind, int holdTicks, std::vector<InjectCmd>& out) {
    auto& s = m_slots[idx(slot)];
    out.push_back({slot, true});

    if (s.released) {
        // Sin preservar hold, el release va un tick después (un tap mínimo).
        int len = m_preserveHold ? std::max(1, holdTicks) : 1;
        s.scheduledRelease = now + len;
    }

    ClickEvent ev;
    ev.slot = slot;
    ev.kind = kind;
    ev.mode = s.mode;
    ev.pressTick = s.pressTick;
    ev.applyTick = now;
    ev.earlyTicks = static_cast<int>(now - s.pressTick);
    m_events.push_back(ev);

    s.pending = false;
    s.released = false;
}

void InputScheduler::drop(Slot slot, Tick now) {
    auto& s = m_slots[idx(slot)];
    // No se inyecta nada: ni el press ni su release (el juego nunca vio el press).
    ClickEvent ev;
    ev.slot = slot;
    ev.kind = ClickKind::Ignored;
    ev.mode = s.mode;
    ev.pressTick = s.pressTick;
    ev.applyTick = now;
    ev.earlyTicks = static_cast<int>(now - s.pressTick);
    m_events.push_back(ev);

    s.pending = false;
    s.released = false;
}

void InputScheduler::applyRelease(Slot slot, Tick now, ClickKind kind, std::vector<InjectCmd>& out) {
    auto& s = m_slots[idx(slot)];
    out.push_back({slot, false});

    ClickEvent ev;
    ev.slot = slot;
    ev.kind = kind;
    ev.mode = s.mode;
    ev.pressTick = s.releaseHeldTick;
    ev.applyTick = now;
    ev.earlyTicks = static_cast<int>(now - s.releaseHeldTick);
    ev.release = true;
    m_events.push_back(ev);

    s.pendingRelease = false;
}

std::vector<ClickEvent> InputScheduler::drainEvents() {
    return std::exchange(m_events, {});
}

void InputScheduler::reset() {
    m_slots = {};
    m_events.clear();
}

int InputScheduler::heldTicks(Slot slot, Tick now) const {
    auto const& s = m_slots[idx(slot)];
    if (s.pending) return static_cast<int>(now - s.pressTick);
    if (s.pendingRelease) return static_cast<int>(now - s.releaseHeldTick);
    return 0;
}

} // namespace ts
