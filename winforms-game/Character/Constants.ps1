# Character/Constants.ps1
# Shared enums / constant strings used across every module - kept in one
# place so combat rules (guard types, tags, invincibility kinds) aren't
# scattered as magic strings through Fighter / BattleSystem / CPUAI.

enum CharState {
    Idle
    WalkForward
    WalkBackward
    Crouch
    Jump
    Attack
    Block
    Hitstun
    Knockdown
    WakeUp
    Throw
    Dead
}

class Constants {
    static [int]   $Fps = 60

    static [string] $GuardHigh     = "High"
    static [string] $GuardLow      = "Low"
    static [string] $GuardOverhead = "Overhead"
    static [string] $GuardThrow    = "Throw"

    static [string] $HitNormal        = "Normal"
    static [string] $HitKnockdown     = "Knockdown"
    static [string] $HitHardKnockdown = "HardKnockdown"
    static [string] $HitLaunch        = "Launch"
    static [string] $HitWallBounce    = "WallBounce"
    static [string] $HitGroundBounce  = "GroundBounce"

    static [string] $InvincibleNone   = "None"
    static [string] $InvincibleFull   = "Full"
    static [string] $InvincibleStrike = "Strike"
    static [string] $InvincibleThrow  = "Throw"

    static [string] $TagLight      = "Light"
    static [string] $TagMedium     = "Medium"
    static [string] $TagHeavy      = "Heavy"
    static [string] $TagNormal     = "Normal"
    static [string] $TagSpecial    = "Special"
    static [string] $TagSuper      = "Super"
    static [string] $TagAntiAir    = "AntiAir"
    static [string] $TagProjectile = "Projectile"
    static [string] $TagLow        = "Low"
    static [string] $TagOverhead   = "Overhead"
    static [string] $TagThrow      = "Throw"
    static [string] $TagReversal   = "Reversal"

    static [int] $FacingRight = 1
    static [int] $FacingLeft  = -1

    static [int] $InputBufferLength = 20
    static [int] $CommandWindow     = 16
}
