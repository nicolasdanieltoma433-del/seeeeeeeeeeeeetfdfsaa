#pragma once
// ============================================================================
//  TickSaver · game/StatsStore.hpp
//  Persistencia en disco:
//
//   <save dir del mod>/levels/<clave>.json   -> LevelStats de cada nivel
//   saved.json (Mod::setSavedValue "adaptive") -> AdaptiveModel global
//
//  Clave de nivel:
//    nivel online      -> "id-<levelID>"
//    nivel local/editor-> "local-<hash del nombre>" (no tienen ID online)
//
//  Un archivo por nivel (en vez de uno gigante) para que guardar tras cada
//  muerte sea barato y un archivo corrupto no borre todo el historial.
// ============================================================================

#include "../core/AdaptiveModel.hpp"
#include "../core/Stats.hpp"

#include <string>

class GJGameLevel;

namespace ts::store {

std::string levelKey(GJGameLevel* level);

LevelStats loadLevel(GJGameLevel* level);
void saveLevel(LevelStats const& stats);
void deleteLevel(std::string const& key);

// Modelo adaptativo compartido entre niveles (tu forma de jugar no cambia
// de un nivel a otro).
AdaptiveModel& adaptive();
void loadAdaptive();
void saveAdaptive();

} // namespace ts::store
