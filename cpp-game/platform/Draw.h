// platform/Draw.h
// Shared GDI+ drawing helpers - the C++/Win32 equivalent of the WinForms
// edition's UI/RenderHelpers.ps1, ported function-for-function onto the
// native GDI+ Flat API. Kept separate from engine/ so combat logic never
// depends on GDI+ (same separation the PowerShell edition used).
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include "Palette.h"
#include "Layout.h"
#include "../engine/Fighter.h"
#include "../engine/BattleSystem.h"

namespace kakuge {

// World-space (game logic, centered on stage) -> virtual canvas pixel space.
constexpr double OriginX = 640.0;
// Ground line's Y in the 1280x720 virtual canvas. Tuned so an idle
// character (~497px tall, see kCharScale in Draw.cpp) leaves ~150px of
// headroom above their head and ~35px of footroom before the HUD gauge
// bars start at y=682 (both within the user's specified 120-160px /
// 30-40px ranges).
constexpr double OriginY = 647.0;

inline double ToScreenX(double worldX) { return OriginX + worldX; }
inline double ToScreenY(double worldY) { return OriginY + worldY; }

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius);

// HARD CANDY "candy" overlay devices - applied on top of the flat,
// zero-radius skeleton (never as a substitute for it). All three are
// deliberately hard-edged: no blur, no rounding.
//   - DrawHardShadow: the panel's own offset shadow, drawn *before* the
//     panel fill so it peeks out from behind it (10px right/down, flat
//     translucent ink, per spec - call with the panel's own rect).
//   - DrawGlossCap: a white-to-transparent gradient over the top ~45% of
//     a solid red block (buttons, badges, accent panels).
//   - DrawDiagonalShine: an ~18-degree translucent white stripe clipped to
//     the rect - reserved for large red blocks only (VS badge, primary
//     title button, title accent mark), not every red surface.
void DrawHardShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& panelRect);
void DrawGlossCap(Gdiplus::Graphics& g, const Gdiplus::RectF& rect);
void DrawDiagonalShine(Gdiplus::Graphics& g, const Gdiplus::RectF& rect);

Gdiplus::Color TagColor(const std::string& tag);
Gdiplus::Color MoveTint(const Fighter& fighter);

// Line-art humanoid fighter (head+headband, torso, two-segment arms/legs) -
// same silhouette design as the WinForms edition's Draw-Humanoid, styled
// after the user's karate reference sketch.
struct HumanoidPose {
    double heightScale = 1.0;
    int facing = 1;
    double armReach = 0.0;
    double legKick = 0.0;
    double leanBack = 0.0;
    double guardRaise = 0.0;
};
void DrawHumanoid(Gdiplus::Graphics& g, double sx, double sy, Gdiplus::Color color, const HumanoidPose& pose = {});

void DrawFighter(Gdiplus::Graphics& g, const Fighter& fighter);
void DrawProjectile(Gdiplus::Graphics& g, const Projectile& proj);

struct EffectStyle { Gdiplus::Color color; double radius; double duration; };
EffectStyle GetEffectStyle(const std::string& kind);

struct LiveEffect { std::string kind; double x, y, age; };
void DrawEffect(Gdiplus::Graphics& g, const LiveEffect& fx);

void DrawBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio,
             Gdiplus::Color fillColor, Gdiplus::Color emptyColor, bool mirror);
void DrawHPBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio, bool mirror);
void DrawGaugeBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio, bool mirror);

// HUD: HP bars, round timer box, gauge bars, combo counter (new - matches
// the reference mockup's "COMBO / N HIT" box).
void DrawHUD(Gdiplus::Graphics& g, const BattleSystem& bs, int p1ComboDisplay, int p2ComboDisplay, double comboFade);

void DrawDebugOverlay(Gdiplus::Graphics& g, const BattleSystem& bs);

// Small text helpers built on StringFormat centering (avoids re-deriving
// MeasureString math for every call site, unlike the PowerShell edition).
void DrawTextCentered(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                       const Gdiplus::RectF& rect, Gdiplus::Color color);
void DrawTextLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                   float x, float y, Gdiplus::Color color);
void DrawTextRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                    float rightX, float y, Gdiplus::Color color);

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// Segoe UI ships with every real Windows 10/11 install, but falls back to
// GDI+'s built-in generic sans-serif family if it's somehow unavailable
// (e.g. a stripped-down environment with no fonts registered at all) -
// this is what let a completely textless title screen under Wine (which
// ships zero fonts by default) get caught and fixed before shipping,
// rather than silently drawing nothing on some real machine too.
Gdiplus::FontFamily* UiFontFamily();
Gdiplus::FontFamily* MonospaceFontFamily();

} // namespace kakuge
