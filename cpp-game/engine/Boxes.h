// engine/Boxes.h
// Hitbox / Hurtbox / Pushbox are plain axis-aligned rectangles (world-space,
// centered on X, center+halfheight style on Y) - pure logic, no platform
// dependency, exercised directly by tests/EngineTests.cpp.
// 1:1 port of winforms-game/Character/Boxes.ps1.
#pragma once
#include <cmath>
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
// named parts rather than one rectangle. Local coordinates: X=0 is the
// character's center (positive = the way they're facing; flipped by the
// caller for a left-facing fighter), Y=0 is the feet line, negative = up.
// RectBox is center-based, so the spec's top-left (x, y, w, h) boxes are
// stored as (x + w/2, y + h/2, w, h).
//
// Defaults are the user's 384x224-native collision spec (see GameSpec in
// Constants.h), verbatim - three parts per stance, so a low hits legs and
// a jab hits the head/torso rather than everything hitting one whole-body
// box. Standing outer extent: 30 wide x 88 tall (spec: 28-34 x 86-88),
// deliberately narrower than the ~55px visual so sleeves/belt/headband/a
// cocked fist don't count as the body. Per-character overrides live in
// the character JSON ("hurtboxes"), editable in the Character Editor.
//
//   Stand   head  (-9,-88,18,16)  torso (-15,-72,30,30)  legs (-14,-42,28,42)
//   Crouch  head  (-9,-57,18,14)  torso (-15,-48,30,24)  legs (-17,-24,34,24)
//           (57 tall in total - spec: 50-58; head overlaps torso by 5,
//            which the spec explicitly allows)
//   Air     head  (-9,-72,18,16)  torso (-14,-56,28,28)  legs (-12,-28,24,28)
//           (tucked jump pose, 72 tall - smaller than standing on purpose)
struct HurtboxSet {
    std::vector<HurtboxPart> Stand{
        {"head",  RectBox{0, -80, 18, 16}},
        {"torso", RectBox{0, -57, 30, 30}},
        {"leg",   RectBox{0, -21, 28, 42}},
    };
    std::vector<HurtboxPart> Crouch{
        {"head",  RectBox{0, -50, 18, 14}},
        {"torso", RectBox{0, -36, 30, 24}},
        {"leg",   RectBox{0, -12, 34, 24}},
    };
    std::vector<HurtboxPart> Air{
        {"head",  RectBox{0, -64, 18, 16}},
        {"torso", RectBox{0, -42, 28, 28}},
        {"leg",   RectBox{0, -14, 24, 28}},
    };

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

    // Places a list of local-space parts into world space at (originX,
    // originY), mirroring X for a left-facing fighter (facing = -1). The
    // origin is snapped to whole pixels first so the resulting boxes sit
    // on integer coordinates even though movement itself is fractional
    // (the spec asks for integer-px collision rects; the renderer snaps
    // the same way, so debug boxes line up with the drawn character).
    static std::vector<RectBox> PlaceParts(const std::vector<HurtboxPart>& parts, int facing, double originX, double originY) {
        std::vector<RectBox> out;
        double ox = std::round(originX), oy = std::round(originY);
        int f = facing < 0 ? -1 : 1;
        for (const auto& part : parts) {
            out.emplace_back(ox + part.Box.CenterX * f, oy + part.Box.CenterY, part.Box.Width, part.Box.Height);
        }
        return out;
    }

    // World-space rects for every part in the defender's current stance -
    // hit detection checks a hit against all of these (see
    // Fighter::HurtboxRects, which also layers per-move overrides on top).
    std::vector<RectBox> RectsForStance(const std::string& stance, int facing, double originX, double originY) const {
        return PlaceParts(PartsForStance(stance), facing, originX, originY);
    }
};

} // namespace kakuge
