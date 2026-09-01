// platform/Layout.h
// Everything is drawn against a fixed, genuinely low-resolution VIRTUAL
// canvas - 384x224, the classic CPS2-era arcade fighting-game native
// resolution - into an offscreen Gdiplus::Bitmap of exactly that size
// (App::LowResBuffer), with anti-aliasing off so every shape lands on
// crisp, discrete pixels. That small bitmap is then scaled up onto the
// real window with nearest-neighbor interpolation (never bilinear/
// smoothed), so the whole game reads as genuine chunky pixel art at any
// output size rather than blurry stretched vector art - this is what lets
// the Settings screen's resolution range (320x200 - 1920x1080) scale
// cleanly without a second, resolution-specific layout pass or the art
// going soft.
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>

namespace kakuge {

constexpr int VirtualW = 384;
constexpr int VirtualH = 224;

struct ViewTransform {
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;

    static ViewTransform Compute(int windowW, int windowH) {
        ViewTransform t;
        if (windowW <= 0 || windowH <= 0) return t;
        double sx = static_cast<double>(windowW) / VirtualW;
        double sy = static_cast<double>(windowH) / VirtualH;
        t.scale = std::min(sx, sy);
        t.offsetX = (windowW - VirtualW * t.scale) / 2.0;
        t.offsetY = (windowH - VirtualH * t.scale) / 2.0;
        return t;
    }

    // Screen (window client) pixel -> virtual canvas coordinate, used for
    // mouse hit-testing against button rects defined in virtual units.
    void ScreenToVirtual(int sx, int sy, double& vx, double& vy) const {
        if (scale <= 0.0001) { vx = 0; vy = 0; return; }
        vx = (sx - offsetX) / scale;
        vy = (sy - offsetY) / scale;
    }
};

// A simple clickable rectangle in virtual-canvas units, used by every
// custom-drawn menu screen (Title/Select/VS/Result/Settings/Pause) so we
// don't need real Win32 HWND buttons for plain navigation actions - only
// the data-entry-heavy Character Editor uses native child controls.
struct UiButton {
    Gdiplus::RectF Rect;
    std::string Text;
    bool Primary = true;
    bool Enabled = true;
    std::function<void()> OnClick;

    bool HitTest(double vx, double vy) const {
        return Enabled && vx >= Rect.X && vx <= Rect.X + Rect.Width && vy >= Rect.Y && vy <= Rect.Y + Rect.Height;
    }
};

} // namespace kakuge
