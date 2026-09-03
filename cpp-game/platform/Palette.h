// platform/Palette.h
// "Modernist candy" design tokens, matching the HARD CANDY reference spec
// exactly (bg/ink/accent/accent-deep etc all pulled straight from its
// token table). Flat, architectural, near-mono red on light ground, zero
// corner radius, strong 2px ink rules - the "candy" feel comes entirely
// from three overlay devices applied on top of that flat skeleton (see
// DrawGlossCap / DrawDiagonalShine / DrawHardShadow in Draw.h), never from
// rounding corners or blurring shadows.
#pragma once
#include "GdiPlusInclude.h"

namespace kakuge {

struct Palette {
    Gdiplus::Color Bg{255, 243, 242, 242};          // --bg #f3f2f2
    Gdiplus::Color Ink{255, 32, 30, 29};             // --ink #201e1d - rules, borders, primary text
    Gdiplus::Color Ink70{255, 68, 65, 65};           // --ink-70 secondary body copy
    Gdiplus::Color Ink55{255, 96, 93, 93};           // --ink-55 micro-caps labels
    Gdiplus::Color Ink45{255, 125, 121, 121};        // --ink-45 least-important line
    Gdiplus::Color Accent{255, 236, 48, 19};         // --accent #ec3013
    Gdiplus::Color AccentDeep{255, 174, 24, 0};      // --accent-deep #ae1800 - accent-colored TEXT only
    Gdiplus::Color OnAccent{255, 255, 255, 255};     // --on-accent
    Gdiplus::Color RuleSoft{64, 32, 30, 29};         // rgba(32,30,29,.25) table/panel rules
    Gdiplus::Color EmptyBar{38, 32, 30, 29};         // rgba(32,30,29,.15) unfilled stat/HP/gauge segments
    Gdiplus::Color TintRed{31, 236, 48, 19};         // rgba(236,48,19,.12) secondary fills, art-area ground
    Gdiplus::Color PanelBg{255, 255, 255, 255};      // card/panel fill (white, on top of --bg)
    Gdiplus::Color PanelBg2{255, 236, 236, 234};     // slightly-shaded panel (selection/hover states)
    Gdiplus::Color Gauge{255, 236, 48, 19};          // gauge fill reuses accent (segmented bar, not a separate hue)
    Gdiplus::Color GroundStrip{255, 224, 210, 196};

    // The in-match HUD ("Screen 03" in the reference) runs on a dark arena
    // ground, unlike every other (light) screen - these are used only by
    // App::DrawGame / DrawHUD.
    Gdiplus::Color ArenaBg{255, 58, 56, 55};
    Gdiplus::Color ArenaPanel{255, 40, 38, 37};        // name tag / FIGHT banner fill
    Gdiplus::Color ArenaLine{255, 235, 232, 230};      // borders/text that must read on ArenaBg
    Gdiplus::Color ArenaTextDim{200, 235, 232, 230};   // secondary labels on ArenaBg

    // ---- Back-compat aliases for call sites not yet migrated to the named
    // tokens above (kept so this pass doesn't require touching every
    // draw call in one shot) ----
    Gdiplus::Color& AccentDark = AccentDeep;
    Gdiplus::Color& TextDark = Ink;
    Gdiplus::Color& TextGray = Ink55;
    Gdiplus::Color White{255, 255, 255, 255};
    Gdiplus::Color& HpEmpty = EmptyBar;
    Gdiplus::Color& GaugeEmpty = EmptyBar;
    Gdiplus::Color& Border = Ink;
};

inline const Palette& GetPalette() {
    static Palette p;
    return p;
}

} // namespace kakuge
