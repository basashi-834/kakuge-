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
constexpr double OriginX = VirtualW / 2.0;
// Ground line's Y in the 384x224 virtual canvas - 85.2% of canvas height,
// matching the user's 1920x1080-proportioned spec (ground at y=920 of
// 1080, see StageConstants::RefGroundY in engine/Constants.h). Tuned so
// an idle character (~156px tall at zoom 1.0, see kCharScale in Draw.cpp)
// leaves ~35px of headroom above their head (below the top HUD row) and
// ~25px of footroom before the HUD gauge bars at the bottom of the
// canvas.
constexpr double OriginY = 191.0;

// Dynamic camera (auto-zoom): follows the midpoint between the two
// fighters and zooms in/out based on the distance between them - closer
// together reads as more zoomed-in (impact/detail), farther apart pulls
// back toward showing the whole stage. Only ever mutated by UpdateCamera/
// ResetCamera (App::OnTimer's fixed-step Game loop, and App::StartMatch),
// and only ever read by ToScreenX/ToScreenY/ScreenScale and DrawFighter/
// DrawProjectile/DrawEffect/DrawDebugOverlay - every one of those is
// exclusively part of the Game screen's own render path, so this global
// state never leaks into Title/Select/VS/Result/Editor (which position
// everything in fixed canvas coordinates, not through these helpers).
struct GameCamera {
    double CenterX = 0.0; // world-space X the canvas center is currently pointed at
    double Zoom = 1.0;    // world-to-canvas scale multiplier (1.0 = no zoom)
};
GameCamera& GetCamera();

// Recomputes the camera's target center/zoom from both fighters' current
// PositionX and smoothly (lerp) moves the live camera toward it - call
// once per fixed simulation step (FixedDt) while Screen::Game is running.
void UpdateCamera(double p1x, double p2x, double dt);
// Snaps the camera directly to the correct framing for the given starting
// positions with no lerp-in - call once when a match starts, so the very
// first frame doesn't inherit a stale zoom/pan left over from a previous
// match's close-range finish.
void ResetCamera(double p1x, double p2x);

inline double ToScreenX(double worldX) { return OriginX + (worldX - GetCamera().CenterX) * GetCamera().Zoom; }
inline double ToScreenY(double worldY) { return OriginY + worldY * GetCamera().Zoom; }
// Scales a world-space length (radius, box width/height, line thickness)
// by the camera's current zoom - use alongside ToScreenX/ToScreenY
// anywhere a size (not just a position) needs to track the zoom level.
inline double ScreenScale(double worldLength) { return worldLength * GetCamera().Zoom; }

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

// side: 0 = the effect was scored by Player1 (shown at the left screen
// edge), 1 = scored by Player2 (right screen edge) - see EffectEvent::side
// in engine/Fighter.h.
struct LiveEffect { std::string kind; double x, y, age; int side = 0; };
void DrawEffect(Gdiplus::Graphics& g, const LiveEffect& fx);

// Counter / Effective Counter info, pinned to the screen edge belonging to
// the player who scored it (1P = left edge, 2P = right edge, per the
// user's spec), vertically centered - rather than floating at the hit
// location, which get too cramped to read on the 384x224 canvas.
void DrawCounterEdgeLabel(Gdiplus::Graphics& g, const LiveEffect& fx);

void DrawBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio,
             Gdiplus::Color fillColor, Gdiplus::Color emptyColor, bool mirror,
             Gdiplus::Color borderColor = GetPalette().Ink);
void DrawHPBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio, bool mirror,
               Gdiplus::Color borderColor = GetPalette().Ink);
void DrawGaugeBar(Gdiplus::Graphics& g, float x, float y, float w, float h, double ratio, bool mirror,
                  Gdiplus::Color borderColor = GetPalette().Ink);

// HUD: HP bars, round timer box, gauge bars, combo counter (new - matches
// the reference mockup's "COMBO / N HIT" box).
void DrawHUD(Gdiplus::Graphics& g, const BattleSystem& bs, int p1ComboDisplay, int p2ComboDisplay, double comboFade);

void DrawDebugOverlay(Gdiplus::Graphics& g, const BattleSystem& bs);

// Small text helpers built on StringFormat centering (avoids re-deriving
// MeasureString math for every call site, unlike the PowerShell edition).
// Used only by the Character Editor (platform/Editor.cpp, App.cpp's Editor
// header bar), which draws at real window-pixel scale with normal-sized
// system fonts and reads fine as-is - NOT used by the 384x224 low-res
// pipeline screens any more (see DrawPixelText* below).
void DrawTextCentered(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                       const Gdiplus::RectF& rect, Gdiplus::Color color);
void DrawTextLeft(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                   float x, float y, Gdiplus::Color color);
void DrawTextRight(Gdiplus::Graphics& g, const std::wstring& text, Gdiplus::Font& font,
                    float rightX, float y, Gdiplus::Color color);

// Hand-authored 5x7 dot-matrix "pixel font", genuinely built from square
// blocks (no anti-aliasing, no hinting) - used for every text draw inside
// the 384x224 low-res pipeline (Draw.cpp's HUD/effects/debug overlay,
// every screen in Screens.cpp), where a normal TrueType/system font
// (UiFontFamily) turned out illegible at the tiny sizes those screens
// need. `dot` is the size in virtual-canvas pixels of one font "pixel" -
// each glyph is 5 dots wide x 7 dots tall, advancing by 6 dots (5 + 1 gap)
// per character. Input is case-folded to uppercase internally (the font
// has no lowercase forms, matching the rest of the UI's all-caps style);
// a character with no glyph (e.g. an unsupported symbol, or non-Latin
// text like a user-renamed character's Japanese name) is skipped - drawn
// blank rather than crashing or drawing tofu - but still advances the
// cursor so surrounding text stays aligned.
float PixelTextWidth(const std::wstring& text, float dot);
void DrawPixelText(Gdiplus::Graphics& g, const std::wstring& text, float x, float y, float dot, Gdiplus::Color color);
void DrawPixelTextCentered(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::RectF& rect, float dot, Gdiplus::Color color);
void DrawPixelTextRight(Gdiplus::Graphics& g, const std::wstring& text, float rightX, float y, float dot, Gdiplus::Color color);

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
