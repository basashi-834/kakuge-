// =====================================================================
// engine/CPUAI.h - CPU 対戦相手の「頭脳」
// =====================================================================
// CPU はキャラクターを直接動かしません。
// 「今フレーム、どのキーを押すか」を決めて RawInput を返すだけです。
// それを Fighter::FrameStep に渡すと、人間が操作したのとまったく
// 同じ経路で処理されます。
//
// この作りの良いところ:
//   - CPU だけが使える裏技（瞬間移動など）が原理的に存在しない。
//     人間と同じ入力しかできないので、必ずフェアになります。
//   - Fighter 側に CPU 用の分岐を一切書かなくて済みます。
//
// 思考の流れ:
//   1. トレーニング用の固定モード（立ち・しゃがみ・ジャンプ）なら
//      その入力を返して終わり。
//   2. コマンド技を入力中なら、その続きを 1 フレームずつ返す。
//   3. 相手が技を出していて近ければ、高い確率でガードする。
//   4. それ以外は「間合い」に応じて行動を決め、
//      8-18 フレームのあいだ同じ行動を続ける（人間らしい間を作るため）。
// =====================================================================
#pragma once
#include <functional>
#include <queue>
#include <random>
#include <vector>

#include "engine/Constants.h"
#include "engine/Fighter.h"
#include "engine/InputSystem.h"

namespace kakuge {

class CPUAI {
public:
    Fighter* Self;  // 自分（＝2P）
    Fighter* Opp;   // 相手（＝1P）
    std::mt19937 Rng{std::random_device{}()}; // 乱数生成器

    // コマンド技を入力中の「残りの入力列」。
    // 波動拳なら 2→3→6→6+P のように複数フレームかかるため、
    // 先に全部作っておいて 1 フレームずつ取り出します。
    std::queue<RawInput> PendingSequence;
    RawInput CurrentInput;
    int DecisionCooldown = 0; // 次に考え直すまでのフレーム数

    DummyMode Mode = DummyMode::CPU;

    // 間合いの基準（中心から中心までの距離）。
    // 立ち同士だと押し合い判定で 30 離れて止まり、
    // 弱攻撃は約 34、強攻撃は約 48 まで届きます。
    static constexpr double CloseRange = 50.0;   // 攻撃が届く距離
    static constexpr double MidRange = 110.0;    // 中距離
    static constexpr double AntiAirRange = 60.0; // 対空を振る距離
    static constexpr double LowHpRatio = 0.25;   // これ以下だと慎重になる

    CPUAI(Fighter* self, Fighter* opp) : Self(self), Opp(opp) {}

    double RandDouble() { return std::uniform_real_distribution<double>(0.0, 1.0)(Rng); }
    int RandInt(int lo, int hiExclusive) {
        return std::uniform_int_distribution<int>(lo, hiExclusive - 1)(Rng);
    }

    RawInput Decide() {
        // トレーニングの固定モード
        if (Mode == DummyMode::Stand) return RawInput{};
        if (Mode == DummyMode::Crouch) { RawInput r; r.Down = true; return r; }
        if (Mode == DummyMode::Jump) { RawInput r; r.Up = true; return r; }
        if (Mode == DummyMode::Guard) return HoldBack(); // 後ろを入れ続ける

        // コマンド入力中なら、その続きを出す
        if (!PendingSequence.empty()) {
            RawInput r = PendingSequence.front();
            PendingSequence.pop();
            return r;
        }
        if (Self->IsDead || Opp->IsDead) return RawInput{};

        double dx = Opp->PositionX - Self->PositionX;
        double dist = std::abs(dx);
        int dirToOpp = dx < 0 ? -1 : 1;

        // 相手が近くで技を出したら、70% の確率でガードする。
        // 100% にすると絶対に攻撃が通らず、遊びとして成立しません。
        if (Opp->SM.CurrentState == CharState::Attack && dist < 55.0 &&
            Self->SM.IsActionable() && RandDouble() < 0.7) {
            return HoldBack();
        }

        // まだ考え直す時間ではないので、前と同じ行動を続ける。
        // これが無いと毎フレーム行動が変わり、その場で細かく震える
        // だけの不気味な動きになります。
        if (DecisionCooldown > 0) {
            DecisionCooldown -= 1;
            return CurrentInput;
        }

        CurrentInput = Plan(dist, dirToOpp);
        DecisionCooldown = RandInt(8, 19); // 0.13〜0.3 秒ごとに考え直す
        return CurrentInput;
    }

    // 間合いに応じて行動を決める。
    RawInput Plan(double dist, int dirToOpp) {
        // 動けない状態なら何もしない
        if (!Self->SM.IsActionable() && Self->SM.CurrentState != CharState::Jump) {
            return RawInput{};
        }

        bool lowHp = Self->CurrentHP < (Self->Stats.MaxHP * LowHpRatio);
        bool oppAirborne = Opp->SM.CurrentState == CharState::Jump;

        // 相手が飛んできたら対空技
        if (oppAirborne && dist < AntiAirRange) {
            const MoveData* antiAir = FindMove(
                [](const MoveData& m) { return m.HasTag(Constants::TagAntiAir); });
            if (antiAir) return UseMove(*antiAir);
        }

        // ゲージが溜まっていて近ければ超必殺技
        if (dist < 90.0 && RandDouble() < 0.5) {
            const MoveData* superMove = FindMove([this](const MoveData& m) {
                return m.HasTag(Constants::TagSuper) && Self->Gauge.CanSpend(m.MeterCost);
            });
            if (superMove) return UseMove(*superMove);
        }

        if (dist < CloseRange) {
            // 近距離: 攻めるか、体力が少なければ引くか
            if (lowHp && RandDouble() < 0.5) return HoldBack();
            if (RandDouble() < 0.55) {
                const MoveData* atk = PickCloseAttack();
                if (atk) return UseMove(*atk);
            }
            return MoveDir(dirToOpp); // さらに近づいて確実に当てにいく
        } else if (dist < MidRange) {
            // 中距離: 近づいたり様子を見たり
            if (lowHp && RandDouble() < 0.35) return MoveDir(-dirToOpp);
            if (RandDouble() < 0.6) return MoveDir(dirToOpp);
            return RawInput{};
        } else {
            // 遠距離: 飛び道具を撃つか、近づく
            if (!lowHp && RandDouble() < 0.6) {
                const MoveData* proj = FindMove(
                    [](const MoveData& m) { return m.HasTag(Constants::TagProjectile); });
                if (proj) return UseMove(*proj);
            }
            return MoveDir(dirToOpp);
        }
    }

    // 近距離で振る通常技をランダムに選ぶ（投げ以外の立ち技）。
    const MoveData* PickCloseAttack() {
        std::vector<const MoveData*> pool;
        if (Self->Moveset) {
            for (const auto& kv : *Self->Moveset) {
                const MoveData& m = kv.second;
                if (m.HasTag(Constants::TagNormal) && m.Stance == "stand" &&
                    !m.HasTag(Constants::TagThrow)) {
                    pool.push_back(&m);
                }
            }
        }
        if (pool.empty()) return nullptr;
        return pool[RandInt(0, static_cast<int>(pool.size()))];
    }

    // 条件に合う技をランダムに 1 つ選ぶ。
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

    // dir 方向（+1 右 / -1 左）へ移動する入力を作る。
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

    // 後ろを入れる（＝ガード姿勢）。たまにしゃがみガードも混ぜます。
    RawInput HoldBack() {
        RawInput input;
        int facing = Self->Facing;
        bool backIsRight = (facing == -1);
        input.Right = backIsRight;
        input.Left = !backIsRight;
        if (RandDouble() < 0.3) input.Down = true;
        return input;
    }

    // 技データのボタン指定が "AnyP"/"AnyK" のとき、実際に押す 1 個を選ぶ。
    static std::string ConcreteButton(const std::string& button) {
        if (button == "AnyP") return "LP";
        if (button == "AnyK") return "LK";
        return button;
    }

    // 技を出すための入力列を組み立てる。
    RawInput UseMove(const MoveData& move) {
        // 通常技はボタンを押すだけ
        if (move.InputCommand.empty()) {
            RawInput input;
            input.Buttons.Set(ConcreteButton(move.Button), true);
            return input;
        }
        // コマンド技は方向を順に入れていく。
        // 各方向を 2 フレームずつ入れているのは、1 フレームだと
        // 入力バッファの記録タイミングによっては取りこぼすためです。
        std::vector<int> digits = CommandParser::ParseDigits(move.InputCommand);
        if (digits.empty()) return RawInput{};
        PendingSequence = std::queue<RawInput>();
        for (int d : digits) {
            RawInput raw = DigitToRaw(d, Self->Facing);
            PendingSequence.push(raw);
            PendingSequence.push(raw);
        }
        // 最後にボタンを足した入力、そのあとニュートラルを入れる
        RawInput finalRaw = DigitToRaw(digits.back(), Self->Facing);
        finalRaw.Buttons.Set(ConcreteButton(move.Button), true);
        PendingSequence.push(finalRaw);
        PendingSequence.push(RawInput{});

        RawInput first = PendingSequence.front();
        PendingSequence.pop();
        return first;
    }

    // テンキー表記の数字を、実際のキー入力に戻す（向きを考慮）。
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
