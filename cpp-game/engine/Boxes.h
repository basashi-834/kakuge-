// engine/Boxes.h
// Hitbox / Hurtbox / Pushbox are plain axis-aligned rectangles (world-space,
// centered on X, center+halfheight style on Y) - pure logic, no platform
// dependency, exercised directly by tests/EngineTests.cpp.
// 1:1 port of winforms-game/Character/Boxes.ps1.
#pragma once
#include <string>
#include <vector>

namespace kakuge {

struct RectBox {
    double CenterX = 0.0;
    double CenterY = 0.0;
    double Width = 0.0;
    double Height = 0.0;

    RectBox() = default;
    RectBox(double cx, double cy, double w, double h) : CenterX(cx), CenterY(cy), Width(w), Height(h) {}

    double Left() const { return CenterX - Width / 2.0; }
    double Right() const { return CenterX + Width / 2.0; }
    double Top() const { return CenterY - Height / 2.0; }
    double Bottom() const { return CenterY + Height / 2.0; }

    bool Intersects(const RectBox& other) const {
        if (Right() <= other.Left()) return false;
        if (Left() >= other.Right()) return false;
        if (Bottom() <= other.Top()) return false;
        if (Top() >= other.Bottom()) return false;
        return true;
    }
};

// One named hurtbox part (e.g. "head", "torso", "arm", "hand", "waist",
// "leg", "foot") - a hit connects if it overlaps ANY part for the
// defender's current stance, not just a single whole-body box. Name is
// freeform (the Character Editor offers the seven above as presets but
// doesn't require them) and purely cosmetic/organizational - hit
// detection just unions every part's rect.
struct HurtboxPart {
    std::string Name = "body";
    RectBox Box;
};

// Per-stance Hurtbox shapes (立ち/しゃがみ/ジャンプで形状変化), each a list of
// named parts rather than one rectangle. Y is measured with 0 = ground,
// negative = up (matches the JSON hitbox offset convention). Defaults
// below are sized to match the renderer's ~79px-tall humanoid
// (platform/Draw.cpp's kCharScale, tuned for the 384x224 pixel-art
// canvas per the user's 1920x1080-proportioned character-size spec, then
// halved again alongside kCharScale per the user's later request to
// shrink the character to about half its size - see StageConstants in
// Constants.h) so hits visually connect where they appear to - a single
// "torso" part per stance, matching the whole-body box every character
// shipped with before per-part editing existed.
struct HurtboxSet {
    std::vector<HurtboxPart> Stand{{"torso", RectBox{0, -35.7, 32.85, 71.35}}};
    std::vector<HurtboxPart> Crouch{{"torso", RectBox{0, -22.15, 32.85, 44.25}}};
    std::vector<HurtboxPart> Air{{"torso", RectBox{0, -35.7, 32.85, 71.35}}};

    std::vector<HurtboxPart>& PartsForStance(const std::string& stance) {
        if (stance == "crouch") return Crouch;
        if (stance == "air") return Air;
        return Stand;
    }
    const std::vector<HurtboxPart>& PartsForStance(const std::string& stance) const {
        if (stance == "crouch") return Crouch;
        if (stance == "air") return Air;
        return Stand;
    }

    // World-space rects for every part in the defender's current stance -
    // hit/pushbox-adjacent code checks a hit against all of these (see
    // Fighter::HurtboxRects).
    std::vector<RectBox> RectsForStance(const std::string& stance, double originX, double originY) const {
        std::vector<RectBox> out;
        for (const auto& part : PartsForStance(stance)) {
            out.emplace_back(originX + part.Box.CenterX, originY + part.Box.CenterY, part.Box.Width, part.Box.Height);
        }
        return out;
    }
};

} // namespace kakuge
