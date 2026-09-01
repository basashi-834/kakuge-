// platform/Layout.h
// Everything is drawn against a fixed VIRTUAL canvas (1280x720, the same
// baseline the earlier WinForms edition used) and then uniformly scaled
// (letterboxed/pillarboxed, never stretched) onto however big the actual
// window client area is. This is what makes the new Settings screen's
// resolution range (320x200 - 1920x1080, 4:3 or 16:9) work cleanly: every
// screen, the HUD, and the humanoid fighter renderer are all written once
// in virtual-canvas units and automatically scale to whatever resolution
// the user picks - satisfying the request that characters "scale to the
// chosen resolution" without a second, resolution-specific layout pass.
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>

namespace kakuge {

constexpr int VirtualW = 1280;
constexpr int VirtualH = 720;

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

    void ApplyTo(Gdiplus::Graphics& g) const {
        Gdiplus::Matrix m(static_cast<Gdiplus::REAL>(scale), 0, 0, static_cast<Gdiplus::REAL>(scale),
                           static_cast<Gdiplus::REAL>(offsetX), static_cast<Gdiplus::REAL>(offsetY));
        g.SetTransform(&m);
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
