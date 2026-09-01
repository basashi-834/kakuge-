// engine/CPUAI.h
// CPU opponent "brain" - deliberately separate from Fighter. Reads public
// state off both fighters and returns a synthetic RawInput that
// Fighter::FrameStep() consumes exactly like real keyboard input.
// 1:1 port of CPUAI/CPUAI.ps1, plus a new DummyMode (CPU/Stand/Crouch/Jump)
// so the pause menu can freeze P2 into a fixed training posture.
#pragma once
#include <vector>
#include <queue>
#include <random>
#include <functional>
#include "Constants.h"
#include "InputSystem.h"
#include "Fighter.h"

namespace kakuge {

class CPUAI {
public:
    Fighter* Self;
    Fighter* Opp;
    std::mt19937 Rng{std::random_device{}()};

    std::queue<RawInput> PendingSequence;
    RawInput CurrentInput;
    int DecisionCooldown = 0;

    DummyMode Mode = DummyMode::CPU;

    // Rescaled alongside the bigger hurtbox/pushbox/hitbox geometry (see
    // engine/Boxes.h and Fighter::PushboxHalfWidth): two pushboxes now
    // physically can't get closer than ~230 units apart, so CloseRange
    // must sit above that floor while still being inside normal-attack
    // reach, or the CPU would never consider itself "close enough" to
    // attack at all.
    static constexpr double CloseRange = 314.2;
    static constexpr double MidRange = 673.2;
    static constexpr double AntiAirRange = 359.0;
    static constexpr double LowHpRatio = 0.25;

    CPUAI(Fighter* self, Fighter* opp) : Self(self), Opp(opp) {}

    double RandDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(Rng); }
    int RandInt(int lo, int hiExclusive) { return std::uniform_int_distribution<int>(lo, hiExclusive - 1)(Rng); }

    RawInput Decide() {
        if (Mode == DummyMode::Stand) return RawInput{};
        if (Mode == DummyMode::Crouch) { RawInput r; r.Down = true; return r; }
        if (Mode == DummyMode::Jump) { RawInput r; r.Up = true; return r; }

        if (!PendingSequence.empty()) {
            RawInput r = PendingSequence.front();
            PendingSequence.pop();
            return r;
        }
        if (Self->IsDead || Opp->IsDead) return RawInput{};

        double dx = Opp->PositionX - Self->PositionX;
        double dist = std::abs(dx);
        int dirToOpp = dx < 0 ? -1 : 1;

        if (Opp->SM.CurrentState == CharState::Attack && dist < 336.6 && Self->SM.IsActionable() && RandDouble() < 0.7) {
            return HoldBack();
        }

        if (DecisionCooldown > 0) {
            DecisionCooldown -= 1;
            return CurrentInput;
        }

        CurrentInput = Plan(dist, dirToOpp);
        DecisionCooldown = RandInt(8, 19);
        return CurrentInput;
    }

    RawInput Plan(double dist, int dirToOpp) {
        if (!Self->SM.IsActionable() && Self->SM.CurrentState != CharState::Jump) {
            return RawInput{};
        }

        bool lowHp = Self->CurrentHP < (Self->Stats.MaxHP * LowHpRatio);
        bool oppAirborne = Opp->SM.CurrentState == CharState::Jump;

        if (oppAirborne && dist < AntiAirRange) {
            const MoveData* antiAir = FindMove([](const MoveData& m) { return m.HasTag(Constants::TagAntiAir); });
            if (antiAir) return UseMove(*antiAir);
        }

        if (dist < 561.0 && RandDouble() < 0.5) {
            const MoveData* superMove = FindMove([this](const MoveData& m) {
                return m.HasTag(Constants::TagSuper) && Self->Gauge.CanSpend(m.MeterCost);
            });
            if (superMove) return UseMove(*superMove);
        }

        if (dist < CloseRange) {
            if (lowHp && RandDouble() < 0.5) return HoldBack();
            if (RandDouble() < 0.55) {
                const MoveData* atk = PickCloseAttack();
                if (atk) return UseMove(*atk);
            }
            return MoveDir(dirToOpp); // keep tightening spacing so attacks actually reach
        } else if (dist < MidRange) {
            if (lowHp && RandDouble() < 0.35) return MoveDir(-dirToOpp);
            if (RandDouble() < 0.6) return MoveDir(dirToOpp);
            return RawInput{};
        } else {
            if (!lowHp && RandDouble() < 0.6) {
                const MoveData* proj = FindMove([](const MoveData& m) { return m.HasTag(Constants::TagProjectile); });
                if (proj) return UseMove(*proj);
            }
            return MoveDir(dirToOpp);
        }
    }

    const MoveData* PickCloseAttack() {
        std::vector<const MoveData*> pool;
        if (Self->Moveset) {
            for (const auto& kv : *Self->Moveset) {
                const MoveData& m = kv.second;
                if (m.HasTag(Constants::TagNormal) && m.Stance == "stand" && !m.HasTag(Constants::TagThrow)) {
                    pool.push_back(&m);
                }
            }
        }
        if (pool.empty()) return nullptr;
        return pool[RandInt(0, static_cast<int>(pool.size()))];
    }

    const MoveData* FindMove(const std::function<bool(const MoveData&)>& predicate) {
        std::vector<const MoveData*> pool;
        if (Self->Moveset) {
            for (const auto& kv : *Self->Moveset) {
                if (predicate(kv.second)) pool.push_back(&kv.second);
            }
        }
        if (pool.empty()) return nullptr;
        return pool[RandInt(0, static_cast<int>(pool.size()))];
    }

    RawInput MoveDir(int dir) {
        RawInput input;
        int facing = Self->Facing;
        if (dir == facing) {
            input.Right = (facing == 1);
            input.Left = (facing == -1);
        } else {
            input.Right = (facing == -1);
            input.Left = (facing == 1);
        }
        return input;
    }

    RawInput HoldBack() {
        RawInput input;
        int facing = Self->Facing;
        bool backIsRight = (facing == -1);
        input.Right = backIsRight;
        input.Left = !backIsRight;
        if (RandDouble() < 0.3) input.Down = true;
        return input;
    }

    // move.Button is "AnyP"/"AnyK" for specials/supers (see
    // CommandParser::ButtonSatisfies) - CPUAI still has to press one real
    // button to actually trigger them, so pick a representative one.
    static std::string ConcreteButton(const std::string& button) {
        if (button == "AnyP") return "LP";
        if (button == "AnyK") return "LK";
        return button;
    }

    RawInput UseMove(const MoveData& move) {
        if (move.InputCommand.empty()) {
            RawInput input;
            input.Buttons.Set(ConcreteButton(move.Button), true);
            return input;
        }
        const auto& motions = CommandParser::Motions();
        auto it = motions.find(move.InputCommand);
        if (it == motions.end()) return RawInput{};
        const auto& digits = it->second;
        PendingSequence = std::queue<RawInput>();
        for (int d : digits) {
            RawInput raw = DigitToRaw(d, Self->Facing);
            PendingSequence.push(raw);
            PendingSequence.push(raw);
        }
        RawInput finalRaw = DigitToRaw(digits.back(), Self->Facing);
        finalRaw.Buttons.Set(ConcreteButton(move.Button), true);
        PendingSequence.push(finalRaw);
        PendingSequence.push(RawInput{});
        RawInput first = PendingSequence.front();
        PendingSequence.pop();
        return first;
    }

    RawInput DigitToRaw(int digit, int facing) {
        RawInput input;
        bool forwardIsRight = (facing == 1);
        bool forward = digit == 3 || digit == 6 || digit == 9;
        bool back = digit == 1 || digit == 4 || digit == 7;
        bool down = digit == 1 || digit == 2 || digit == 3;
        bool up = digit == 7 || digit == 8 || digit == 9;
        if (forward) { input.Right = forwardIsRight; input.Left = !forwardIsRight; }
        else if (back) { input.Right = !forwardIsRight; input.Left = forwardIsRight; }
        input.Down = down;
        input.Up = up;
        return input;
    }
};

} // namespace kakuge
