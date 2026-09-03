// =====================================================================
// engine/BattleSystem.h - 1 試合ぶんの進行役（審判）
// =====================================================================
// 2 人のキャラクター、飛び道具、制限時間、勝敗をまとめて管理します。
//
// このクラスがなぜ必要なのか
// -----------------------
// Fighter が自分で「相手に当たったか」を調べる作りにすると、
// 1P と 2P が同時に処理を始めて、どちらが先に当たったのかが
// 状況によって変わってしまいます（実行順で結果が変わる）。
// 格闘ゲームでこれは致命的です。
//
// そこで BattleSystem が唯一の司令塔になり、毎フレーム必ず
//   1. 2 人の入力処理（Fighter::FrameStep）
//   2. 押し合いの解決
//   3. 1P → 2P の当たり判定
//   4. 2P → 1P の当たり判定
//   5. 飛び道具の更新
// という決まった順番で処理します。順番が固定なので、
// 同じ入力からは必ず同じ結果が出ます（再現性がある＝デバッグしやすい）。
//
// 注意: このクラスは std::unique_ptr などで「1 か所に置いたまま」
// 使ってください。Fighter::Opponent や CPUAI が中の Player1/Player2 を
// ポインタで指しているので、コピーしたり動かしたりすると
// そのポインタが壊れた場所を指してしまいます。
// =====================================================================
#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "engine/CPUAI.h"
#include "engine/CharacterStats.h"
#include "engine/Constants.h"
#include "engine/Fighter.h"
#include "engine/MoveData.h"
#include "engine/Projectile.h"

namespace kakuge {

class BattleSystem {
public:
    Fighter Player1;
    Fighter Player2;
    std::unique_ptr<CPUAI> CpuAI;
    std::vector<Projectile> Projectiles;

    int FramesLeft = 0;       // 制限時間の残りフレーム数
    bool MatchActive = false; // 試合中か
    Fighter* Winner = nullptr;
    bool IsDraw = false;

    // 描画側に渡す演出・効果音の注文（毎フレーム作り直される）
    std::vector<EffectEvent> AllEffects;
    std::vector<std::string> AllSounds;

    // 画面揺れ。カウンターヒットで発生し、ShakeFrames が 0 に向かって
    // 減るにつれて揺れ幅も小さくなります。
    int ShakeFrames = 0;
    double ShakeMagnitude = 0.0;

    // 試合開始時の「FIGHT」表示の残りフレーム
    int RoundStartFlashFrames = 0;
    static constexpr int RoundStartFlashDuration = 50;

    // コンボ数（ガードされていないヒットが連続した回数）
    int P1ComboCount = 0, P2ComboCount = 0;
    int P1MaxCombo = 0, P2MaxCombo = 0;

    // トレーニングモード。制限時間が減らず、KO でも試合が終わりません。
    // TrainingAutoHeal が true なら 2P の体力が自動で回復するので、
    // 同じコンボを何度でも試せます。
    bool TrainingMode = false;
    bool TrainingAutoHeal = true;
    static constexpr int TrainingHealPerFrame = 6;

    void ResetHP() {
        for (Fighter* p : {&Player1, &Player2}) {
            p->CurrentHP = p->Stats.MaxHP;
            p->IsDead = false;
            if (p->SM.CurrentState == CharState::Dead ||
                p->SM.CurrentState == CharState::Knockdown ||
                p->SM.CurrentState == CharState::WakeUp) {
                p->SM.ChangeState(CharState::Idle, "");
            }
        }
    }

    static constexpr double StageMinX = StageConstants::StageMinX;
    static constexpr double StageMaxX = StageConstants::StageMaxX;

    // 試合を開始する（キャラクターを配置して初期化）。
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

    // =================================================================
    // 1 フレーム（1/60 秒）ぶんの試合進行
    // =================================================================
    void Update(double dt, const RawInput& p1RawInput) {
        if (!MatchActive) return;
        AllEffects.clear();
        AllSounds.clear();

        // 1) 2 人ぶんの入力処理。順番は必ず 1P → 2P。
        RawInput p2Input = CpuAI->Decide();
        Player1.FrameStep(dt, p1RawInput);
        Player2.FrameStep(dt, p2Input);

        if (ShakeFrames > 0) ShakeFrames -= 1;
        if (RoundStartFlashFrames > 0) RoundStartFlashFrames -= 1;

        // 2) 押し合い → 3) 4) 当たり判定
        ResolvePushboxes();
        ResolveCombat(Player1, Player2);
        ResolveCombat(Player2, Player1);

        // コンボは「相手が動けるようになった時点」で終了。
        if (Player1.SM.IsActionable()) P2ComboCount = 0;
        if (Player2.SM.IsActionable()) P1ComboCount = 0;

        // 5) 飛び道具
        UpdateProjectiles(dt);

        // Fighter が溜めた演出・効果音・飛び道具の注文を回収する
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
            return; // トレーニング中は KO も時間切れも起こさない
        }

        if (Player1.IsDead || Player2.IsDead) {
            EndByKO();
            return;
        }

        FramesLeft -= 1;
        if (FramesLeft <= 0) EndByTimeout();
    }

    // -----------------------------------------------------------------
    // 押し合いの解決（プッシュボックスの唯一の用途）
    // -----------------------------------------------------------------
    // 2 人の体が重なっていたら、重なった幅だけ左右に押し離します。
    // 基本は半分ずつ。ただし
    //   - 画面端に張り付いている側は動かさず、相手が全部下がる
    //   - しゃがみ・ガード中は踏ん張って動かない
    // という例外があります。画面端で押し込む攻防を成立させるためです。
    void ResolvePushboxes() {
        RectBox r1 = Player1.PushboxRect();
        RectBox r2 = Player2.PushboxRect();
        if (!r1.Intersects(r2)) return;
        double overlapX = std::min(r1.Right(), r2.Right()) - std::max(r1.Left(), r2.Left());
        if (overlapX <= 0) return;

        // dir は 1P が押される向き（+1 が右）。2P はその逆に押されます。
        double dir = (Player1.PositionX < Player2.PositionX) ? -1.0 : 1.0;

        bool p1Planted = Player1.IsPlanted();
        bool p2Planted = Player2.IsPlanted();
        double push1 = overlapX / 2.0, push2 = overlapX / 2.0;
        if (p1Planted && !p2Planted) { push1 = 0.0; push2 = overlapX; }
        else if (p2Planted && !p1Planted) { push2 = 0.0; push1 = overlapX; }

        // それぞれ壁までどれだけ余裕があるか。
        // 押しきれないぶんは相手に肩代わりさせます。そうしないと
        // 押し離しが足りず、体が重なったままになります。
        double room1 = (dir > 0) ? (Player1.StageMaxX - Player1.PositionX)
                                 : (Player1.PositionX - Player1.StageMinX);
        double room2 = (dir > 0) ? (Player2.PositionX - Player2.StageMinX)
                                 : (Player2.StageMaxX - Player2.PositionX);
        room1 = std::max(0.0, room1);
        room2 = std::max(0.0, room2);
        if (push1 > room1) { push2 += push1 - room1; push1 = room1; }
        if (push2 > room2) { push1 = std::min(room1, push1 + (push2 - room2)); push2 = room2; }

        Player1.PositionX += push1 * dir;
        Player2.PositionX -= push2 * dir;
        Player1.ClampToStage();
        Player2.ClampToStage();
    }

    // 投げが成立するかの判定（当たり判定ではなく距離で決まる）。
    static bool ThrowInRange(const Fighter& attacker, const Fighter& defender) {
        const MoveData* move = attacker.CurrentMoveData;
        if (move == nullptr || attacker.SM.CurrentState != CharState::Attack) return false;
        if (MoveExecutor::GetPhase(*move, attacker.SM.CurrentFrame) != MovePhase::Active) return false;
        if (attacker.Stance() == "air" || !defender.IsThrowable()) return false;
        double dx = std::round(defender.PositionX) - std::round(attacker.PositionX);
        if (dx * attacker.Facing < 0) return false; // 相手が背後にいる
        return std::abs(dx) <= move->ThrowRange;
    }

    // -----------------------------------------------------------------
    // 当たり判定の解決
    // -----------------------------------------------------------------
    // 「攻撃側のヒットボックス」対「防御側のハートボックス」だけを
    // 調べます。ヒットボックスどうしや、プッシュボックスとは
    // 絶対に判定しません。投げだけは距離で判定します。
    void ResolveCombat(Fighter& attacker, Fighter& defender) {
        if (attacker.CurrentMoveData == nullptr || attacker.SM.CurrentState != CharState::Attack) return;
        if (defender.IsDead) return;
        // すでにこの判定で当てた相手には二度当てない
        if (std::find(attacker.AlreadyHit.begin(), attacker.AlreadyHit.end(), &defender) !=
            attacker.AlreadyHit.end()) return;

        const MoveData& move = *attacker.CurrentMoveData;

        bool connects = false;
        if (move.GuardType == Constants::GuardThrow) {
            connects = ThrowInRange(attacker, defender);
        } else {
            if (attacker.ActiveHitboxRects.empty()) return;
            std::vector<RectBox> hurtRects = defender.HurtboxRects();
            // 攻撃判定と食らい判定の総当たり。どれか 1 組でも
            // 重なっていればヒットです。
            for (const auto& hb : attacker.ActiveHitboxRects) {
                for (const auto& hr : hurtRects) {
                    if (hb.Intersects(hr)) { connects = true; break; }
                }
                if (connects) break;
            }
        }
        if (!connects) return;

        attacker.AlreadyHit.push_back(&defender);
        // 「食らう前からすでにのけぞっていたか」＝コンボが続いているか
        bool wasAlreadyStunned = (defender.SM.CurrentState == CharState::Hitstun ||
                                  defender.SM.CurrentState == CharState::Knockdown);
        HitResult result = defender.ReceiveHit(move, attacker);

        // ヒットストップは攻撃側にも掛かります（両者が同時に止まる）。
        if (move.Hitstop > attacker.HitstopTimer) attacker.HitstopTimer = move.Hitstop;

        double gain = move.MeterGain;
        if (result.blocked) gain *= 0.5;
        attacker.Gauge.Add(gain);

        if (!result.blocked && !result.whiffed) {
            int* comboCount = (&attacker == &Player1) ? &P1ComboCount : &P2ComboCount;
            int* maxCombo = (&attacker == &Player1) ? &P1MaxCombo : &P2MaxCombo;
            *comboCount = wasAlreadyStunned ? (*comboCount + 1) : 1;
            *maxCombo = std::max(*maxCombo, *comboCount);

            // カウンターヒットなら画面を揺らして演出を出す
            int side = (&attacker == &Player1) ? 0 : 1;
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

    // 飛び道具を進めて、当たったものと寿命切れのものを消す。
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
                            alive = false; // 当たったら消える
                            break;
                        }
                    }
                }
            }
            if (alive) survivors.push_back(proj);
        }
        Projectiles = std::move(survivors);
    }

    // Fighter が溜めた「注文」を回収して、実際に処理する。
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
            IsDraw = true;   // 相打ちで両者 KO
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
        // 時間切れは残り体力の多いほうの勝ち
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
