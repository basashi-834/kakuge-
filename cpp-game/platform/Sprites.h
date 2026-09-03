// platform/Sprites.h
// Optional bitmap sprite art for fighters, layered on top of the existing
// procedural pixel-art line-art humanoid (DrawHumanoid in Draw.cpp) rather
// than replacing it: any pose a character doesn't have art for keeps
// drawing with the line-art renderer exactly as before, so the game works
// (and looks consistent) with zero sprite files present, and characters
// can be given real art one pose at a time.
//
// Per-character sprite files live under <dataDir>/sprites/<charId>/ (same
// base/user-override split as DataManager's character/move JSON):
//   stand_0.png .. stand_3.png   4-frame standing idle loop
//   crouch.png                  crouching
//   punch.png / kick.png        grounded normal-attack poses
//   jump.png                    airborne, not attacking
//   jumppunch.png / jumpkick.png airborne attack poses
//   hitstun.png                 getting hit (のけぞり)
//   knockdown.png               knocked down (倒れこみ)
// Any file that doesn't exist leaves that pose's slot null. Block/Throw/
// WakeUp/Dead have no sprite slot (not part of the requested pose set) and
// always use the line-art renderer.
#pragma once
#include "GdiPlusInclude.h"
#include <string>
#include <memory>
#include <filesystem>

namespace kakuge {
namespace fs = std::filesystem;

struct CharacterSpriteSet {
    std::unique_ptr<Gdiplus::Image> Stand[4];
    std::unique_ptr<Gdiplus::Image> Crouch;
    std::unique_ptr<Gdiplus::Image> Punch;
    std::unique_ptr<Gdiplus::Image> Kick;
    std::unique_ptr<Gdiplus::Image> Jump;
    std::unique_ptr<Gdiplus::Image> JumpPunch;
    std::unique_ptr<Gdiplus::Image> JumpKick;
    std::unique_ptr<Gdiplus::Image> Hitstun;
    std::unique_ptr<Gdiplus::Image> Knockdown;
};

// Loads (once) and caches a character's sprite set. Safe to call every
// frame - only actually hits disk the first time a given charId is seen,
// or again after ClearSpriteCache().
const CharacterSpriteSet& GetCharacterSprites(const std::string& charId, const fs::path& baseDir, const fs::path& userDir);

// Drops every cached sprite set, so the next GetCharacterSprites() call
// re-checks disk - call this if sprite files might have changed since
// they were loaded (e.g. once the Character Editor can assign them).
void ClearSpriteCache();

// How many game ticks (60/sec) each of the 4 standing-idle frames holds
// before advancing to the next - 1 matches the user's literal "switch
// every 1 frame" spec (a fast, jittery cycle at 60fps); raise this if real
// art makes that read as flicker rather than a breathing idle loop once
// it's in and can be judged visually.
constexpr int kIdleTicksPerFrame = 1;

} // namespace kakuge
