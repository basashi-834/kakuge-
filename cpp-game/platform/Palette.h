// platform/Palette.h
// Shared color palette - switched to a LIGHT white/red/charcoal-text theme
// per the user's reference mockups (previously the WinForms edition used a
// dark charcoal theme; the user asked for the game to stop looking "black
// and dark" and match the reference images instead).
#pragma once
#include <windows.h>
#include <gdiplus.h>

namespace kakuge {

struct Palette {
    Gdiplus::Color Accent{255, 230, 45, 40};       // primary red
    Gdiplus::Color AccentDark{255, 178, 30, 27};
    Gdiplus::Color Bg{255, 250, 250, 248};         // near-white page background
    Gdiplus::Color PanelBg{255, 255, 255, 255};    // card/panel background
    Gdiplus::Color PanelBg2{255, 242, 242, 240};   // slightly-shaded panel
    Gdiplus::Color Border{255, 224, 224, 220};
    Gdiplus::Color TextDark{255, 30, 28, 28};      // primary text (near-black)
    Gdiplus::Color TextGray{255, 110, 108, 106};   // secondary text
    Gdiplus::Color White{255, 255, 255, 255};
    Gdiplus::Color HpEmpty{255, 232, 214, 210};
    Gdiplus::Color GaugeEmpty{255, 222, 220, 234};
    Gdiplus::Color Gauge{255, 130, 80, 220};        // purple super gauge
    Gdiplus::Color GroundStrip{255, 224, 210, 196};
};

inline const Palette& GetPalette() {
    static Palette p;
    return p;
}

} // namespace kakuge
