#pragma once
// ============================================================================
//  TickSaver · game/LiveLog.hpp
//  Registro en vivo de lo que hizo el mod (los últimos 64 eventos): cada
//  click evaluado, salvadas, predicciones fallidas, calibración. Se ve en la
//  pestaña "Log" del menú y se puede copiar para mandarlo como reporte.
//  Idea tomada de Afterflow (MIT, (c) 2026 prsevend).
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace ts::livelog {

inline constexpr std::size_t kCapacity = 64;

void push(std::string line);
// Los más viejos primero.
std::vector<std::string> lines();
std::string joined();
void clear();

} // namespace ts::livelog
