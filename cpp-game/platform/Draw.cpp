// platform/Draw.cpp
#include "Draw.h"
#include <algorithm>
#include <cmath>

using namespace Gdiplus;

namespace kakuge {

// Sized so an idle character stands ~156px tall (70% of the 224px virtual
// canvas height, see OriginY in Draw.h for the other half of that math) -
// matches the user's 1920x1080-proportioned spec (PLAYER_HEIGHT=760 of a
// 1080-tall screen, see StageConstants::PlayerHeightRatio in engine/
// Constants.h). Shared by DrawHumanoid and the lying-down Knockdown/Dead
// poses in DrawFighter so both scale together. Every character is drawn
// with this same vector line-art path - the earlier round's photo-sprite
// renders (data/images/fighter_*.png) were removed per the user's
// explicit request to express everything as pixel art, and the low-res-
// buffer + nearest-neighbor pipeline (see App::OnPaint) is what turns
// this line art into genuine chunky pixels rather than smooth vector
// strokes.
constexpr double kCharScale = 1.43;

// ---- Dynamic camera (auto-zoom) ----
// Extra world-space width kept visible around the two fighters (beyond
// their raw separation) so they're never crammed edge-to-edge - chosen so
// the fighters' StageConstants::PlayerStartDistance (120, matching the
// user's spec's ~600px starting range at 1920x1080) reads as almost
// exactly baseline zoom (384/(120+264) = 1.008): the round's opening
// neutral position is the natural "standard magnification" reference
// point the user's spec describes, not an already-zoomed-in one.
constexpr double kCameraPaddingWorld = 264.0;
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
// not horizontal: an idle character is ~108*kCharScale+2 px tall at zoom
// 1 (~156px - 70% of the canvas, see kCharScale's comment) and scales
// linearly with zoom, while the canvas is only 224px tall with the HUD's
// HP bar/name tag occupying its own ~20px at the top. 1.05 keeps an idle
// character under ~165px tall (still ~27px short of the top HUD even at
// the closest range) while still giving the "distance is close -> zoom in
// a little" effect the spec asks for something real to show.
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
} // namespace

GameCamera& GetCamera() { return g_Camera; }

void UpdateCamera(double p1x, double p2x, double dt) {
    double targetCenter = (p1x + p2x) / 2.0;
    double targetZoom = CameraTargetZoom(std::abs(p1x - p2x));
    double t = std::clamp(kCameraLerpSpeed * dt, 0.0, 1.0);
    g_Camera.CenterX += (targetCenter - g_Camera.CenterX) * t;
    g_Camera.Zoom += (targetZoom - g_Camera.Zoom) * t;
}

void ResetCamera(double p1x, double p2x) {
    g_Camera.CenterX = (p1x + p2x) / 2.0;
    g_Camera.Zoom = CameraTargetZoom(std::abs(p1x - p2x));
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
// Humanoid fighter - line art (head+headband, torso, two-segment limbs).
// ---------------------------------------------------------------------
void DrawHumanoid(Graphics& g, double sx, double sy, Color color, const HumanoidPose& pose) {
    double s = pose.heightScale * kCharScale;
    double legH = 46.0 * s, torsoH = 38.0 * s, headR = 12.0 * s;
    double hipY = sy - legH;
    double shoulderY = hipY - torsoH;
    double neckY = shoulderY - 2;
    double headCenterY = neckY - headR;
    double cx = sx + (pose.leanBack * 0.35);
    int facing = pose.facing;

    Pen bodyPen(color, static_cast<REAL>(3.2 * s));
    bodyPen.SetLineJoin(LineJoinRound);
    Pen limbPen(color, static_cast<REAL>(5.5 * s));
    limbPen.SetStartCap(LineCapRound);
    limbPen.SetEndCap(LineCapRound);
    Pen headPen(color, static_cast<REAL>(2.6 * s));
    Color bandColor(255, 230, 45, 40);
    Pen bandPen(bandColor, static_cast<REAL>(3.0 * s));

    // back leg (static stance)
    double backHipX = cx - (5 * s);
    double backKneeX = backHipX - (2 * s);
    double backKneeY = hipY + (legH * 0.55);
    g.DrawLine(&limbPen, static_cast<REAL>(backHipX), static_cast<REAL>(hipY), static_cast<REAL>(backKneeX), static_cast<REAL>(backKneeY));
    g.DrawLine(&limbPen, static_cast<REAL>(backKneeX), static_cast<REAL>(backKneeY), static_cast<REAL>(cx - 12 * s), static_cast<REAL>(sy));

    // front leg (kicks forward/up when legKick > 0)
    double frontHipX = cx + (5 * s);
    if (pose.legKick > 0) {
        double kneeX = frontHipX + (facing * 10 * s);
        double kneeY = hipY + (legH * 0.35);
        double footX = frontHipX + (facing * (16 + pose.legKick) * s);
        double footY = hipY - (std::min(pose.legKick, 30.0) * 0.5 * s);
        g.DrawLine(&limbPen, static_cast<REAL>(frontHipX), static_cast<REAL>(hipY), static_cast<REAL>(kneeX), static_cast<REAL>(kneeY));
        g.DrawLine(&limbPen, static_cast<REAL>(kneeX), static_cast<REAL>(kneeY), static_cast<REAL>(footX), static_cast<REAL>(footY));
    } else {
        double kneeX = frontHipX + (2 * s);
        double kneeY = hipY + (legH * 0.55);
        g.DrawLine(&limbPen, static_cast<REAL>(frontHipX), static_cast<REAL>(hipY), static_cast<REAL>(kneeX), static_cast<REAL>(kneeY));
        g.DrawLine(&limbPen, static_cast<REAL>(kneeX), static_cast<REAL>(kneeY), static_cast<REAL>(cx + 12 * s), static_cast<REAL>(sy));
    }

    // torso (outline, slight taper)
    double torsoTopW = 24.0 * s, torsoBotW = 20.0 * s;
    PointF pts[4] = {
        PointF(static_cast<REAL>(cx - torsoTopW / 2), static_cast<REAL>(shoulderY)),
        PointF(static_cast<REAL>(cx + torsoTopW / 2), static_cast<REAL>(shoulderY)),
        PointF(static_cast<REAL>(cx + torsoBotW / 2), static_cast<REAL>(hipY)),
        PointF(static_cast<REAL>(cx - torsoBotW / 2), static_cast<REAL>(hipY)),
    };
    g.DrawPolygon(&bodyPen, pts, 4);

    // back arm (static, guard-ish)
    double backShoulderX = cx - (10 * s);
    double backElbowX = backShoulderX - (3 * s);
    double backElbowY = shoulderY + (14 * s);
    g.DrawLine(&limbPen, static_cast<REAL>(backShoulderX), static_cast<REAL>(shoulderY + 2 * s), static_cast<REAL>(backElbowX), static_cast<REAL>(backElbowY));
    g.DrawLine(&limbPen, static_cast<REAL>(backElbowX), static_cast<REAL>(backElbowY), static_cast<REAL>(backElbowX - 2 * s), static_cast<REAL>(backElbowY + 12 * s));

    // front arm (punches forward when armReach > 0, raised when guardRaise > 0)
    double frontShoulderX = cx + (10 * s);
    if (pose.armReach > 0) {
        double elbowX = frontShoulderX + (facing * 10 * s);
        double elbowY = shoulderY + (6 * s);
        double handX = frontShoulderX + (facing * (14 + pose.armReach) * s);
        double handY = shoulderY + (2 * s) - (std::min(pose.armReach, 20.0) * 0.25 * s);
        g.DrawLine(&limbPen, static_cast<REAL>(frontShoulderX), static_cast<REAL>(shoulderY + 2 * s), static_cast<REAL>(elbowX), static_cast<REAL>(elbowY));
        g.DrawLine(&limbPen, static_cast<REAL>(elbowX), static_cast<REAL>(elbowY), static_cast<REAL>(handX), static_cast<REAL>(handY));
    } else if (pose.guardRaise > 0) {
        double elbowX = frontShoulderX + (facing * 4 * s);
        double elbowY = shoulderY + (2 * s);
        double handX = frontShoulderX + (facing * 8 * s);
        double handY = shoulderY - (10 * s);
        g.DrawLine(&limbPen, static_cast<REAL>(frontShoulderX), static_cast<REAL>(shoulderY + 2 * s), static_cast<REAL>(elbowX), static_cast<REAL>(elbowY));
        g.DrawLine(&limbPen, static_cast<REAL>(elbowX), static_cast<REAL>(elbowY), static_cast<REAL>(handX), static_cast<REAL>(handY));
    } else {
        double elbowX = frontShoulderX + (3 * s);
        double elbowY = shoulderY + (14 * s);
        g.DrawLine(&limbPen, static_cast<REAL>(frontShoulderX), static_cast<REAL>(shoulderY + 2 * s), static_cast<REAL>(elbowX), static_cast<REAL>(elbowY));
        g.DrawLine(&limbPen, static_cast<REAL>(elbowX), static_cast<REAL>(elbowY), static_cast<REAL>(elbowX + 2 * s), static_cast<REAL>(elbowY + 12 * s));
    }

    // head + headband
    RectF headRect(static_cast<REAL>(cx - headR), static_cast<REAL>(headCenterY - headR), static_cast<REAL>(headR * 2), static_cast<REAL>(headR * 2));
    g.DrawEllipse(&headPen, headRect);
    double bandY = headCenterY - (headR * 0.15);
    g.DrawLine(&bandPen, static_cast<REAL>(cx - headR + 1 * s), static_cast<REAL>(bandY), static_cast<REAL>(cx + headR - 1 * s), static_cast<REAL>(bandY));
    double tailX = cx - (facing * headR * 0.6);
    g.DrawLine(&bandPen, static_cast<REAL>(tailX), static_cast<REAL>(bandY), static_cast<REAL>(tailX - facing * 8 * s), static_cast<REAL>(bandY - 10 * s));
    g.DrawLine(&bandPen, static_cast<REAL>(tailX), static_cast<REAL>(bandY), static_cast<REAL>(tailX - facing * 4 * s), static_cast<REAL>(bandY - 14 * s));
}

void DrawFighter(Graphics& g, const Fighter& fighter) {
    Color bodyColor(255, static_cast<BYTE>(fighter.Stats.ColorR), static_cast<BYTE>(fighter.Stats.ColorG), static_cast<BYTE>(fighter.Stats.ColorB));
    double sx = ToScreenX(fighter.PositionX);
    double sy = ToScreenY(fighter.PositionY);
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

    switch (fighter.SM.CurrentState) {
        case CharState::Knockdown: {
            Color c(255, static_cast<BYTE>(std::max(0, bodyColor.GetR() - 60)), static_cast<BYTE>(std::max(0, bodyColor.GetG() - 60)), static_cast<BYTE>(std::max(0, bodyColor.GetB() - 60)));
            Pen pen(c, static_cast<REAL>(3.5 * cs));
            g.DrawLine(&pen, static_cast<REAL>(sx - 44 * cs), static_cast<REAL>(sy - 6 * cs), static_cast<REAL>(sx + 44 * cs), static_cast<REAL>(sy - 6 * cs));
            g.DrawEllipse(&pen, static_cast<REAL>(sx + facing * 40 * cs), static_cast<REAL>(sy - 24 * cs), static_cast<REAL>(20 * cs), static_cast<REAL>(20 * cs));
            break;
        }
        case CharState::WakeUp:
            DrawHumanoid(g, sx, sy, bodyColor, {0.75 * zoom, facing});
            break;
        case CharState::Crouch:
            DrawHumanoid(g, sx, sy, bodyColor, {0.72 * zoom, facing});
            break;
        case CharState::Block:
            DrawHumanoid(g, sx, sy, Color(255, 60, 120, 210), {zoom, facing, 0, 0, 0, 10});
            break;
        case CharState::Hitstun:
            DrawHumanoid(g, sx, sy, Color(255, 220, 60, 60), {zoom, facing, 0, 0, 8.0 * facing * zoom, 0});
            break;
        case CharState::Throw:
            DrawHumanoid(g, sx, sy, Color(255, 200, 50, 50), {0.85 * zoom, facing});
            break;
        case CharState::Dead: {
            // Lighter than before (was 140,140,140) - the arena ground is
            // now dark (Screen 03 reference), and the old gray barely read
            // against it.
            Color c(255, 220, 218, 216);
            Pen pen(c, static_cast<REAL>(3.0 * cs));
            g.DrawLine(&pen, static_cast<REAL>(sx - 44 * cs), static_cast<REAL>(sy - 4 * cs), static_cast<REAL>(sx + 44 * cs), static_cast<REAL>(sy - 4 * cs));
            g.DrawEllipse(&pen, static_cast<REAL>(sx + facing * 40 * cs), static_cast<REAL>(sy - 20 * cs), static_cast<REAL>(18 * cs), static_cast<REAL>(18 * cs));
            break;
        }
        case CharState::Attack: {
            std::string btn = fighter.CurrentMoveData ? fighter.CurrentMoveData->Button : std::string();
            bool isKick = !btn.empty() && btn.back() == 'K';
            Color tint = MoveTint(fighter);
            if (isKick) DrawHumanoid(g, sx, sy, tint, {zoom, facing, 0, 34, 0, 0});
            else DrawHumanoid(g, sx, sy, tint, {zoom, facing, 34, 0, 0, 0});
            break;
        }
        case CharState::Jump: {
            Color c(255, static_cast<BYTE>(std::min(255, bodyColor.GetR() + 20)), static_cast<BYTE>(std::min(255, bodyColor.GetG() + 20)), static_cast<BYTE>(std::min(255, bodyColor.GetB() + 20)));
            DrawHumanoid(g, sx, sy, c, {0.92 * zoom, facing});
            break;
        }
        default:
            // Idle/standing.
            DrawHumanoid(g, sx, sy, bodyColor, {zoom, facing});
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
    AddRoundedRect(path, rect, 0.0f); // hard-edged bar, no rounding, no glow (spec)

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

    Pen pen(borderColor, 2.0f);
    g.DrawPath(&pen, &path);
}

void DrawHPBar(Graphics& g, float x, float y, float w, float h, double ratio, bool mirror, Color borderColor) {
    const auto& pal = GetPalette();
    DrawBar(g, x, y, w, h, ratio, pal.Accent, pal.HpEmpty, mirror, borderColor);
}

void DrawGaugeBar(Graphics& g, float x, float y, float w, float h, double ratio, bool mirror, Color borderColor) {
    const auto& pal = GetPalette();
    DrawBar(g, x, y, w, h, ratio, pal.Gauge, pal.GaugeEmpty, mirror, borderColor);
}

// Player name, drawn as a tiny caption directly under their HP bar - the
// old design's boxed tag + round-win pips didn't fit inside the 384x224
// canvas' ~8px-tall HUD row budget, so this is deliberately bare.
static void DrawNameTag(Graphics& g, float barX, float barBottom, float barW, const std::wstring& name, bool mirror) {
    const auto& pal = GetPalette();
    if (mirror) DrawPixelTextRight(g, name, barX + barW, barBottom, 1.0f, pal.ArenaLine);
    else DrawPixelText(g, name, barX, barBottom, 1.0f, pal.ArenaLine);
}

void DrawHUD(Graphics& g, const BattleSystem& bs, int p1ComboDisplay, int p2ComboDisplay, double comboFade) {
    const auto& pal = GetPalette();

    float barY = 3.0f, barH = 8.0f;
    float p1BarX = 3.0f, barW = 140.0f;
    float p2BarX = VirtualW - 3.0f - barW;
    DrawHPBar(g, p1BarX, barY, barW, barH, bs.Player1.CurrentHP / static_cast<double>(bs.Player1.Stats.MaxHP), false, pal.ArenaLine);
    DrawHPBar(g, p2BarX, barY, barW, barH, bs.Player2.CurrentHP / static_cast<double>(bs.Player2.Stats.MaxHP), true, pal.ArenaLine);
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
    float boxW = 28, boxH = 20;
    float boxX = (VirtualW - boxW) / 2.0f;
    RectF boxRect(boxX, 2, boxW, boxH);
    GraphicsPath boxPath;
    AddRoundedRect(boxPath, boxRect, 0.0f);
    SolidBrush accentBrush(pal.Accent);
    g.FillPath(&accentBrush, &boxPath);
    Pen boxBorder(pal.ArenaLine, 1.0f);
    g.DrawPath(&boxBorder, &boxPath);
    DrawPixelTextCentered(g, timerText, boxRect, 2.0f, pal.White);
    RectF roundLabelRect(VirtualW / 2.0f - 30, 23, 60, 8);
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
            RectF cbRect(onRight ? VirtualW - 82.0f : 12.0f, 28, 70, 10);
            SolidBrush bg(Color(static_cast<BYTE>(std::min(220, alpha)), pal.Accent.GetR(), pal.Accent.GetG(), pal.Accent.GetB()));
            GraphicsPath cbPath;
            AddRoundedRect(cbPath, cbRect, 0.0f);
            g.FillPath(&bg, &cbPath);
            DrawPixelTextCentered(g, comboText, cbRect, 1.0f, Color(static_cast<BYTE>(alpha), 255, 255, 255));
        }
    }

    float gaugeW = 90.0f, gaugeH = 6.0f, gaugeY = static_cast<float>(VirtualH) - gaugeH - 2.0f;
    DrawGaugeBar(g, 3, gaugeY, gaugeW, gaugeH, bs.Player1.Gauge.Value / SuperGauge::MaxValue, false, pal.ArenaLine);
    DrawGaugeBar(g, VirtualW - 3.0f - gaugeW, gaugeY, gaugeW, gaugeH, bs.Player2.Gauge.Value / SuperGauge::MaxValue, true, pal.ArenaLine);
    DrawPixelText(g, L"SP", 3, gaugeY - 7, 1.0f, pal.ArenaTextDim);
    DrawPixelTextRight(g, L"SP", VirtualW - 3.0f, gaugeY - 7, 1.0f, pal.ArenaTextDim);
}

void DrawDebugOverlay(Graphics& g, const BattleSystem& bs) {
    Pen pushPen(Color(255, 40, 200, 40), 1.0f);
    Pen hurtPen(Color(255, 50, 110, 230), 1.0f);
    Pen hitPen(Color(255, 230, 30, 30), 1.5f);
    Color textColor = GetPalette().ArenaLine; // the arena ground is dark now - needs a light-on-dark color

    for (const Fighter* f : {&bs.Player1, &bs.Player2}) {
        RectBox push = f->PushboxRect();
        g.DrawRectangle(&pushPen, static_cast<REAL>(ToScreenX(push.Left())), static_cast<REAL>(ToScreenY(push.Top())), static_cast<REAL>(ScreenScale(push.Width)), static_cast<REAL>(ScreenScale(push.Height)));
        for (const RectBox& hurt : f->HurtboxRects()) {
            g.DrawRectangle(&hurtPen, static_cast<REAL>(ToScreenX(hurt.Left())), static_cast<REAL>(ToScreenY(hurt.Top())), static_cast<REAL>(ScreenScale(hurt.Width)), static_cast<REAL>(ScreenScale(hurt.Height)));
        }
        for (const RectBox& hb : f->ActiveHitboxRects) {
            g.DrawRectangle(&hitPen, static_cast<REAL>(ToScreenX(hb.Left())), static_cast<REAL>(ToScreenY(hb.Top())), static_cast<REAL>(ScreenScale(hb.Width)), static_cast<REAL>(ScreenScale(hb.Height)));
        }
        // Fixed HUD position (top corner, below the HP bar/name) rather
        // than tracking the character around the stage.
        bool isP1 = (f == &bs.Player1);
        float tx = isP1 ? 2.0f : (VirtualW - 190.0f);
        float ty = 28.0f;
        auto info = f->DebugInfo();
        std::wstring line1 = L"st=" + Utf8ToWide(info.state) + L" mv=" + Utf8ToWide(info.move) + L" f=" + std::to_wstring(info.frame);
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
}

} // namespace kakuge
