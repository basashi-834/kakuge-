// engine/MoveExecutor.h
// Frame management for whichever move is currently playing. Stateless:
// phase is derived purely from (move, currentFrame), so it can never
// desync from StateMachine.CurrentFrame.
//   Startup:  frame in [0, startup-1]
//   Active:   frame in [startup, startup+active-1]
//   Recovery: frame in [startup+active, startup+active+recovery-1]
//   Done:     frame >= startup+active+recovery
// 1:1 port of MoveData/MoveExecutor.ps1.
#pragma once
#include <string>
#include "MoveData.h"
#include "Boxes.h"
#include "Constants.h"

namespace kakuge {

enum class MovePhase { Startup, Active, Recovery, Done };

struct MoveExecutor {
    static MovePhase GetPhase(const MoveData& move, int frame) {
        if (frame < move.Startup) return MovePhase::Startup;
        if (frame < move.Startup + move.Active) return MovePhase::Active;
        if (frame < move.Startup + move.Active + move.Recovery) return MovePhase::Recovery;
        return MovePhase::Done;
    }

    static bool IsInvincible(const MoveData& move, int frame, const std::string& kind) {
        const auto& inv = move.Inv;
        if (inv.type == Constants::InvincibleNone) return false;
        if (frame < inv.start_frame || frame > inv.end_frame) return false;
        if (kind.empty()) return true;
        if (inv.type == Constants::InvincibleFull) return true;
        return inv.type == kind;
    }

    static bool CanCancel(const MoveData& move, int frame) {
        return move.IsCancelWindowOpen(frame);
    }

    // Every hitbox in move.Hitboxes is simultaneously active during the
    // move's Active window (they all share the move's single startup/
    // active/recovery timing - per-hitbox timing isn't modeled). Returns
    // world-space, facing-flipped rects; empty when no hitbox should be
    // live this frame.
    static std::vector<RectBox> GetActiveHitboxRects(const MoveData& move, int frame, int facing,
                                                       double originX, double originY) {
        std::vector<RectBox> out;
        if (GetPhase(move, frame) != MovePhase::Active) return out;
        for (const auto& box : move.Hitboxes) {
            double offsetX = box.offsetX * facing;
            double offsetY = box.offsetY;
            out.emplace_back(originX + offsetX, originY + offsetY, box.width, box.height);
        }
        return out;
    }
};

} // namespace kakuge
