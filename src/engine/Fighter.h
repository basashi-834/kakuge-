// =====================================================================
// engine/Fighter.h - 試合中に実際に動いているキャラクター 1 人ぶん
// =====================================================================
// このゲームで一番大きくて、一番大事なファイルです。
// キャラクターの物理（移動・重力）と、状態の移り変わり（立つ・歩く・
// 攻撃する・食らう）をここで管理します。
//
// 設計の要点: Fighter はキーボードを直接読みません
// ---------------------------------------------------------------
// FrameStep(dt, raw) という関数に「このフレームの入力」を渡してもらう
// 形にしています。渡すのは BattleSystem で、1P には本物のキーボード
// 入力を、2P には CPU が考えた入力を渡します。
// Fighter からは両者の区別がつきません。
// おかげで「CPU 戦」「対人戦」「リプレイ再生」を、Fighter を
// 一切書き換えずに実現できます。
//
// 主な構成要素（それぞれ別ファイル）:
//   StateMachine … 今の状態と経過フレーム
//   MoveExecutor … 技が今どの段階か
//   InputBuffer  … 入力履歴（コマンド技の判定用）
//   SuperGauge   … 超必殺技ゲージ
//   HurtboxSet   … 食らい判定
// =====================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/Boxes.h"
#include "engine/CharacterStats.h"
#include "engine/Constants.h"
#include "engine/InputSystem.h"
#include "engine/MoveData.h"
#include "engine/MoveExecutor.h"
#include "engine/StateMachine.h"
#include "engine/SuperGauge.h"

namespace kakuge {

// 画面に出したい演出の「注文」。Fighter は絵を描かないので、
// 「ここで火花を出して」と書き置きだけして、実際に描くのは描画側です。
// side は 0 が 1P 側、1 が 2P 側（画面のどちら端に出すかに使います）。
struct EffectEvent { std::string kind; double x = 0, y = 0; int side = 0; };

// 「飛び道具を出して」という注文。実際に作るのは BattleSystem です。
struct ProjectileRequest {
    const MoveData* move = nullptr;
    double x = 0, y = 0;
    int facing = 1;
};

// カウンターヒットの種類。
//   Counter          … 相手が技の「発生中」（まだ攻撃判定が出る前）に
//                      殴られた。技を振ろうとした出鼻をくじいた状態。
//   EffectiveCounter … 相手が技の「硬直中」（攻撃が空振りしたあと）に
//                      殴られた。もっとも手痛い反撃。
enum class CounterKind { None, Counter, EffectiveCounter };

// 攻撃が当たったときの結果。
struct HitResult {
    bool blocked = false;  // ガードされた
    bool whiffed = false;  // 無敵などで当たらなかった
    CounterKind counter = CounterKind::None;
    // この当たりで発生したストップの長さ（ヒットなら Hitstop、
    // ガードなら Guardstop）。攻撃側にも同じ値を掛けるため、
    // 呼び出し元（BattleSystem）に返します。
    // 「どちらにも同じ値を同時に掛ける」ことが、ストップを
    // 有利不利に影響させないための条件です。
    int stopFrames = 0;
};

class Fighter {
public:
    CharacterStats Stats;   // 基本性能（設計図）
    StateMachine SM;        // 今の状態
    InputBuffer InputBuf;   // 入力履歴
    SuperGauge Gauge;       // 超必ゲージ
    HurtboxSet Hurtboxes;   // 食らい判定（Stats からコピーされる）

    int CurrentHP = 10000;
    int Facing = 1;              // +1 右向き / -1 左向き
    bool FacingLocked = false;   // 技の最中は向きを変えない
    bool IsDead = false;
    Fighter* Opponent = nullptr; // 相手（向きの決定などに使う）

    double StageMinX = StageConstants::StageMinX;
    double StageMaxX = StageConstants::StageMaxX;

    // 位置と速度。Y は上がマイナス、地面が 0 です。
    double PositionX = 0.0, PositionY = 0.0;
    double VelocityX = 0.0, VelocityY = 0.0;

    const MoveData* CurrentMoveData = nullptr;      // 今出している技
    // 今出している技が当たったか（キャンセル条件の判定に使う）。
    // 技を出した瞬間は Whiff（＝まだ当たっていない）で、
    // 当たった / ガードされた時点で BattleSystem が書き換えます。
    MoveContact CurrentMoveContact = MoveContact::Whiff;
    bool ProjectileSpawnedThisActivation = false;   // 飛び道具は 1 回だけ

    // 各種タイマー（すべてフレーム単位。0 になったら効果終了）
    int HitstunTimer = 0;   // のけぞり
    int BlockstunTimer = 0; // ガード硬直
    // ストップ（ヒットストップ / ガードストップ）の残り。
    // 0 より大きい間、このキャラクターの時間は完全に止まります
    //（アニメーション・座標・すべてのタイマーが進みません）。
    int HitstopTimer = 0;
    int KnockdownTimer = 0; // ダウン
    int WakeupTimer = 0;    // 起き上がり
    int ThrownTimer = 0;    // 投げられ中
    int DashTimer = 0;      // 前ダッシュ中
    int StepTimer = 0;      // 前ステップ中（決まった距離だけ前へ踏み込む）
    int BackDashTimer = 0;  // バックダッシュ中
    int BackStepTimer = 0;  // バックステップ中（決まった距離だけ後ろへ下がる）
    // 空中技の着地硬直。空中技の硬直は空中では消化されず、
    // 着地してからこのタイマーぶんだけ動けません。
    int LandingRecoveryTimer = 0;
    bool LandedDuringMove = false; // 今出している空中技で、もう着地したか
    bool IsCrouchingGuard = false;
    int FrameCounter = 0;   // 試合開始からの総フレーム数

    // 押し合い判定（プッシュボックス）は、姿勢ごとの大きさを
    // キャラクターデータ（Stats.Pushboxes）が持っています。
    // エディタで 1px 単位に調整でき、書かなければ標準の値です。

    bool HitboxesWereLive = false;          // 前フレームに攻撃判定が出ていたか
    std::vector<RectBox> ActiveHitboxRects; // 今出ている攻撃判定（複数可）
    std::vector<Fighter*> AlreadyHit;       // この判定ですでに当てた相手

    // 描画側・BattleSystem 側に渡す「注文」置き場。
    std::vector<EffectEvent> PendingEffects;
    std::vector<std::string> PendingSounds;
    bool PendingProjectileValid = false;
    ProjectileRequest PendingProjectileRequestData;
    bool LastHitBlocked = false;
    CounterKind LastCounterKind = CounterKind::None;

    // ---- 先行入力（バッファ）----
    // 動けない間（ストップ中・のけぞり中・技の硬直中）に押したボタンを
    // 少しの間だけ覚えておき、動けるようになった瞬間に技を出します。
    //
    // これが無いと、ヒットストップ中に押したボタンは「押した瞬間」を
    // 逃して消えてしまい、目押しでしかコンボがつながらなくなります。
    // 実機の格闘ゲームがどれも持っている仕組みです。
    struct BufferedPress {
        std::string button;
        int clock = 0; // 押したときの BufferClock
    };
    std::vector<BufferedPress> InputQueue;
    // 先行入力を覚えておく長さ。長すぎると「押していないのに技が出る」、
    // 短すぎると「押したのに出ない」。6 フレーム（0.1 秒）が目安です。
    static constexpr int InputBufferFrames = 6;
    // 先行入力の寿命を数える時計。ストップ中は進めません
    //（止まっている間に先行入力が期限切れになってしまわないように）。
    int BufferClock = 0;

    ButtonsHeld HeldButtonsPrev;      // 前フレームのボタン状態（押した瞬間の検出用）
    // ---- 「前・前」の検出用 ----
    // 前を「入れた瞬間」だけを数えます。押しっぱなしを数えてしまうと、
    // 前に歩こうとしただけで毎回ダッシュになってしまいます
    //（1 フレーム目に 1 回目、2 フレーム目に 2 回目と数えられるため）。
    int LastForwardTapFrame = -999;   // 前を入れた最後のフレーム
    bool ForwardHeldPrev = false;     // 前フレームに前を入れていたか
    bool ForwardTappedNow = false;    // 今フレームに前を「入れ直した」か
    bool ForwardDoubleTapped = false; // 今フレームが「前・前」の 2 回目か
    // 後ろも同じように数えます。前とは完全に別のカウンタなので、
    // 前ステップの設定を変えてもバックステップには影響しません。
    int LastBackTapFrame = -999;
    bool BackHeldPrev = false;
    bool BackTappedNow = false;
    bool BackDoubleTapped = false;

    static constexpr double GroundY = 0.0;
    static constexpr int DashInputWindow = 14;   // 前・前 をこのフレーム以内に入れるとダッシュ
    static constexpr int DashDuration = 14;
    static constexpr int KnockdownFrames = 40;
    static constexpr int HardKnockdownFrames = 60;
    static constexpr int WakeupFrames = 14;
    static constexpr int ThrownLockFrames = 20;

    // このキャラクターが使える技の一覧（DataManager が持っている本体を指す）
    const std::unordered_map<std::string, MoveData>* Moveset = nullptr;

    void Setup(const CharacterStats& stats, const std::unordered_map<std::string, MoveData>* moveset) {
        Stats = stats;
        Hurtboxes = stats.Hurtboxes;
        Moveset = moveset;
        ResetForRound();
    }

    // ラウンド開始時の初期化。体力とゲージを戻し、状態を立ちにします。
    void ResetForRound() {
        CurrentHP = Stats.MaxHP;
        IsDead = false;
        CurrentMoveData = nullptr;
        CurrentMoveContact = MoveContact::Whiff;
        HitstunTimer = BlockstunTimer = HitstopTimer = 0;
        InputQueue.clear();
        BufferClock = 0;
        KnockdownTimer = WakeupTimer = ThrownTimer = DashTimer = StepTimer = 0;
        BackDashTimer = BackStepTimer = 0;
        LandingRecoveryTimer = 0;
        LandedDuringMove = false;
        LastForwardTapFrame = -999;
        ForwardHeldPrev = false;
        ForwardTappedNow = false;
        ForwardDoubleTapped = false;
        LastBackTapFrame = -999;
        BackHeldPrev = false;
        BackTappedNow = false;
        BackDoubleTapped = false;
        Gauge.Value = 0.0;
        InputBuf.Clear();
        SM.ChangeState(CharState::Idle, "");
        ActiveHitboxRects.clear();
        AlreadyHit.clear();
        HitboxesWereLive = false;
        PendingEffects.clear();
        PendingSounds.clear();
        PendingProjectileValid = false;
        VelocityX = VelocityY = 0.0;
    }

    // =================================================================
    // 毎フレームの処理の入口（BattleSystem から 1/60 秒ごとに呼ばれる）
    // =================================================================
    void FrameStep(double dt, const RawInput& raw) {
        // 死亡後は倒れるだけで、操作も技も受け付けません。
        //
        // ただし「落として終わり」にすると、KO したときの吹き飛び速度が
        // そのまま残り続けます。ダウン中や のけぞり中と違って速度を
        // 0 に近づける処理が無く、ステージ端で止める処理も通らないため、
        // 死体が同じ速さで滑り続け、やがて画面の外へ出ていってしまいます
        //（トレーニングモードのように KO で試合が終わらない場面で
        //  はっきり分かります）。
        //
        // そこで、生きているときと同じ 2 つを死亡後にも掛けます。
        //   ・地面に着いていれば、吹き飛び速度を 0 に近づける（滑って止まる）
        //   ・ステージの外へは出さない
        if (IsDead) {
            VelocityY += Stats.Gravity * dt;
            PositionX += VelocityX * dt;
            PositionY += VelocityY * dt;
            if (PositionY > GroundY) { PositionY = GroundY; VelocityY = 0.0; }
            // 減り方はダウン中（Knockdown）と同じにします。倒れた体の
            // 滑り方が、KO かダウンかで変わるのは不自然なためです。
            if (PositionY >= GroundY - 0.01) {
                VelocityX = MoveToward(VelocityX, 0.0, 339.1 / Constants::Fps);
            }
            ClampToStage();
            return;
        }

        FrameCounter += 1;

        // 1) 入力を履歴に記録する（コマンド技の判定に使う）
        int digit = InputBuffer::ComputeDigit(raw.Left, raw.Right, raw.Down, raw.Up, Facing);
        std::vector<std::string> pressed = NewlyPressedButtons(raw.Buttons);

        // 投げは専用キーが無く、LP と LK の同時押しで出します。
        // 「どちらかを新しく押した瞬間に、もう片方も押されていれば投げ」
        // という判定にすると、完全な同時押しでなくても成立します。
        bool pressedHasLP = std::find(pressed.begin(), pressed.end(), "LP") != pressed.end();
        bool pressedHasLK = std::find(pressed.begin(), pressed.end(), "LK") != pressed.end();
        bool pressedHasThrow = std::find(pressed.begin(), pressed.end(), "Throw") != pressed.end();
        if (raw.Buttons.LP && raw.Buttons.LK && (pressedHasLP || pressedHasLK) && !pressedHasThrow) {
            pressed.push_back("Throw");
        }
        InputBuf.RecordFrame(FrameCounter, digit, pressed);

        // 2) ストップ（ヒットストップ / ガードストップ）中は、
        //    入力の受け付け以外をすべて止める。
        //
        //    ここで return するので、この下にある
        //      SM.Tick()          … アニメーションのコマ送り
        //      HandleStateLogic() … のけぞり・硬直などのカウントダウン
        //      ApplyPhysics()     … 座標の更新（ノックバックもここ）
        //    がすべて動きません。つまり「完全停止」です。
        //    ノックバックはストップが解けた次のフレームから始まります。
        //
        //    入力だけは上で記録済みなので、止まっている間に押した
        //    ボタンも先行入力として拾えます。
        if (HitstopTimer > 0) {
            HitstopTimer -= 1;
            PushBufferedPresses(pressed); // 止まっている間の入力も覚える
            return;
        }

        // 3) 「前（後ろ）を入れ直したか」「2 回目か」を判定する。
        //    ここでやるのは、どの状態にいても（技の最中でも、のけぞり
        //    中でも）レバーの前フレームとの差を正しく取り続けるため
        //    です。技の最中にも見ているのは、ドライブラッシュ
        //    キャンセル（技をキャンセルして前へ踏み込む）のためです。
        bool forwardNow = IsHoldingForward(raw);
        ForwardTappedNow = forwardNow && !ForwardHeldPrev;
        ForwardHeldPrev = forwardNow;
        ForwardDoubleTapped =
            ForwardTappedNow && (FrameCounter - LastForwardTapFrame) <= DashInputWindow;
        if (ForwardTappedNow) LastForwardTapFrame = FrameCounter;

        bool backNow = IsHoldingBack(raw);
        BackTappedNow = backNow && !BackHeldPrev;
        BackHeldPrev = backNow;
        BackDoubleTapped = BackTappedNow && (FrameCounter - LastBackTapFrame) <= DashInputWindow;
        if (BackTappedNow) LastBackTapFrame = FrameCounter;

        // 4) 先行入力の管理。止まっていないフレームだけ時計を進めます。
        BufferClock += 1;
        PushBufferedPresses(pressed);
        ExpireBufferedPresses();

        // 5) 状態を 1 フレーム進め、状態ごとの処理をして、物理を適用する
        SM.Tick();
        HandleStateLogic(raw, pressed);
        ApplyPhysics(dt);
        ClampToStage();
        UpdateFacing();
    }

    // 押されたボタンを先行入力に積む。
    void PushBufferedPresses(const std::vector<std::string>& pressed) {
        for (const auto& b : pressed) InputQueue.push_back({b, BufferClock});
    }

    // 古くなった先行入力を捨てる。
    void ExpireBufferedPresses() {
        InputQueue.erase(std::remove_if(InputQueue.begin(), InputQueue.end(),
                                        [this](const BufferedPress& p) {
                                            return (BufferClock - p.clock) > InputBufferFrames;
                                        }),
                         InputQueue.end());
    }

    // ストップ中かどうか（BattleSystem が「全部止める」判断に使います）。
    bool IsStopped() const { return HitstopTimer > 0; }

    // 「今フレームで新しく押されたボタン」だけを取り出す。
    // 押しっぱなしを除くのが大事です。除かないと、ボタンを押している
    // 間じゅう技が出続けてしまいます。
    std::vector<std::string> NewlyPressedButtons(const ButtonsHeld& held) {
        std::vector<std::string> result;
        for (const char* key : {"LP", "MP", "HP", "LK", "MK", "HK"}) {
            bool isHeld = held.Get(key);
            bool wasHeld = HeldButtonsPrev.Get(key);
            if (isHeld && !wasHeld) result.push_back(key);
        }
        HeldButtonsPrev = held;
        HeldButtonsPrev.Throw = false; // 投げは合成ボタンなので覚えない
        return result;
    }

    // 「後ろ」を入れているか。向きによって左右どちらが後ろかが変わります。
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

    // =================================================================
    // 状態ごとの処理
    // =================================================================
    // 順番が大事です。
    //   1. 硬直（のけぞり・ガード硬直・技・ダウン）を 1 フレーム消化する
    //   2. 消化しきったら、そのフレームのうちに動ける状態へ戻す
    //   3. そのうえで、今の状態に応じた処理をする
    //
    // 2 を「次のフレーム」にしてしまうと、硬直が明けるのが 1 フレーム
    // 遅れます。硬直差はフレーム単位の差そのものなので、この 1 フレーム
    // のずれがそのまま「表の数字と実際の挙動が違う」ことになります。
    void HandleStateLogic(const RawInput& raw, const std::vector<std::string>& pressed) {
        AdvanceStateTimers(raw);

        switch (SM.CurrentState) {
            case CharState::Hitstun: {
                // のけぞり中。吹き飛び速度をだんだん 0 に近づける。
                VelocityX = MoveToward(VelocityX, 0.0, 254.3 / Constants::Fps);
                break;
            }
            case CharState::Block: {
                // ガード状態には 2 通りの入り方があります。
                //  (1) 実際に攻撃を受けてガードした → BlockstunTimer > 0
                //  (2) 攻撃は来ていないが、後ろ（＋下）を入れて構えている
                //      → BlockstunTimer は 0 のまま
                // どちらも「いつ構えを解くか」は上の AdvanceStateTimers が
                // 決めているので、ここは構えている間の振る舞いだけです。
                if (BlockstunTimer > 0) {
                    VelocityX = MoveToward(VelocityX, 0.0, 254.3 / Constants::Fps);
                    break;
                }
                // しゃがみガードか立ちガードかを毎フレーム更新します
                //（食らい判定の姿勢と、出せる技の姿勢に使います）。
                IsCrouchingGuard = raw.Down && IsHoldingBack(raw);
                VelocityX = 0.0;
                // ガード姿勢のままでも攻撃ボタンで技が出せます。
                // しゃがみガード中なら、ちゃんとしゃがみ技が出ます。
                (void)TryStartMove(raw, pressed);
                break;
            }
            case CharState::Throw:
                break;  // 投げられ中は何もできない（タイマーは上で消化済み）
            case CharState::Knockdown: {
                VelocityX = MoveToward(VelocityX, 0.0, 339.1 / Constants::Fps);
                break;
            }
            case CharState::WakeUp:
                break;
            case CharState::Attack: {
                // 技の最中。キャンセル可能時間帯なら次の技を受け付けます
                //（＝コンボ）。受け付けなければ今の技を進めるだけ。
                //
                // 技へのキャンセルより先に、前・前 での踏み込み
                //（ドライブラッシュキャンセル）を見ます。同じフレームに
                // 両方成立することはまずありませんが、順番を決めておかないと
                // 状況によって結果が変わってしまいます。
                if (TryDriveRushCancel(raw)) break;
                if (!TryStartMove(raw, pressed)) ProgressMove();
                break;
            }
            case CharState::Jump: {
                // 空中。空中技を出せるほか、左右に軌道修正できます。
                if (!TryStartMove(raw, pressed)) {
                    double vx = 0.0;
                    if (raw.Right) vx = Stats.WalkForwardSpeed;
                    if (raw.Left) vx = vx - Stats.WalkForwardSpeed;
                    VelocityX = vx;
                }
                // 着地判定
                if (PositionY >= GroundY && VelocityY >= 0) {
                    PositionY = GroundY;
                    VelocityY = 0;
                    if (raw.Down) SM.ChangeState(CharState::Crouch, "");
                    else SM.ChangeState(CharState::Idle, "");
                }
                break;
            }
            default: {
                // 立ち・歩き・しゃがみ（操作を受け付けられる状態）
                if (DashTimer > 0) DashTimer -= 1;
                if (BackDashTimer > 0) BackDashTimer -= 1;
                if (!TryStartMove(raw, pressed)) HandleGroundMovement(raw);
                break;
            }
        }
    }

    // -----------------------------------------------------------------
    // 硬直を 1 フレーム消化して、消化しきったら動ける状態へ戻す
    // -----------------------------------------------------------------
    // ここで状態を戻したフレームから、上の switch がそのまま
    // 「動ける状態の処理」を行うので、硬直が明けたフレームに
    // ちょうど次の技を出せます。
    void AdvanceStateTimers(const RawInput& raw) {
        switch (SM.CurrentState) {
            case CharState::Hitstun:
                HitstunTimer -= 1;
                if (HitstunTimer <= 0) ReturnToNeutral(raw);
                break;
            case CharState::Block:
                if (BlockstunTimer > 0) BlockstunTimer -= 1;
                // ガード硬直が明けたあとも、本人が構え続けているなら
                // Block のまま。構えを解いたら、そのフレームから動けます
                //（後ろ歩き・しゃがみ・技、どれでも）。
                if (BlockstunTimer <= 0 && !WantsGuardPosture(raw)) ReturnToNeutral(raw);
                break;
            case CharState::Throw:
                ThrownTimer -= 1;
                if (ThrownTimer <= 0) EnterKnockdown(false, 0);
                break;
            case CharState::Knockdown:
                KnockdownTimer -= 1;
                if (KnockdownTimer <= 0) {
                    WakeupTimer = WakeupFrames;
                    SM.ChangeState(CharState::WakeUp, "");
                }
                break;
            case CharState::WakeUp:
                WakeupTimer -= 1;
                if (WakeupTimer <= 0) ReturnToNeutral(raw);
                break;
            case CharState::Attack:
                // 空中技の着地硬直。地上技には関係ありません。
                if (LandingRecoveryTimer > 0) LandingRecoveryTimer -= 1;
                if (IsMoveOver()) FinishMove(raw);
                break;
            default:
                break;
        }
    }

    // 「今このフレーム、ガード姿勢を取りたがっているか」。
    // ここは HandleGroundMovement がガード姿勢に入る条件と同じにします。
    // 揃えておかないと、入った次のフレームに抜ける（またはその逆）を
    // 繰り返して、姿勢が高速で点滅します。
    bool WantsGuardPosture(const RawInput& raw) const {
        if (!IsHoldingBack(raw)) return false;
        if (raw.Down) return true; // 下＋後ろ ＝ しゃがみガードの構え
        // 立ちガードの構えは「相手が技を出している間」だけ。
        // そうしないと、後ろに歩きたいだけのときも構えてしまいます。
        return Opponent != nullptr && Opponent->SM.CurrentState == CharState::Attack;
    }

    // 硬直が明けたときに戻る状態。
    // 空中なら空中へ、下を入れているならしゃがみへ戻します。
    void ReturnToNeutral(const RawInput& raw) {
        if (PositionY < (GroundY - 0.01)) { SM.ChangeState(CharState::Jump, ""); return; }
        if (raw.Down) SM.ChangeState(CharState::Crouch, "");
        else SM.ChangeState(CharState::Idle, "");
    }

    // このキャラクターの前進が「ステップ」方式か。
    bool UsesStep() const { return Stats.ForwardMoveType == "step"; }

    // ステップの速さ（px/秒）。
    // 「決めた距離を、決めたフレーム数でちょうど進みきる」速さを
    // 逆算します。距離とフレーム数のどちらを変えても、進む距離は
    // 必ず StepDistance になります。
    double StepSpeed() const {
        int frames = std::max(1, Stats.StepFrames);
        return Stats.StepDistance * Constants::Fps / frames;
    }

    // このキャラクターの後退が「バックステップ」方式か。
    bool UsesBackStep() const { return Stats.BackMoveType == "step"; }

    // バックステップの速さ（px/秒）。前ステップと同じ考え方で、
    // 「決めた距離を決めたフレーム数で下がりきる」速さを逆算します。
    double BackStepSpeed() const {
        int frames = std::max(1, Stats.BackStepFrames);
        return Stats.BackStepDistance * Constants::Fps / frames;
    }

    // 前・前 が成立したときの踏み込み開始。
    void StartForwardDash() {
        if (UsesStep()) {
            StepTimer = std::max(1, Stats.StepFrames);
            // 「前・前」の記録を消します。消さないと、ステップ中に
            // 入れた前入力でもう一度成立して 2 回連続で飛んでしまいます。
            LastForwardTapFrame = -999;
        } else {
            DashTimer = DashDuration;
        }
    }

    // -----------------------------------------------------------------
    // ドライブラッシュキャンセル（技をキャンセルして前へ踏み込む）
    // -----------------------------------------------------------------
    // 技の途中で 前・前 と入れると、残りのフレームを打ち切って
    // 前ステップ（またはダッシュ）に移ります。技ごとに
    // 「何フレーム目から何フレーム目まで」「ヒット時だけか」などを
    // 設定できます（MoveData の CancelKind::DriveRush）。
    bool TryDriveRushCancel(const RawInput& raw) {
        if (CurrentMoveData == nullptr) return false;
        if (!ForwardDoubleTapped) return false;
        if (DashTimer > 0 || StepTimer > 0) return false;
        if (!CurrentMoveData->AllowsCancel(CancelKind::DriveRush, SM.CurrentFrame,
                                           CurrentMoveContact)) {
            return false;
        }
        FinishMove(raw);          // 技を打ち切る（残りの持続・硬直を飛ばす）
        StartForwardDash();
        VelocityX = (StepTimer > 0 ? StepSpeed() : Stats.DashSpeed) * Facing;
        SM.ChangeState(CharState::WalkForward, "");
        PendingSounds.push_back("dash");
        return true;
    }

    // 後ろ・後ろ が成立したときのバックステップ開始。
    void StartBackDash() {
        if (UsesBackStep()) {
            BackStepTimer = std::max(1, Stats.BackStepFrames);
            LastBackTapFrame = -999;
        } else {
            BackDashTimer = DashDuration;
        }
    }

    // 値を目標へ step ずつ近づける（行き過ぎないように）。
    static double MoveToward(double current, double target, double step) {
        if (current < target) return std::min(current + step, target);
        if (current > target) return std::max(current - step, target);
        return target;
    }

    // 地上での移動・ジャンプ・しゃがみ・ガード姿勢の処理。
    void HandleGroundMovement(const RawInput& raw) {
        // ---- ステップ中は操作を受け付けない ----
        // ステップは「キャラクター 1 体ぶん前へ踏み込む」決まった動きです。
        // 途中でレバーを離しても最後まで進みきるのが肝心で、そのおかげで
        // 「ステップ 1 回でどれだけ近づけるか」を体で覚えられます。
        // 逆にここで入力を見てしまうと、進む距離が毎回変わってしまい、
        // ダッシュと変わらないものになります。
        //
        // なお技でキャンセルすることはできます（TryStartMove が
        // この関数より先に呼ばれるため）。踏み込んでから殴る、という
        // 使い方ができるのはそのおかげです。
        if (StepTimer > 0) {
            StepTimer -= 1;
            VelocityX = StepSpeed() * Facing;
            SM.ChangeState(CharState::WalkForward, "");
            if (StepTimer <= 0) VelocityX = 0.0;
            return;
        }
        // バックステップも同じ考え方で、最後まで下がりきります。
        // フレーム数も距離も前ステップとは別の設定です。
        if (BackStepTimer > 0) {
            BackStepTimer -= 1;
            VelocityX = -BackStepSpeed() * Facing;
            SM.ChangeState(CharState::WalkBackward, "");
            if (BackStepTimer <= 0) VelocityX = 0.0;
            return;
        }

        IsCrouchingGuard = raw.Down && IsHoldingBack(raw);

        // 上入力 → ジャンプ。斜めに入れれば前ジャンプ・後ろジャンプ。
        if (raw.Up && !raw.Down) {
            VelocityY = Stats.JumpVelocity;
            SM.ChangeState(CharState::Jump, "");
            if (IsHoldingForward(raw)) VelocityX = Stats.WalkForwardSpeed;
            else if (IsHoldingBack(raw)) VelocityX = -Stats.WalkForwardSpeed;
            else VelocityX = 0.0;
            return;
        }

        // 下入力 → しゃがみ（後ろも入れていればしゃがみガード姿勢）
        if (raw.Down) {
            if (IsHoldingBack(raw)) SM.ChangeState(CharState::Block, "");
            else SM.ChangeState(CharState::Crouch, "");
            VelocityX = 0.0;
            return;
        }

        if (IsHoldingForward(raw)) {
            // 前入力。短い間に 2 回入れると、前へ踏み込みます
            //（DashInputWindow フレーム以内の 2 回目を検出）。
            // 踏み込み方はキャラクターの設定で 2 通りあります。
            // 2 回目の「入れ直し」が、前回から DashInputWindow フレーム
            // 以内なら踏み込みます。押しっぱなしは 1 回目のままなので、
            // ただの前歩きになります。
            if (ForwardDoubleTapped && DashTimer <= 0 && StepTimer <= 0) StartForwardDash();
            if (StepTimer > 0) {
                // 踏み込んだ最初のフレーム。次のフレームからは上の
                // 「ステップ中」の分岐が処理を引き継ぎます。
                VelocityX = StepSpeed() * Facing;
                SM.ChangeState(CharState::WalkForward, "");
                return;
            }
            double spd = Stats.WalkForwardSpeed;
            if (DashTimer > 0) spd = Stats.DashSpeed;
            VelocityX = spd * Facing;
            SM.ChangeState(CharState::WalkForward, "");
        } else if (IsHoldingBack(raw)) {
            // 後ろ・後ろ でバックステップ（バックダッシュ）。
            // ガード姿勢に入る前に見ます。相手が攻撃している最中こそ
            // 後ろへ逃げたい場面なので、そこで出せないと意味がありません。
            if (BackDoubleTapped && BackDashTimer <= 0 && BackStepTimer <= 0) {
                StartBackDash();
                if (BackStepTimer > 0) {
                    VelocityX = -BackStepSpeed() * Facing;
                    SM.ChangeState(CharState::WalkBackward, "");
                    return;
                }
            }
            // 後ろ入力。相手が技を出していれば、下がらずガード姿勢に入ります。
            // このとき速度を必ず 0 にするのが大事です。0 にしないと、
            // 同じフレームで設定した後退速度が残り、ガード硬直中に
            // ずるずる滑って見えてしまいます。
            if (Opponent != nullptr && Opponent->SM.CurrentState == CharState::Attack) {
                VelocityX = 0.0;
                SM.ChangeState(CharState::Block, "");
            } else {
                double spd = Stats.WalkBackwardSpeed;
                if (BackDashTimer > 0) spd = Stats.BackDashSpeed;
                VelocityX = -spd * Facing;
                SM.ChangeState(CharState::WalkBackward, "");
            }
        } else {
            VelocityX = 0.0;
            SM.ChangeState(CharState::Idle, "");
        }
    }

    // =================================================================
    // 技を出す
    // =================================================================
    // 今フレームの入力で出せる技を探し、出せたら true を返します。
    //
    // 優先順位が重要です。同じ「パンチ」の入力でも、
    // 超必殺技 → 必殺技 → 通常技 の順に調べます。
    // 逆順だと、波動拳コマンドを入れても先に立ち弱パンチが出てしまい、
    // 必殺技が永遠に出せません。
    bool TryStartMove(const RawInput& raw, const std::vector<std::string>& pressed) {
        if (Moveset == nullptr) return false;

        // 「今フレーム押されたボタン」と「先行入力で覚えているボタン」を
        // 合わせて候補にします。動けない間に押したボタンが、
        // 動けるようになった瞬間に技として出るのはこの合成のおかげです。
        std::vector<std::string> usable = pressed;
        for (const auto& p : InputQueue) {
            if (std::find(usable.begin(), usable.end(), p.button) == usable.end()) {
                usable.push_back(p.button);
            }
        }
        // 投げ（LP＋LK の同時押し）も先行入力から復元します。
        bool hasLP = std::find(usable.begin(), usable.end(), "LP") != usable.end();
        bool hasLK = std::find(usable.begin(), usable.end(), "LK") != usable.end();
        bool hasThrow = std::find(usable.begin(), usable.end(), "Throw") != usable.end();
        if (hasLP && hasLK && !hasThrow) usable.push_back("Throw");

        if (usable.empty()) return false;
        // 姿勢は「今フレームのレバー」から決めます。
        //
        // 状態（立ち / しゃがみ）だけで決めてはいけません。状態が
        // しゃがみに変わるのは、この関数のあとに呼ばれる
        // HandleGroundMovement なので、立ち状態から下＋ボタンを同時に
        // 押した最初のフレームは、まだ「立ち」のままだからです。
        // それだと、しゃがみたいのに立ち技が出てしまいます。
        std::string stance = StanceForInput(raw);

        std::vector<const MoveData*> superCandidates, specialCandidates, normalCandidates;

        // コマンド技（"236" などの入力を持つ技）
        for (const auto& kv : *Moveset) {
            const MoveData& move = kv.second;
            if (!move.InputCommand.empty() &&
                CommandParser::Matches(InputBuf, move.InputCommand, move.Button, Constants::CommandWindow)) {
                if (move.HasTag(Constants::TagSuper)) superCandidates.push_back(&move);
                else if (move.HasTag(Constants::TagSpecial)) specialCandidates.push_back(&move);
                else normalCandidates.push_back(&move);
            }
        }
        // 通常技（ボタンだけ。姿勢が一致するものだけ）
        for (const auto& btn : usable) {
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

    // 「今フレームのレバー入力から見て、どの姿勢で技を出すか」。
    //   ・空中にいれば空中技
    //   ・下を入れていればしゃがみ技（しゃがみガード中も含む）
    //   ・それ以外は立ち技
    std::string StanceForInput(const RawInput& raw) const {
        if (SM.CurrentState == CharState::Jump || PositionY < (GroundY - 0.01)) return "air";
        if (raw.Down && !raw.Up) return "crouch";
        if (SM.CurrentState == CharState::Crouch) return "crouch";
        if (SM.CurrentState == CharState::Block && IsCrouchingGuard) return "crouch";
        return "stand";
    }

    // 「今どの姿勢か」（レバーを見ない版）。判定や描画に使います。
    std::string CurrentStance() const {
        if (SM.CurrentState == CharState::Jump) return "air";
        if (PositionY < (GroundY - 0.01)) return "air";
        if (SM.CurrentState == CharState::Crouch) return "crouch";
        if (SM.CurrentState == CharState::Block && IsCrouchingGuard) return "crouch";
        return "stand";
    }

    // しゃがみ中・ガード中は「踏ん張っている」ものとして、
    // 押し合いで押されないようにします。歩いてきた相手に
    // ガードごと押し込まれると理不尽なためです。
    bool IsPlanted() const {
        return SM.CurrentState == CharState::Crouch || SM.CurrentState == CharState::Block;
    }

    // その技を今出せるか。
    bool CanStart(const MoveData& move) const {
        // ゲージが足りなければ超必殺技は出せない
        if ((move.HasTag(Constants::TagSuper) || move.MeterCost > 0) && !Gauge.CanSpend(move.MeterCost)) {
            return false;
        }
        if (SM.CurrentState == CharState::Attack) {
            if (CurrentMoveData == nullptr) return false;
            // 技の最中に出せるのはキャンセル可能時間帯だけ。
            // 出したい技の種類（必殺技 / 超必殺技 / 通常技）ごとに
            // 別々の設定を見るので、「必殺技にはキャンセルできるが
            // 通常技にはできない」といった作り分けができます。
            return MoveExecutor::CanCancelInto(*CurrentMoveData, SM.CurrentFrame,
                                               CurrentMoveContact, move);
        }
        return true;
    }

    void StartMove(const MoveData& move) {
        if (move.MeterCost > 0) Gauge.Spend(move.MeterCost);
        CurrentMoveData = &move;
        CurrentMoveContact = MoveContact::Whiff; // まだ当たっていない
        ProjectileSpawnedThisActivation = false;
        AlreadyHit.clear();
        HitboxesWereLive = false;
        FacingLocked = true; // 技の最中に向きが変わらないように固定

        // 地上技は必ず止まった状態から始めます。
        // これをしないと、歩きながら技を出したときの移動速度が
        // 技の間ずっと残り続け（技中は VelocityX を触らないため）、
        // キャラクターが前に滑っていくように見えてしまいます。
        // 空中技は逆に、ジャンプの勢いを保つのが自然なのでそのままです。
        if (CurrentStance() != "air") VelocityX = 0.0;

        // 先行入力を空にします。消さないと、1 回の押しがバッファに
        // 残ったまま、キャンセル可能時間帯に入った瞬間もう一度技を
        // 出してしまいます（押していないのに 2 回出る）。
        InputQueue.clear();

        LandingRecoveryTimer = 0;
        LandedDuringMove = false;

        SM.ChangeState(CharState::Attack, move.Id);
        // 技のフレームは 1 から数えます。技を出したこのフレームが 1F 目
        // なので、発生 1F の技はこのフレームにもう攻撃判定が出ます。
        //（ChangeState は同じ技への切り替えでは何もしないので、
        //  同じ技を続けて出すときのために、ここで必ず 1 に戻します）
        SM.CurrentFrame = 1;
        PendingSounds.push_back("attack");

        // 1F 目の攻撃判定をこのフレームのうちに用意します。
        // 次のフレームまで待つと、発生が 1 フレーム遅れてしまいます。
        ProgressMove();
    }

    // 技が終わったか（＝このフレームから動けるか）。
    //
    // 地上技は「発生＋持続＋硬直」を過ぎたら終わりです。
    // 空中技は違います。硬直は空中では消化されず、着地してから
    // 着地硬直として消化するので、着地するまで終わりません。
    bool IsMoveOver() const {
        if (CurrentMoveData == nullptr) return true;
        const MoveData& move = *CurrentMoveData;
        if (move.IsAirMove()) {
            if (PositionY < (GroundY - 0.01)) return false; // まだ空中
            return LandedDuringMove && LandingRecoveryTimer <= 0;
        }
        return MoveExecutor::GetPhase(move, SM.CurrentFrame) == MovePhase::Done;
    }

    // 技を終わらせて、元の姿勢へ戻す。
    //
    // 立ち技のあとは立ち、しゃがみ技のあとはしゃがみへ戻します。
    // ここで必ず Idle に戻してしまうと、しゃがみ技を出したあとに
    // 1 フレームだけ立ち姿勢が見えてしまいます。
    void FinishMove(const RawInput& raw) {
        ActiveHitboxRects.clear();
        HitboxesWereLive = false;
        FacingLocked = false;
        bool wasAir = PositionY < (GroundY - 1.0);
        bool crouchMove = CurrentMoveData != nullptr && CurrentMoveData->Stance == "crouch";
        CurrentMoveData = nullptr;
        LandingRecoveryTimer = 0;
        LandedDuringMove = false;
        if (wasAir) {
            SM.ChangeState(CharState::Jump, "");        // 空中技なら空中に戻る
        } else if (raw.Down || (crouchMove && raw.Down)) {
            SM.ChangeState(CharState::Crouch, "");      // しゃがんだまま
        } else {
            SM.ChangeState(CharState::Idle, "");
        }
    }

    // 技を 1 フレーム進める。
    void ProgressMove() {
        if (CurrentMoveData == nullptr) { SM.ChangeState(CharState::Idle, ""); return; }
        int frame = SM.CurrentFrame;
        const MoveData& move = *CurrentMoveData;
        MovePhase phase = MoveExecutor::GetPhase(move, frame);

        // 空中技の着地。着地したフレームに着地硬直を積みます。
        if (move.IsAirMove() && !LandedDuringMove && PositionY >= (GroundY - 0.01)) {
            LandedDuringMove = true;
            LandingRecoveryTimer = move.LandingRecoveryFrames();
            VelocityX = 0.0;  // 着地したらその場で止まる
        }

        // 攻撃判定は毎フレーム作り直します（フレームごとに形が変わる
        // 技に対応するため）。ただし「もう当てた相手」の記録をリセット
        // するのは、判定が出た最初の 1 フレームだけです。毎フレーム
        // リセットすると、持続 3 フレームの技が 3 回当たってしまいます。
        bool live = MoveExecutor::HasLiveHitboxes(move, frame);
        if (live) {
            if (!HitboxesWereLive) AlreadyHit.clear();
            ActiveHitboxRects = MoveExecutor::GetActiveHitboxRects(move, frame, Facing, PositionX, PositionY);
        } else {
            ActiveHitboxRects.clear();
        }
        HitboxesWereLive = live;

        // 飛び道具は持続フレームに入った瞬間、1 回だけ生成を注文します。
        if (phase == MovePhase::Active && move.HasTag(Constants::TagProjectile) &&
            !ProjectileSpawnedThisActivation) {
            ProjectileSpawnedThisActivation = true;
            PendingProjectileValid = true;
            PendingProjectileRequestData = {&move, PositionX, PositionY, Facing};
        }

        // 技が終わったかどうかの判定と、そこからの復帰は
        // AdvanceStateTimers / FinishMove が受け持ちます
        //（硬直が明けたフレームにそのまま動けるようにするため）。
    }

    // =================================================================
    // 攻撃を受けたときの処理（BattleSystem から呼ばれる）
    // =================================================================
    bool IsInvincibleAgainst(const std::string& kind) const {
        if (SM.CurrentState == CharState::Attack && CurrentMoveData != nullptr) {
            return MoveExecutor::IsInvincible(*CurrentMoveData, SM.CurrentFrame, kind);
        }
        return false;
    }

    HitResult ReceiveHit(const MoveData& move, Fighter& attacker) {
        if (IsDead) return {false, true};

        // 無敵判定。投げなら投げ無敵、それ以外なら打撃無敵を見ます。
        std::string invKind = Constants::InvincibleStrike;
        if (move.GuardType == Constants::GuardThrow) invKind = Constants::InvincibleThrow;
        if (IsInvincibleAgainst(invKind)) return {false, true};

        // カウンターヒットの判定。
        // 「自分が技の何段階目にいるときに殴られたか」で決まります。
        // 状態を書き換える前に調べるのが大事です（下の処理で
        // CharState::Hitstun に変わってしまうので）。
        CounterKind counter = CounterKind::None;
        if (SM.CurrentState == CharState::Attack && CurrentMoveData != nullptr) {
            MovePhase myPhase = MoveExecutor::GetPhase(*CurrentMoveData, SM.CurrentFrame);
            // 空中技で着地待ち・着地硬直中なら「硬直中」とみなします
            //（GetPhase の上では技が終わったことになっているため）。
            if (CurrentMoveData->IsAirMove() && myPhase == MovePhase::Done) {
                myPhase = MovePhase::Recovery;
            }
            if (myPhase == MovePhase::Startup) counter = CounterKind::Counter;
            else if (myPhase == MovePhase::Recovery) counter = CounterKind::EffectiveCounter;
        }

        bool blocked = false;
        if (move.GuardType != Constants::GuardThrow) blocked = CheckGuard(move);
        if (blocked) counter = CounterKind::None; // ガードできたならカウンターではない

        // ストップの長さ。ヒットとガードで別の値です。
        // ここでは自分（防御側）に掛けるだけで、攻撃側には触りません。
        // 攻撃側にも同じ値を同じフレームに掛けるのは呼び出し元
        //（BattleSystem::ApplyStop）の仕事です。1 か所にまとめておけば、
        // 「片方だけ長く止まって有利不利がずれる」ことが起こりません。
        int stopFrames = blocked ? move.Guardstop : move.Hitstop;
        HitstopTimer = std::max(HitstopTimer, stopFrames);

        if (blocked) {
            // ---- ガードされた ----
            int chip = static_cast<int>(std::lround(move.Damage * move.ChipDamagePercent));
            CurrentHP = std::max(0, CurrentHP - chip); // 削りダメージ
            BlockstunTimer = move.BlockstunFrames();
            SM.ChangeState(CharState::Block, "");
            Gauge.Add(move.MeterGain * 0.5); // ガードでもゲージは半分溜まる
            ApplyKnockback(move, attacker, true);
            PendingEffects.push_back({"guard", PositionX, PositionY});
            PendingSounds.push_back("block");
        } else {
            // ---- 当たった ----
            CurrentHP = std::max(0, CurrentHP - move.Damage);
            Gauge.Add(move.MeterGain);
            ApplyKnockback(move, attacker, false);
            if (move.GuardType == Constants::GuardThrow) {
                ThrownTimer = ThrownLockFrames;
                SM.ChangeState(CharState::Throw, "");
            } else if (move.HitOutcome == Constants::HitNormal) {
                HitstunTimer = move.HitstunFrames();
                SM.ChangeState(CharState::Hitstun, "");
            } else {
                EnterKnockdown(move.HitOutcome == std::string(Constants::HitHardKnockdown),
                               move.KnockdownDuration());
            }
            // 演出の種類を技の強さから選ぶ。
            // 弱 → 中 → 強 → 必殺 → 超必 の順に、火花が大きくなります。
            // 「強い技を当てた」ことが見た目だけで分かるようにするためで、
            // これがヒットストップ・ノックバックと合わさって
            // 「当たった感じ」を作ります。
            std::string fx = "hit"; // 弱（既定）
            if (move.HasTag(Constants::TagSuper)) fx = "super";
            else if (move.HasTag(Constants::TagSpecial)) fx = "special";
            else if (move.HasTag(Constants::TagHeavy)) fx = "heavy_hit";
            else if (move.HasTag(Constants::TagMedium)) fx = "medium_hit";
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
        return {blocked, false, counter, stopFrames};
    }

    void EnterKnockdown(bool hard, int customFrames) {
        if (customFrames > 0) KnockdownTimer = customFrames;
        else if (hard) KnockdownTimer = HardKnockdownFrames;
        else KnockdownTimer = KnockdownFrames;
        SM.ChangeState(CharState::Knockdown, "");
    }

    // ガードが成立するかの判定。
    //
    // 「今このフレームに記録された入力」を見て判断します。
    // BattleSystem は必ず FrameStep（入力の記録）を先に呼んでから
    // 当たり判定を処理するので、この時点で最新の入力が入っています。
    bool CheckGuard(const MoveData& move) const {
        if (InputBuf.History.empty()) return false;
        const auto& lastEntry = InputBuf.History.back();
        int digit = lastEntry.digit;
        bool holdingBack = digit == 1 || digit == 4 || digit == 7; // 後ろ方向
        bool inGuardPosture = (SM.CurrentState == CharState::Block) ||
                              (SM.IsActionable() && holdingBack);
        if (!inGuardPosture || !holdingBack) return false;

        // ガードの種類ごとの成立条件。ここが「下段はしゃがみガード、
        // 中段は立ちガード」という格闘ゲームの基本ルールです。
        if (move.GuardType == Constants::GuardHigh) return true; // どちらでも防げる
        if (move.GuardType == Constants::GuardLow) {
            return IsCrouchingGuard || SM.CurrentState == CharState::Crouch;
        }
        if (move.GuardType == Constants::GuardOverhead) {
            return !(IsCrouchingGuard || SM.CurrentState == CharState::Crouch);
        }
        return false;
    }

    void ApplyKnockback(const MoveData& move, const Fighter& attacker, bool isBlock) {
        // 吹き飛ぶ向きは「攻撃した人が向いている方向」です。
        // 攻撃が当たった時点で攻撃側は必ず相手のほうを向いているので、
        // その向きがそのまま「相手を遠ざける向き」になります。
        // （ここを -attacker.Facing にすると相手を引き寄せてしまい、
        //   押し合い処理と毎フレーム喧嘩して、2 人が変な動きをします）
        double dir = attacker.Facing;
        double kx = move.KnockbackX;
        if (isBlock) kx *= 0.4; // ガード時は控えめに
        VelocityX = kx * dir;
        if (!isBlock && move.KnockbackY != 0.0) {
            VelocityY = -std::abs(move.KnockbackY); // 上方向はマイナス
        }
    }

    // =================================================================
    // 物理・座標まわり
    // =================================================================
    void ApplyPhysics(double dt) {
        // 空中なら重力で下向きに加速。地上なら縦速度は 0。
        if (SM.CurrentState == CharState::Jump || PositionY < (GroundY - 0.01)) {
            VelocityY += Stats.Gravity * dt;
        } else {
            VelocityY = 0.0;
        }
        PositionX += VelocityX * dt;
        PositionY += VelocityY * dt;
        if (PositionY > GroundY) { PositionY = GroundY; VelocityY = 0.0; } // 着地

        // 攻撃判定はキャラクターと一緒に動くので、位置が変わったら
        // 作り直します（移動しながら出す技でも判定がついてくる）。
        if (!ActiveHitboxRects.empty() && CurrentMoveData != nullptr) {
            ActiveHitboxRects = MoveExecutor::GetActiveHitboxRects(
                *CurrentMoveData, SM.CurrentFrame, Facing, PositionX, PositionY);
        }
    }

    // 今の姿勢での押し合い判定の「半分の幅」。
    // 画面端で体がはみ出さないようにするために使います。
    double PushboxHalfWidth() const {
        if (const FrameBoxSet* fb = CurrentFrameBoxes(); fb != nullptr && fb->hasPushbox) {
            return fb->pushbox.Width / 2.0;
        }
        return Stats.Pushboxes.ForStance(Stance()).Width / 2.0;
    }

    // 指定した向き（+1 右 / -1 左）へ、壁にぶつかるまであと何動けるか。
    // ClampToStage と同じ「判定の半分の幅ぶん内側で止まる」基準で
    // 測るので、押し合いの計算が壁を突き抜ける想定をしなくなります。
    double RoomToWall(double dir) const {
        double half = PushboxHalfWidth();
        double room = (dir > 0) ? (std::floor(StageMaxX - half) - PositionX)
                                : (PositionX - std::ceil(StageMinX + half));
        return std::max(0.0, room);
    }

    // ステージの外へ出ないように位置を制限する。
    //
    // 以前は「中心座標」だけを見て止めていたため、画面端に張り付くと
    // 体の左半分（＝押し合い判定の半分）がステージの外にはみ出して
    // 見えていました。壁は体の表面で止まるべきなので、
    // 判定の半分の幅だけ内側で止めます。
    //
    // 姿勢によって判定の幅が変わる（しゃがみは立ちより広い）ので、
    // しゃがんだ瞬間に壁へめり込まないよう、そのつど計算し直します。
    void ClampToStage() {
        double half = PushboxHalfWidth();
        // ceil / floor で「整数に丸めた位置」を基準にします。
        // 判定の四角形は座標を整数に丸めてから作られる（PushboxRect）ので、
        // 丸める前の座標で止めると、丸めた結果 0.5 ピクセルだけ
        // 壁を突き抜けて見えることがあります。
        double minX = std::ceil(StageMinX + half);
        double maxX = std::floor(StageMaxX - half);
        // ステージが判定より狭いという異常時は中央に置く（安全策）。
        if (minX > maxX) { PositionX = (StageMinX + StageMaxX) / 2.0; return; }
        if (PositionX < minX) PositionX = minX;
        if (PositionX > maxX) PositionX = maxX;
    }

    // 相手のほうを向く。ただし技の最中（FacingLocked）や、
    // 動けない状態のときは向きを変えません。
    // 技の途中でくるっと振り向いたら不自然だからです。
    void UpdateFacing() {
        if (FacingLocked || Opponent == nullptr) return;
        if (!SM.IsActionable()) return;
        if (Opponent->PositionX >= PositionX) Facing = Constants::FacingRight;
        else Facing = Constants::FacingLeft;
    }

    // -----------------------------------------------------------------
    // 空中判定
    // -----------------------------------------------------------------
    // 「ゲーム上の空中判定」は、次の 2 つのどちらかで成立します。
    //   1. 実際に地面から浮いている（Y 座標が地面より上）
    //   2. 出している技が「このフレームは空中判定」と指定している
    //
    // 2 があるおかげで、見た目は地面に足がついていても空中扱いに
    // でき（昇龍拳の出際など）、地上投げを避けられます。逆に、
    // 指定が無ければ少し浮いて見えても地上判定のままです。
    bool IsPhysicallyAirborne() const { return PositionY < (GroundY - 0.01); }

    bool IsAirborne() const {
        if (IsPhysicallyAirborne()) return true;
        if (SM.CurrentState == CharState::Attack && CurrentMoveData != nullptr) {
            return CurrentMoveData->IsAirborneAtFrame(SM.CurrentFrame, IsPhysicallyAirborne());
        }
        return false;
    }

    // 判定に使う姿勢名。CurrentStance() と似ていますが、
    // こちらは実際の高さ（空中にいるか）としゃがみガードも見ます。
    std::string Stance() const {
        if (SM.CurrentState == CharState::Jump || IsAirborne()) return "air";
        if (SM.CurrentState == CharState::Crouch || IsCrouchingGuard) return "crouch";
        return "stand";
    }

    // 今のフレームに対する判定の上書き指定（あれば）。
    const FrameBoxSet* CurrentFrameBoxes() const {
        if (SM.CurrentState != CharState::Attack || CurrentMoveData == nullptr) return nullptr;
        return CurrentMoveData->FrameBoxesAt(SM.CurrentFrame);
    }

    // 今フレームの食らい判定（ワールド座標）。
    // 技による上書きがあればそちら、無ければ姿勢ごとの標準を使います。
    std::vector<RectBox> HurtboxRects() const {
        if (const FrameBoxSet* fb = CurrentFrameBoxes(); fb != nullptr && fb->hasHurtboxes) {
            return HurtboxSet::PlaceParts(fb->hurtboxes, Facing, PositionX, PositionY);
        }
        return Hurtboxes.RectsForStance(Stance(), Facing, PositionX, PositionY);
    }

    // 今フレームの押し合い判定（ワールド座標）。
    RectBox PushboxRect() const {
        double ox = std::round(PositionX), oy = std::round(PositionY);
        int f = Facing < 0 ? -1 : 1;
        if (const FrameBoxSet* fb = CurrentFrameBoxes(); fb != nullptr && fb->hasPushbox) {
            return RectBox(ox + fb->pushbox.CenterX * f, oy + fb->pushbox.CenterY,
                           fb->pushbox.Width, fb->pushbox.Height);
        }
        const RectBox& b = Stats.Pushboxes.ForStance(Stance());
        return RectBox(ox + b.CenterX * f, oy + b.CenterY, b.Width, b.Height);
    }

    // 投げられ得る状態か（投げる側ではなく、投げられる側の条件）。
    // ダウン中・起き上がり中・空中は投げられません。
    bool IsThrowable() const {
        if (IsDead) return false;
        if (Stance() == "air") return false;
        switch (SM.CurrentState) {
            case CharState::Knockdown:
            case CharState::WakeUp:
            case CharState::Throw:
            case CharState::Dead:
                return false;
            default:
                return true;
        }
    }

    const MoveData* GetMove(const std::string& id) const {
        if (Moveset == nullptr) return nullptr;
        auto it = Moveset->find(id);
        return it == Moveset->end() ? nullptr : &it->second;
    }

    // デバッグ表示（トレーニングモードで F1）に出す情報をまとめたもの。
    struct DebugInfoT {
        std::string state, move;
        int frame = 0, hp = 0;
        double gauge = 0, velocityX = 0, velocityY = 0;
        int hitstun = 0, blockstun = 0, hitstop = 0;
        double positionX = 0, positionY = 0;
        // 出している技のフレームデータ（トレーニングで確認しやすいように）
        int startup = 0, active = 0, recovery = 0, total = 0;
        int hitAdvantage = 0, blockAdvantage = 0;
        std::string phase;
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
        if (CurrentMoveData != nullptr) {
            const MoveData& m = *CurrentMoveData;
            d.startup = m.Startup;
            d.active = m.Active;
            d.recovery = m.Recovery;
            d.total = m.TotalFrames();
            d.hitAdvantage = m.HitAdvantage;
            d.blockAdvantage = m.BlockAdvantage;
            switch (MoveExecutor::GetPhase(m, SM.CurrentFrame)) {
                case MovePhase::Startup: d.phase = "STARTUP"; break;
                case MovePhase::Active: d.phase = "ACTIVE"; break;
                case MovePhase::Recovery: d.phase = "RECOVERY"; break;
                case MovePhase::Done: d.phase = LandingRecoveryTimer > 0 ? "LANDING" : "DONE"; break;
            }
        }
        return d;
    }
};

} // namespace kakuge
