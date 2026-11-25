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

    // Re-press mientras el anterior sigue retenido: no creamos otro pendiente,
    // simplemente el jugador vuelve a estar "mantenido".
    if (s.pending) {
        s.released = false;
        result.forwardNow = false;
        return result;
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
            ev.kind = ClickKind::Clean;
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

bool InputScheduler::onPhysicalRelease(Slot slot, Tick now) {
    auto& s = m_slots[idx(slot)];
    if (s.pending) {
        s.released = true;
        s.releaseTick = now;
        return false;  // se reinyecta cuando se aplique el press
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

        if (!s.pending) continue;

        // El press físico ya se evaluó para su propio tick (pressTick) en
        // onPhysicalPress; reevaluarlo ahí daría el mismo resultado.
        if (now <= s.pressTick) continue;

        int elapsed = static_cast<int>(now - s.pressTick);
        int hold = s.released ? std::max<int>(1, static_cast<int>(s.releaseTick - s.pressTick)) : p.expectedHold;

        // 2) Mod desactivado a mitad de una retención: soltar sin juzgar el click.
        if (p.maxDelay <= 0) {
            apply(slot, now, ClickKind::Unchecked, hold, out);
            continue;
        }

        // 3) Se acabó el margen permitido: lo soltamos tal cual.
        if (elapsed >= p.maxDelay) {
            apply(slot, now, ClickKind::Unsalvageable, hold, out);
            continue;
        }

        // 4) ¿Apretar en ESTE tick sobrevive?
        auto outcome = predictor.simulatePress(slot, hold);
        if (outcome == SimOutcome::Survives || outcome == SimOutcome::Inconclusive) {
            apply(slot, now, ClickKind::Saved, hold, out);
            continue;
        }
        if (outcome == SimOutcome::Skipped) continue;  // sin CPU este tick: seguimos esperando

        // 5) Sigue matando. Si además esperar un tick más ya mata (por ejemplo
        //    un pincho justo delante del cubo), retener sólo empeora: aplicar.
        if (predictor.simulateIdle(slot, 1) == SimOutcome::Dies) {
            apply(slot, now, ClickKind::Unsalvageable, hold, out);
        }
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

std::vector<ClickEvent> InputScheduler::drainEvents() {
    return std::exchange(m_events, {});
}

void InputScheduler::reset() {
    m_slots = {};
    m_events.clear();
}

int InputScheduler::heldTicks(Slot slot, Tick now) const {
    auto const& s = m_slots[idx(slot)];
    return s.pending ? static_cast<int>(now - s.pressTick) : 0;
}

} // namespace ts
