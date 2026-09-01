// engine/Projectile.h
// Standalone object - deliberately NOT a Fighter, just a moving rectangle
// with a lifetime and a reference to the move that defines its damage/
// hitstun/etc. 1:1 port of BattleSystem/Projectile.ps1.
#pragma once
#include <cmath>
#include "Boxes.h"
#include "MoveData.h"

namespace kakuge {

class Fighter;

class Projectile {
public:
    const MoveData* Move = nullptr;
    Fighter* Owner = nullptr;
    double PositionX = 0.0, PositionY = 0.0;
    double Speed = 500.0;
    int LifetimeFrames = 90;
    int Facing = 1;
    double Width = 30.0, Height = 30.0;
    bool HasHit = false;
    double StageMinX = -600.0, StageMaxX = 600.0;

    void Setup(const MoveData& move, Fighter* owner, double spawnX, double spawnY, int facing) {
        Move = &move;
        Owner = owner;
        Facing = facing;
        const auto& p = move.Projectile;
        Speed = p.present ? p.speed : 500.0;
        LifetimeFrames = p.present ? p.lifetime : 90;
        Width = p.present ? p.width : 30.0;
        Height = p.present ? p.height : 30.0;
        double offsetX = p.present ? p.spawnOffsetX : 40.0;
        double offsetY = p.present ? p.spawnOffsetY : -40.0;
        PositionX = spawnX + (offsetX * facing);
        PositionY = spawnY + offsetY;
    }

    // Returns false once the projectile should be removed.
    bool FrameStep(double dt) {
        PositionX += Speed * Facing * dt;
        LifetimeFrames -= 1;
        if (LifetimeFrames <= 0) return false;
        if (PositionX < (StageMinX - 100) || PositionX > (StageMaxX + 100)) return false;
        return true;
    }

    RectBox HitboxRect() const { return RectBox(PositionX, PositionY, Width, Height); }
};

} // namespace kakuge
