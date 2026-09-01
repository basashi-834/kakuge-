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

    // New: combo tracking (unblocked hits only), feeds the HUD's "N HIT
    // COMBO" popup and the Result screen's "MAX COMBO" stat tile.
    int P1ComboCount = 0, P2ComboCount = 0;
    int P1MaxCombo = 0, P2MaxCombo = 0;

    static constexpr double StageMinX = -560.0;
    static constexpr double StageMaxX = 560.0;

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
        Player1.PositionX = -220.0; Player1.PositionY = 0.0;
        Player2.PositionX = 220.0; Player2.PositionY = 0.0;
        Player1.Facing = Constants::FacingRight;
        Player2.Facing = Constants::FacingLeft;

        CpuAI = std::make_unique<CPUAI>(&Player2, &Player1);
        Projectiles.clear();
        FramesLeft = roundTimeSeconds * Constants::Fps;
        MatchActive = true;
        Winner = nullptr;
        IsDraw = false;
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

        if (Player1.IsDead || Player2.IsDead) {
            EndByKO();
            return;
        }

        FramesLeft -= 1;
        if (FramesLeft <= 0) EndByTimeout();
    }

    void ResolvePushboxes() {
        RectBox r1 = Player1.PushboxRect();
        RectBox r2 = Player2.PushboxRect();
        if (!r1.Intersects(r2)) return;
        double overlapX = std::min(r1.Right(), r2.Right()) - std::max(r1.Left(), r2.Left());
        if (overlapX <= 0) return;
        double dir = 1.0;
        if (Player1.PositionX < Player2.PositionX) dir = -1.0;
        double push = overlapX / 2.0;
        Player1.PositionX += push * dir;
        Player2.PositionX -= push * dir;
        Player1.ClampToStage();
        Player2.ClampToStage();
    }

    void ResolveCombat(Fighter& attacker, Fighter& defender) {
        if (!attacker.ActiveHitboxValid || attacker.CurrentMoveData == nullptr) return;
        if (defender.IsDead) return;
        if (std::find(attacker.AlreadyHit.begin(), attacker.AlreadyHit.end(), &defender) != attacker.AlreadyHit.end()) return;
        RectBox hurtRect = defender.HurtboxRect();
        if (!attacker.ActiveHitboxRect.Intersects(hurtRect)) return;

        attacker.AlreadyHit.push_back(&defender);
        const MoveData& move = *attacker.CurrentMoveData;
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
        }
    }

    void UpdateProjectiles(double dt) {
        std::vector<Projectile> survivors;
        for (auto& proj : Projectiles) {
            bool alive = proj.FrameStep(dt);
            if (alive) {
                Fighter* target = (proj.Owner == &Player1) ? &Player2 : &Player1;
                if (!proj.HasHit && !target->IsDead) {
                    if (proj.HitboxRect().Intersects(target->HurtboxRect())) {
                        proj.HasHit = true;
                        target->ReceiveHit(*proj.Move, *proj.Owner);
                        alive = false;
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
