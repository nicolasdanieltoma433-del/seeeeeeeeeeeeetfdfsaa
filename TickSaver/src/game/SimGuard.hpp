#pragma once
// ============================================================================
//  TickSaver · game/SimGuard.hpp
//  Estado global de "estamos simulando un fantasma" + aislamiento de
//  efectos secundarios.
//
//  El fantasma usa el MISMO código de colisiones del juego
//  (GJBaseGameLayer::checkCollisions). Ese código, además de mover al
//  jugador, toca el nivel: marca orbes/pads como usados, recoge monedas,
//  dispara triggers por contacto, cambia la cámara en portales, crea
//  checkpoints de práctica, mata al jugador... Los hooks de
//  hooks/GhostGuardHooks.cpp consultan este módulo y:
//    * bloquean lo que no debe pasar nunca durante una simulación
//      (monedas, triggers, checkpoints, partículas, sonidos, dual portal);
//    * registran en un "libro" los flags de activación de orbes/pads que el
//      fantasma cambió, y los RESTAURAN al terminar (Scope::~Scope);
//    * convierten la muerte del fantasma en un flag en vez de matarlo.
//
//  Todo esto corre en el hilo principal del juego: no hace falta sincronizar.
// ============================================================================

class PlayerObject;
class EnhancedGameObject;

namespace ts::sim {

bool active();
bool isGhost(PlayerObject const* player);

void registerGhost(PlayerObject* ghost);
void unregisterGhost(PlayerObject* ghost);

// RAII: marca el inicio y fin de UNA simulación.
class Scope {
public:
    Scope();
    ~Scope();
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
};

// RAII: copiando el estado del jugador al fantasma (loadFromCheckpoint
// cambia de modo, toca orbes...). Los hooks bloquean efectos sobre el nivel
// igual que en una simulación, pero no se anotan muertes ni "inconclusos".
class SyncScope {
public:
    SyncScope();
    ~SyncScope();
    SyncScope(SyncScope const&) = delete;
    SyncScope& operator=(SyncScope const&) = delete;
};

// --- resultados que escriben los hooks durante la simulación ---------------
void markDeath(PlayerObject* ghost);
bool died(PlayerObject const* ghost);

// El fantasma tocó algo que no podemos simular sin efectos colaterales
// (portal de modo, dual, teleport...). Se corta la simulación y se trata
// como "vive" para no retener clicks por una predicción dudosa.
void markInconclusive(char const* reason);
bool inconclusive();
char const* inconclusiveReason();

// Llamar ANTES de que el juego marque un orbe/pad como activado por el
// fantasma. Guarda los flags originales una sola vez por objeto.
void recordActivation(EnhancedGameObject* object);

} // namespace ts::sim
