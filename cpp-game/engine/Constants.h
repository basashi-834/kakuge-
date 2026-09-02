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
// The values below reproduce - as ratios against that 384-wide canvas,
// which plays the role of "camera view width" at zoom 1.0 - the exact
// proportions of a user-specified 1920x1080 reference layout (stage
// 3200px wide, ground at y=920, player height 760px/70% of screen,
// start positions x=1250/x=1850 i.e. 600px apart), rather than that
// layout's literal pixel values, so the game keeps its pixel-art look
// (see kCharScale/OriginY in platform/Draw.h/.cpp for the matching
// on-screen size/ground-line constants - character height and ground
// position are rendering concerns, not gameplay ones, so they live
// there instead of here; PlayerHeightRatio below is recorded purely as
// the source ratio those were derived from).
//
// Scale factor used throughout: 384 / 1920 = 0.2 (canvas width relative
// to the reference layout's camera view width).
struct StageConstants {
    // Reference layout this was derived from (1920x1080, informational -
    // not read by any code, kept so the ratios above stay traceable to
    // their source spec without re-deriving them by hand).
    static constexpr double RefCameraViewWidth = 1920.0;
    static constexpr double RefStageWidth = 3200.0;
    static constexpr double RefGroundY = 920.0;
    static constexpr double RefScreenHeight = 1080.0;
    // Halved from the original spec's 760 (-> 380) per the user's later
    // request to shrink the character to about half its size; kept here
    // so PlayerHeightRatio below stays in sync with platform/Draw.cpp's
    // kCharScale rather than silently drifting from it.
    static constexpr double RefPlayerHeight = 380.0;
    static constexpr double RefPlayer1StartX = 1250.0;
    static constexpr double RefPlayer2StartX = 1850.0;

    // STAGE_WIDTH: 3200 * 0.2 = 640, centered on world X=0 (StageMinX/
    // StageMaxX), same convention Fighter/BattleSystem already used.
    static constexpr double StageWidth = 640.0;
    static constexpr double StageMinX = -StageWidth / 2.0;
    static constexpr double StageMaxX = StageWidth / 2.0;

    // PLAYER1_START_X / PLAYER2_START_X: the reference spec's 1250/1850
    // (out of a 0-3200 stage, center 1600) re-centered on 0 and scaled by
    // 0.2 - reproduces both the 600-unit start distance (*0.2 = 120) and
    // the (slightly asymmetric, as given) margins to each stage edge.
    static constexpr double Player1StartX = -70.0;
    static constexpr double Player2StartX = 50.0;
    static constexpr double PlayerStartDistance = Player2StartX - Player1StartX; // 120

    // PLAYER_HEIGHT ratio (760/1080 = 0.704 of screen height) - the actual
    // pixel constant derived from it (kCharScale) lives in platform/
    // Draw.cpp since character draw size is a rendering concern; recorded
    // here too so the ratio itself isn't duplicated-and-drifts if either
    // side gets retuned later.
    static constexpr double PlayerHeightRatio = RefPlayerHeight / RefScreenHeight;
};

} // namespace kakuge
