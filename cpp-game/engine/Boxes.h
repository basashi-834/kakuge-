// engine/Boxes.h
// Hitbox / Hurtbox / Pushbox are plain axis-aligned rectangles (world-space,
// centered on X, center+halfheight style on Y) - pure logic, no platform
// dependency, exercised directly by tests/EngineTests.cpp.
// 1:1 port of winforms-game/Character/Boxes.ps1.
#pragma once
#include <string>

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

// Per-stance Hurtbox shapes (立ち/しゃがみ/ジャンプで形状変化). Y is measured
// with 0 = ground, negative = up (matches the JSON hitbox offset convention).
// Sized to match the renderer's ~440px-tall humanoid (platform/Draw.cpp's
// kCharScale) so hits visually connect where they appear to.
struct HurtboxSet {
    RectBox Stand{0, -205, 188.6, 410};
    RectBox Crouch{0, -127.1, 188.6, 254.2};
    RectBox Air{0, -205, 188.6, 410};

    RectBox ForStance(const std::string& stance, double originX, double originY) const {
        const RectBox* src = &Stand;
        if (stance == "crouch") src = &Crouch;
        else if (stance == "air") src = &Air;
        return RectBox(originX + src->CenterX, originY + src->CenterY, src->Width, src->Height);
    }
};

} // namespace kakuge
