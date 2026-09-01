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

} // namespace kakuge
