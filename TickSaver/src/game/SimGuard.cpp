#include "SimGuard.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/EnhancedGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace ts::sim {
namespace {

struct ObjectSnapshot {
    GameObject* object;
    EnhancedGameObject* enhanced;  // nullptr si no es EnhancedGameObject
    bool isActivated;
    bool activated;
    bool byP1;
    bool byP2;
};

struct GhostEntry {
    PlayerObject* ghost = nullptr;
    PlayerObject* real = nullptr;
};

struct State {
    bool active = false;
    bool syncing = false;
    std::array<GhostEntry, 4> ghosts{};  // P1, P2 (+ margen)
    std::array<PlayerObject*, 4> dead{};
    bool portal = false;
    char const* reason = "";
    int step = 0;

    std::vector<ObjectSnapshot> snapshots;
    std::unordered_set<GameObject*> snapshotted;
    std::unordered_set<GameObject*> usedObjects;
    std::unordered_set<GameObject*> disabledObjects;
    std::unordered_set<GameObject*> harmlessObjects;

    bool realRects = false;
    CCRect realP1, realP2;
};

State& state() {
    static State s;
    return s;
}

void restoreSnapshots() {
    auto& s = state();
    // Orden inverso: si un objeto se fotografió dos veces (no debería) queda
    // el valor más viejo.
    for (auto it = s.snapshots.rbegin(); it != s.snapshots.rend(); ++it) {
        it->object->m_isActivated = it->isActivated;
        if (it->enhanced) {
            it->enhanced->m_activated = it->activated;
            it->enhanced->m_activatedByPlayer1 = it->byP1;
            it->enhanced->m_activatedByPlayer2 = it->byP2;
        }
    }
    s.snapshots.clear();
    s.snapshotted.clear();
}

void resetSimulationState() {
    auto& s = state();
    s.dead.fill(nullptr);
    s.portal = false;
    s.reason = "";
    s.step = 0;
    s.usedObjects.clear();
    s.disabledObjects.clear();
    s.harmlessObjects.clear();
    s.realRects = false;
}

} // namespace

bool active() { return state().active; }

bool isGhost(PlayerObject const* player) {
    if (!player) return false;
    for (auto const& g : state().ghosts) {
        if (g.ghost == player) return true;
    }
    return false;
}

void registerGhost(PlayerObject* ghost) {
    for (auto& slot : state().ghosts) {
        if (slot.ghost == ghost) return;
    }
    for (auto& slot : state().ghosts) {
        if (!slot.ghost) {
            slot.ghost = ghost;
            slot.real = nullptr;
            return;
        }
    }
}

void unregisterGhost(PlayerObject* ghost) {
    for (auto& slot : state().ghosts) {
        if (slot.ghost == ghost) slot = GhostEntry{};
    }
}

void setCounterpart(PlayerObject* ghost, PlayerObject* real) {
    for (auto& slot : state().ghosts) {
        if (slot.ghost == ghost) slot.real = real;
    }
}

PlayerObject* counterpart(PlayerObject const* ghost) {
    for (auto const& slot : state().ghosts) {
        if (slot.ghost == ghost) return slot.real;
    }
    return nullptr;
}

Scope::Scope() {
    auto& s = state();
    s.active = true;
    resetSimulationState();
}

Scope::~Scope() {
    restoreSnapshots();
    resetSimulationState();
    state().active = false;
}

SyncScope::SyncScope() {
    auto& s = state();
    s.active = true;
    s.syncing = true;
}

SyncScope::~SyncScope() {
    restoreSnapshots();
    auto& s = state();
    s.syncing = false;
    s.active = false;
}

void markDeath(PlayerObject* ghost) {
    auto& s = state();
    if (s.syncing) return;
    for (auto& slot : s.dead) {
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

bool anyDied() { return state().dead[0] != nullptr; }

void markPortal(char const* reason) {
    auto& s = state();
    if (s.syncing) return;
    if (!s.portal) {
        s.portal = true;
        s.reason = reason;
    }
}

bool hitPortal() { return state().portal; }
char const* portalReason() { return state().reason; }

void setStep(int step) { state().step = step; }
int step() { return state().step; }

void snapshot(GameObject* object) {
    if (!object) return;
    auto& s = state();
    if (!s.snapshotted.insert(object).second) return;  // ya tenemos la foto original
    ObjectSnapshot snap{object, nullptr, object->m_isActivated, false, false, false};
    if (auto* enhanced = typeinfo_cast<EnhancedGameObject*>(object)) {
        snap.enhanced = enhanced;
        snap.activated = enhanced->m_activated;
        snap.byP1 = enhanced->m_activatedByPlayer1;
        snap.byP2 = enhanced->m_activatedByPlayer2;
    }
    s.snapshots.push_back(snap);
}

void markUsed(GameObject* object) {
    if (object) state().usedObjects.insert(object);
}

bool used(GameObject* object) { return object && state().usedObjects.contains(object); }

bool seen(GameObject* object) { return object && state().snapshotted.contains(object); }

void disable(GameObject* object) {
    if (object) state().disabledObjects.insert(object);
}

bool disabled(GameObject* object) {
    auto const& d = state().disabledObjects;
    return !d.empty() && d.contains(object);
}

void setRealRects(bool enabled, CCRect const& p1, CCRect const& p2) {
    auto& s = state();
    s.realRects = enabled;
    s.realP1 = p1;
    s.realP2 = p2;
}

bool overlapsRealPlayer(CCRect const& rect) {
    auto const& s = state();
    return s.realRects && (rect.intersectsRect(s.realP1) || rect.intersectsRect(s.realP2));
}

void markHarmless(GameObject* object) {
    if (object) state().harmlessObjects.insert(object);
}

bool harmless(GameObject* object) {
    auto const& h = state().harmlessObjects;
    return !h.empty() && h.contains(object);
}

} // namespace ts::sim
