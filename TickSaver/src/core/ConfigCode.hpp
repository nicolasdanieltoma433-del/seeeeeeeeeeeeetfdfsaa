#pragma once
// ============================================================================
//  TickSaver · core/ConfigCode.hpp
//  Exportar / importar la configuración como un código corto "TS2-...".
//
//  Formato binario v2 (antes de codificar):
//    [0]      versión = 2
//    [1]      flags  b0 enabled  b1 correctBoth  b2 adaptive  b3 preserveHold
//                    b4 safeMode b5 region.on   b6 paleta propia  b7 reservado
//    [2]      preset
//    [3]      globalMaxTicks (0..240)
//    [4..11]  modeMaxTicks[8]
//    [12..14] region start%, end%, extraTicks
//    [15]     HUD flags  b0 hud  b1 barra  b2 resumen al morir
//    [16]     hudScale * 50 (0.5 .. 1.5  ->  25 .. 75)
//    [17..48] SOLO si b6: 8 colores RGBA (32 bytes)
//    [último] CRC-8 (poly 0x07) de todo lo anterior
//
//  Texto: "TS2-" + Base32 Crockford (sin I, L, O, U) en grupos de 5.
//  Sin paleta propia el código ocupa 38 caracteres
//  (ej. TS2-08ZG8-1G01G-00060-00062-GF0C0-WYCP); con paleta propia, 99.
//  Al importar se ignoran guiones/espacios y se aceptan minúsculas, y
//  I/L -> 1, O -> 0 (letras que la gente confunde al copiar a mano).
// ============================================================================

#include "Config.hpp"

#include <string>
#include <string_view>
#include <variant>

namespace ts {

inline constexpr std::string_view kCodePrefix = "TS2-";

// Error de importación legible para mostrar en una notificación.
struct CodeError {
    std::string message;
};

std::string encodeConfig(Config const& cfg);

// Devuelve la Config importada o un error. Los campos que el código no
// transporta (avanzado) se copian de `base`.
std::variant<Config, CodeError> decodeConfig(std::string_view code, Config const& base);

} // namespace ts
