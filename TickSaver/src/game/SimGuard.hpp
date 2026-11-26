#pragma once
// ============================================================================
//  TickSaver · game/SimGuard.hpp
//  Estado global de "estamos simulando un fantasma" + aislamiento de
//  efectos secundarios.
//
//  El fantasma usa el MISMO código de colisiones del juego
//  (GJBaseGameLayer::checkCollisions). Ese código, además de mover al
//  jugador, toca el nivel: marca orbes/pads como usados, recoge monedas,
//  dispara triggers, apaga grupos, crea checkpoints de práctica, mata al
//  jugador... Los hooks de hooks/GhostGuardHooks.cpp consultan este módulo y:
//    * bloquean lo que no debe pasar nunca durante una simulación
//      (monedas, triggers, grupos, contadores, partículas, sonidos);
//    * guardan una FOTO de cada objeto que el fantasma toca (activado, usado
//      por P1/P2) y la restauran al terminar (Scope::~Scope);
//    * llevan la cuenta de qué orbes/pads ya usó el fantasma EN ESTA
//      simulación (para no usarlos dos veces);
//    * recuerdan los objetos que un orbe/trigger apagó durante la
//      simulación, para que el fantasma deje de chocarlos;
//    * convierten la muerte del fantasma en un flag en vez de matarlo.
//
//  Técnica adaptada de Afterflow (MIT, (c) 2026 prsevend).
//  Todo esto corre en el hilo principal del juego: no hace falta sincronizar.
// ============================================================================

#include <Geode/cocos/cocoa/CCGeometry.h>

class PlayerObject;
class GameObject;

namespace ts::sim {

bool active();
bool isGhost(PlayerObject const* player);

void registerGhost(PlayerObject* ghost);
void unregisterGhost(PlayerObject* ghost);

// Jugador REAL que copia cada fantasma (se fija en cada simulación). Sirve
// para preguntarle al juego "¿este orbe/pad todavía lo puede usar?".
void setCounterpart(PlayerObject* ghost, PlayerObject* real);
PlayerObject* counterpart(PlayerObject const* ghost);

// RAII: marca el inicio y fin de UNA simulación.
class Scope {
public:
    Scope();
    ~Scope();
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
};

// RAII: copiando el estado del jugador al fantasma (copyAttributes puede
// cambiar de modo). Los hooks bloquean efectos sobre el nivel igual que en
// una simulación, pero no se anotan muertes ni portales.
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
bool anyDied();

// El fantasma llegó a un portal de modo / dual / teleport, o al final del
// nivel. No lo simulamos más allá: llegar vivo hasta ahí cuenta como "vive".
void markPortal(char const* reason);
bool hitPortal();
char const* portalReason();

// Paso actual de la simulación (0 = primer paso).
void setStep(int step);
int step();

// --- foto y restauración de objetos ---------------------------------------
// Llamar ANTES de que el juego modifique un objeto por culpa del fantasma.
// Guarda sus flags originales una sola vez por simulación.
void snapshot(GameObject* object);

// Orbes / pads / dash ya usados por el fantasma en ESTA simulación.
void markUsed(GameObject* object);
bool used(GameObject* object);
// ¿Le preguntamos ya al juego por este objeto en esta simulación?
bool seen(GameObject* object);

// Objetos apagados (toggle de grupo) durante esta simulación.
void disable(GameObject* object);
bool disabled(GameObject* object);

// Pinchos que el jugador real YA está tocando al empezar (hitbox que lo
// roza sin matarlo): el fantasma los ignora, como hace el juego.
void setRealRects(bool enabled, cocos2d::CCRect const& p1, cocos2d::CCRect const& p2);
bool overlapsRealPlayer(cocos2d::CCRect const& rect);
void markHarmless(GameObject* object);
bool harmless(GameObject* object);

} // namespace ts::sim
