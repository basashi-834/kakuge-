// platform/Draw.cpp
#include "Draw.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

using namespace Gdiplus;

namespace kakuge {
// Defined in WinMain.cpp, alongside the identical g_AudioDir convention -
// set once at startup to Data/Images next to the .exe.
extern std::filesystem::path g_ImagesDir;
}

namespace kakuge {

// Sized so an idle character stands ~497px tall in the 1280x720 virtual
// canvas (per the user's refined spec: 460-500px height / ~64% of 720,
// 120-160px headroom, 30-40px footroom - see OriginY in Draw.h for the
// other half of that math). Shared by DrawHumanoid and the lying-down
// Knockdown/Dead poses in DrawFighter so both scale together.
constexpr double kCharScale = 4.6;

namespace {

// Real character-render art for Attack/Block frames (see data/images/) -
// each file is pre-trimmed to its opaque bounding box. anchorXFrac locates
// the character's grounded support foot (or, for symmetric stances, the
// stance's horizontal center) as a fraction of image width, so the sprite
// plants at the fighter's actual world position instead of at its own
// bounding-box center - important for the kick pose, whose swinging leg
// pulls the box far off to one side.
struct AttackSprite {
    std::unique_ptr<Gdiplus::Image> image;
    bool ok = false;
    double anchorXFrac = 0.5;
};

const AttackSprite& GetAttackSprite(const wchar_t* fileName, double anchorXFrac) {
    static std::map<std::wstring, AttackSprite> cache;
    auto it = cache.find(fileName);
    if (it != cache.end()) return it->second;
    AttackSprite s;
    s.anchorXFrac = anchorXFrac;
    std::filesystem::path p = g_ImagesDir / fileName;
    s.image = std::make_unique<Gdiplus::Image>(p.wstring().c_str());
    s.ok = (s.image && s.image->GetLastStatus() == Ok && s.image->GetWidth() > 0);
    auto res = cache.emplace(fileName, std::move(s));
    return res.first->second;
}

// All three renders share one pixel-to-world scale, derived from the
// standing punch pose's ~1404px trimmed height mapping to kCharScale's
// ~497px standing height. The kick pose's taller bounding box (from its
// raised leg) renders correspondingly taller under that same scale, which
// is correct - not an inconsistency to normalize away.
constexpr double kSourceStandHeight = 1404.0;

// Draws a real sprite anchored to the fighter's ground position (sx, sy),
// mirrored about that same anchor when facing left. Returns false (leaving
// the frame untouched) if the art file isn't present, so callers can fall
// back to the line-art humanoid - the shipped install always has these
// files, but a from-source build without data/images/ shouldn't crash.
bool DrawAttackSprite(Graphics& g, double sx, double sy, int facing, const wchar_t* fileName, double anchorXFrac) {
    const AttackSprite& sprite = GetAttackSprite(fileName, anchorXFrac);
    if (!sprite.ok) return false;
    Image* img = sprite.image.get();
    double srcW = img->GetWidth();
    double srcH = img->GetHeight();

    double scale = (kCharScale * 108.0) / kSourceStandHeight;
    double drawW = srcW * scale;
    double drawH = srcH * scale;
    double anchorX = srcW * sprite.anchorXFrac * scale;

    GraphicsState state = g.Save();
    g.TranslateTransform(static_cast<REAL>(sx), static_cast<REAL>(sy));
    if (facing < 0) g.ScaleTransform(-1.0f, 1.0f);
    g.DrawImage(img, static_cast<REAL>(-anchorX), static_cast<REAL>(-drawH), static_cast<REAL>(drawW), static_cast<REAL>(drawH));
    g.Restore(state);
    return true;
}

} // namespace

bool DrawSpriteFit(Graphics& g, const wchar_t* fileName, const RectF& rect, float paddingFrac) {
    const AttackSprite& sprite = GetAttackSprite(fileName, 0.5);
    if (!sprite.ok) return false;
    Image* img = sprite.image.get();
    double srcW = img->GetWidth();
    double srcH = img->GetHeight();
    if (srcW <= 0 || srcH <= 0) return false;

    float pad = std::min(rect.Width, rect.Height) * paddingFrac;
    float availW = rect.Width - pad * 2.0f, availH = rect.Height - pad * 2.0f;
    double scale = std::min(availW / srcW, availH / srcH);
    double drawW = srcW * scale, drawH = srcH * scale;
    // Standing renders read best anchored to the panel's bottom (feet on
    // the "ground"), horizontally centered, rather than vertically centered.
    double drawX = rect.X + (rect.Width - drawW) / 2.0;
    double drawY = rect.Y + rect.Height - pad - drawH;
    g.DrawImage(img, static_cast<REAL>(drawX), static_cast<REAL>(drawY), static_cast<REAL>(drawW), static_cast<REAL>(drawH));
    return true;
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

    switch (fighter.SM.CurrentState) {
        case CharState::Knockdown: {
            Color c(255, static_cast<BYTE>(std::max(0, bodyColor.GetR() - 60)), static_cast<BYTE>(std::max(0, bodyColor.GetG() - 60)), static_cast<BYTE>(std::max(0, bodyColor.GetB() - 60)));
            Pen pen(c, static_cast<REAL>(3.5 * kCharScale));
            g.DrawLine(&pen, static_cast<REAL>(sx - 44 * kCharScale), static_cast<REAL>(sy - 6 * kCharScale), static_cast<REAL>(sx + 44 * kCharScale), static_cast<REAL>(sy - 6 * kCharScale));
            g.DrawEllipse(&pen, static_cast<REAL>(sx + facing * 40 * kCharScale), static_cast<REAL>(sy - 24 * kCharScale), static_cast<REAL>(20 * kCharScale), static_cast<REAL>(20 * kCharScale));
            break;
        }
        case CharState::WakeUp:
            DrawHumanoid(g, sx, sy, bodyColor, {0.75, facing});
            break;
        case CharState::Crouch:
            DrawHumanoid(g, sx, sy, bodyColor, {0.72, facing});
            break;
        case CharState::Block:
            DrawHumanoid(g, sx, sy, Color(255, 60, 120, 210), {1.0, facing, 0, 0, 0, 10});
            break;
        case CharState::Hitstun:
            DrawHumanoid(g, sx, sy, Color(255, 220, 60, 60), {1.0, facing, 0, 0, 8.0 * facing, 0});
            break;
        case CharState::Throw:
            DrawHumanoid(g, sx, sy, Color(255, 200, 50, 50), {0.85, facing});
            break;
        case CharState::Dead: {
            // Lighter than before (was 140,140,140) - the arena ground is
            // now dark (Screen 03 reference), and the old gray barely read
            // against it.
            Color c(255, 220, 218, 216);
            Pen pen(c, static_cast<REAL>(3.0 * kCharScale));
            g.DrawLine(&pen, static_cast<REAL>(sx - 44 * kCharScale), static_cast<REAL>(sy - 4 * kCharScale), static_cast<REAL>(sx + 44 * kCharScale), static_cast<REAL>(sy - 4 * kCharScale));
            g.DrawEllipse(&pen, static_cast<REAL>(sx + facing * 40 * kCharScale), static_cast<REAL>(sy - 20 * kCharScale), static_cast<REAL>(18 * kCharScale), static_cast<REAL>(18 * kCharScale));
            break;
        }
        case CharState::Attack: {
            std::string btn = fighter.CurrentMoveData ? fighter.CurrentMoveData->Button : std::string();
            bool isKick = !btn.empty() && btn.back() == 'K';
            bool isPunch = !btn.empty() && btn.back() == 'P';
            if (isKick && DrawAttackSprite(g, sx, sy, facing, L"fighter_kick.png", 0.083)) break;
            if (isPunch && DrawAttackSprite(g, sx, sy, facing, L"fighter_punch.png", 0.5)) break;

            Color tint = MoveTint(fighter);
            if (isKick) DrawHumanoid(g, sx, sy, tint, {1.0, facing, 0, 34, 0, 0});
            else DrawHumanoid(g, sx, sy, tint, {1.0, facing, 34, 0, 0, 0});
            break;
        }
        case CharState::Jump: {
            Color c(255, static_cast<BYTE>(std::min(255, bodyColor.GetR() + 20)), static_cast<BYTE>(std::min(255, bodyColor.GetG() + 20)), static_cast<BYTE>(std::min(255, bodyColor.GetB() + 20)));
            DrawHumanoid(g, sx, sy, c, {0.92, facing});
            break;
        }
        default:
            // Idle/standing - the user-provided "1P Stand" render, falling
            // back to the line-art humanoid if the art file is missing.
            if (DrawAttackSprite(g, sx, sy, facing, L"fighter_stand.png", 0.55)) break;
            DrawHumanoid(g, sx, sy, bodyColor, {1.0, facing});
            break;
    }
}

void DrawProjectile(Graphics& g, const Projectile& proj) {
    double sx = ToScreenX(proj.PositionX);
    double sy = ToScreenY(proj.PositionY);
    SolidBrush brush(Color(255, 235, 130, 30));
    g.FillEllipse(&brush, static_cast<REAL>(sx - 16), static_cast<REAL>(sy - 16), 32.0f, 32.0f);
    Pen pen(Color(255, 255, 200, 60), 3.0f);
    g.DrawEllipse(&pen, static_cast<REAL>(sx - 16), static_cast<REAL>(sy - 16), 32.0f, 32.0f);
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
    double r = style.radius * (0.4 + t * 1.1);
    int alpha = static_cast<int>(255 * (1.0 - t));
    if (alpha <= 0) return;
    double sx = ToScreenX(fx.x), sy = ToScreenY(fx.y);
    SolidBrush brush(Color(static_cast<BYTE>(alpha), style.color.GetR(), style.color.GetG(), style.color.GetB()));
    g.FillEllipse(&brush, static_cast<REAL>(sx - r), static_cast<REAL>(sy - r), static_cast<REAL>(r * 2), static_cast<REAL>(r * 2));

    // Counter / Effective Counter get a bold label too - a plain flash
    // reads as an ordinary hit spark otherwise, and the whole point is to
    // tell the two tiers apart.
    if (fx.kind == "counter" || fx.kind == "effective_counter") {
        bool effective = (fx.kind == "effective_counter");
        std::wstring label = effective ? L"EFFECTIVE COUNTER" : L"COUNTER";
        REAL size = effective ? 22.0f : 16.0f;
        Font font(UiFontFamily(), size, FontStyleBold, UnitPixel);
        Color textColor(static_cast<BYTE>(alpha), style.color.GetR(), style.color.GetG(), style.color.GetB());
        RectF labelRect(static_cast<REAL>(sx - 200), static_cast<REAL>(sy - 320.0 - t * 30.0), 400.0f, 34.0f);
        DrawTextCentered(g, label, font, labelRect, textColor);
    }
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

// Small dark name tag + 3 round-win pips, sitting at the bottom-left (or,
// mirrored, bottom-right) corner of a player's HP bar - matches the
// reference "Screen 03" HUD's tag placement. This build doesn't play best-
// of-N rounds yet, so the pips are decorative: the first one lit shows
// "round in progress" rather than a real win count.
static void DrawNameTag(Graphics& g, float barX, float barBottom, float barW, const std::wstring& name, bool mirror) {
    const auto& pal = GetPalette();
    Font nameFont(UiFontFamily(), 13, FontStyleBold, UnitPixel);
    float tagH = 22.0f;
    RectF bbox;
    Font measureFont(UiFontFamily(), 13, FontStyleBold, UnitPixel);
    g.MeasureString(name.c_str(), -1, &measureFont, PointF(0, 0), &bbox);
    float tagW = bbox.Width + 16.0f;
    float tagX = mirror ? (barX + barW - tagW) : barX;
    RectF tagRect(tagX, barBottom - 2.0f, tagW, tagH);
    SolidBrush tagBg(pal.ArenaPanel);
    g.FillRectangle(&tagBg, tagRect);
    Pen tagBorder(pal.ArenaLine, 1.5f);
    g.DrawRectangle(&tagBorder, tagRect);
    DrawTextCentered(g, name, nameFont, tagRect, pal.ArenaLine);

    float pipSize = 10.0f, pipGap = 4.0f;
    float pipsX = mirror ? (tagX - pipGap - pipSize) : (tagX + tagW + pipGap);
    for (int i = 0; i < 3; i++) {
        float px = mirror ? (pipsX - i * (pipSize + pipGap)) : (pipsX + i * (pipSize + pipGap));
        RectF pipRect(px, barBottom + (tagH - pipSize) / 2.0f - 2.0f, pipSize, pipSize);
        SolidBrush pipBrush(i == 0 ? pal.Accent : pal.EmptyBar);
        g.FillRectangle(&pipBrush, pipRect);
        Pen pipBorder(pal.ArenaLine, 1.0f);
        g.DrawRectangle(&pipBorder, pipRect);
    }
}

void DrawHUD(Graphics& g, const BattleSystem& bs, int p1ComboDisplay, int p2ComboDisplay, double comboFade) {
    const auto& pal = GetPalette();
    Font labelFont(UiFontFamily(), 13, FontStyleBold, UnitPixel);
    Font timerFont(UiFontFamily(), 32, FontStyleBold, UnitPixel);
    Font comboFont(UiFontFamily(), 20, FontStyleBold, UnitPixel);

    float barY = 34.0f, barH = 26.0f;
    float p1BarX = 24.0f, barW = 420.0f;
    float p2BarX = VirtualW - 444.0f;
    DrawHPBar(g, p1BarX, barY, barW, barH, bs.Player1.CurrentHP / static_cast<double>(bs.Player1.Stats.MaxHP), false, pal.ArenaLine);
    DrawHPBar(g, p2BarX, barY, barW, barH, bs.Player2.CurrentHP / static_cast<double>(bs.Player2.Stats.MaxHP), true, pal.ArenaLine);
    DrawNameTag(g, p1BarX, barY + barH, barW, Utf8ToWide(bs.Player1.Stats.Name), false);
    DrawNameTag(g, p2BarX, barY + barH, barW, Utf8ToWide(bs.Player2.Stats.Name), true);

    // Round timer: bold red rounded box. Training mode never runs out, so
    // it shows an infinity symbol instead of a counting-down number.
    std::wstring timerText = bs.TrainingMode ? L"∞" : std::to_wstring(static_cast<int>(std::ceil(bs.FramesLeft / 60.0)));
    float boxW = 90, boxH = 58;
    float boxX = (VirtualW - boxW) / 2.0f;
    RectF boxRect(boxX, 12, boxW, boxH);
    GraphicsPath boxPath;
    AddRoundedRect(boxPath, boxRect, 0.0f);
    SolidBrush accentBrush(pal.Accent);
    g.FillPath(&accentBrush, &boxPath);
    DrawGlossCap(g, boxRect);
    Pen boxBorder(pal.ArenaLine, 2.0f);
    g.DrawPath(&boxBorder, &boxPath);
    DrawTextCentered(g, timerText, timerFont, boxRect, pal.White);
    RectF roundLabelRect(VirtualW / 2.0f - 60, 74, 120, 20);
    DrawTextCentered(g, bs.TrainingMode ? L"TRAINING" : L"ROUND 1", labelFont, roundLabelRect, pal.ArenaTextDim);

    // "FIGHT" banner - a brief flash right as the match starts (Screen 03's
    // "FIGHT / 勝負" panel), fading out over its last ~15 frames.
    if (bs.RoundStartFlashFrames > 0) {
        double t = bs.RoundStartFlashFrames / static_cast<double>(BattleSystem::RoundStartFlashDuration);
        int alpha = t > 0.3 ? 255 : static_cast<int>(255 * (t / 0.3));
        RectF fightRect(VirtualW / 2.0f - 130, 190, 260, 46);
        GraphicsPath fp;
        AddRoundedRect(fp, fightRect, 0.0f);
        SolidBrush fightBg(Color(static_cast<BYTE>(alpha), 20, 19, 18));
        g.FillPath(&fightBg, &fp);
        Font fightFont(UiFontFamily(), 24, FontStyleBold, UnitPixel);
        DrawTextCentered(g, L"FIGHT / 勝負", fightFont, fightRect, Color(static_cast<BYTE>(alpha), 255, 255, 255));
    }

    // Combo counter (matches the reference HUD's "COMBO / N HIT" box).
    if (comboFade > 0.01) {
        int alpha = static_cast<int>(255 * comboFade);
        int shownCombo = p1ComboDisplay > 0 ? p1ComboDisplay : p2ComboDisplay;
        bool onRight = p2ComboDisplay > 0;
        if (shownCombo >= 2) {
            std::wstring comboText = std::to_wstring(shownCombo) + L" HIT COMBO";
            RectF cbRect(onRight ? VirtualW - 300.0f : 100.0f, 100, 200, 34);
            SolidBrush bg(Color(static_cast<BYTE>(std::min(220, alpha)), pal.Accent.GetR(), pal.Accent.GetG(), pal.Accent.GetB()));
            GraphicsPath cbPath;
            AddRoundedRect(cbPath, cbRect, 0.0f);
            g.FillPath(&bg, &cbPath);
            DrawTextCentered(g, comboText, comboFont, cbRect, Color(static_cast<BYTE>(alpha), 255, 255, 255));
        }
    }

    DrawGaugeBar(g, 24, 682, 320, 14, bs.Player1.Gauge.Value / SuperGauge::MaxValue, false, pal.ArenaLine);
    DrawGaugeBar(g, VirtualW - 344.0f, 682, 320, 14, bs.Player2.Gauge.Value / SuperGauge::MaxValue, true, pal.ArenaLine);
    DrawTextLeft(g, L"GAUGE", labelFont, 24, 663, pal.ArenaTextDim);
    DrawTextRight(g, L"GAUGE", labelFont, VirtualW - 24.0f, 663, pal.ArenaTextDim);
}

void DrawDebugOverlay(Graphics& g, const BattleSystem& bs) {
    Pen pushPen(Color(255, 40, 200, 40), 2.0f);
    Pen hurtPen(Color(255, 50, 110, 230), 2.0f);
    Pen hitPen(Color(255, 230, 30, 30), 3.0f);
    Font font(MonospaceFontFamily(), 12, FontStyleRegular, UnitPixel);
    SolidBrush brush(GetPalette().ArenaLine); // the arena ground is dark now - needs a light-on-dark color

    for (const Fighter* f : {&bs.Player1, &bs.Player2}) {
        RectBox push = f->PushboxRect();
        g.DrawRectangle(&pushPen, static_cast<REAL>(ToScreenX(push.Left())), static_cast<REAL>(ToScreenY(push.Top())), static_cast<REAL>(push.Width), static_cast<REAL>(push.Height));
        RectBox hurt = f->HurtboxRect();
        g.DrawRectangle(&hurtPen, static_cast<REAL>(ToScreenX(hurt.Left())), static_cast<REAL>(ToScreenY(hurt.Top())), static_cast<REAL>(hurt.Width), static_cast<REAL>(hurt.Height));
        if (f->ActiveHitboxValid) {
            const RectBox& hb = f->ActiveHitboxRect;
            g.DrawRectangle(&hitPen, static_cast<REAL>(ToScreenX(hb.Left())), static_cast<REAL>(ToScreenY(hb.Top())), static_cast<REAL>(hb.Width), static_cast<REAL>(hb.Height));
        }
        // Fixed HUD position (under each player's name/HP bar) rather than
        // tracking the character around the stage - easier to keep an eye
        // on while also watching the actual on-stage action.
        bool isP1 = (f == &bs.Player1);
        double tx = isP1 ? 24.0 : (VirtualW - 444.0);
        double ty = 110.0;
        auto info = f->DebugInfo();
        std::wstring line1 = L"state=" + Utf8ToWide(info.state) + L" move=" + Utf8ToWide(info.move) + L" frame=" + std::to_wstring(info.frame);
        g.DrawString(line1.c_str(), -1, &font, PointF(static_cast<REAL>(tx), static_cast<REAL>(ty)), &brush);
        std::wstring line2 = L"hp=" + std::to_wstring(info.hp) + L" gauge=" + std::to_wstring(static_cast<int>(info.gauge));
        g.DrawString(line2.c_str(), -1, &font, PointF(static_cast<REAL>(tx), static_cast<REAL>(ty + 16)), &brush);
        std::wstring line3 = L"vel=(" + std::to_wstring(static_cast<int>(info.velocityX)) + L"," + std::to_wstring(static_cast<int>(info.velocityY)) + L")";
        g.DrawString(line3.c_str(), -1, &font, PointF(static_cast<REAL>(tx), static_cast<REAL>(ty + 32)), &brush);
        std::wstring line4 = L"hitstun=" + std::to_wstring(info.hitstun) + L" blockstun=" + std::to_wstring(info.blockstun) + L" hitstop=" + std::to_wstring(info.hitstop);
        g.DrawString(line4.c_str(), -1, &font, PointF(static_cast<REAL>(tx), static_cast<REAL>(ty + 48)), &brush);
        std::wstring line5 = L"pos=(" + std::to_wstring(static_cast<int>(info.positionX)) + L"," + std::to_wstring(static_cast<int>(info.positionY)) + L")";
        g.DrawString(line5.c_str(), -1, &font, PointF(static_cast<REAL>(tx), static_cast<REAL>(ty + 64)), &brush);
    }
}

} // namespace kakuge
