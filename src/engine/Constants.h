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
// CPU なら普通に戦い、それ以外はその行動で固まります。
//   Stand / Crouch / Jump … その姿勢のまま動かない
//   Guard                 … 後ろを入れ続けて必ずガードする
// コンボの練習で「相手をしゃがませたまま固定したい」ときや、
// ガードされたときの硬直差を確かめたいときに使います。
enum class DummyMode { CPU, Stand, Crouch, Jump, Guard };

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
// 対戦中の表示倍率は常に 100% 固定なので、
//   ここに書いた数値 = 画面にそのまま出るピクセル数
// です（カメラが拡大縮小しないので、換算が要りません）。
// =====================================================================
struct GameSpec {
    static constexpr int BaseWidth = 384;   // 内部画面の幅
    static constexpr int BaseHeight = 224;  // 内部画面の高さ

    // 地面の高さ（画面の上から 200 ピクセル目）。
    // 両キャラクターの足はこの線の上に立ちます。
    // 下に 24 ピクセル残るので、そこに超必殺技ゲージを置けます。
    static constexpr int GroundY = 200;

    // ---- キャラクターの大きさ ----
    // 立ち姿の身長 95px は画面高 224px の約 42%。
    // 「大きく見えるが画面が窮屈にならない」ちょうど中間の値です。
    //   90px → 40.2%  /  95px → 42.4%  /  100px → 44.6%
    static constexpr int CharacterVisualHeight = 95;
    // 立ち姿の見た目の幅の目安（45〜60 の中間）。
    static constexpr int CharacterVisualWidth = 52;

    // 1 コマぶんのキャラクター画像を描くときの推奨キャンバスサイズ。
    // ただし攻撃・ジャンプなどでは、この枠に収める必要はありません。
    // 技によって必要な広さが違うためです（例: 横蹴りは幅 110〜130px）。
    static constexpr int CharacterSpriteWidth = 80;
    static constexpr int CharacterSpriteHeight = 100;

    static constexpr int HudHeight = 30;        // 画面上部（体力ゲージ・制限時間）の帯
    static constexpr int SuperGaugeHeight = 18; // 画面下部（超必ゲージ）の帯

    // ---- 押し合い判定（プッシュボックス）----
    // 体と体が重ならないように押し合うためだけの四角形で、
    // 攻撃が当たるかどうかには一切関係しません。
    // 見た目の幅（52px）よりわざと細くしてあります。腕や髪で
    // 相手を押してしまうと不自然だからです。胴・腰・脚が目安。
    static constexpr int PushboxStandWidth = 32, PushboxStandHeight = 78;
    static constexpr int PushboxCrouchWidth = 35, PushboxCrouchHeight = 52;
    static constexpr int PushboxAirWidth = 30, PushboxAirHeight = 56;

    // 投げが成立する距離（中心から中心まで）。投げだけは当たり判定の
    // 四角形ではなく、この距離で成立するかを決めます。
    static constexpr int NormalThrowRange = 30;
};

// =====================================================================
// StageConstants - ステージ（戦う場所）とカメラの基準値
// =====================================================================
// ゲーム内部の座標は「ステージの中央が X=0」です。左に行くとマイナス、
// 右に行くとプラス。画面座標（左上が 0,0）ではないので注意してください。
// 画面座標への変換はカメラ（platform/Camera.h）が担当します。
//
// 表示倍率が 100% 固定なので、ここでの 1 単位 = 画面の 1 ピクセルです。
// =====================================================================
struct StageConstants {
    // ステージ全体の幅。画面（384）の約 2.2 倍あるので、
    // カメラが左右にスクロールして戦う場所を追いかけます。
    static constexpr double StageWidth = 850.0;
    static constexpr double StageMinX = -StageWidth / 2.0;  // 左端 -425
    static constexpr double StageMaxX = StageWidth / 2.0;   // 右端 +425

    // ラウンド開始時の 2 人の距離（中心から中心まで）。
    // 遠すぎず、開幕からいきなり殴り合いにもならない間合いで、
    // 歩き・牽制・ジャンプ・飛び道具のどれも選べる距離です。
    static constexpr double RoundStartDistance = 175.0;
    static constexpr double Player1StartX = -RoundStartDistance / 2.0; // -87.5
    static constexpr double Player2StartX = RoundStartDistance / 2.0;  // +87.5

    // 2 人がこれ以上離れないようにする上限。
    //
    // この値は「画面の幅」から逆算して決めています。
    //   画面幅 384 － キャラクター 1 体ぶんの絵の幅 80 ＝ 304
    // 2 人がちょうどこの距離まで離れたとき、カメラを 2 人の中間に
    // 置けば、左のキャラクターの絵の左端が画面の左端に、右の
    // キャラクターの絵の右端が画面の右端に、ぴったり重なります。
    // つまり「これ以上離れると、どちらかが必ず画面からはみ出す」
    // という限界そのものが、そのまま上限になっています。
    //
    // 1 体ぶんの幅に、見た目の幅（52）ではなく 1 コマの絵の幅
    // （CharacterSpriteWidth = 80）を使っているのは、技を出して
    // 腕や脚を伸ばしたコマでもはみ出さないようにするためです。
    //
    // なお、この上限だけでは「2 人とも画面に映る」ことは保証
    // されません。カメラにはデッドゾーン（動かない帯）があるため、
    // 中間点と画面中央がずれることがあるからです。そのずれの
    // 打ち消しはカメラ側（platform/Camera.cpp）で行っています。
    static constexpr double MaxPlayerDistance =
        GameSpec::BaseWidth - GameSpec::CharacterSpriteWidth; // 384 - 80 = 304

    // キャラクター 1 体を画面に収めるために、画面端との間に空けておく
    // 距離（＝ 1 コマの絵の幅の半分）。カメラがこの値を使って
    // 「2 人とも画面内」を保ちます。
    static constexpr double PlayerScreenMargin = GameSpec::CharacterSpriteWidth / 2.0; // 40

    // カメラのデッドゾーン（＝カメラを動かさない中央の帯）の幅。
    // 2 人の中間点がこの帯の中にいる限り、カメラは止まったままです。
    // これが無いと、少し歩くたびに背景が動いて画面が落ち着きません。
    static constexpr double CameraDeadZoneWidth = 140.0;

    // キャラクターの身長が画面の高さの何割を占めるか（95 / 224 ≒ 0.42）。
    static constexpr double PlayerHeightRatio =
        static_cast<double>(GameSpec::CharacterVisualHeight) / GameSpec::BaseHeight;
};

} // namespace kakuge
