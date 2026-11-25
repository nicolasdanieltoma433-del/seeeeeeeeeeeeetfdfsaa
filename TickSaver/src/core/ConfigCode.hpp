#pragma once
// ============================================================================
//  TickSaver · core/ConfigCode.hpp
//  Exportar / importar la configuración como un código corto "TS2-...".
//
//  Formato binario v3 (antes de codificar):
//    [0]      versión = 3
//    [1]      flags  b0 enabled  b1 correctBoth  b2 adaptive  b3 preserveHold
//                    b4 safeMode b5 region.on   b6 paleta propia  b7 reservado
//    [2]      flags2 b0 descartar innecesarios  b1 corregir soltar
//    [3]      preset
//    [4..5]   globalMaxTicks (u16, 0..1500)
//    [6..21]  modeMaxTicks[8] (u16)
//    [22..23] tramo desde%, hasta%
//    [24..25] tramo ticks extra (u16)
//    [26]     HUD flags  b0 hud  b1 barra  b2 resumen al morir
//    [27]     hudScale * 50 (0.5 .. 1.5  ->  25 .. 75)
//    [28..59] SOLO si b6: 8 colores RGBA (32 bytes)
//    [último] CRC-8 (poly 0x07) de todo lo anterior
//  Los códigos v2 (ticks de 1 byte, hasta 240) se siguen pudiendo importar.
//
//  Texto: "TS2-" + Base32 Crockford (sin I, L, O, U) en grupos de 5.
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
