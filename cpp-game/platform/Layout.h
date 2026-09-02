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

// Two symmetric, horizontally-centered "placement frames" for showing two
// characters/UI elements side by side (e.g. the VS screen's two fighter
// portraits) - a fixed gap between them and a fixed margin from the
// canvas's bottom edge, both boxes vertically aligned to that same bottom
// margin. Anchored to this game's own 384x224 canvas (80x95 boxes, 80px
// gap, 8px bottom margin - reproduces that pattern exactly when called
// with VirtualW/VirtualH) and scaled proportionally for any other canvas
// size, so the same layout call stays correct if the virtual canvas ever
// changes size - not a naive uniform scale of some other resolution's
// numbers, which wouldn't necessarily hit round pixel values here.
struct TwoBoxLayout {
    Gdiplus::RectF Left;
    Gdiplus::RectF Right;
};

inline TwoBoxLayout ComputeTwoBoxLayout(float canvasW, float canvasH) {
    constexpr float kRefW = 384.0f, kRefH = 224.0f;
    constexpr float kBoxW = 80.0f, kBoxH = 95.0f, kGap = 80.0f, kBottomMargin = 8.0f;
    float sx = canvasW / kRefW, sy = canvasH / kRefH;
    float boxW = kBoxW * sx, boxH = kBoxH * sy, gap = kGap * sx, bottom = kBottomMargin * sy;
    float totalW = boxW * 2.0f + gap;
    float leftX = (canvasW - totalW) / 2.0f;
    float rightX = leftX + boxW + gap;
    float y = canvasH - bottom - boxH;
    return {Gdiplus::RectF(leftX, y, boxW, boxH), Gdiplus::RectF(rightX, y, boxW, boxH)};
}

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
