#pragma once
// ============================================================================
//  TickSaver · ui/Palette.hpp
//  Conversión de la paleta de 8 colores (ts::RGBA) a tipos de cocos2d.
// ============================================================================

#include "../core/Types.hpp"

#include <Geode/Geode.hpp>

namespace ts::ui {

inline cocos2d::ccColor3B c3(RGBA c) { return {c.r, c.g, c.b}; }

inline cocos2d::ccColor4B c4b(RGBA c) { return {c.r, c.g, c.b, c.a}; }

inline cocos2d::ccColor4F c4f(RGBA c, float alphaMul = 1.f) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, (c.a / 255.f) * alphaMul};
}

inline RGBA role(Palette const& p, PaletteRole r) { return paletteColor(p, r); }

// Color del marcador según el tipo de click.
inline RGBA colorForKind(Palette const& p, ClickKind kind) {
    switch (kind) {
        case ClickKind::Clean: return role(p, PaletteRole::Good);
        case ClickKind::Saved: return role(p, PaletteRole::Saved);
        case ClickKind::Unsalvageable: return role(p, PaletteRole::Danger);
        case ClickKind::Unchecked:
        case ClickKind::Unnecessary: return role(p, PaletteRole::Text);
        case ClickKind::Ignored: return role(p, PaletteRole::Saved);
    }
    return role(p, PaletteRole::Text);
}

} // namespace ts::ui
