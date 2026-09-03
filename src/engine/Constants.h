// =====================================================================
// engine/Constants.h - ゲーム全体で共有する「決めごと」
// =====================================================================
// ここには、あちこちのファイルから使われる定数（変わらない値）と
// 列挙型（選択肢の一覧）をまとめています。
//
// なぜ 1 か所にまとめるのか
// -----------------------
// 例えばガードの種類を表す "High" という文字列を、Fighter.cpp と
// BattleSystem.cpp と CPUAI.cpp にそれぞれ直接書いてしまうと、
// あとで "Standing" に変えたくなったとき 3 か所を直す必要があり、
// 1 か所直し忘れただけで「なぜかガードできない」バグになります。
// こういう「意味を持つ文字列・数値」は必ずここに集めて、
// 各ファイルは Constants::GuardHigh のように参照します。
//
// このファイルは engine/ の中で最も「下」にある土台なので、
// ほかのどのファイルも include していません（循環参照を防ぐため）。
// =====================================================================
#pragma once
#include <string>

namespace kakuge {

// ---------------------------------------------------------------------
// キャラクターの状態（ステート）
// ---------------------------------------------------------------------
// 格闘ゲームのキャラクターは、常にこのうちのどれか 1 つの状態にいます。
// 「立ち → 攻撃 → 立ち」「食らい → ダウン → 起き上がり → 立ち」のように
// 状態を移り変わっていくのが、格闘ゲームの動作の基本です。
// この移り変わりを管理するのが StateMachine.h です。
enum class CharState {
    Idle,          // 立ち（何もしていない）
    WalkForward,   // 前進
    WalkBackward,  // 後退
    Crouch,        // しゃがみ
    Jump,          // ジャンプ中（空中）
    Attack,        // 技を出している最中
    Block,         // ガード中
    Hitstun,       // 攻撃を食らってのけぞっている最中（動けない）
    Knockdown,     // ダウン（倒れている）
    WakeUp,        // 起き上がり中
    Throw,         // 投げられている最中
    Dead           // 体力 0（KO された）
};

// 状態を画面に表示するとき（デバッグ表示など）に使う名前。
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

// ---------------------------------------------------------------------
// トレーニングモードで 2P（練習相手）に何をさせるか
// ---------------------------------------------------------------------
// CPU なら普通に戦い、Stand / Crouch / Jump ならその姿勢で固まります。
// コンボの練習で「相手をしゃがませたまま固定したい」ときに使います。
enum class DummyMode { CPU, Stand, Crouch, Jump };

struct Constants {
    // このゲームは 1 秒間に 60 回、戦闘の計算を行います（60FPS）。
    // 格闘ゲームでは「発生 5F（フレーム）」のように、時間をこの 1/60 秒を
    // 単位として数えるのが慣習です。技のデータもすべてフレーム単位です。
    static constexpr int Fps = 60;

    // ---- ガードの種類（どう防げる攻撃か）----
    static constexpr const char* GuardHigh = "High";        // 立ちでもしゃがみでも防げる（中段扱いの通常技）
    static constexpr const char* GuardLow = "Low";          // しゃがみガードでしか防げない（下段）
    static constexpr const char* GuardOverhead = "Overhead";// 立ちガードでしか防げない（中段）
    static constexpr const char* GuardThrow = "Throw";      // 投げ（ガード不能。当たり判定ではなく距離で判定）

    // ---- ヒットしたあと相手がどうなるか ----
    static constexpr const char* HitNormal = "Normal";              // のけぞるだけ
    static constexpr const char* HitKnockdown = "Knockdown";        // ダウンする
    static constexpr const char* HitHardKnockdown = "HardKnockdown";// 長くダウンする
    static constexpr const char* HitLaunch = "Launch";              // 打ち上げ
    static constexpr const char* HitWallBounce = "WallBounce";      // 壁バウンド
    static constexpr const char* HitGroundBounce = "GroundBounce";  // 地面バウンド

    // ---- 無敵の種類 ----
    // 昇龍拳のような技は「出始めの数フレームだけ攻撃を受け付けない」
    // という性質を持ちます。それをどの範囲に効かせるかの指定です。
    static constexpr const char* InvincibleNone = "None";      // 無敵なし
    static constexpr const char* InvincibleFull = "Full";      // 打撃も投げも無敵
    static constexpr const char* InvincibleStrike = "Strike";  // 打撃だけ無敵
    static constexpr const char* InvincibleThrow = "Throw";    // 投げだけ無敵

    // ---- 技につける「タグ」（性質のラベル）----
    // 1 つの技に複数付けられます。例えば波動拳は Special + Projectile。
    // CPU の思考（CPUAI.h）や、エフェクトの色分けに使われます。
    static constexpr const char* TagLight = "Light";          // 弱攻撃
    static constexpr const char* TagMedium = "Medium";        // 中攻撃
    static constexpr const char* TagHeavy = "Heavy";          // 強攻撃
    static constexpr const char* TagNormal = "Normal";        // 通常技
    static constexpr const char* TagSpecial = "Special";      // 必殺技
    static constexpr const char* TagSuper = "Super";          // 超必殺技（ゲージ消費）
    static constexpr const char* TagAntiAir = "AntiAir";      // 対空技
    static constexpr const char* TagProjectile = "Projectile";// 飛び道具
    static constexpr const char* TagLow = "Low";              // 下段
    static constexpr const char* TagOverhead = "Overhead";    // 中段
    static constexpr const char* TagThrow = "Throw";          // 投げ
    static constexpr const char* TagReversal = "Reversal";    // リバーサル向き

    // 向き。+1 が右向き、-1 が左向き。掛け算するだけで座標を反転できる
    // ので、if 文を書かずに左右対称の処理が書けます。
    static constexpr int FacingRight = 1;
    static constexpr int FacingLeft = -1;

    // 入力履歴を何フレーム分覚えておくか（コマンド技の判定に使う）。
    static constexpr int InputBufferLength = 20;
    // コマンド（例: 236 = 波動拳）を何フレーム以内に入力し切れば
    // 成立とみなすか。長いほど簡単、短いほどシビアになります。
    static constexpr int CommandWindow = 16;
};

// =====================================================================
// GameSpec - 画面・キャラクター・当たり判定の基準寸法
// =====================================================================
// このゲームは 384x224 ピクセルという小さな画面（1990 年代のアーケード
// 基板 CPS2 と同じ解像度）を「内部の本当の画面」として使い、それを
// 実際のウィンドウサイズまで整数倍に拡大して表示します。
// こうすると、どんな解像度のディスプレイでも、にじみのない
// くっきりしたドット絵として表示できます。
//
// ここに並んでいるのは、その 384x224 の中での寸法です。
// 単位は「カメラ倍率 1.0 のときの画面ピクセル」で、ゲーム内部の
// 座標の単位（ワールド単位）と同じです。
// =====================================================================
struct GameSpec {
    static constexpr int BaseWidth = 384;   // 内部画面の幅
    static constexpr int BaseHeight = 224;  // 内部画面の高さ

    // 地面の高さ（画面の上から 189 ピクセル目 ＝ 高さの約 84%）。
    // 両キャラクターの足はこの線の上に立ちます。
    static constexpr int GroundY = 189;

    // 1 コマぶんのキャラクター画像を描くときの推奨キャンバスサイズ。
    static constexpr int CharacterSpriteWidth = 75;
    static constexpr int CharacterSpriteHeight = 90;
    // そのキャンバスの中で、実際に体が占める大きさ。
    // 描画側の拡大率はこの「高さ 88」から逆算しています。
    static constexpr int CharacterVisualWidth = 55;
    static constexpr int CharacterVisualHeight = 88;

    // ラウンド開始時の立ち位置（画面幅の 30% と 70% の位置）。
    // 2 人の間隔は 268 - 116 = 152 ピクセルになります。
    static constexpr int Player1StartX = 116;
    static constexpr int Player2StartX = 268;

    static constexpr int HudHeight = 30;        // 画面上部（体力ゲージ・制限時間）の帯の高さ
    static constexpr int SuperGaugeHeight = 18; // 画面下部（超必ゲージ）の帯の高さ

    // ---- 押し合い判定（プッシュボックス）----
    // 体と体が重ならないように押し合うためだけの四角形で、
    // 攻撃が当たるかどうかには一切関係しません。
    // 立ち・しゃがみ・空中で大きさが変わります。
    static constexpr int PushboxStandWidth = 30, PushboxStandHeight = 72;
    static constexpr int PushboxCrouchWidth = 32, PushboxCrouchHeight = 48;
    static constexpr int PushboxAirWidth = 28, PushboxAirHeight = 52;

    // 投げが成立する距離（中心から中心まで）。投げだけは当たり判定の
    // 四角形ではなく、この距離で成立するかを決めます。
    static constexpr int NormalThrowRange = 28;

    // 通常技の当たり判定の大きさの目安。実際に使われる値は
    // data/moves/ryu/*.json のほうで、ここはその元になった基準値です。
    static constexpr int LightPunchHitboxWidth = 16, LightPunchHitboxHeight = 10;
    static constexpr int HeavyPunchHitboxWidth = 20, HeavyPunchHitboxHeight = 12;
    static constexpr int LightKickHitboxWidth = 18, LightKickHitboxHeight = 10;
    static constexpr int HeavyKickHitboxWidth = 24, HeavyKickHitboxHeight = 14;
    static constexpr int CrouchLightKickHitboxWidth = 20, CrouchLightKickHitboxHeight = 8;
};

// =====================================================================
// StageConstants - ステージ（戦う場所）の広さと開始位置
// =====================================================================
// ゲーム内部の座標は「画面の中央が X=0」です。左に行くとマイナス、
// 右に行くとプラス。画面座標（左上が 0,0）ではないので注意してください。
// 画面座標への変換は描画側（platform/Camera.h）が担当します。
// =====================================================================
struct StageConstants {
    // ステージ全体の幅。384 の画面より広いので、カメラが左右に動きます。
    static constexpr double StageWidth = 640.0;
    static constexpr double StageMinX = -StageWidth / 2.0;  // 左端
    static constexpr double StageMaxX = StageWidth / 2.0;   // 右端

    // ラウンド開始位置。GameSpec の画面座標 116/268 を、
    // 「画面中央が 0」の座標に直したもの（-76 と +76）。
    static constexpr double Player1StartX = GameSpec::Player1StartX - GameSpec::BaseWidth / 2.0;
    static constexpr double Player2StartX = GameSpec::Player2StartX - GameSpec::BaseWidth / 2.0;
    static constexpr double PlayerStartDistance = Player2StartX - Player1StartX; // 152

    // キャラクターの身長が画面の高さの何割を占めるか（88 / 224 ≒ 0.39）。
    static constexpr double PlayerHeightRatio =
        static_cast<double>(GameSpec::CharacterVisualHeight) / GameSpec::BaseHeight;
};

} // namespace kakuge
