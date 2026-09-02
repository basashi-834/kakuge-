// engine/Constants.h
// Shared enums / constant strings used across every engine module - kept in
// one place so combat rules (guard types, tags, invincibility kinds) aren't
// scattered as magic strings through Fighter / BattleSystem / CPUAI.
// 1:1 port of winforms-game/Character/Constants.ps1.
#pragma once
#include <string>

namespace kakuge {

enum class CharState {
    Idle, WalkForward, WalkBackward, Crouch, Jump, Attack, Block,
    Hitstun, Knockdown, WakeUp, Throw, Dead
};

inline std::string CharStateName(CharState s) {
    switch (s) {
        case CharState::Idle: return "Idle";
        case CharState::WalkForward: return "WalkForward";
        case CharState::WalkBackward: return "WalkBackward";
        case CharState::Crouch: return "Crouch";
        case CharState::Jump: return "Jump";
        case CharState::Attack: return "Attack";
        case CharState::Block: return "Block";
        case CharState::Hitstun: return "Hitstun";
        case CharState::Knockdown: return "Knockdown";
        case CharState::WakeUp: return "WakeUp";
        case CharState::Throw: return "Throw";
        case CharState::Dead: return "Dead";
    }
    return "Idle";
}

// CPU dummy-mode control for the pause menu toggle (new: CPU / Stand /
// Crouch / Jump - lets the player freeze P2 into a fixed posture for
// practice, matching the user's request to switch P2's behavior from the
// pause menu).
enum class DummyMode { CPU, Stand, Crouch, Jump };

struct Constants {
    static constexpr int Fps = 60;

    static constexpr const char* GuardHigh = "High";
    static constexpr const char* GuardLow = "Low";
    static constexpr const char* GuardOverhead = "Overhead";
    static constexpr const char* GuardThrow = "Throw";

    static constexpr const char* HitNormal = "Normal";
    static constexpr const char* HitKnockdown = "Knockdown";
    static constexpr const char* HitHardKnockdown = "HardKnockdown";
    static constexpr const char* HitLaunch = "Launch";
    static constexpr const char* HitWallBounce = "WallBounce";
    static constexpr const char* HitGroundBounce = "GroundBounce";

    static constexpr const char* InvincibleNone = "None";
    static constexpr const char* InvincibleFull = "Full";
    static constexpr const char* InvincibleStrike = "Strike";
    static constexpr const char* InvincibleThrow = "Throw";

    static constexpr const char* TagLight = "Light";
    static constexpr const char* TagMedium = "Medium";
    static constexpr const char* TagHeavy = "Heavy";
    static constexpr const char* TagNormal = "Normal";
    static constexpr const char* TagSpecial = "Special";
    static constexpr const char* TagSuper = "Super";
    static constexpr const char* TagAntiAir = "AntiAir";
    static constexpr const char* TagProjectile = "Projectile";
    static constexpr const char* TagLow = "Low";
    static constexpr const char* TagOverhead = "Overhead";
    static constexpr const char* TagThrow = "Throw";
    static constexpr const char* TagReversal = "Reversal";

    static constexpr int FacingRight = 1;
    static constexpr int FacingLeft = -1;

    static constexpr int InputBufferLength = 20;
    static constexpr int CommandWindow = 16;
};

// ---------------------------------------------------------------------
// Stage / start-position tuning constants (user-specified proportions).
// ---------------------------------------------------------------------
// World units here are the same unit the whole engine/renderer already
// used before this: "canvas pixels at camera zoom 1.0" (see platform/
// Draw.h's ToScreenX/ToScreenY) - not literal screen pixels, since the
// game always renders through a fixed 384x224 low-res pixel-art canvas
// (platform/Layout.h's VirtualW/VirtualH) that then gets nearest-neighbor
// upscaled to the real window, whatever the window's actual resolution.
//
// History: this struct originally reproduced - as ratios against the
// 384-wide canvas - a user-specified *1920x1080* reference layout (stage
// 3200px wide, ground at y=920, player height 760px/70%, start positions
// x=1250/x=1850), scaled by 384/1920 = 0.2. That was later superseded by
// a second, precise spec given *directly* in 384x224 canvas terms (no
// external reference resolution to scale from) - CHARACTER_VISUAL_HEIGHT
// ~88px/39.3%, PLAYER1_START_X/PLAYER2_START_X at screen x=116/268 (the
// canvas's 30%/70% points), ~150px center-to-center distance. Character
// height/ground-line constants derived from that spec (kCharScale/
// OriginY) live in platform/Draw.h/.cpp since those are rendering
// concerns, not gameplay ones; Player1StartX/Player2StartX below (a
// gameplay concern - Fighter/BattleSystem read these) were recomputed to
// match: symmetric world positions +-76 put their zoom-1.0 screen
// projection (see kCameraPaddingWorld's comment in platform/Draw.cpp)
// exactly at 116/268 with a 152-unit center-to-center distance, matching
// that second spec almost exactly (150 recommended, 152 exact from its
// own 116/268 example numbers). Stage width wasn't covered by either
// later spec, so it's kept from the original 1920x1080-derived value.
struct StageConstants {
    // Original 1920x1080 reference layout (informational only, not read by
    // any code - see the history note above; StageWidth below is the one
    // value still actually derived from it).
    static constexpr double RefCameraViewWidth = 1920.0;
    static constexpr double RefStageWidth = 3200.0;
    static constexpr double RefGroundY = 920.0;
    static constexpr double RefScreenHeight = 1080.0;
    static constexpr double RefPlayerHeight = 760.0;
    static constexpr double RefPlayer1StartX = 1250.0;
    static constexpr double RefPlayer2StartX = 1850.0;

    // STAGE_WIDTH: 3200 * 0.2 = 640, centered on world X=0 (StageMinX/
    // StageMaxX), same convention Fighter/BattleSystem already used.
    static constexpr double StageWidth = 640.0;
    static constexpr double StageMinX = -StageWidth / 2.0;
    static constexpr double StageMaxX = StageWidth / 2.0;

    // PLAYER1_START_X / PLAYER2_START_X: symmetric +-76 around world X=0 -
    // see the history note above for why (zoom-1.0 screen projection lands
    // on the later 384x224-native spec's 116/268 canvas positions).
    static constexpr double Player1StartX = -76.0;
    static constexpr double Player2StartX = 76.0;
    static constexpr double PlayerStartDistance = Player2StartX - Player1StartX; // 152

    // PLAYER_HEIGHT ratio - superseded from the original 760/1080 (0.704)
    // to the later 384x224-native spec's 88/224 (~0.393); the actual pixel
    // constant (kCharScale) lives in platform/Draw.cpp since character
    // draw size is a rendering concern, recorded here too so the ratio
    // itself isn't duplicated-and-drifts if either side gets retuned later.
    static constexpr double PlayerHeightRatio = 88.0 / 224.0;
};

} // namespace kakuge
