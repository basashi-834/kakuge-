// engine/BattleSystem.h
// Per-match orchestrator. The ONLY place that calls Fighter::FrameStep(), so
// hit detection / pushbox resolution / projectile updates always run in one
// deterministic order every fixed 60Hz logic tick.
// 1:1 port of winforms-game/BattleSystem/BattleSystem.ps1.
//
// NOTE: BattleSystem must be heap-allocated (e.g. via std::unique_ptr) and
// left in place for the life of a match - Fighter::Opponent and CPUAI's
// Self/Opp are raw pointers into Player1/Player2, so moving or copying a
// live BattleSystem would dangle them.
#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include "Constants.h"
#include "Fighter.h"
#include "CPUAI.h"
#include "Projectile.h"
#include "CharacterStats.h"
#include "MoveData.h"

namespace kakuge {

class BattleSystem {
public:
    Fighter Player1;
    Fighter Player2;
    std::unique_ptr<CPUAI> CpuAI;
    std::vector<Projectile> Projectiles;

    int FramesLeft = 0;
    bool MatchActive = false;
    Fighter* Winner = nullptr;
    bool IsDraw = false;

    std::vector<EffectEvent> AllEffects;
    std::vector<std::string> AllSounds;

    // Screen shake, driven by counter-hits (see Fighter::ReceiveHit /
    // CounterKind). ShakeFrames counts down every Update() tick; the
    // renderer reads ShakeFrames/ShakeMagnitude each draw to offset the
    // camera by a random amount that decays to 0 as ShakeFrames reaches 0.
    int ShakeFrames = 0;
    double ShakeMagnitude = 0.0;

    // "FIGHT" banner shown for a brief moment right as a match starts (see
    // Screen 03's "FIGHT / 勝負" panel) - counts down to 0 once per match.
    int RoundStartFlashFrames = 0;
    static constexpr int RoundStartFlashDuration = 50;

    // New: combo tracking (unblocked hits only), feeds the HUD's "N HIT
    // COMBO" popup and the Result screen's "MAX COMBO" stat tile.
    int P1ComboCount = 0, P2ComboCount = 0;
    int P1MaxCombo = 0, P2MaxCombo = 0;

    // New: Training Mode (set by the caller right after StartMatch). The
    // round timer never runs out and neither player's death ends the
    // match, so the player can freely practice without interruption.
    // TrainingAutoHeal continuously regenerates Player2 (the practice
    // dummy) back to full HP - and revives them out of Dead/Knockdown -
    // so combos can be repeated without a manual reset.
    bool TrainingMode = false;
    bool TrainingAutoHeal = true;
    static constexpr int TrainingHealPerFrame = 6;

    void ResetHP() {
        for (Fighter* p : {&Player1, &Player2}) {
            p->CurrentHP = p->Stats.MaxHP;
            p->IsDead = false;
            if (p->SM.CurrentState == CharState::Dead || p->SM.CurrentState == CharState::Knockdown ||
                p->SM.CurrentState == CharState::WakeUp) {
                p->SM.ChangeState(CharState::Idle, "");
            }
        }
    }

    // See StageConstants in engine/Constants.h for how these (and the
    // starting positions below) were derived from the user's 1920x1080-
    // proportioned stage/character spec.
    static constexpr double StageMinX = StageConstants::StageMinX;
    static constexpr double StageMaxX = StageConstants::StageMaxX;

    void StartMatch(const CharacterStats& p1Stats, const std::unordered_map<std::string, MoveData>* p1Moves,
                     const CharacterStats& p2Stats, const std::unordered_map<std::string, MoveData>* p2Moves,
                     int roundTimeSeconds) {
        Player1 = Fighter();
        Player2 = Fighter();
        Player1.Setup(p1Stats, p1Moves);
        Player2.Setup(p2Stats, p2Moves);
        Player1.Opponent = &Player2;
        Player2.Opponent = &Player1;
        for (Fighter* p : {&Player1, &Player2}) {
            p->StageMinX = StageMinX;
            p->StageMaxX = StageMaxX;
        }
        Player1.PositionX = StageConstants::Player1StartX; Player1.PositionY = 0.0;
        Player2.PositionX = StageConstants::Player2StartX; Player2.PositionY = 0.0;
        Player1.Facing = Constants::FacingRight;
        Player2.Facing = Constants::FacingLeft;

        CpuAI = std::make_unique<CPUAI>(&Player2, &Player1);
        Projectiles.clear();
        FramesLeft = roundTimeSeconds * Constants::Fps;
        MatchActive = true;
        Winner = nullptr;
        IsDraw = false;
        ShakeFrames = 0;
        RoundStartFlashFrames = RoundStartFlashDuration;
    }

    // One fixed 60Hz logic tick. p1RawInput is real keyboard state from
    // GameScreen; the CPU's (or dummy-mode) input is produced internally.
    void Update(double dt, const RawInput& p1RawInput) {
        if (!MatchActive) return;
        AllEffects.clear();
        AllSounds.clear();

        RawInput p2Input = CpuAI->Decide();
        Player1.FrameStep(dt, p1RawInput);
        Player2.FrameStep(dt, p2Input);

        if (ShakeFrames > 0) ShakeFrames -= 1;
        if (RoundStartFlashFrames > 0) RoundStartFlashFrames -= 1;

        ResolvePushboxes();
        ResolveCombat(Player1, Player2);
        ResolveCombat(Player2, Player1);
        // A combo ends once its target is actionable again (walked out of
        // hitstun/knockdown without being hit again).
        if (Player1.SM.IsActionable()) P2ComboCount = 0;
        if (Player2.SM.IsActionable()) P1ComboCount = 0;
        UpdateProjectiles(dt);

        DrainFighterEvents(Player1);
        DrainFighterEvents(Player2);

        if (TrainingMode) {
            if (TrainingAutoHeal) {
                Fighter& dummy = Player2;
                if (dummy.CurrentHP < dummy.Stats.MaxHP) {
                    dummy.CurrentHP = std::min(dummy.Stats.MaxHP, dummy.CurrentHP + TrainingHealPerFrame);
                }
                if (dummy.IsDead && dummy.CurrentHP > 0) {
                    dummy.IsDead = false;
                    dummy.SM.ChangeState(CharState::Idle, "");
                }
            }
            return; // no KO/timeout match-end while training
        }

        if (Player1.IsDead || Player2.IsDead) {
            EndByKO();
            return;
        }

        FramesLeft -= 1;
        if (FramesLeft <= 0) EndByTimeout();
    }

    // Pushbox-vs-pushbox separation (the ONLY thing pushboxes are for).
    // Overlap is split half/half by default; a fighter already against
    // their stage edge doesn't move and the other absorbs the whole
    // separation instead (the spec's "画面端側のキャラクターを動かさず" rule -
    // stage edge == screen edge here, since the camera clamps to the
    // stage). A crouching/blocking fighter also holds their ground (an
    // earlier user request, kept), so a walked-into guard doesn't drift.
    void ResolvePushboxes() {
        RectBox r1 = Player1.PushboxRect();
        RectBox r2 = Player2.PushboxRect();
        if (!r1.Intersects(r2)) return;
        double overlapX = std::min(r1.Right(), r2.Right()) - std::max(r1.Left(), r2.Left());
        if (overlapX <= 0) return;
        // dir = the direction Player1 gets pushed (+1 right / -1 left);
        // Player2 is pushed the opposite way.
        double dir = (Player1.PositionX < Player2.PositionX) ? -1.0 : 1.0;

        bool p1Planted = Player1.IsPlanted();
        bool p2Planted = Player2.IsPlanted();
        double push1 = overlapX / 2.0, push2 = overlapX / 2.0;
        if (p1Planted && !p2Planted) { push1 = 0.0; push2 = overlapX; }
        else if (p2Planted && !p1Planted) { push2 = 0.0; push1 = overlapX; }

        // Room each fighter has left before their own wall, in the
        // direction they'd be pushed; any push that wouldn't fit is handed
        // to the other fighter so the full overlap still gets resolved
        // this frame instead of leaking through ClampToStage.
        double room1 = (dir > 0) ? (Player1.StageMaxX - Player1.PositionX) : (Player1.PositionX - Player1.StageMinX);
        double room2 = (dir > 0) ? (Player2.PositionX - Player2.StageMinX) : (Player2.StageMaxX - Player2.PositionX);
        room1 = std::max(0.0, room1);
        room2 = std::max(0.0, room2);
        if (push1 > room1) { push2 += push1 - room1; push1 = room1; }
        if (push2 > room2) { push1 = std::min(room1, push1 + (push2 - room2)); push2 = room2; }

        Player1.PositionX += push1 * dir;
        Player2.PositionX -= push2 * dir;
        Player1.ClampToStage();
        Player2.ClampToStage();
    }

    // Throw connect check (spec: center-to-center distance, not a hitbox).
    // Attacker must be grounded, in the throw's Active window, and facing
    // the defender; the defender must be grounded and throwable
    // (Fighter::IsThrowable). Throw invincibility is ReceiveHit's job.
    static bool ThrowInRange(const Fighter& attacker, const Fighter& defender) {
        const MoveData* move = attacker.CurrentMoveData;
        if (move == nullptr || attacker.SM.CurrentState != CharState::Attack) return false;
        if (MoveExecutor::GetPhase(*move, attacker.SM.CurrentFrame) != MovePhase::Active) return false;
        if (attacker.Stance() == "air" || !defender.IsThrowable()) return false;
        double dx = std::round(defender.PositionX) - std::round(attacker.PositionX);
        if (dx * attacker.Facing < 0) return false; // opponent is behind
        return std::abs(dx) <= move->ThrowRange;
    }

    // Attacker Hitbox vs defender Hurtbox (never hitbox-vs-hitbox, never
    // hurtbox-vs-hurtbox, never anything-vs-pushbox); throws use the
    // distance rule above instead.
    void ResolveCombat(Fighter& attacker, Fighter& defender) {
        if (attacker.CurrentMoveData == nullptr || attacker.SM.CurrentState != CharState::Attack) return;
        if (defender.IsDead) return;
        if (std::find(attacker.AlreadyHit.begin(), attacker.AlreadyHit.end(), &defender) != attacker.AlreadyHit.end()) return;
        const MoveData& move = *attacker.CurrentMoveData;

        bool connects = false;
        if (move.GuardType == Constants::GuardThrow) {
            connects = ThrowInRange(attacker, defender);
        } else {
            if (attacker.ActiveHitboxRects.empty()) return;
            std::vector<RectBox> hurtRects = defender.HurtboxRects();
            for (const auto& hb : attacker.ActiveHitboxRects) {
                for (const auto& hr : hurtRects) {
                    if (hb.Intersects(hr)) { connects = true; break; }
                }
                if (connects) break;
            }
        }
        if (!connects) return;

        attacker.AlreadyHit.push_back(&defender);
        bool wasAlreadyStunned = (defender.SM.CurrentState == CharState::Hitstun || defender.SM.CurrentState == CharState::Knockdown);
        HitResult result = defender.ReceiveHit(move, attacker);
        if (move.Hitstop > attacker.HitstopTimer) attacker.HitstopTimer = move.Hitstop;
        double gain = move.MeterGain;
        if (result.blocked) gain *= 0.5;
        attacker.Gauge.Add(gain);

        if (!result.blocked && !result.whiffed) {
            int* comboCount = (&attacker == &Player1) ? &P1ComboCount : &P2ComboCount;
            int* maxCombo = (&attacker == &Player1) ? &P1MaxCombo : &P2MaxCombo;
            *comboCount = wasAlreadyStunned ? (*comboCount + 1) : 1;
            *maxCombo = std::max(*maxCombo, *comboCount);

            int side = (&attacker == &Player1) ? 0 : 1; // scored by attacker -> attacker's screen edge
            if (result.counter == CounterKind::Counter) {
                ShakeFrames = 8;
                ShakeMagnitude = 6.0;
                AllEffects.push_back({"counter", defender.PositionX, defender.PositionY, side});
            } else if (result.counter == CounterKind::EffectiveCounter) {
                ShakeFrames = 18;
                ShakeMagnitude = 18.0;
                AllEffects.push_back({"effective_counter", defender.PositionX, defender.PositionY, side});
            }
        }
    }

    void UpdateProjectiles(double dt) {
        std::vector<Projectile> survivors;
        for (auto& proj : Projectiles) {
            bool alive = proj.FrameStep(dt);
            if (alive) {
                Fighter* target = (proj.Owner == &Player1) ? &Player2 : &Player1;
                if (!proj.HasHit && !target->IsDead) {
                    RectBox projRect = proj.HitboxRect();
                    for (const auto& hr : target->HurtboxRects()) {
                        if (projRect.Intersects(hr)) {
                            proj.HasHit = true;
                            target->ReceiveHit(*proj.Move, *proj.Owner);
                            alive = false;
                            break;
                        }
                    }
                }
            }
            if (alive) survivors.push_back(proj);
        }
        Projectiles = std::move(survivors);
    }

    void DrainFighterEvents(Fighter& fighter) {
        for (const auto& e : fighter.PendingEffects) AllEffects.push_back(e);
        fighter.PendingEffects.clear();
        for (const auto& s : fighter.PendingSounds) AllSounds.push_back(s);
        fighter.PendingSounds.clear();
        if (fighter.PendingProjectileValid) {
            const auto& req = fighter.PendingProjectileRequestData;
            Projectile proj;
            proj.StageMinX = StageMinX;
            proj.StageMaxX = StageMaxX;
            proj.Setup(*req.move, &fighter, req.x, req.y, req.facing);
            Projectiles.push_back(proj);
            fighter.PendingProjectileValid = false;
        }
    }

    void EndByKO() {
        if (!MatchActive) return;
        MatchActive = false;
        if (Player1.IsDead && Player2.IsDead) {
            IsDraw = true;
            Winner = nullptr;
        } else if (Player1.IsDead) {
            Winner = &Player2;
        } else {
            Winner = &Player1;
        }
    }

    void EndByTimeout() {
        if (!MatchActive) return;
        MatchActive = false;
        if (Player1.CurrentHP == Player2.CurrentHP) {
            IsDraw = true;
            Winner = nullptr;
        } else if (Player1.CurrentHP > Player2.CurrentHP) {
            Winner = &Player1;
        } else {
            Winner = &Player2;
        }
    }
};

} // namespace kakuge
