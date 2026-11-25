#include "SimGuard.hpp"

#include <Geode/binding/EnhancedGameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <vector>

namespace ts::sim {
namespace {

struct ActivationRecord {
    EnhancedGameObject* object;
    bool byP1;
    bool byP2;
};

struct State {
    bool active = false;
    std::array<PlayerObject*, 4> ghosts{};  // P1, P2 (+ margen)
    std::array<PlayerObject*, 4> dead{};
    bool inconclusive = false;
    char const* reason = "";
    std::vector<ActivationRecord> ledger;
};

State& state() {
    static State s;
    return s;
}

} // namespace

bool active() { return state().active; }

bool isGhost(PlayerObject const* player) {
    if (!player) return false;
    auto const& g = state().ghosts;
    return std::find(g.begin(), g.end(), player) != g.end();
}

void registerGhost(PlayerObject* ghost) {
    for (auto& slot : state().ghosts) {
        if (slot == ghost) return;
        if (!slot) {
            slot = ghost;
            return;
        }
    }
}

void unregisterGhost(PlayerObject* ghost) {
    for (auto& slot : state().ghosts) {
        if (slot == ghost) slot = nullptr;
    }
}

Scope::Scope() {
    auto& s = state();
    s.active = true;
    s.dead.fill(nullptr);
    s.inconclusive = false;
    s.reason = "";
    s.ledger.clear();
}

Scope::~Scope() {
    auto& s = state();
    // Deshacer los flags de activación que tocó el fantasma, en orden
    // inverso, para que el jugador real encuentre los orbes/pads intactos.
    for (auto it = s.ledger.rbegin(); it != s.ledger.rend(); ++it) {
        it->object->m_activatedByPlayer1 = it->byP1;
        it->object->m_activatedByPlayer2 = it->byP2;
    }
    s.ledger.clear();
    s.active = false;
}

void markDeath(PlayerObject* ghost) {
    for (auto& slot : state().dead) {
        if (slot == ghost) return;
        if (!slot) {
            slot = ghost;
            return;
        }
    }
}

bool died(PlayerObject const* ghost) {
    auto const& d = state().dead;
    return std::find(d.begin(), d.end(), ghost) != d.end();
}

void markInconclusive(char const* reason) {
    auto& s = state();
    if (!s.inconclusive) {
        s.inconclusive = true;
        s.reason = reason;
    }
}

bool inconclusive() { return state().inconclusive; }
char const* inconclusiveReason() { return state().reason; }

void recordActivation(EnhancedGameObject* object) {
    if (!object) return;
    auto& ledger = state().ledger;
    for (auto const& r : ledger) {
        if (r.object == object) return;  // ya guardamos el estado original
    }
    ledger.push_back({object, object->m_activatedByPlayer1, object->m_activatedByPlayer2});
}

} // namespace ts::sim
