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
#include <cmath>
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

    // Hitboxes live this frame, as world-space, facing-flipped rects
    // (empty when none should be). Two sources, in priority order:
    //   1. a FrameBoxSet covering this frame with "hitboxes" set - the
    //      per-frame override, honored regardless of startup/active/
    //      recovery phase (an empty list is a valid override meaning "no
    //      hitbox on these frames");
    //   2. otherwise move.Hitboxes, all simultaneously active during the
    //      move's shared Active window.
    // Throws (GuardType "Throw") never connect through these - see
    // BattleSystem::ResolveCombat's distance check - but still report them
    // so the debug overlay can draw the throw's reach in its own color.
    // The origin is snapped to whole pixels (see HurtboxSet::PlaceParts).
    static std::vector<RectBox> GetActiveHitboxRects(const MoveData& move, int frame, int facing,
                                                       double originX, double originY) {
        std::vector<RectBox> out;
        const std::vector<HitboxDef>* source = nullptr;
        if (const FrameBoxSet* fb = move.FrameBoxesAt(frame); fb != nullptr && fb->hasHitboxes) {
            source = &fb->hitboxes;
        } else if (GetPhase(move, frame) == MovePhase::Active) {
            source = &move.Hitboxes;
        }
        if (source == nullptr) return out;
        double ox = std::round(originX), oy = std::round(originY);
        int f = facing < 0 ? -1 : 1;
        for (const auto& box : *source) {
            out.emplace_back(ox + box.offsetX * f, oy + box.offsetY, box.width, box.height);
        }
        return out;
    }

    // Whether the move is in a window where it can connect at all this
    // frame - Active phase, or a per-frame hitbox override that's live.
    static bool HasLiveHitboxes(const MoveData& move, int frame) {
        if (const FrameBoxSet* fb = move.FrameBoxesAt(frame); fb != nullptr && fb->hasHitboxes) {
            return !fb->hitboxes.empty();
        }
        return GetPhase(move, frame) == MovePhase::Active && !move.Hitboxes.empty();
    }
};

} // namespace kakuge
