// platform/Draw.cpp
#include "Draw.h"
#include "Sprites.h"
#include "HudSkin.h"
#include <algorithm>
#include <cmath>

using namespace Gdiplus;

namespace kakuge {

// Sized so an idle character stands exactly 88px tall (108*kCharScale ==
// 88, ~39.3% of the 224px virtual canvas height, see OriginY in Draw.h for
// the other half of that math) - matches the user's later, precise
// 384x224-native size/layout spec (CHARACTER_VISUAL_HEIGHT ~= 88px, ratio
// screen-height*0.39~0.40), which superseded two earlier iterations: the
// original 1920x1080-proportioned spec (kCharScale 1.43, ~156px/70%) and
// a rougher "about half that" request in between (kCharScale 0.715,
// ~79px/35%). Shared by DrawHumanoid and the lying-down Knockdown/Dead
// poses in DrawFighter so both scale together. Every character is drawn
// with this same vector line-art path - the earlier round's photo-sprite
// renders (data/images/fighter_*.png) were removed per the user's
// explicit request to express everything as pixel art, and the low-res-
// buffer + nearest-neighbor pipeline (see App::OnPaint) is what turns
// this line art into genuine chunky pixels rather than smooth vector
// strokes.
constexpr double kCharScale = static_cast<double>(GameSpec::CharacterVisualHeight) / 108.0;
// A separate horizontal-only scale was tried here to also hit the user's
// spec'd CHARACTER_VISUAL_WIDTH (~55px, ~14-15% of the 384px canvas)
// alongside height - solving 24*kCharWidthScale == 55 against the shared
// coefficient behind the foot-stance spread and torso top width. Visually
// verified via Wine screenshot and reverted: it stretched the torso into
// a flat, box-like rectangle (as wide as the full leg stance) rather than
// a body, because this stick figure's limbs are thin single-width strokes
// with no independent "bulk" to add width to - width and height were
// always the same single scale for a reason. Getting a genuinely ~55px-
// wide silhouette without that distortion needs an actual shape/stroke-
// weight redesign (thicker limbs, a real torso mass), not a coordinate
// stretch; flagged in DEVLOG.md rather than attempted blind a second time.

// ---- Dynamic camera (auto-zoom) ----
// Extra world-space width kept visible around the two fighters (beyond
// their raw separation) so they're never crammed edge-to-edge - chosen so
// the fighters' StageConstants::PlayerStartDistance (152, matching the
// user's later 384x224-native spec's center-to-center distance of ~150px,
// see Player1StartX/Player2StartX below) reads as *exactly* baseline zoom
// (384/(152+232) = 1.0): the round's opening neutral position is the
// natural "standard magnification" reference point that spec describes
// (screen positions 116/268), not an already-zoomed-in one.
constexpr double kCameraPaddingWorld = 232.0;
// At StageMaxX-StageMinX separation (640 units, both fighters pinned to
// opposite walls - StageConstants::StageWidth) the natural zoom-to-fit
// would be 384/(640+264) ~= 0.42, but the floor is set higher, at the
// zoom where 384/zoom still covers that full 640 (plus a pushbox-width-ish
// margin on each side): 384/0.49 ~= 784, comfortably >= 640 - so a
// worst-case corner-to-corner separation still keeps both fighters (and
// the stage edges) fully on screen, per the "両キャラクターをできるだけ常に
// 画面内に表示する" / "ステージ外を映さない" requirements, without
// zooming out further than that floor needs.
constexpr double kCameraMinZoom = 0.49;
// At minimum meaningful distance (pushbox contact) the natural horizontal
// zoom-to-fit would run well past 1.4, but the real ceiling is vertical,
// not horizontal: an idle character is 88px tall at zoom 1 (see
// kCharScale's comment) and scales linearly with zoom (~92px at 1.05),
// while the canvas is only 224px tall with the HUD's HP bar/name tag/
// round timer occupying its own ~30-37px at the top. 1.05 keeps an idle
// character comfortably clear of the top HUD even at the closest range -
// this ceiling has been conservative (more headroom than strictly
// necessary) since the character was shrunk from its original ~156px
// height across two later user requests, but it was never broken by
// either shrink, so it's left as-is - while still giving the "distance is
// close -> zoom in a little" effect the spec asks for something real to
// show.
constexpr double kCameraMaxZoom = 1.05;
// Exponential lerp rate (per second) for both center and zoom - high
// enough that the camera visibly keeps pace with a dash or a knockback,
// low enough that it never reads as a hard cut (the user's "急激に倍率を
// 変更せず" / "カメラが急に動かない" requirements).
constexpr double kCameraLerpSpeed = 6.0;

namespace {
GameCamera g_Camera;

double CameraTargetZoom(double distance) {
    double desiredWidth = distance + kCameraPaddingWorld;
    double zoom = (desiredWidth > 1.0) ? (VirtualW / desiredWidth) : kCameraMaxZoom;
    return std::clamp(zoom, kCameraMinZoom, kCameraMaxZoom);
}

// Keeps the camera's visible window inside the stage: the raw midpoint of
// the two fighters stays within [StageMinX, StageMaxX] on its own, but the
// visible WINDOW around that midpoint (width = VirtualW/zoom) doesn't -
// pin against a fighter cornered near a wall and it still centers with a
// wide gap of empty stage past them, instead of the tight, wall-close
// framing a real corner shot should have (this is what a fighting-game
// camera cornering someone actually looks like - the camera stops
// following the midpoint and pins to the wall instead). Falls back to
// centering on the stage when the view is wider than the stage itself
// (only possible right at kCameraMinZoom, which is sized to guarantee
// that case still shows both fighters - see kCameraMinZoom's comment).
double ClampCameraCenter(double centerX, double zoom) {
    double halfVisible = VirtualW / (2.0 * zoom);
    double minCenter = StageConstants::StageMinX + halfVisible;
    double maxCenter = StageConstants::StageMaxX - halfVisible;
    if (minCenter > maxCenter) return (StageConstants::StageMinX + StageConstants::StageMaxX) / 2.0;
    return std::clamp(centerX, minCenter, maxCenter);
}
} // namespace

GameCamera& GetCamera() { return g_Camera; }

void UpdateCamera(double p1x, double p2x, double dt) {
    double targetZoom = CameraTargetZoom(std::abs(p1x - p2x));
    double targetCenter = ClampCameraCenter((p1x + p2x) / 2.0, targetZoom);
    double t = std::clamp(kCameraLerpSpeed * dt, 0.0, 1.0);
    g_Camera.CenterX += (targetCenter - g_Camera.CenterX) * t;
    g_Camera.Zoom += (targetZoom - g_Camera.Zoom) * t;
}

void ResetCamera(double p1x, double p2x) {
    g_Camera.Zoom = CameraTargetZoom(std::abs(p1x - p2x));
    g_Camera.CenterX = ClampCameraCenter((p1x + p2x) / 2.0, g_Camera.Zoom);
}

namespace {

// 5x7 dot-matrix glyphs, one 5-char row per string ('#' = lit, '.' = off).
// Covers uppercase A-Z, 0-9, space, and the punctuation actually used
// across the low-res screens (see DrawPixelText's comment in Draw.h).
struct PixelGlyphEntry { wchar_t ch; const char* rows[7]; };

const PixelGlyphEntry kGlyphTable[] = {
    {L' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {L'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {L'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {L'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {L'3', {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."}},
    {L'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {L'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {L'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {L'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {L'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {L'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {L'A', {"..#..", ".#.#.", "#...#", "#...#", "#####", "#...#", "#...#"}},
    {L'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {L'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {L'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {L'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {L'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {L'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
    {L'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {L'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {L'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {L'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {L'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {L'M', {"#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"}},
    {L'N', {"#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"}},
    {L'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {L'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {L'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {L'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {L'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {L'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {L'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {L'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {L'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {L'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {L'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {L'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {L'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {L'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {L':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {L'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {L'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {L')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {L',', {".....", ".....", ".....", ".....", ".....", ".##..", ".#..."}},
    {L'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {L'>', {"#....", ".#...", "..#..", "...#.", "..#..", ".#...", "#...."}},
    {L'/', {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}},
    {L'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {L'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
};

const char* const* FindGlyph(wchar_t c) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - 32);
    for (const auto& entry : kGlyphTable) {
        if (entry.ch == c) return entry.rows;
    }
    return nullptr;
}

constexpr int kGlyphCols = 5, kGlyphRows = 7, kGlyphAdvanceCols = 6;

} // namespace

float PixelTextWidth(const std::wstring& text, float dot) {
    if (text.empty()) return 0.0f;
    return static_cast<float>(text.size()) * kGlyphAdvanceCols * dot - dot;
}

void DrawPixelText(Graphics& g, const std::wstring& text, float x, float y, float dot, Color color) {
    if (dot <= 0.0f) return;
    SolidBrush brush(color);
    float cursorX = x;
    for (wchar_t c : text) {
        const char* const* rows = FindGlyph(c);
        if (rows) {
            for (int r = 0; r < kGlyphRows; r++) {
                for (int col = 0; col < kGlyphCols; col++) {
                    if (rows[r][col] == '#') {
                        g.FillRectangle(&brush, cursorX + col * dot, y + r * dot, dot, dot);
                    }
                }
            }
        }
        cursorX += kGlyphAdvanceCols * dot;
    }
}

void DrawPixelTextCentered(Graphics& g, const std::wstring& text, const RectF& rect, float dot, Color color) {
    float w = PixelTextWidth(text, dot);
    float h = kGlyphRows * dot;
    float x = rect.X + (rect.Width - w) / 2.0f;
    float y = rect.Y + (rect.Height - h) / 2.0f;
    DrawPixelText(g, text, x, y, dot, color);
}

void DrawPixelTextRight(Graphics& g, const std::wstring& text, float rightX, float y, float dot, Color color) {
    float w = PixelTextWidth(text, dot);
    DrawPixelText(g, text, rightX - w, y, dot, color);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], len, nullptr, nullptr);
    return out;
}

FontFamily* UiFontFamily() {
    static FontFamily* cached = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto* candidate = new FontFamily(L"Segoe UI");
        if (candidate->GetLastStatus() == Ok && candidate->IsAvailable()) {
            cached = candidate;
        } else {
            delete candidate;
        }
    }
    if (cached) return cached;
    return const_cast<FontFamily*>(FontFamily::GenericSansSerif());
}

FontFamily* MonospaceFontFamily() {
    static FontFamily* cached = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        auto* candidate = new FontFamily(L"Consolas");
        if (candidate->GetLastStatus() == Ok && candidate->IsAvailable()) {
            cached = candidate;
        } else {
            delete candidate;
        }
    }
    if (cached) return cached;
    return const_cast<FontFamily*>(FontFamily::GenericMonospace());
}

void DrawTextCentered(Graphics& g, const std::wstring& text, Font& font, const RectF& rect, Color color) {
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, rect, &fmt, &brush);
}

void DrawTextLeft(Graphics& g, const std::wstring& text, Font& font, float x, float y, Color color) {
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, PointF(x, y), &brush);
}

void DrawTextRight(Graphics& g, const std::wstring& text, Font& font, float rightX, float y, Color color) {
    RectF bbox;
    g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &bbox);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, PointF(rightX - bbox.Width, y), &brush);
}

void AddRoundedRect(GraphicsPath& path, const RectF& rect, float radius) {
    float d = radius * 2;
    if (d > rect.Height) d = rect.Height;
    if (d > rect.Width) d = rect.Width;
    if (d <= 1.0f) {
        path.AddRectangle(rect);
        return;
    }
    path.AddArc(rect.X, rect.Y, d, d, 180, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void DrawHardShadow(Graphics& g, const RectF& panelRect) {
    // "10px 10px 0 rgba(32,30,29,.25)" - a flat offset rectangle behind the
    // panel, never blurred. Caller draws this first, then the panel fill on
    // top, so the shadow only peeks out on the right/bottom edges.
    const auto& pal = GetPalette();
    SolidBrush shadowBrush(pal.RuleSoft);
    RectF shadow(panelRect.X + 10.0f, panelRect.Y + 10.0f, panelRect.Width, panelRect.Height);
    g.FillRectangle(&shadowBrush, shadow);
}

void DrawGlossCap(Graphics& g, const RectF& rect) {
    // White-to-transparent gradient over the top ~45% of a solid red block.
    float capH = rect.Height * 0.45f;
    if (capH < 1.0f) return;
    RectF capRect(rect.X, rect.Y, rect.Width, capH);
    LinearGradientBrush brush(capRect, Color(115, 255, 255, 255), Color(0, 255, 255, 255),
                               LinearGradientModeVertical);
    g.FillRectangle(&brush, capRect);
}

void DrawDiagonalShine(Graphics& g, const RectF& rect) {
    // ~18-degree translucent white stripe, clipped to rect. Reserved for
    // large red blocks only (VS badge, primary title button, title mark).
    Region oldClip;
    g.GetClip(&oldClip);
    g.SetClip(rect);

    GraphicsState state = g.Save();
    float cx = rect.X + rect.Width * 0.5f;
    float cy = rect.Y + rect.Height * 0.5f;
    g.TranslateTransform(cx, cy);
    g.RotateTransform(18.0f);
    g.TranslateTransform(-cx, -cy);

    float stripeW = rect.Width * 0.22f;
    float stripeX = rect.X + rect.Width * 0.58f;
    float pad = rect.Width + rect.Height;
    SolidBrush stripeBrush(Color(46, 255, 255, 255));
    g.FillRectangle(&stripeBrush, RectF(stripeX, cy - pad, stripeW, pad * 2.0f));

    g.Restore(state);
    g.SetClip(&oldClip);
}

Color TagColor(const std::string& tag) {
    if (tag == "Light") return Color(255, 224, 176, 40);
    if (tag == "Medium") return Color(255, 224, 120, 30);
    if (tag == "Heavy") return Color(255, 210, 45, 38);
    if (tag == "Special") return Color(255, 140, 60, 220);
    if (tag == "Super") return Color(255, 230, 30, 120);
    return Color(255, 255, 255, 255);
}

Color MoveTint(const Fighter& fighter) {
    if (fighter.CurrentMoveData != nullptr) {
        for (const char* t : {"Super", "Special", "Heavy", "Medium", "Light"}) {
            if (fighter.CurrentMoveData->HasTag(t)) return TagColor(t);
        }
    }
    return Color(255, static_cast<BYTE>(fighter.Stats.ColorR), static_cast<BYTE>(fighter.Stats.ColorG), static_cast<BYTE>(fighter.Stats.ColorB));
}

// ---------------------------------------------------------------------
// Humanoid fighter - filled-shape figure (head, torso, capsule limbs).
// ---------------------------------------------------------------------
// Replaced the earlier thin line-art stick figure per the user's request
// to drop the "pixel line art" constraint: the body is now solid shapes
// (filled polygons/capsules with a dark outline), so it reads as a real
// silhouette at the spec's 55x88 size instead of a wireframe. Geometry is
// in "figure units" where the standing figure is 108 units tall (head 24
// + neck 2 + torso 38 + legs 46) and s = heightScale*kCharScale converts
// units to canvas pixels - unchanged from the line-art version so every
// existing call site (Select/VS/Result portraits, the Editor preview,
// DrawFighter) keeps its sizing. Standing width is ~66 units (~54px at
// kCharScale), matching GameSpec::CharacterVisualWidth. Drawn with anti-
// aliasing on regardless of the target Graphics' mode (the user relaxed
// the no-AA rule for the character), restored afterwards.
namespace {
struct FigureStyle {
    Color body, outline, skin, belt, band, dark;
};

FigureStyle MakeFigureStyle(Color body) {
    auto shade = [](Color c, double f) {
        return Color(c.GetA(), static_cast<BYTE>(std::clamp(static_cast<int>(c.GetR() * f), 0, 255)),
                     static_cast<BYTE>(std::clamp(static_cast<int>(c.GetG() * f), 0, 255)),
                     static_cast<BYTE>(std::clamp(static_cast<int>(c.GetB() * f), 0, 255)));
    };
    FigureStyle st;
    st.body = body;
    st.outline = shade(body, 0.45);
    st.skin = Color(body.GetA(), 236, 194, 156);
    st.belt = Color(body.GetA(), 34, 30, 32);
    st.band = Color(body.GetA(), 232, 44, 38);
    st.dark = shade(body, 0.7);
    return st;
}

// A limb segment as a rounded "capsule": a thick round-capped line, drawn
// twice (outline color slightly wider underneath, body color on top).
void DrawCapsule(Graphics& g, const FigureStyle& st, Color fill, double x1, double y1, double x2, double y2, double thickness, double outlineW) {
    Pen outer(st.outline, static_cast<REAL>(thickness + outlineW * 2));
    outer.SetStartCap(LineCapRound); outer.SetEndCap(LineCapRound);
    g.DrawLine(&outer, static_cast<REAL>(x1), static_cast<REAL>(y1), static_cast<REAL>(x2), static_cast<REAL>(y2));
    Pen inner(fill, static_cast<REAL>(thickness));
    inner.SetStartCap(LineCapRound); inner.SetEndCap(LineCapRound);
    g.DrawLine(&inner, static_cast<REAL>(x1), static_cast<REAL>(y1), static_cast<REAL>(x2), static_cast<REAL>(y2));
}

void DrawDisc(Graphics& g, const FigureStyle& st, Color fill, double cx, double cy, double r, double outlineW) {
    SolidBrush ob(st.outline);
    g.FillEllipse(&ob, static_cast<REAL>(cx - r - outlineW), static_cast<REAL>(cy - r - outlineW), static_cast<REAL>((r + outlineW) * 2), static_cast<REAL>((r + outlineW) * 2));
    SolidBrush fb(fill);
    g.FillEllipse(&fb, static_cast<REAL>(cx - r), static_cast<REAL>(cy - r), static_cast<REAL>(r * 2), static_cast<REAL>(r * 2));
}
} // namespace

void DrawHumanoid(Graphics& g, double sx, double sy, Color color, const HumanoidPose& pose) {
    SmoothingMode prevMode = g.GetSmoothingMode();
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    double s = pose.heightScale * kCharScale;
    int f = pose.facing < 0 ? -1 : 1;
    FigureStyle st = MakeFigureStyle(color);
    double ow = std::max(0.6, 1.4 * s);   // outline width (px)
    double limbT = 13.0 * s;               // leg thickness
    double armT = 11.0 * s;
    double headR = 12.0 * s;

    // Vertical stack (units): legs 46, torso 38, neck 2, head 24.
    double legH = 46.0 * s, torsoH = 38.0 * s;
    if (pose.crouch) { legH = 24.0 * s; torsoH = 26.0 * s; }
    double bob = (pose.idleFrame == 1 || pose.idleFrame == 2) ? 1.0 * s : 0.0;
    double hipY = sy - legH + bob;
    double shoulderY = hipY - torsoH;
    double headCY = shoulderY - 2.0 * s - headR;
    double cx = sx + (pose.leanBack * 0.35);

    // ---- legs ----
    double hipBackX = cx - f * 8.0 * s, hipFrontX = cx + f * 8.0 * s;
    double kneeBX, kneeBY, footBX, footBY, kneeFX, kneeFY, footFX, footFY;
    if (pose.crouch) {
        kneeBX = cx - f * 24.0 * s; kneeBY = hipY + 6.0 * s; footBX = cx - f * 20.0 * s; footBY = sy;
        kneeFX = cx + f * 24.0 * s; kneeFY = hipY + 6.0 * s; footFX = cx + f * 22.0 * s; footFY = sy;
    } else if (pose.jump) {
        kneeBX = cx - f * 14.0 * s; kneeBY = hipY + 16.0 * s; footBX = cx - f * 6.0 * s; footBY = hipY + 30.0 * s;
        kneeFX = cx + f * 18.0 * s; kneeFY = hipY + 14.0 * s; footFX = cx + f * 12.0 * s; footFY = hipY + 30.0 * s;
    } else {
        kneeBX = cx - f * 13.0 * s; kneeBY = hipY + legH * 0.52; footBX = cx - f * 20.0 * s; footBY = sy;
        kneeFX = cx + f * 12.0 * s; kneeFY = hipY + legH * 0.52; footFX = cx + f * 20.0 * s; footFY = sy;
        if (pose.legKick > 0) {
            kneeFX = cx + f * 22.0 * s; kneeFY = hipY + 10.0 * s;
            footFX = cx + f * (30.0 + pose.legKick * 0.9) * s;
            footFY = hipY + 6.0 * s - std::min(pose.legKick, 34.0) * 0.55 * s;
        }
    }
    // Back leg first (drawn under everything).
    DrawCapsule(g, st, st.dark, hipBackX, hipY, kneeBX, kneeBY, limbT, ow);
    DrawCapsule(g, st, st.dark, kneeBX, kneeBY, footBX, footBY, limbT, ow);
    DrawDisc(g, st, st.outline, footBX - f * 2.0 * s, footBY - 3.0 * s, 4.5 * s, 0.0);

    // ---- back arm (guard, low) ----
    double shBackX = cx - f * 18.0 * s, shBackY = shoulderY + 4.0 * s;
    double elBackX, elBackY, hdBackX, hdBackY;
    if (pose.guardRaise > 0) { elBackX = cx - f * 6.0 * s; elBackY = shoulderY + 14.0 * s; hdBackX = cx + f * 14.0 * s; hdBackY = shoulderY - 2.0 * s; }
    else { elBackX = cx - f * 26.0 * s; elBackY = shoulderY + 18.0 * s; hdBackX = cx - f * 12.0 * s; hdBackY = shoulderY + 24.0 * s - bob; }
    DrawCapsule(g, st, st.dark, shBackX, shBackY, elBackX, elBackY, armT, ow);
    DrawCapsule(g, st, st.dark, elBackX, elBackY, hdBackX, hdBackY, armT, ow);
    DrawDisc(g, st, st.skin, hdBackX, hdBackY, 5.5 * s, ow);

    // ---- torso ----
    {
        PointF pts[4] = {
            PointF(static_cast<REAL>(cx - 22.0 * s), static_cast<REAL>(shoulderY)),
            PointF(static_cast<REAL>(cx + 22.0 * s), static_cast<REAL>(shoulderY)),
            PointF(static_cast<REAL>(cx + 15.0 * s), static_cast<REAL>(hipY + 4.0 * s)),
            PointF(static_cast<REAL>(cx - 15.0 * s), static_cast<REAL>(hipY + 4.0 * s)),
        };
        SolidBrush fill(st.body);
        g.FillPolygon(&fill, pts, 4);
        Pen outline(st.outline, static_cast<REAL>(ow * 1.4));
        outline.SetLineJoin(LineJoinRound);
        g.DrawPolygon(&outline, pts, 4);
        // Belt.
        SolidBrush belt(st.belt);
        g.FillRectangle(&belt, static_cast<REAL>(cx - 15.0 * s), static_cast<REAL>(hipY - 5.0 * s), static_cast<REAL>(30.0 * s), static_cast<REAL>(5.0 * s));
        // Gi lapel line (a single dark diagonal) so the torso has a front.
        Pen lapel(st.outline, static_cast<REAL>(ow));
        g.DrawLine(&lapel, static_cast<REAL>(cx + f * 12.0 * s), static_cast<REAL>(shoulderY + 2.0 * s), static_cast<REAL>(cx - f * 2.0 * s), static_cast<REAL>(hipY - 6.0 * s));
    }

    // ---- front leg (over torso bottom) ----
    DrawCapsule(g, st, st.body, hipFrontX, hipY, kneeFX, kneeFY, limbT, ow);
    DrawCapsule(g, st, st.body, kneeFX, kneeFY, footFX, footFY, limbT, ow);
    DrawDisc(g, st, st.outline, footFX + f * 2.0 * s, footFY - 3.0 * s, 4.5 * s, 0.0);

    // ---- head ----
    double headCX = cx + f * 2.0 * s;
    DrawDisc(g, st, st.skin, headCX, headCY, headR, ow);
    {
        // Hair: darker cap over the top of the head.
        SolidBrush hair(st.belt);
        g.FillPie(&hair, static_cast<REAL>(headCX - headR), static_cast<REAL>(headCY - headR), static_cast<REAL>(headR * 2), static_cast<REAL>(headR * 2), 180.0f, 180.0f);
        // Headband + tails trailing behind.
        SolidBrush band(st.band);
        g.FillRectangle(&band, static_cast<REAL>(headCX - headR), static_cast<REAL>(headCY - 5.0 * s), static_cast<REAL>(headR * 2), static_cast<REAL>(5.0 * s));
        Pen tail(st.band, static_cast<REAL>(3.0 * s));
        double tx = headCX - f * headR * 0.9, ty = headCY - 3.0 * s;
        g.DrawLine(&tail, static_cast<REAL>(tx), static_cast<REAL>(ty), static_cast<REAL>(tx - f * 9.0 * s), static_cast<REAL>(ty - 6.0 * s));
        g.DrawLine(&tail, static_cast<REAL>(tx), static_cast<REAL>(ty), static_cast<REAL>(tx - f * 7.0 * s), static_cast<REAL>(ty + 4.0 * s));
        // Eye.
        SolidBrush eye(st.belt);
        g.FillRectangle(&eye, static_cast<REAL>(headCX + f * 4.0 * s - 1.2 * s), static_cast<REAL>(headCY + 1.0 * s), static_cast<REAL>(2.4 * s), static_cast<REAL>(2.4 * s));
    }

    // ---- front arm (fist forward / punch / guard) ----
    double shFrontX = cx + f * 18.0 * s, shFrontY = shoulderY + 4.0 * s;
    double elX, elY, hdX, hdY;
    if (pose.armReach > 0) {
        elX = cx + f * 34.0 * s; elY = shoulderY + 8.0 * s;
        hdX = cx + f * (46.0 + pose.armReach * 0.8) * s; hdY = shoulderY + 4.0 * s - std::min(pose.armReach, 20.0) * 0.2 * s;
    } else if (pose.guardRaise > 0) {
        elX = cx + f * 26.0 * s; elY = shoulderY + 12.0 * s;
        hdX = cx + f * 22.0 * s; hdY = shoulderY - 8.0 * s;
    } else {
        elX = cx + f * 30.0 * s; elY = shoulderY + 16.0 * s;
        hdX = cx + f * 27.0 * s; hdY = shoulderY + 2.0 * s + bob;
    }
    DrawCapsule(g, st, st.body, shFrontX, shFrontY, elX, elY, armT, ow);
    DrawCapsule(g, st, st.body, elX, elY, hdX, hdY, armT, ow);
    DrawDisc(g, st, st.skin, hdX, hdY, 6.0 * s, ow);

    g.SetSmoothingMode(prevMode);
}

namespace {
// Draws a sprite bottom-center anchored at (sx,sy) (matching where the
// procedural humanoid's feet land), scaled so its height matches heightPx,
// mirrored horizontally when facing < 0 (art is assumed drawn facing
// right, same convention as the line-art renderer's `facing` parameter).
void DrawSpriteFacing(Graphics& g, Gdiplus::Image* img, double sx, double sy, double heightPx, int facing) {
    double scale = heightPx / img->GetHeight();
    double widthPx = img->GetWidth() * scale;
    double left = sx - widthPx / 2.0, top = sy - heightPx, right = left + widthPx, bottom = sy;
    PointF destPoints[3];
    if (facing >= 0) {
        destPoints[0] = PointF(static_cast<REAL>(left), static_cast<REAL>(top));
        destPoints[1] = PointF(static_cast<REAL>(right), static_cast<REAL>(top));
        destPoints[2] = PointF(static_cast<REAL>(left), static_cast<REAL>(bottom));
    } else {
        destPoints[0] = PointF(static_cast<REAL>(right), static_cast<REAL>(top));
        destPoints[1] = PointF(static_cast<REAL>(left), static_cast<REAL>(top));
        destPoints[2] = PointF(static_cast<REAL>(right), static_cast<REAL>(bottom));
    }
    g.DrawImage(img, destPoints, 3);
}

// Picks the sprite (if any) matching the fighter's current pose - see
// Sprites.h's file-naming comment for the full state->file mapping.
// Returns nullptr (meaning: fall back to the line-art renderer) for any
// state outside the requested pose set (Block/Throw/WakeUp/Dead keep no
// sprite slot at all) or when the matching file wasn't found on disk.
Gdiplus::Image* PickSprite(const Fighter& fighter, const CharacterSpriteSet& sprites) {
    bool airborne = fighter.PositionY < (Fighter::GroundY - 1.0);
    switch (fighter.SM.CurrentState) {
        case CharState::Crouch: return sprites.Crouch.get();
        case CharState::Hitstun: return sprites.Hitstun.get();
        case CharState::Knockdown: return sprites.Knockdown.get();
        case CharState::Jump: return sprites.Jump.get();
        case CharState::Attack: {
            std::string btn = fighter.CurrentMoveData ? fighter.CurrentMoveData->Button : std::string();
            bool isKick = !btn.empty() && btn.back() == 'K';
            if (airborne) return isKick ? sprites.JumpKick.get() : sprites.JumpPunch.get();
            return isKick ? sprites.Kick.get() : sprites.Punch.get();
        }
        case CharState::Idle:
        case CharState::WalkForward:
        case CharState::WalkBackward: {
            int frame = (fighter.FrameCounter / kIdleTicksPerFrame) % 4;
            return sprites.Stand[frame].get();
        }
        default:
            return nullptr;
    }
}
} // namespace

void DrawFighter(Graphics& g, const Fighter& fighter, const fs::path& baseDataDir, const fs::path& userDir) {
    Color bodyColor(255, static_cast<BYTE>(fighter.Stats.ColorR), static_cast<BYTE>(fighter.Stats.ColorG), static_cast<BYTE>(fighter.Stats.ColorB));
    // Snapped to whole canvas pixels (the spec asks for integer draw
    // coordinates; movement math itself stays fractional) - and snapped the
    // same way the collision rects are (HurtboxSet::PlaceParts rounds the
    // origin first), so the debug boxes and the drawn body never drift a
    // sub-pixel apart from each other.
    double sx = std::round(ToScreenX(fighter.PositionX));
    double sy = std::round(ToScreenY(fighter.PositionY));
    int facing = fighter.Facing;
    // The dynamic camera (see GameCamera in Draw.h) moves fighters via
    // ToScreenX/Y above, but DrawHumanoid itself is zoom-agnostic (it's
    // reused for Character Select/VS portraits, which must never be
    // affected by the in-match camera) - so every heightScale passed to it
    // here, and every raw kCharScale-based knockdown/dead measurement
    // below, is scaled by the live zoom explicitly at this call site
    // instead.
    double zoom = GetCamera().Zoom;
    double cs = kCharScale * zoom;

    const CharacterSpriteSet& sprites = GetCharacterSprites(fighter.Stats.Id, baseDataDir, userDir);
    if (Gdiplus::Image* sprite = PickSprite(fighter, sprites)) {
        // Same on-screen height the line-art renderer's heightScale=zoom
        // (idle/standard) pose resolves to, so sprite and line-art poses
        // read as the same size regardless of which one a given state
        // currently has art for.
        double heightPx = 108.0 * kCharScale * zoom + 2.0;
        DrawSpriteFacing(g, sprite, sx, sy, heightPx, facing);
        return;
    }

    bool airborne = fighter.PositionY < (Fighter::GroundY - 1.0);
    // Lying-down figure shared by Knockdown/Dead: a solid body capsule with
    // the head at the far end (facing side), drawn with the same style
    // helpers as the standing figure so it reads as the same character.
    auto drawLying = [&](Color body) {
        SmoothingMode prev = g.GetSmoothingMode();
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        FigureStyle st = MakeFigureStyle(body);
        double ow = std::max(0.6, 1.4 * cs);
        DrawCapsule(g, st, st.dark, sx - facing * 40 * cs, sy - 7 * cs, sx - facing * 6 * cs, sy - 8 * cs, 13.0 * cs, ow); // legs
        DrawCapsule(g, st, st.body, sx - facing * 8 * cs, sy - 9 * cs, sx + facing * 22 * cs, sy - 10 * cs, 17.0 * cs, ow);  // torso
        DrawCapsule(g, st, st.body, sx + facing * 4 * cs, sy - 12 * cs, sx + facing * 24 * cs, sy - 22 * cs, 10.0 * cs, ow); // raised arm
        DrawDisc(g, st, st.skin, sx + facing * 36 * cs, sy - 11 * cs, 11.0 * cs, ow);                                        // head
        SolidBrush band(st.band);
        g.FillRectangle(&band, static_cast<REAL>(sx + facing * 36 * cs - 11 * cs), static_cast<REAL>(sy - 15 * cs), static_cast<REAL>(22 * cs), static_cast<REAL>(4 * cs));
        g.SetSmoothingMode(prev);
    };

    HumanoidPose pose;
    pose.heightScale = zoom;
    pose.facing = facing;
    switch (fighter.SM.CurrentState) {
        case CharState::Knockdown:
            drawLying(Color(255, static_cast<BYTE>(std::max(0, bodyColor.GetR() - 40)), static_cast<BYTE>(std::max(0, bodyColor.GetG() - 40)), static_cast<BYTE>(std::max(0, bodyColor.GetB() - 40))));
            break;
        case CharState::Dead:
            drawLying(Color(255, 205, 202, 200));
            break;
        case CharState::WakeUp:
        case CharState::Crouch:
            pose.crouch = true;
            DrawHumanoid(g, sx, sy, bodyColor, pose);
            break;
        case CharState::Block:
            pose.guardRaise = 10;
            pose.crouch = fighter.IsCrouchingGuard;
            DrawHumanoid(g, sx, sy, Color(255, 60, 120, 210), pose);
            break;
        case CharState::Hitstun:
            pose.leanBack = -8.0 * facing * zoom;
            pose.jump = airborne;
            DrawHumanoid(g, sx, sy, Color(255, 220, 60, 60), pose);
            break;
        case CharState::Throw:
            pose.leanBack = -6.0 * facing * zoom;
            DrawHumanoid(g, sx, sy, Color(255, 200, 50, 50), pose);
            break;
        case CharState::Attack: {
            std::string btn = fighter.CurrentMoveData ? fighter.CurrentMoveData->Button : std::string();
            bool isKick = !btn.empty() && btn.back() == 'K';
            bool crouchMove = fighter.CurrentMoveData && fighter.CurrentMoveData->Stance == "crouch";
            pose.crouch = crouchMove;
            pose.jump = airborne;
            if (isKick) pose.legKick = 34; else pose.armReach = 34;
            DrawHumanoid(g, sx, sy, MoveTint(fighter), pose);
            break;
        }
        case CharState::Jump:
            pose.jump = true;
            DrawHumanoid(g, sx, sy, bodyColor, pose);
            break;
        default:
            // Idle / walking: 4-frame breathing cycle.
            pose.idleFrame = (fighter.FrameCounter / 8) % 4;
            DrawHumanoid(g, sx, sy, bodyColor, pose);
            break;
    }
}

void DrawProjectile(Graphics& g, const Projectile& proj) {
    double sx = ToScreenX(proj.PositionX);
    double sy = ToScreenY(proj.PositionY);
    double r = ScreenScale(16.0);
    SolidBrush brush(Color(255, 235, 130, 30));
    g.FillEllipse(&brush, static_cast<REAL>(sx - r), static_cast<REAL>(sy - r), static_cast<REAL>(r * 2), static_cast<REAL>(r * 2));
    Pen pen(Color(255, 255, 200, 60), static_cast<REAL>(ScreenScale(3.0)));
    g.DrawEllipse(&pen, static_cast<REAL>(sx - r), static_cast<REAL>(sy - r), static_cast<REAL>(r * 2), static_cast<REAL>(r * 2));
}

EffectStyle GetEffectStyle(const std::string& kind) {
    if (kind == "hit") return {Color(255, 255, 200, 40), 16.0, 0.14};
    if (kind == "heavy_hit") return {Color(255, 235, 110, 20), 26.0, 0.18};
    if (kind == "guard") return {Color(255, 60, 130, 220), 20.0, 0.14};
    if (kind == "special") return {Color(255, 140, 40, 220), 30.0, 0.22};
    if (kind == "super") return {Color(255, 230, 30, 30), 46.0, 0.32};
    if (kind == "counter") return {Color(255, 255, 210, 30), 24.0, 0.35};
    if (kind == "effective_counter") return {Color(255, 255, 40, 40), 40.0, 0.5};
    return {Color(255, 255, 255, 255), 16.0, 0.14};
}

void DrawEffect(Graphics& g, const LiveEffect& fx) {
    EffectStyle style = GetEffectStyle(fx.kind);
    double t = std::min(1.0, fx.age / style.duration);
    double r = ScreenScale(style.radius * (0.4 + t * 1.1));
    int alpha = static_cast<int>(255 * (1.0 - t));
    if (alpha <= 0) return;
    double sx = ToScreenX(fx.x), sy = ToScreenY(fx.y);
    SolidBrush brush(Color(static_cast<BYTE>(alpha), style.color.GetR(), style.color.GetG(), style.color.GetB()));
    g.FillEllipse(&brush, static_cast<REAL>(sx - r), static_cast<REAL>(sy - r), static_cast<REAL>(r * 2), static_cast<REAL>(r * 2));
    // Counter / Effective Counter's text label is drawn separately, pinned
    // to the scoring player's screen edge - see DrawCounterEdgeLabel.
}

void DrawCounterEdgeLabel(Graphics& g, const LiveEffect& fx) {
    bool effective = (fx.kind == "effective_counter");
    if (!effective && fx.kind != "counter") return;
    EffectStyle style = GetEffectStyle(fx.kind);
    double t = std::min(1.0, fx.age / style.duration);
    int alpha = static_cast<int>(255 * (1.0 - t));
    if (alpha <= 0) return;

    std::wstring label = effective ? L"E.COUNTER" : L"COUNTER";
    float dot = effective ? 1.5f : 1.0f;
    Color textColor(static_cast<BYTE>(alpha), style.color.GetR(), style.color.GetG(), style.color.GetB());

    float labelW = 72.0f, labelH = 12.0f;
    bool leftEdge = (fx.side == 0);
    float x = leftEdge ? 2.0f : (VirtualW - 2.0f - labelW);
    float y = (VirtualH - labelH) / 2.0f;
    RectF labelRect(x, y, labelW, labelH);
    DrawPixelTextCentered(g, label, labelRect, dot, textColor);
}

void DrawBar(Graphics& g, float x, float y, float w, float h, double ratio, Color fillColor, Color emptyColor, bool mirror, Color borderColor) {
    ratio = std::max(0.0, std::min(1.0, ratio));
    RectF rect(x, y, w, h);
    GraphicsPath path;
    AddRoundedRect(path, rect, 0.0f); // hard-edged bar, no rounding

    SolidBrush bg(emptyColor);
    g.FillPath(&bg, &path);

    float fillW = static_cast<float>(w * ratio);
    if (fillW > 0) {
        Region clipRegion(&path);
        Region oldClip;
        g.GetClip(&oldClip);
        g.SetClip(&clipRegion, CombineModeReplace);
        RectF fillRect = mirror ? RectF(x + w - fillW, y, fillW, h) : RectF(x, y, fillW, h);
        SolidBrush fg(fillColor);
        g.FillRectangle(&fg, fillRect);
        g.SetClip(&oldClip, CombineModeReplace);
    }

    // Glossy top sheen + a beveled two-tone frame (dark outer/light inner
    // hairline) instead of a single flat border - per the user's classic-
    // Capcom-HUD reference screenshots, whose bars read as a metallic
    // beveled casing rather than a flat outline.
    DrawGlossCap(g, rect);
    Pen outerPen(borderColor, 2.0f);
    g.DrawPath(&outerPen, &path);
    RectF innerRect(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f);
    if (innerRect.Width > 0 && innerRect.Height > 0) {
        Pen innerPen(Color(90, 255, 255, 255), 1.0f);
        g.DrawRectangle(&innerPen, innerRect);
    }
}

void DrawHPBar(Graphics& g, float x, float y, float w, float h, double ratio, bool mirror, Color borderColor) {
    const auto& pal = GetPalette();
    DrawBar(g, x, y, w, h, ratio, pal.Accent, pal.HpEmpty, mirror, borderColor);
}

void DrawGaugeBar(Graphics& g, float x, float y, float w, float h, double ratio, bool mirror, Color borderColor) {
    const auto& pal = GetPalette();
    DrawBar(g, x, y, w, h, ratio, pal.Gauge, pal.GaugeEmpty, mirror, borderColor);
}

// Placeholder portrait bust drawn in the HP bar's outer corner, matching
// the reference screenshots' portrait-then-bar layout. This project has no
// image-generation tool and no per-character portrait art exists, so this
// is a generic procedural mark (silhouette head + headband + shoulders in
// the character's accent color) rather than a likeness - it fills the
// reference layout's slot without claiming to be real art. If per-character
// portrait PNGs are ever added (e.g. `sprites/<charId>/portrait.png`,
// following Sprites.h's existing load/cache pattern), swap this for a
// `g.DrawImage` call keyed off the fighter's character id.
static void DrawPortraitBust(Graphics& g, float x, float y, float size, Color accent, Color borderColor) {
    RectF box(x, y, size, size);
    GraphicsPath boxPath;
    AddRoundedRect(boxPath, box, 0.0f);
    SolidBrush boxBg(Color(255, 34, 32, 40));
    g.FillPath(&boxBg, &boxPath);

    Region oldClip;
    g.GetClip(&oldClip);
    Region boxClip(&boxPath);
    g.SetClip(&boxClip, CombineModeReplace);

    float headR = size * 0.24f;
    float cx = x + size / 2.0f;
    float headY = y + size * 0.16f;
    SolidBrush skin(Color(255, 222, 170, 130));
    g.FillEllipse(&skin, cx - headR, headY, headR * 2.0f, headR * 2.0f);
    SolidBrush band(accent);
    g.FillRectangle(&band, RectF(cx - headR, headY + headR * 0.35f, headR * 2.0f, headR * 0.5f));

    SolidBrush shoulderBrush(Color(255, 60, 62, 74));
    g.FillEllipse(&shoulderBrush, cx - size * 0.44f, y + size * 0.62f, size * 0.88f, size * 0.6f);

    g.SetClip(&oldClip, CombineModeReplace);

    Pen border(borderColor, 1.5f);
    g.DrawPath(&border, &boxPath);
}

// Player name, drawn as a tiny caption directly under their HP bar - the
// old design's boxed tag + round-win pips didn't fit inside the 384x224
// canvas' ~8px-tall HUD row budget, so this is deliberately bare.
static void DrawNameTag(Graphics& g, float barX, float barBottom, float barW, const std::wstring& name, bool mirror) {
    const auto& pal = GetPalette();
    if (mirror) DrawPixelTextRight(g, name, barX + barW, barBottom, 1.0f, pal.ArenaLine);
    else DrawPixelText(g, name, barX, barBottom, 1.0f, pal.ArenaLine);
}

namespace {
// One skinnable bar (HP or super gauge): optional backing image, then the
// fill - per-percent unit sprites (ceil(ratio*100) copies from the inner
// end, the rest simply not drawn), else a full fill image clipped to the
// ratio, else a flat accent fill - then the frame image. Called only when
// the skin has at least one image for this bar; otherwise DrawHUD keeps
// the procedural DrawHPBar/DrawGaugeBar.
void DrawSkinnedBar(Graphics& g, const RectF& bar, double ratio, bool mirror, Image* frame, Image* fill, Image* unit, Image* empty, Color fallbackFill) {
    ratio = std::clamp(ratio, 0.0, 1.0);
    if (empty) DrawSkinImage(g, empty, bar, mirror);
    if (unit) {
        int count = static_cast<int>(std::ceil(ratio * 100.0 - 1e-9));
        float slotW = bar.Width / 100.0f;
        for (int i = 0; i < count; i++) {
            float x = mirror ? (bar.X + bar.Width - (i + 1) * slotW) : (bar.X + i * slotW);
            DrawSkinImage(g, unit, RectF(x, bar.Y, slotW, bar.Height), mirror);
        }
    } else {
        float fillW = static_cast<float>(bar.Width * ratio);
        RectF clip = mirror ? RectF(bar.X + bar.Width - fillW, bar.Y, fillW, bar.Height) : RectF(bar.X, bar.Y, fillW, bar.Height);
        if (fillW > 0) {
            if (fill) {
                Region oldClip;
                g.GetClip(&oldClip);
                g.SetClip(clip, CombineModeIntersect);
                DrawSkinImage(g, fill, bar, mirror);
                g.SetClip(&oldClip, CombineModeReplace);
            } else {
                SolidBrush b(fallbackFill);
                g.FillRectangle(&b, clip);
            }
        }
    }
    if (frame) DrawSkinImage(g, frame, RectF(bar.X - 3, bar.Y - 3, bar.Width + 6, bar.Height + 6), mirror);
}
} // namespace

void DrawHUD(Graphics& g, const BattleSystem& bs, int p1ComboDisplay, int p2ComboDisplay, double comboFade,
             const fs::path& baseDataDir, const fs::path& userDir) {
    const auto& pal = GetPalette();
    const HudSkin& skin = GetHudSkin(baseDataDir, userDir);

    // Nudged down from the top edge (was barY=3, barH=8) and a touch
    // thicker, per the user's classic-Capcom-HUD reference screenshots -
    // those show a visible gap above the HP bars rather than sitting
    // flush against the top, and a bolder bar than this project's
    // previous flat/thin default.
    float barY = 9.0f, barH = 10.0f;
    // Portrait bust sits in the outer corner (screen edge side), with the
    // HP bar starting just inside it and running toward center - matches
    // the reference screenshots' portrait+bar composition rather than the
    // previous plain edge-to-edge bar.
    float portraitSize = 22.0f, portraitGap = 3.0f;
    float portraitY = barY - 6.0f;
    float p1PortraitX = 2.0f;
    float p1BarX = p1PortraitX + portraitSize + portraitGap, barW = 115.0f;
    float p2BarX = VirtualW - 2.0f - portraitSize - portraitGap - barW;
    float p2PortraitX = VirtualW - 2.0f - portraitSize;
    double p1Ratio = bs.Player1.CurrentHP / static_cast<double>(bs.Player1.Stats.MaxHP);
    double p2Ratio = bs.Player2.CurrentHP / static_cast<double>(bs.Player2.Stats.MaxHP);
    if (skin.HasHpArt()) {
        DrawSkinnedBar(g, RectF(p1BarX, barY, barW, barH), p1Ratio, false, skin.HpFrame.get(), skin.HpFill.get(), skin.HpUnit.get(), skin.HpEmpty.get(), pal.Accent);
        DrawSkinnedBar(g, RectF(p2BarX, barY, barW, barH), p2Ratio, true, skin.HpFrame.get(), skin.HpFill.get(), skin.HpUnit.get(), skin.HpEmpty.get(), pal.Accent);
    } else {
        DrawHPBar(g, p1BarX, barY, barW, barH, p1Ratio, false, pal.ArenaLine);
        DrawHPBar(g, p2BarX, barY, barW, barH, p2Ratio, true, pal.ArenaLine);
    }
    // Portraits: per-character image when the skin has one, else the
    // procedural bust; an optional frame image goes over either.
    struct { float x; const Fighter* f; bool mirror; } portraits[] = {{p1PortraitX, &bs.Player1, false}, {p2PortraitX, &bs.Player2, true}};
    for (const auto& p : portraits) {
        RectF slot(p.x, portraitY, portraitSize, portraitSize);
        if (Image* img = skin.Portrait(p.f->Stats.Id)) DrawSkinImage(g, img, slot, p.mirror);
        else DrawPortraitBust(g, p.x, portraitY, portraitSize, pal.Accent, pal.ArenaLine);
        if (skin.PortraitFrame) DrawSkinImage(g, skin.PortraitFrame.get(), slot, p.mirror);
    }
    DrawNameTag(g, p1BarX, barY + barH + 1.0f, barW, Utf8ToWide(bs.Player1.Stats.Name), false);
    DrawNameTag(g, p2BarX, barY + barH + 1.0f, barW, Utf8ToWide(bs.Player2.Stats.Name), true);

    // Round timer: bold red box. Training mode never runs out, so it shows
    // an infinity symbol instead of a counting-down number.
    // "--" rather than an infinity glyph: at this tiny pixel-art scale (and
    // under some font fallback situations) "∞" doesn't render cleanly.
    std::wstring timerText = bs.TrainingMode ? L"--" : std::to_wstring(static_cast<int>(std::ceil(bs.FramesLeft / 60.0)));
    // A couple of px of padding around the digits (rather than an exact
    // fit) keeps the box border from touching the glyphs - at this tiny a
    // scale a border pixel flush against a digit reads as part of the
    // digit and makes it illegible.
    // Top HUD band budget: GameSpec::HudHeight (30, spec range 28-32). The
    // timer box (y 5..23) and the round label under it (y 24..32) are the
    // tallest elements, so the band's bottom edge is y=32; the HP bars/
    // portraits/names all finish above that.
    float boxW = 28, boxH = 18;
    float boxX = (VirtualW - boxW) / 2.0f;
    RectF boxRect(boxX, 5, boxW, boxH);
    if (skin.TimerFrame) {
        DrawSkinImage(g, skin.TimerFrame.get(), boxRect, false);
    } else {
        GraphicsPath boxPath;
        AddRoundedRect(boxPath, boxRect, 0.0f);
        SolidBrush accentBrush(pal.Accent);
        g.FillPath(&accentBrush, &boxPath);
        Pen boxBorder(pal.ArenaLine, 1.0f);
        g.DrawPath(&boxBorder, &boxPath);
    }
    DrawPixelTextCentered(g, timerText, boxRect, 2.0f, pal.White);
    RectF roundLabelRect(VirtualW / 2.0f - 30, 24, 60, 8);
    DrawPixelTextCentered(g, bs.TrainingMode ? L"TRAINING" : L"ROUND 1", roundLabelRect, 1.0f, pal.ArenaTextDim);

    // "FIGHT" banner - a brief flash right as the match starts, fading out
    // over its last ~15 frames. (Dropped the Japanese "/ 勝負" - the
    // hand-authored pixel font only covers Latin glyphs, and kanji isn't
    // legible as dot-matrix at this tiny a scale anyway.)
    if (bs.RoundStartFlashFrames > 0) {
        double t = bs.RoundStartFlashFrames / static_cast<double>(BattleSystem::RoundStartFlashDuration);
        int alpha = t > 0.3 ? 255 : static_cast<int>(255 * (t / 0.3));
        RectF fightRect(VirtualW / 2.0f - 60, 62, 120, 16);
        GraphicsPath fp;
        AddRoundedRect(fp, fightRect, 0.0f);
        SolidBrush fightBg(Color(static_cast<BYTE>(alpha), 20, 19, 18));
        g.FillPath(&fightBg, &fp);
        DrawPixelTextCentered(g, L"FIGHT", fightRect, 2.0f, Color(static_cast<BYTE>(alpha), 255, 255, 255));
    }

    // Combo counter.
    if (comboFade > 0.01) {
        int alpha = static_cast<int>(255 * comboFade);
        int shownCombo = p1ComboDisplay > 0 ? p1ComboDisplay : p2ComboDisplay;
        bool onRight = p2ComboDisplay > 0;
        if (shownCombo >= 2) {
            std::wstring comboText = std::to_wstring(shownCombo) + L" HIT COMBO";
            RectF cbRect(onRight ? VirtualW - 82.0f : 12.0f, 34, 70, 10);
            SolidBrush bg(Color(static_cast<BYTE>(std::min(220, alpha)), pal.Accent.GetR(), pal.Accent.GetG(), pal.Accent.GetB()));
            GraphicsPath cbPath;
            AddRoundedRect(cbPath, cbRect, 0.0f);
            g.FillPath(&bg, &cbPath);
            DrawPixelTextCentered(g, comboText, cbRect, 1.0f, Color(static_cast<BYTE>(alpha), 255, 255, 255));
        }
    }

    // Bottom super-gauge strip: SUPER_GAUGE_HEIGHT ~= 18px per the user's
    // 384x224-native size/layout spec (label top at gaugeY-7 to the 3px
    // bottom margin spans 224-206 = 18px), up from the previous 6px-tall
    // bar that only spanned ~13px total.
    float gaugeW = 90.0f, gaugeH = 8.0f, gaugeY = static_cast<float>(VirtualH) - gaugeH - 3.0f;
    double sp1 = bs.Player1.Gauge.Value / SuperGauge::MaxValue, sp2 = bs.Player2.Gauge.Value / SuperGauge::MaxValue;
    if (skin.HasSpArt()) {
        DrawSkinnedBar(g, RectF(3, gaugeY, gaugeW, gaugeH), sp1, false, skin.SpFrame.get(), skin.SpFill.get(), skin.SpUnit.get(), skin.SpEmpty.get(), pal.Gauge);
        DrawSkinnedBar(g, RectF(VirtualW - 3.0f - gaugeW, gaugeY, gaugeW, gaugeH), sp2, true, skin.SpFrame.get(), skin.SpFill.get(), skin.SpUnit.get(), skin.SpEmpty.get(), pal.Gauge);
    } else {
        DrawGaugeBar(g, 3, gaugeY, gaugeW, gaugeH, sp1, false, pal.ArenaLine);
        DrawGaugeBar(g, VirtualW - 3.0f - gaugeW, gaugeY, gaugeW, gaugeH, sp2, true, pal.ArenaLine);
    }
    DrawPixelText(g, L"SP", 3, gaugeY - 7, 1.0f, pal.ArenaTextDim);
    DrawPixelTextRight(g, L"SP", VirtualW - 3.0f, gaugeY - 7, 1.0f, pal.ArenaTextDim);
}

namespace {
// Debug-overlay rect: both edges are projected and snapped to whole canvas
// pixels independently (rather than snapping the origin and scaling the
// size) so a box's right/bottom edge lands on the same pixel column/row
// as an adjacent box's left/top edge at any camera zoom.
void DrawWorldRect(Graphics& g, Pen& pen, const RectBox& r) {
    REAL left = static_cast<REAL>(std::round(ToScreenX(r.Left())));
    REAL top = static_cast<REAL>(std::round(ToScreenY(r.Top())));
    REAL right = static_cast<REAL>(std::round(ToScreenX(r.Right())));
    REAL bottom = static_cast<REAL>(std::round(ToScreenY(r.Bottom())));
    g.DrawRectangle(&pen, left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top));
}
} // namespace

namespace {
void FillWorldRect(Graphics& g, Brush& brush, const RectBox& r) {
    REAL left = static_cast<REAL>(std::round(ToScreenX(r.Left())));
    REAL top = static_cast<REAL>(std::round(ToScreenY(r.Top())));
    REAL right = static_cast<REAL>(std::round(ToScreenX(r.Right())));
    REAL bottom = static_cast<REAL>(std::round(ToScreenY(r.Bottom())));
    g.FillRectangle(&brush, left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top));
}
} // namespace

// Collision-box debug view (F1 in Training Mode - see App::OnKeyDown;
// never part of the real game screen). Color code per the user's spec:
// pushbox = blue, hurtbox = green, hitbox = red, throw range = yellow.
// Every box is drawn as a translucent fill plus an outline, and the
// pushbox outline is dashed, so boxes that share an edge (the standing
// pushbox and torso hurtbox are both 30 wide) still both read - with
// plain 1px outlines the last one drawn simply covered the other, which
// looked like a box kind was missing. Projectile hitboxes are included
// (a fireball's hitbox lives on the projectile, not the thrower).
void DrawDebugOverlay(Graphics& g, const BattleSystem& bs) {
    Color pushC(255, 60, 120, 255), hurtC(255, 40, 210, 70), hitC(255, 235, 35, 35), throwC(255, 245, 210, 30);
    Pen pushPen(pushC, 1.0f);
    pushPen.SetDashStyle(DashStyleDash);
    Pen hurtPen(hurtC, 1.0f);
    Pen hitPen(hitC, 1.0f);
    Pen throwPen(throwC, 1.0f);
    SolidBrush pushFill(Color(60, 60, 120, 255));
    SolidBrush hurtFill(Color(55, 40, 210, 70));
    SolidBrush hitFill(Color(95, 235, 35, 35));
    SolidBrush throwFill(Color(70, 245, 210, 30));
    Color textColor = GetPalette().ArenaLine; // the arena ground is dark now - needs a light-on-dark color

    for (const Fighter* f : {&bs.Player1, &bs.Player2}) {
        for (const RectBox& hurt : f->HurtboxRects()) { FillWorldRect(g, hurtFill, hurt); DrawWorldRect(g, hurtPen, hurt); }
        RectBox push = f->PushboxRect();
        FillWorldRect(g, pushFill, push);
        DrawWorldRect(g, pushPen, push);

        bool throwing = f->SM.CurrentState == CharState::Attack && f->CurrentMoveData != nullptr &&
                        f->CurrentMoveData->GuardType == Constants::GuardThrow;
        for (const RectBox& hb : f->ActiveHitboxRects) {
            FillWorldRect(g, throwing ? throwFill : hitFill, hb);
            DrawWorldRect(g, throwing ? throwPen : hitPen, hb);
        }
        if (throwing && MoveExecutor::GetPhase(*f->CurrentMoveData, f->SM.CurrentFrame) == MovePhase::Active) {
            // The throw's real connect rule is a center-distance check, so
            // show that reach as a box from the fighter's center out to
            // ThrowRange in front of them, pushbox-tall.
            double range = f->CurrentMoveData->ThrowRange;
            double cx = std::round(f->PositionX) + f->Facing * range / 2.0;
            RectBox reach(cx, std::round(f->PositionY) - f->PushboxStandH / 2.0, range, f->PushboxStandH);
            FillWorldRect(g, throwFill, reach);
            DrawWorldRect(g, throwPen, reach);
        }
        // Fixed HUD position (top corner, below the HP bar/name) rather
        // than tracking the character around the stage.
        bool isP1 = (f == &bs.Player1);
        float tx = isP1 ? 2.0f : (VirtualW - 190.0f);
        float ty = 34.0f; // just under the 32px top HUD band (GameSpec::HudHeight)
        auto info = f->DebugInfo();
        // Frame readout caps at 99 per the user (a state's own frame count
        // never needs more than two digits; Idle would otherwise count up
        // forever and just add noise).
        std::wstring line1 = L"st=" + Utf8ToWide(info.state) + L" mv=" + Utf8ToWide(info.move) + L" f=" + std::to_wstring(std::min(info.frame, 99));
        DrawPixelText(g, line1, tx, ty, 1.0f, textColor);
        std::wstring line2 = L"hp=" + std::to_wstring(info.hp) + L" sp=" + std::to_wstring(static_cast<int>(info.gauge));
        DrawPixelText(g, line2, tx, ty + 7, 1.0f, textColor);
        std::wstring line3 = L"vel=(" + std::to_wstring(static_cast<int>(info.velocityX)) + L"," + std::to_wstring(static_cast<int>(info.velocityY)) + L")";
        DrawPixelText(g, line3, tx, ty + 14, 1.0f, textColor);
        std::wstring line4 = L"hs=" + std::to_wstring(info.hitstun) + L" bs=" + std::to_wstring(info.blockstun) + L" hp0=" + std::to_wstring(info.hitstop);
        DrawPixelText(g, line4, tx, ty + 21, 1.0f, textColor);
        std::wstring line5 = L"pos=(" + std::to_wstring(static_cast<int>(info.positionX)) + L"," + std::to_wstring(static_cast<int>(info.positionY)) + L")";
        DrawPixelText(g, line5, tx, ty + 28, 1.0f, textColor);
    }

    for (const auto& proj : bs.Projectiles) {
        RectBox r = proj.HitboxRect();
        FillWorldRect(g, hitFill, r);
        DrawWorldRect(g, hitPen, r);
    }

    // Legend, centered just above the bottom gauge band.
    struct { const wchar_t* name; Color c; } legend[] = {{L"PUSH", pushC}, {L"HURT", hurtC}, {L"HIT", hitC}, {L"THROW", throwC}};
    float lx = VirtualW / 2.0f - 62.0f, ly = static_cast<float>(VirtualH) - 30.0f;
    for (const auto& item : legend) {
        SolidBrush sw(item.c);
        g.FillRectangle(&sw, lx, ly, 5.0f, 5.0f);
        DrawPixelText(g, item.name, lx + 7, ly - 1, 1.0f, item.c);
        lx += 8.0f + PixelTextWidth(item.name, 1.0f) + 8.0f;
    }
}

} // namespace kakuge
