// engine/Fighter.h
// The character controller. Owns physics + delegates to small focused
// helpers (StateMachine, MoveExecutor, InputBuffer, CommandParser,
// SuperGauge) rather than doing everything itself.
//
// Fighter never reads a keyboard itself: BattleSystem.Update() calls
// FrameStep(dt, rawInput) once per fixed 60Hz logic tick for player1 then
// player2, passing in already-resolved input (real keys for the human,
// CPUAI's synthesized input for the CPU).
// 1:1 port of winforms-game/Character/Fighter.ps1.
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "Constants.h"
#include "Boxes.h"
#include "CharacterStats.h"
#include "MoveData.h"
#include "MoveExecutor.h"
#include "StateMachine.h"
#include "SuperGauge.h"
#include "InputSystem.h"

namespace kakuge {

// side: 0 = this effect belongs to Player1 (drawn at the screen's left
// edge), 1 = Player2 (right edge) - see BattleSystem::ResolveCombat, which
// is the only place that knows attacker identity when a Counter/Effective
// Counter fires. Effects pushed straight from Fighter::PendingEffects (hit
// sparks, guard flashes) don't use this field, so it defaults harmlessly.
struct EffectEvent { std::string kind; double x = 0, y = 0; int side = 0; };

struct ProjectileRequest {
    const MoveData* move = nullptr;
    double x = 0, y = 0;
    int facing = 1;
};

// A hit lands as a "Counter" if the defender was themselves mid-attack in
// startup (committed but not yet active) when it landed, or an "Effective
// Counter" if it landed during the defender's recovery (their own move's
// active frames already came and went) - the classic fighting-game
// counter-hit bonus, split into two tiers per the user's spec.
enum class CounterKind { None, Counter, EffectiveCounter };

struct HitResult { bool blocked = false; bool whiffed = false; CounterKind counter = CounterKind::None; };

class Fighter {
public:
    CharacterStats Stats;
    const std::unordered_map<std::string, MoveData>* Moveset = nullptr;
    StateMachine SM;
    InputBuffer InputBuf;
    SuperGauge Gauge;
    HurtboxSet Hurtboxes;

    int CurrentHP = 1000;
    int Facing = 1;
    bool FacingLocked = false;
    bool IsDead = false;
    Fighter* Opponent = nullptr;

    double StageMinX = -130.0;
    double StageMaxX = 130.0;

    double PositionX = 0.0, PositionY = 0.0;
    double VelocityX = 0.0, VelocityY = 0.0;

    const MoveData* CurrentMoveData = nullptr;
    bool ProjectileSpawnedThisActivation = false;

    int HitstunTimer = 0, BlockstunTimer = 0, HitstopTimer = 0;
    int KnockdownTimer = 0, WakeupTimer = 0, ThrownTimer = 0, DashTimer = 0;
    bool IsCrouchingGuard = false;
    int FrameCounter = 0;

    // Scaled to match the renderer's ~440px-tall humanoid (platform/Draw.cpp's
    // kCharScale) so two fighters visually stop shoulder-to-shoulder instead
    // of overlapping/passing through each other.
    double PushboxHalfWidth = 36.4, PushboxHalfHeight = 71.5;
    // Every entry is simultaneously live this frame (a move can carry more
    // than one hitbox - e.g. separate hand/foot boxes on a multi-part
    // attack); empty means no hitbox is active. See MoveExecutor::
    // GetActiveHitboxRects.
    std::vector<RectBox> ActiveHitboxRects;
    std::vector<Fighter*> AlreadyHit;

    std::vector<EffectEvent> PendingEffects;
    std::vector<std::string> PendingSounds;
    bool PendingProjectileValid = false;
    ProjectileRequest PendingProjectileRequestData;
    bool LastHitBlocked = false;
    CounterKind LastCounterKind = CounterKind::None;

    ButtonsHeld HeldButtonsPrev;
    int LastForwardTapFrame = -999;

    static constexpr double GroundY = 0.0;
    static constexpr int DashInputWindow = 14;
    static constexpr int DashDuration = 14;
    static constexpr int KnockdownFrames = 40;
    static constexpr int HardKnockdownFrames = 60;
    static constexpr int WakeupFrames = 14;
    static constexpr int ThrownLockFrames = 20;

    void Setup(const CharacterStats& stats, const std::unordered_map<std::string, MoveData>* moveset) {
        Stats = stats;
        Hurtboxes = stats.Hurtboxes;
        Moveset = moveset;
        ResetForRound();
    }

    void ResetForRound() {
        CurrentHP = Stats.MaxHP;
        IsDead = false;
        CurrentMoveData = nullptr;
        HitstunTimer = BlockstunTimer = HitstopTimer = 0;
        KnockdownTimer = WakeupTimer = ThrownTimer = DashTimer = 0;
        Gauge.Value = 0.0;
        InputBuf.Clear();
        SM.ChangeState(CharState::Idle, "");
        ActiveHitboxRects.clear();
        AlreadyHit.clear();
        PendingEffects.clear();
        PendingSounds.clear();
        PendingProjectileValid = false;
        VelocityX = VelocityY = 0.0;
    }

    // -----------------------------------------------------------------
    // Main per-frame entry point (called by BattleSystem)
    // -----------------------------------------------------------------
    void FrameStep(double dt, const RawInput& raw) {
        if (IsDead) {
            VelocityY += Stats.Gravity * dt;
            PositionX += VelocityX * dt;
            PositionY += VelocityY * dt;
            if (PositionY > GroundY) { PositionY = GroundY; VelocityY = 0.0; }
            return;
        }

        FrameCounter += 1;
        int digit = InputBuffer::ComputeDigit(raw.Left, raw.Right, raw.Down, raw.Up, Facing);
        std::vector<std::string> pressed = NewlyPressedButtons(raw.Buttons);
        // Synthetic Throw: LP+LK held together (no dedicated throw key in
        // the 6-button control scheme).
        bool pressedHasLP = std::find(pressed.begin(), pressed.end(), "LP") != pressed.end();
        bool pressedHasLK = std::find(pressed.begin(), pressed.end(), "LK") != pressed.end();
        bool pressedHasThrow = std::find(pressed.begin(), pressed.end(), "Throw") != pressed.end();
        if (raw.Buttons.LP && raw.Buttons.LK && (pressedHasLP || pressedHasLK) && !pressedHasThrow) {
            pressed.push_back("Throw");
        }
        InputBuf.RecordFrame(FrameCounter, digit, pressed);

        if (HitstopTimer > 0) {
            HitstopTimer -= 1;
            return;
        }

        SM.Tick();
        HandleStateLogic(raw, pressed);
        ApplyPhysics(dt);
        ClampToStage();
        UpdateFacing();
    }

    std::vector<std::string> NewlyPressedButtons(const ButtonsHeld& held) {
        std::vector<std::string> result;
        for (const char* key : {"LP", "MP", "HP", "LK", "MK", "HK"}) {
            bool isHeld = held.Get(key);
            bool wasHeld = HeldButtonsPrev.Get(key);
            if (isHeld && !wasHeld) result.push_back(key);
        }
        HeldButtonsPrev = held;
        HeldButtonsPrev.Throw = false;
        return result;
    }

    bool IsHoldingBack(const RawInput& raw) const {
        if (raw.Left && Facing == Constants::FacingRight) return true;
        if (raw.Right && Facing == Constants::FacingLeft) return true;
        return false;
    }
    bool IsHoldingForward(const RawInput& raw) const {
        if (raw.Right && Facing == Constants::FacingRight) return true;
        if (raw.Left && Facing == Constants::FacingLeft) return true;
        return false;
    }

    // -----------------------------------------------------------------
    // State logic
    // -----------------------------------------------------------------
    void HandleStateLogic(const RawInput& raw, const std::vector<std::string>& pressed) {
        switch (SM.CurrentState) {
            case CharState::Hitstun: {
                HitstunTimer -= 1;
                if (HitstunTimer <= 0) SM.ChangeState(CharState::Idle, "");
                VelocityX = MoveToward(VelocityX, 0.0, 254.3 / Constants::Fps);
                break;
            }
            case CharState::Block: {
                BlockstunTimer -= 1;
                VelocityX = MoveToward(VelocityX, 0.0, 254.3 / Constants::Fps);
                if (BlockstunTimer <= 0) SM.ChangeState(CharState::Idle, "");
                break;
            }
            case CharState::Throw: {
                ThrownTimer -= 1;
                if (ThrownTimer <= 0) EnterKnockdown(false, 0);
                break;
            }
            case CharState::Knockdown: {
                KnockdownTimer -= 1;
                VelocityX = MoveToward(VelocityX, 0.0, 339.1 / Constants::Fps);
                if (KnockdownTimer <= 0) {
                    WakeupTimer = WakeupFrames;
                    SM.ChangeState(CharState::WakeUp, "");
                }
                break;
            }
            case CharState::WakeUp: {
                WakeupTimer -= 1;
                if (WakeupTimer <= 0) SM.ChangeState(CharState::Idle, "");
                break;
            }
            case CharState::Attack: {
                (void)TryStartMove(raw, pressed);
                ProgressMove();
                break;
            }
            case CharState::Jump: {
                if (!TryStartMove(raw, pressed)) {
                    double vx = 0.0;
                    if (raw.Right) vx = Stats.WalkForwardSpeed;
                    if (raw.Left) vx = vx - Stats.WalkForwardSpeed;
                    VelocityX = vx;
                }
                if (PositionY >= GroundY && VelocityY >= 0) {
                    PositionY = GroundY;
                    VelocityY = 0;
                    if (raw.Down) SM.ChangeState(CharState::Crouch, "");
                    else SM.ChangeState(CharState::Idle, "");
                }
                break;
            }
            default: {
                if (DashTimer > 0) DashTimer -= 1;
                if (!TryStartMove(raw, pressed)) HandleGroundMovement(raw);
                break;
            }
        }
    }

    static double MoveToward(double current, double target, double step) {
        if (current < target) return std::min(current + step, target);
        if (current > target) return std::max(current - step, target);
        return target;
    }

    void HandleGroundMovement(const RawInput& raw) {
        IsCrouchingGuard = raw.Down && IsHoldingBack(raw);

        if (raw.Up && !raw.Down) {
            VelocityY = Stats.JumpVelocity;
            SM.ChangeState(CharState::Jump, "");
            if (IsHoldingForward(raw)) VelocityX = Stats.WalkForwardSpeed;
            else if (IsHoldingBack(raw)) VelocityX = -Stats.WalkForwardSpeed;
            else VelocityX = 0.0;
            return;
        }

        if (raw.Down) {
            if (IsHoldingBack(raw)) SM.ChangeState(CharState::Block, "");
            else SM.ChangeState(CharState::Crouch, "");
            VelocityX = 0.0;
            return;
        }

        if (IsHoldingForward(raw)) {
            if ((FrameCounter - LastForwardTapFrame) <= DashInputWindow && DashTimer <= 0) {
                DashTimer = DashDuration;
            }
            LastForwardTapFrame = FrameCounter;
            double spd = Stats.WalkForwardSpeed;
            if (DashTimer > 0) spd = Stats.DashSpeed;
            VelocityX = spd * Facing;
            SM.ChangeState(CharState::WalkForward, "");
        } else if (IsHoldingBack(raw)) {
            // Only actually walk backward when that's where we're staying -
            // entering Block here must leave VelocityX at 0, or the walk
            // speed set in this same frame briefly carries over and the
            // character visibly slides during blockstun's decay instead of
            // planting immediately (same bug the crouch-guard fix below
            // guards against).
            if (Opponent != nullptr && Opponent->SM.CurrentState == CharState::Attack) {
                VelocityX = 0.0;
                SM.ChangeState(CharState::Block, "");
            } else {
                VelocityX = -Stats.WalkBackwardSpeed * Facing;
                SM.ChangeState(CharState::WalkBackward, "");
            }
        } else {
            VelocityX = 0.0;
            SM.ChangeState(CharState::Idle, "");
        }
    }

    // -----------------------------------------------------------------
    // Moves
    // -----------------------------------------------------------------
    bool TryStartMove(const RawInput& raw, const std::vector<std::string>& pressed) {
        (void)raw;
        if (pressed.empty() || Moveset == nullptr) return false;
        std::string stance = CurrentStance();

        std::vector<const MoveData*> superCandidates, specialCandidates, normalCandidates;

        for (const auto& kv : *Moveset) {
            const MoveData& move = kv.second;
            if (!move.InputCommand.empty() && CommandParser::Matches(InputBuf, move.InputCommand, move.Button, Constants::CommandWindow)) {
                if (move.HasTag(Constants::TagSuper)) superCandidates.push_back(&move);
                else if (move.HasTag(Constants::TagSpecial)) specialCandidates.push_back(&move);
                else normalCandidates.push_back(&move);
            }
        }
        for (const auto& btn : pressed) {
            for (const auto& kv : *Moveset) {
                const MoveData& move = kv.second;
                if (move.InputCommand.empty() && move.Button == btn && move.Stance == stance) {
                    normalCandidates.push_back(&move);
                }
            }
        }

        for (auto* group : {&superCandidates, &specialCandidates, &normalCandidates}) {
            for (const MoveData* move : *group) {
                if (CanStart(*move)) {
                    StartMove(*move);
                    return true;
                }
            }
        }
        return false;
    }

    std::string CurrentStance() const {
        if (SM.CurrentState == CharState::Jump) return "air";
        if (SM.CurrentState == CharState::Crouch) return "crouch";
        return "stand";
    }

    // Crouching (with or without holding back) and blocking are "planted" -
    // the fighter should hold their ground rather than get shoved back by
    // pushbox separation when the opponent walks into them (see
    // ResolvePushboxes in BattleSystem.h).
    bool IsPlanted() const {
        return SM.CurrentState == CharState::Crouch || SM.CurrentState == CharState::Block;
    }

    bool CanStart(const MoveData& move) const {
        if ((move.HasTag(Constants::TagSuper) || move.MeterCost > 0) && !Gauge.CanSpend(move.MeterCost)) {
            return false;
        }
        if (SM.CurrentState == CharState::Attack) {
            if (CurrentMoveData == nullptr) return false;
            // Cancel timing (cancelStartFrame..cancelEndFrame) still gates
            // WHEN you can act out of your current move, but not WHICH move
            // you cancel into - combos aren't a fixed move-A-to-move-B
            // whitelist, they fall out of timing: any move that hits before
            // the opponent's hitstun ends is a combo (see
            // BattleSystem::ResolveCombat's hitstun-based combo counter).
            return MoveExecutor::CanCancel(*CurrentMoveData, SM.CurrentFrame);
        }
        return true;
    }

    void StartMove(const MoveData& move) {
        if (move.MeterCost > 0) Gauge.Spend(move.MeterCost);
        CurrentMoveData = &move;
        ProjectileSpawnedThisActivation = false;
        FacingLocked = true;
        // Grounded moves (fireball, dragon punch, normals, ...) should
        // start from a dead stop even if the player walk-canceled or
        // dash-canceled into them - otherwise whatever VelocityX was
        // carried from the previous frame's movement keeps being applied
        // every frame for the entire move (ApplyPhysics doesn't touch
        // VelocityX during Attack), making the character visibly drift/
        // slide forward through the whole animation. Airborne moves (jump
        // attack) keep their momentum, matching normal jump-arc physics.
        if (CurrentStance() != "air") VelocityX = 0.0;
        SM.ChangeState(CharState::Attack, move.Id);
        PendingSounds.push_back("attack");
    }

    void ProgressMove() {
        if (CurrentMoveData == nullptr) { SM.ChangeState(CharState::Idle, ""); return; }
        int frame = SM.CurrentFrame;
        const MoveData& move = *CurrentMoveData;
        MovePhase phase = MoveExecutor::GetPhase(move, frame);

        if (phase == MovePhase::Active) {
            if (ActiveHitboxRects.empty()) {
                ActiveHitboxRects = MoveExecutor::GetActiveHitboxRects(move, frame, Facing, PositionX, PositionY);
                AlreadyHit.clear();
            }
        } else {
            ActiveHitboxRects.clear();
        }

        if (phase == MovePhase::Active && move.HasTag(Constants::TagProjectile) && !ProjectileSpawnedThisActivation) {
            ProjectileSpawnedThisActivation = true;
            PendingProjectileValid = true;
            PendingProjectileRequestData = {&move, PositionX, PositionY, Facing};
        }

        if (phase == MovePhase::Done) {
            ActiveHitboxRects.clear();
            FacingLocked = false;
            bool wasAir = PositionY < (GroundY - 1.0);
            CurrentMoveData = nullptr;
            if (wasAir) SM.ChangeState(CharState::Jump, "");
            else SM.ChangeState(CharState::Idle, "");
        }
    }

    // -----------------------------------------------------------------
    // Combat resolution (called by BattleSystem)
    // -----------------------------------------------------------------
    bool IsInvincibleAgainst(const std::string& kind) const {
        if (SM.CurrentState == CharState::Attack && CurrentMoveData != nullptr) {
            return MoveExecutor::IsInvincible(*CurrentMoveData, SM.CurrentFrame, kind);
        }
        return false;
    }

    HitResult ReceiveHit(const MoveData& move, Fighter& attacker) {
        if (IsDead) return {false, true};
        std::string invKind = Constants::InvincibleStrike;
        if (move.GuardType == Constants::GuardThrow) invKind = Constants::InvincibleThrow;
        if (IsInvincibleAgainst(invKind)) return {false, true};

        // Was *this* fighter itself mid-attack (committed to their own move)
        // at the moment the hit landed? Captured before anything below
        // changes SM.CurrentState, since that's what StartMove/ProgressMove
        // would otherwise clobber.
        CounterKind counter = CounterKind::None;
        if (SM.CurrentState == CharState::Attack && CurrentMoveData != nullptr) {
            MovePhase myPhase = MoveExecutor::GetPhase(*CurrentMoveData, SM.CurrentFrame);
            if (myPhase == MovePhase::Startup) counter = CounterKind::Counter;
            else if (myPhase == MovePhase::Recovery) counter = CounterKind::EffectiveCounter;
        }

        bool blocked = false;
        if (move.GuardType != Constants::GuardThrow) blocked = CheckGuard(move);
        if (blocked) counter = CounterKind::None; // guarding is never a counter hit

        HitstopTimer = move.Hitstop;
        if (blocked) {
            int chip = static_cast<int>(std::lround(move.Damage * move.ChipDamagePercent));
            CurrentHP = std::max(0, CurrentHP - chip);
            BlockstunTimer = move.Blockstun;
            SM.ChangeState(CharState::Block, "");
            Gauge.Add(move.MeterGain * 0.5);
            ApplyKnockback(move, attacker, true);
            PendingEffects.push_back({"guard", PositionX, PositionY});
            PendingSounds.push_back("block");
        } else {
            CurrentHP = std::max(0, CurrentHP - move.Damage);
            Gauge.Add(move.MeterGain);
            ApplyKnockback(move, attacker, false);
            if (move.GuardType == Constants::GuardThrow) {
                ThrownTimer = ThrownLockFrames;
                SM.ChangeState(CharState::Throw, "");
            } else if (move.HitOutcome == Constants::HitNormal) {
                HitstunTimer = move.Hitstun;
                SM.ChangeState(CharState::Hitstun, "");
            } else {
                EnterKnockdown(move.HitOutcome == std::string(Constants::HitHardKnockdown), move.Hitstun);
            }
            std::string fx = "hit";
            if (move.HasTag(Constants::TagSuper)) fx = "super";
            else if (move.HasTag(Constants::TagSpecial)) fx = "special";
            else if (move.HasTag(Constants::TagHeavy)) fx = "heavy_hit";
            PendingEffects.push_back({fx, PositionX, PositionY});
            PendingSounds.push_back("hit");
        }

        if (CurrentHP <= 0 && !IsDead) {
            IsDead = true;
            SM.ChangeState(CharState::Dead, "");
            PendingSounds.push_back("ko");
        }
        LastHitBlocked = blocked;
        LastCounterKind = counter;
        return {blocked, false, counter};
    }

    void EnterKnockdown(bool hard, int customFrames) {
        if (customFrames > 0) KnockdownTimer = customFrames;
        else if (hard) KnockdownTimer = HardKnockdownFrames;
        else KnockdownTimer = KnockdownFrames;
        SM.ChangeState(CharState::Knockdown, "");
    }

    bool CheckGuard(const MoveData& move) const {
        // Guard is decided from THIS FRAME's own most-recent recorded input
        // (the last entry in InputBuf), since Fighter no longer reads a live
        // keyboard itself - BattleSystem always calls FrameStep() (which
        // records input) before resolving hits for that same tick.
        if (InputBuf.History.empty()) return false;
        const auto& lastEntry = InputBuf.History.back();
        int digit = lastEntry.digit;
        bool holdingBack = digit == 1 || digit == 4 || digit == 7;
        bool inGuardPosture = (SM.CurrentState == CharState::Block) || (SM.IsActionable() && holdingBack);
        if (!inGuardPosture || !holdingBack) return false;

        if (move.GuardType == Constants::GuardHigh) return true;
        if (move.GuardType == Constants::GuardLow) {
            return IsCrouchingGuard || SM.CurrentState == CharState::Crouch;
        }
        if (move.GuardType == Constants::GuardOverhead) {
            return !(IsCrouchingGuard || SM.CurrentState == CharState::Crouch);
        }
        return false;
    }

    void ApplyKnockback(const MoveData& move, const Fighter& attacker, bool isBlock) {
        // Knockback pushes the defender AWAY from the attacker, i.e. in the
        // direction the attacker is facing (they're facing toward the
        // defender when the hit lands) - NOT the negation of it. The old
        // `-attacker.Facing` pulled the defender back toward the attacker
        // instead, which fought the pushbox-separation logic every single
        // frame and dragged both fighters around unpredictably (reported
        // as "the attacker moves forward when a hit connects").
        double dir = attacker.Facing;
        double kx = move.KnockbackX;
        if (isBlock) kx *= 0.4;
        VelocityX = kx * dir;
        if (!isBlock && move.KnockbackY != 0.0) {
            VelocityY = -std::abs(move.KnockbackY);
        }
    }

    // -----------------------------------------------------------------
    // Physics / misc
    // -----------------------------------------------------------------
    void ApplyPhysics(double dt) {
        if (SM.CurrentState == CharState::Jump || PositionY < (GroundY - 0.01)) {
            VelocityY += Stats.Gravity * dt;
        } else {
            VelocityY = 0.0;
        }
        PositionX += VelocityX * dt;
        PositionY += VelocityY * dt;
        if (PositionY > GroundY) { PositionY = GroundY; VelocityY = 0.0; }
        if (!ActiveHitboxRects.empty() && CurrentMoveData != nullptr) {
            ActiveHitboxRects = MoveExecutor::GetActiveHitboxRects(*CurrentMoveData, SM.CurrentFrame, Facing, PositionX, PositionY);
        }
    }

    void ClampToStage() {
        if (PositionX < StageMinX) PositionX = StageMinX;
        if (PositionX > StageMaxX) PositionX = StageMaxX;
    }

    void UpdateFacing() {
        if (FacingLocked || Opponent == nullptr) return;
        if (!SM.IsActionable()) return;
        if (Opponent->PositionX >= PositionX) Facing = Constants::FacingRight;
        else Facing = Constants::FacingLeft;
    }

    std::string Stance() const {
        if (SM.CurrentState == CharState::Jump || PositionY < (GroundY - 0.01)) return "air";
        if (SM.CurrentState == CharState::Crouch || IsCrouchingGuard) return "crouch";
        return "stand";
    }

    std::vector<RectBox> HurtboxRects() const { return Hurtboxes.RectsForStance(Stance(), PositionX, PositionY); }
    RectBox PushboxRect() const {
        return RectBox(PositionX, PositionY - PushboxHalfHeight, PushboxHalfWidth * 2.0, PushboxHalfHeight * 2.0);
    }

    const MoveData* GetMove(const std::string& id) const {
        if (Moveset == nullptr) return nullptr;
        auto it = Moveset->find(id);
        return it == Moveset->end() ? nullptr : &it->second;
    }

    struct DebugInfoT {
        std::string state, move;
        int frame = 0, hp = 0;
        double gauge = 0, velocityX = 0, velocityY = 0;
        int hitstun = 0, blockstun = 0, hitstop = 0;
        double positionX = 0, positionY = 0;
    };

    DebugInfoT DebugInfo() const {
        DebugInfoT d;
        d.state = CharStateName(SM.CurrentState);
        d.move = SM.CurrentMove;
        d.frame = SM.CurrentFrame;
        d.hp = CurrentHP;
        d.gauge = Gauge.Value;
        d.velocityX = VelocityX;
        d.velocityY = VelocityY;
        d.hitstun = HitstunTimer;
        d.blockstun = BlockstunTimer;
        d.positionX = PositionX;
        d.positionY = PositionY;
        d.hitstop = HitstopTimer;
        return d;
    }
};

} // namespace kakuge
