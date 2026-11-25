#pragma once
// ============================================================================
//  TickSaver · core/Types.hpp
//  Tipos básicos compartidos por todo el mod.
//
//  REGLA DE ORO DE LA CARPETA core/:
//    Nada aquí incluye Geode ni headers de Geometry Dash. Es C++23 puro.
//    Así la lógica (scheduler, presets, código TS2, estadísticas, modo
//    adaptativo) se puede compilar y testear en cualquier PC con
//    `tests/` sin abrir el juego.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ts {

// Un "tick" es un paso de física de GD. En 2.2+ el juego simula a 240 TPS
// (240 pasos por segundo), independientemente de los FPS de pantalla.
using Tick = std::int64_t;

inline constexpr int kTicksPerSecond = 240;
// Límite duro del slider de ticks (1500t = 6,25 segundos de retraso máximo).
inline constexpr int kMaxSaveTicks = 1500;

// ----------------------------------------------------------------------------
// Modos de juego. El orden importa: se usa como índice en arrays y en el
// código de exportación TS2, así que NO reordenar (sólo añadir al final).
// ----------------------------------------------------------------------------
enum class Mode : std::uint8_t {
    Cube = 0,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Count
};
inline constexpr std::size_t kModeCount = static_cast<std::size_t>(Mode::Count);

constexpr std::size_t idx(Mode m) { return static_cast<std::size_t>(m); }

// Claves estables (settings de mod.json, JSON de estadísticas).
inline constexpr std::array<std::string_view, kModeCount> kModeKeys = {
    "cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing",
};

// Nombres para la UI. OJO: las fuentes bitmap de GD (bigFont, goldFont)
// no tienen tildes ni ñ, por eso todo el texto visible va sin acentos.
inline constexpr std::array<std::string_view, kModeCount> kModeNames = {
    "Cubo", "Nave", "Bola", "UFO", "Wave", "Robot", "Arana", "Swing",
};

// ----------------------------------------------------------------------------
// Slot de input: cada "botón lógico" que puede quedar retenido.
//  - Modo normal / dual de 1 jugador: todo va al slot P1.
//  - Modo 2 jugadores (level->m_twoPlayerMode): P1 y P2 tienen su propio
//    slot, cada uno con su propia cola de clicks retrasados.
// ----------------------------------------------------------------------------
enum class Slot : std::uint8_t { P1 = 0, P2 = 1 };
inline constexpr std::size_t kSlotCount = 2;
constexpr std::size_t idx(Slot s) { return static_cast<std::size_t>(s); }

// ----------------------------------------------------------------------------
// Resultado de una simulación "fantasma".
// ----------------------------------------------------------------------------
enum class SimOutcome : std::uint8_t {
    Survives,      // el fantasma sobrevivió a toda la ventana
    Dies,          // el fantasma murió dentro de la ventana
    Inconclusive,  // tocó algo que no simulamos (portal, teleport...). Se trata como "vive".
    Skipped,       // no hubo presupuesto de CPU este tick para simular
};

// ----------------------------------------------------------------------------
// Clasificación de cada click del jugador (para HUD, racha y stats).
// ----------------------------------------------------------------------------
enum class ClickKind : std::uint8_t {
    Clean,          // el click en su tick original no mata -> "justo"
    Saved,          // el click mataba; se retrasó N ticks y sobrevive
    Unsalvageable,  // se retuvo hasta el máximo y ningún tick servía
    Unchecked,      // no se evaluó (modo con 0 ticks, sin presupuesto, etc.)
    Unnecessary,    // el click no cambia nada (ej. cubo en el aire): no cuenta
    Ignored,        // el click mataba y NO hacía falta: se descartó
};

// ----------------------------------------------------------------------------
// Paleta de 8 colores personalizables.
// ----------------------------------------------------------------------------
struct RGBA {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
    constexpr bool operator==(RGBA const&) const = default;
};

inline constexpr std::size_t kPaletteSize = 8;
using Palette = std::array<RGBA, kPaletteSize>;

// Qué significa cada color de la paleta (índice = rol).
enum class PaletteRole : std::uint8_t {
    Streak = 0,  // número de racha "x12"
    Good,        // click justo / zona verde de la barra
    Early,       // zona "temprano" de la barra
    Saved,       // marcador y aviso "SALVADO +Nt"
    Danger,      // sin salvación / muerte
    BarBg,       // fondo de la barra y paneles
    Text,        // texto secundario
    Accent,      // histograma y detalles
};
inline constexpr std::array<std::string_view, kPaletteSize> kPaletteKeys = {
    "color-streak", "color-good", "color-early", "color-saved",
    "color-danger", "color-bar-bg", "color-text", "color-accent",
};

constexpr RGBA paletteColor(Palette const& p, PaletteRole role) {
    return p[static_cast<std::size_t>(role)];
}

} // namespace ts
