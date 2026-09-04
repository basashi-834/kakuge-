// =====================================================================
// engine/MoveData.h - 1 つの技のデータ（フレームデータ）
// =====================================================================
// 「立ち強パンチは 発生 8F、持続 3F、硬直 18F、ダメージ 120」といった、
// 技の性能をすべて表すデータです。data/moves/<キャラ>/<技>.json から
// 読み込みます。プログラムを書き換えずに JSON を編集するだけで
// 技のバランスを調整できる、というのがこの作りの狙いです。
//
// 格闘ゲームのフレームデータの基礎
// -------------------------------
// 技はボタンを押してから終わるまで、必ず 3 つの段階を通ります。
//
//   発生 (Startup)   … 拳を引いて構えている間。まだ当たらない。
//   持続 (Active)    … 攻撃判定が出ている間。ここで当たる。
//   硬直 (Recovery)  … 拳を戻している間。もう当たらないし、動けない。
//
// フレームの数え方（このゲーム全体で統一）
// ---------------------------------------
// フレームは 1 から数えます。技を出したフレームが 1F 目です。
// 「発生 4F」は「4F 目に最初の攻撃判定が出る」という意味で、
// 「4F 待ってから 5F 目に出る」ではありません。
//
//   例: 発生 4F / 持続 3F / 硬直 7F
//       1F   発生前
//       2F   発生前
//       3F   発生前
//       4F   攻撃判定      ← 発生 4F とはここのこと
//       5F   攻撃判定
//       6F   攻撃判定
//       7F   硬直
//       8F   硬直
//        …
//       13F  硬直          ← 硬直 7F ＝ 7〜13F の 7 フレームぶん
//       14F  行動可能
//
// したがって全体フレーム（動けないフレームの数）は
//
//       TotalFrames = (Startup - 1) + Active + Recovery
//                   = (4 - 1) + 3 + 7 = 13F
//
// です。Startup + Active + Recovery（= 14）ではありません。
// Startup の「4」は「4F 目に出る」という位置の指定なので、
// 攻撃判定が出る前のフレーム数は Startup - 1 だからです。
//
// 「発生が速い技は弱く、遅い技は強い」というバランスの根っこがこれです。
// =====================================================================
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "core/Json.h"
#include "engine/Boxes.h"
#include "engine/Constants.h"

namespace kakuge {

// ---------------------------------------------------------------------
// HitboxDef - 攻撃判定の四角形 1 個ぶん（技データの中での書き方）
// ---------------------------------------------------------------------
// offsetX / offsetY は「キャラクターの原点（中心X・足元Y）から見た、
// 四角形の中心の位置」です。向いている方向がプラスなので、
// 左向きのときはエンジンが自動で左右反転します
//（MoveExecutor::GetActiveHitboxRects）。だからデータは 1 通りで済みます。
struct HitboxDef {
    double offsetX = 0, offsetY = 0, width = 40, height = 40;
};

// ---------------------------------------------------------------------
// FrameBoxSet - 特定のフレームだけ判定を差し替える指定
// ---------------------------------------------------------------------
// 上級者向けの機能です。通常はハートボックスは姿勢で決まり、
// ヒットボックスは持続フレーム中に出ますが、
//   「昇龍拳の 4-6 フレーム目だけ、腕側の食らい判定を小さくしたい」
//   「同じ技でも 10 フレーム目からは攻撃判定の位置を変えたい」
// といった、フレーム単位の細かい調整をしたいことがあります。
// そのときにこの指定を使います（JSON のキーは "frameBoxes"）。
//
//   [{"startFrame":4, "endFrame":6,
//     "hurtboxes":[{"part":"head","offsetX":0,"offsetY":-80,"width":18,"height":16}],
//     "pushbox"  :{"offsetX":0,"offsetY":-36,"width":30,"height":72},
//     "hitboxes" :[{"offsetX":26,"offsetY":-61,"width":16,"height":10}]}]
//
// 3 種類の判定は独立して指定でき、書かなかったものは通常どおりです。
struct FrameBoxSet {
    int startFrame = 0, endFrame = 0; // 何フレーム目から何フレーム目まで（両端を含む）
    bool hasHurtboxes = false;
    std::vector<HurtboxPart> hurtboxes;
    bool hasPushbox = false;
    RectBox pushbox;
    bool hasHitboxes = false;
    std::vector<HitboxDef> hitboxes;

    bool Covers(int frame) const { return frame >= startFrame && frame <= endFrame; }
};

// ---------------------------------------------------------------------
// CancelKind - キャンセルの種類
// ---------------------------------------------------------------------
// 「キャンセルできる / できない」の 2 択ではなく、種類ごとに
// 別々の時間帯と条件を持たせます。通常技から必殺技へは繋がるが
// 超必殺技へは繋がらない、といった作り分けができます。
//
// 種類を増やしたいときは、この enum と CancelKindKey / CancelKindCount
// に 1 行足すだけで、データの読み書きもエディタも自動でついてきます。
enum class CancelKind {
    Special,     // 必殺技キャンセル
    Super,       // 超必殺技キャンセル
    DriveRush,   // ドライブラッシュ（前ステップ／ダッシュ）キャンセル
    TargetCombo  // ターゲットコンボ（通常技から通常技）
};
inline constexpr int CancelKindCount = 4;

// JSON に書くときのキー名。
inline const char* CancelKindKey(CancelKind kind) {
    switch (kind) {
        case CancelKind::Special: return "special";
        case CancelKind::Super: return "super";
        case CancelKind::DriveRush: return "driveRush";
        case CancelKind::TargetCombo: return "targetCombo";
    }
    return "special";
}

// ---------------------------------------------------------------------
// CancelRule - 1 種類ぶんのキャンセル設定
// ---------------------------------------------------------------------
//   enabled      … この種類のキャンセルを使うか
//   startFrame   … 何フレーム目からキャンセルできるか
//   endFrame     … 何フレーム目までキャンセルできるか
//   onHit / onBlock / onWhiff … どの結果のときに許すか
//   allowedMoves … 派生できる技の ID（空なら制限なし）
//
// 時間帯は持続中に限りません。発生中・硬直中のどこにでも置けます
//（発生前からキャンセルできる技、硬直だけキャンセルできる技も作れます）。
struct CancelRule {
    bool Enabled = false;
    int StartFrame = 0;
    int EndFrame = 0;
    bool OnHit = true;
    bool OnBlock = true;
    bool OnWhiff = false;
    std::vector<std::string> AllowedMoves;

    bool CoversFrame(int frame) const {
        if (StartFrame <= 0 && EndFrame <= 0) return false;
        return frame >= StartFrame && frame <= EndFrame;
    }
    bool AllowsContact(MoveContact contact) const {
        switch (contact) {
            case MoveContact::Hit: return OnHit;
            case MoveContact::Blocked: return OnBlock;
            case MoveContact::Whiff: return OnWhiff;
        }
        return false;
    }
    bool AllowsTarget(const std::string& moveId) const {
        if (AllowedMoves.empty()) return true; // 制限なし
        return std::find(AllowedMoves.begin(), AllowedMoves.end(), moveId) != AllowedMoves.end();
    }
};

// ---------------------------------------------------------------------
// AirborneMode - 技中の空中判定の続き方
// ---------------------------------------------------------------------
//   FixedDuration … 指定したフレーム数だけ空中判定
//   UntilLanding  … 指定フレームから、実際に着地するまで空中判定
enum class AirborneMode { FixedDuration, UntilLanding };

// 無敵時間の指定。
//   type … 何に対して無敵か（Constants::Invincible* を参照）
//   start_frame / end_frame … 何フレーム目から何フレーム目まで
struct Invincibility {
    std::string type = "None";
    int start_frame = 0;
    int end_frame = 0;
};

// 飛び道具（波動拳など）の設定。
struct ProjectileDef {
    bool present = false;        // この技は飛び道具を出すか
    double speed = 500.0;        // 進む速さ（1 秒あたり）
    int lifetime = 90;           // 何フレームで消えるか
    double width = 30.0;
    double height = 30.0;
    double spawnOffsetX = 40.0;  // どこから出るか（キャラ原点からの相対位置）
    double spawnOffsetY = -40.0;
};

class MoveData {
public:
    std::string Id;    // 技の識別子（ファイル名と同じ。例 "fireball"）
    std::string Name;  // 画面に出す名前（例 "波動拳"）

    // ---- フレームデータ ----
    // 数え方はファイル冒頭のとおり、フレームは 1 始まりです。
    //   Startup  = 何フレーム目に最初の攻撃判定が出るか（4 なら 4F 目）
    //   Active   = 攻撃判定が出ているフレーム数
    //   Recovery = 持続が終わったあと動けないフレーム数
    int Startup = 1;    // 発生
    int Active = 1;     // 持続
    int Recovery = 1;   // 硬直
    int TotalFrame = 0; // 全体（表示用の写し。計算には TotalFrames() を使う）

    // 全体フレーム＝技を出してから動けるようになるまでの、動けない
    // フレーム数です。攻撃判定が出る前のフレーム数は Startup - 1 なので
    //     (Startup - 1) + Active + Recovery
    // になります。Startup + Active + Recovery ではありません。
    int TotalFrames() const { return (Startup - 1) + Active + Recovery; }

    // 技を出したフレームを 1 としたとき、行動可能になるフレーム番号。
    //   発生 4 / 持続 3 / 硬直 7 なら 14F 目。
    int ActionableFrame() const { return Startup + Active + Recovery; }

    // ---- 当たったときの効果 ----
    int Damage = 0;     // ダメージ

    // ---- 硬直差（このゲームでは硬直差そのものを入力します）----
    // 硬直差とは「攻撃側と防御側の、どちらが何フレーム早く動けるか」。
    //   +4 … 攻撃側が 4 フレーム早く動ける（有利）
    //   -1 … 防御側が 1 フレーム早く動ける（不利）
    //
    // のけぞり時間（Hitstun / Blockstun）は、この硬直差から自動的に
    // 計算します（下の HitstunFrames / BlockstunFrames）。
    // 「硬直差」と「のけぞり時間」を別々に手入力できるようにすると、
    // 必ずどこかで食い違って「表の数字と実際の挙動が違う」ことに
    // なるので、入力するのは硬直差だけ、という作りにしています。
    int HitAdvantage = 0;    // ヒット時硬直差（+ が攻撃側有利）
    int BlockAdvantage = 0;  // ガード時硬直差（- が攻撃側不利）

    // ダウンさせる技（HitOutcome が Normal 以外）の、倒れている
    // フレーム数。のけぞりではなくダウンなので、硬直差ではなく
    // 「何フレーム倒れているか」を直接指定します。
    // 0 なら Fighter 側の既定値（通常ダウン / 強制ダウン）を使います。
    int KnockdownFrames = 0;

    // 空中技（Stance が "air"）の着地硬直。
    // 空中技の硬直は空中では消化されず、着地してから消化します。
    // 0 なら Recovery と同じフレーム数を着地硬直として使います。
    int LandingRecovery = 0;
    // ---- ストップ（当たった瞬間、双方の時間が止まる時間）----
    // ヒットとガードで長さを分けます。ガードストップのほうが短いのが
    // 一般的で、「ガードすると手応えが軽い」という差になります。
    // どちらも攻撃側・防御側の双方に同時に掛かり、同時に解けるので、
    // 有利不利（硬直差）には一切影響しません。
    int Hitstop = 0;    // ヒット時に両者の時間が止まるフレーム数
    int Guardstop = 0;  // ガード時に両者の時間が止まるフレーム数

    // ガードストップの既定値。JSON に書かれていなければ、
    // ヒットストップから 2 フレーム引いた値（最低 1）にします。
    static int DefaultGuardstop(int hitstop) {
        return hitstop > 0 ? std::max(1, hitstop - 2) : 0;
    }
    std::string GuardType = "High";  // どうガードできるか
    double ChipDamagePercent = 0.0;  // ガードされたときの削りダメージの割合

    // ---- 判定 ----
    std::vector<HitboxDef> Hitboxes;    // 持続フレーム中に出る攻撃判定（複数可）
    std::vector<FrameBoxSet> FrameBoxes;// フレーム単位の上書き指定

    // ---- 技ごとの食らい判定（ハートボックス）----
    //
    // どこが、どうなって、こうなるのか
    // -------------------------------
    // ふだんの食らい判定は「姿勢（立ち / しゃがみ / 空中）」で決まります
    //（CharacterStats::Hurtboxes）。しかし技を出している間だけは、
    // 体の形が姿勢とはまるで違うものになります。
    //
    //   しゃがみ強キック … 足を長く前へ伸ばすので、脚が長くなる
    //   昇龍拳の出際     … 体が縮こまり、腕だけが上へ伸びる
    //   飛び道具の硬直   … 前へ突き出した腕が「殴れる場所」になる
    //
    // 「攻撃判定は伸びているのに、食らい判定は突っ立ったときのまま」だと、
    // 差し合い（お互いの技を当てに行く読み合い）が成立しません。
    // 伸ばした脚を狙って潰す、という反撃ができなくなるからです。
    //
    // そこで、技ごとに専用の食らい判定を持てるようにしてあります。
    //   Hurtboxes            … その技の間ずっと使う食らい判定（部位の一覧）
    //   HurtboxOverrideEnabled … それを使うかどうか（false なら姿勢の判定）
    //
    // 判定に使う四角形は、次の順に決まります（上ほど優先）。
    //   1. FrameBoxes の "hurtboxes"（フレーム単位の上書き）
    //   2. この Hurtboxes（技全体の上書き）      ← 今回の項目
    //   3. 姿勢ごとの標準（CharacterStats::Hurtboxes）
    // 実際に選んでいるのは Fighter::HurtboxRects() です。
    //
    // JSON では技ファイルの一番上に、こう書きます。
    //   "hurtboxes": [
    //     {"part":"head",  "offsetX":0,  "offsetY":-86, "width":19, "height":18},
    //     {"part":"leg",   "offsetX":14, "offsetY":-10, "width":58, "height":20}
    //   ]
    // 一時的に切りたいときは、次の形でも書けます（消さずに残せます）。
    //   "hurtboxes": {"enabled": false, "parts": [ ... ]}
    bool HurtboxOverrideEnabled = false;
    std::vector<HurtboxPart> Hurtboxes;

    // この技専用の食らい判定を使うか。
    // 「使う設定になっていて、かつ中身が 1 個以上ある」ときだけ true。
    // 空の一覧をそのまま使うと、食らい判定がまったく無い（＝絶対に
    // 当たらない）キャラクターができてしまうので、その事故を防ぎます。
    bool HasHurtboxOverride() const {
        return HurtboxOverrideEnabled && !Hurtboxes.empty();
    }

    // 投げ（GuardType が "Throw"）は攻撃判定ではなく、
    // 中心どうしの距離で成立します。その距離。
    double ThrowRange = GameSpec::NormalThrowRange;

    // ---- のけぞり・吹き飛び ----
    double KnockbackX = 0.0, KnockbackY = 0.0;
    std::string HitOutcome = "Normal";  // ヒット後の状態（のけぞり / ダウン等）

    // ---- ゲージ ----
    int MeterGain = 0;  // この技で溜まるゲージ量
    int MeterCost = 0;  // この技を出すのに必要なゲージ量（超必殺技用）

    // ---- キャンセル（コンボ）----
    // 技の途中から次の技に移れる時間帯。ここが開いている間だけ、
    // 硬直を飛ばして次の技を出せます。これがコンボの仕組みです。
    //
    // 種類ごとに別々の時間帯・条件を持ちます（必殺技キャンセルは
    // ヒット時とガード時だけ、ターゲットコンボは特定の技だけ…など）。
    CancelRule Cancels[CancelKindCount];

    CancelRule& Cancel(CancelKind kind) { return Cancels[static_cast<int>(kind)]; }
    const CancelRule& Cancel(CancelKind kind) const { return Cancels[static_cast<int>(kind)]; }

    std::vector<std::string> Tags;  // 技の性質ラベル（Constants::Tag* 参照）
    Invincibility Inv;              // 無敵時間

    // ---- 技中の空中判定 ----
    // 「ゲーム上の空中判定」と「見た目の Y 座標」は別物として扱います。
    // 地面に足がついて見えていても、指定したフレームの間は空中判定に
    // でき（対空技や無敵技の表現）、逆に少し浮いて見えても
    // 指定していなければ地上判定のままです。
    //
    //   AirborneStart    … 何フレーム目から空中判定になるか
    //   AirborneDuration … 何フレーム続くか（FixedDuration のとき）
    //   AirborneMode     … FixedDuration（指定フレーム数）
    //                      UntilLanding（実際に着地するまで）
    bool AirborneEnabled = false;
    int AirborneStart = 0;
    int AirborneDuration = 0;
    AirborneMode AirborneKind = AirborneMode::FixedDuration;

    // このフレームで空中判定か（技の指定だけを見る。Y 座標は見ない）。
    // physicallyAirborne は UntilLanding のときだけ使い、
    // 「まだ実際に空中にいるか」を呼び出し側から渡します。
    bool IsAirborneAtFrame(int frame, bool physicallyAirborne) const {
        if (!AirborneEnabled) return false;
        if (frame < AirborneStart) return false;
        if (AirborneKind == AirborneMode::UntilLanding) return physicallyAirborne;
        return frame < AirborneStart + std::max(1, AirborneDuration);
    }

    // ---- 出し方 ----
    std::string InputCommand;   // レバーコマンド（例 "236"）。空なら通常技
    std::string Button;         // 使うボタン（"HP" や "AnyP"）
    std::string Stance = "stand"; // どの姿勢から出せるか（stand/crouch/air）
    bool RequiresAir = false;

    ProjectileDef Projectile;   // 飛び道具の設定
    double EffectiveRange = 0.0;// CPU が「この距離なら当たる」と判断する目安

    // 予約項目（まだゲームには影響しません）。技の最中に判定そのものが
    // 独自の軌道で動くタイプの技を将来入れるための場所です。
    bool HasDynamicHitbox = false;
    // 技のモーション参考画像のパス（資料用。描画には使っていません）。
    std::string MotionImagePath;

    // -----------------------------------------------------------------
    // 便利関数
    // -----------------------------------------------------------------
    bool HasTag(const std::string& tag) const {
        return std::find(Tags.begin(), Tags.end(), tag) != Tags.end();
    }
    // -----------------------------------------------------------------
    // キャンセルできるか
    // -----------------------------------------------------------------
    // 次に出したい技の種類（必殺技 / 超必殺技 / 通常技）に対応する
    // 設定を見て、フレーム・結果（ヒット/ガード/空振り）・派生先の
    // 3 つがそろっていれば許可します。
    bool AllowsCancel(CancelKind kind, int frame, MoveContact contact,
                      const std::string& targetMoveId = std::string()) const {
        const CancelRule& rule = Cancel(kind);
        if (!rule.Enabled) return false;
        if (!rule.CoversFrame(frame)) return false;
        if (!rule.AllowsContact(contact)) return false;
        return rule.AllowsTarget(targetMoveId);
    }

    // どの種類のキャンセルでもよいので、今キャンセルできるか。
    bool IsCancelWindowOpen(int frame, MoveContact contact) const {
        for (int i = 0; i < CancelKindCount; ++i) {
            const CancelRule& rule = Cancels[i];
            if (rule.Enabled && rule.CoversFrame(frame) && rule.AllowsContact(contact)) return true;
        }
        return false;
    }

    // 「この技へ移るとき、どの種類のキャンセルを使うか」。
    // 技そのものの性質（タグ）で決まります。
    CancelKind CancelKindAsTarget() const {
        if (HasTag(Constants::TagSuper)) return CancelKind::Super;
        if (HasTag(Constants::TagSpecial)) return CancelKind::Special;
        return CancelKind::TargetCombo; // 通常技から通常技への派生
    }

    // このフレームに対する上書き指定があれば返す（無ければ nullptr）。
    const FrameBoxSet* FrameBoxesAt(int frame) const {
        for (const auto& fb : FrameBoxes) {
            if (fb.Covers(frame)) return &fb;
        }
        return nullptr;
    }

    // -----------------------------------------------------------------
    // 有利フレーム / 不利フレーム（硬直差）
    // -----------------------------------------------------------------
    // 定義:
    //     硬直差 ＝ 防御側の硬直フレーム数 － 攻撃側の残り全体フレーム数
    //
    // 「+2」なら当てたあと自分が 2 フレーム早く動ける（有利）、
    // 「-5」なら 5 フレーム遅い（不利＝反撃が確定する）という意味です。
    //
    // ストップ（ヒットストップ / ガードストップ）は双方に同時に掛かり、
    // 同時に解けるので、この式には出てきません。
    //
    // 「残り全体フレーム数」は、当たったフレームから数えて、攻撃側が
    // あと何フレーム動けないかです。持続の 1 フレーム目（＝ Startup の
    // フレーム）で当てた場合は
    //     残り ＝ 持続 ＋ 硬直
    // になります。持続の後半で当てるほど残りが減り、そのぶん有利に
    // なります（いわゆる「持続当て」）。
    int RemainingFramesAt(int contactFrame) const {
        return std::max(0, ActionableFrame() - contactFrame);
    }
    // 持続 1 フレーム目で当てたときの残り（表に載せる標準の値）。
    //
    // 2 つだけ例外があります。
    //  ・飛び道具の技: 当てるのは飛んでいった弾で、当たるころには
    //    技の硬直はもう終わっています。だから残りは 0 として数えます。
    //  ・空中技: 硬直は空中では消化されず、着地してから着地硬直として
    //    消化します。だから残りは着地硬直のフレーム数として数えます
    //    （＝着地直前に当てる、いわゆる「めくり・飛び込み」の基準）。
    int RemainingFramesOnEarliestHit() const {
        if (Projectile.present) return 0;
        if (IsAirMove()) return LandingRecoveryFrames();
        return Active + Recovery;
    }

    // -----------------------------------------------------------------
    // のけぞり時間（硬直差から逆算する）
    // -----------------------------------------------------------------
    //     硬直差 ＝ のけぞり － (持続 ＋ 硬直)
    // を のけぞり について解いた式です。こう決めておけば、
    // 「入力した硬直差」と「実際に測った硬直差」は必ず一致します。
    int HitstunFrames() const {
        return std::max(0, HitAdvantage + RemainingFramesOnEarliestHit());
    }
    int BlockstunFrames() const {
        return std::max(0, BlockAdvantage + RemainingFramesOnEarliestHit());
    }

    // ダウンさせる技が、相手を倒れさせておくフレーム数。
    // 0（未指定）なら呼び出し側の既定値を使います。
    int KnockdownDuration() const { return std::max(0, KnockdownFrames); }

    // 空中技の着地硬直。未指定なら Recovery をそのまま使います。
    int LandingRecoveryFrames() const {
        return LandingRecovery > 0 ? LandingRecovery : Recovery;
    }
    bool IsAirMove() const { return Stance == "air" || RequiresAir; }

    int OnHitAdvantage() const { return HitAdvantage; }
    int OnBlockAdvantage() const { return BlockAdvantage; }

    // -----------------------------------------------------------------
    // コンボ・確定反撃の判定（仕様どおりの単純な比較）
    // -----------------------------------------------------------------
    // 前の技のヒット時硬直差が、次の技の発生フレーム以上ならつながる。
    bool CombosInto(const MoveData& next) const {
        return HitAdvantage >= next.Startup;
    }
    // ガードされたとき、発生 startupFrames の技で確定反撃を受けるか。
    bool IsPunishableBy(int startupFrames) const {
        return BlockAdvantage < 0 && -BlockAdvantage >= startupFrames;
    }

    // -----------------------------------------------------------------
    // JSON からの読み込み
    // -----------------------------------------------------------------
    static MoveData FromJson(const Json& obj) {
        MoveData m;
        m.Id = obj.GetString("id", std::string());
        m.Name = obj.GetString("name", m.Id);
        m.Startup = obj.GetInt("startup", 1);
        m.Active = obj.GetInt("active", 1);
        m.Recovery = obj.GetInt("recovery", 1);
        m.Damage = obj.GetInt("damage", 0);

        // 硬直差の読み込み。
        //   新しい形式: "hitAdvantage" / "blockAdvantage"（硬直差そのもの）
        //   古い形式  : "hitstun" / "blockstun"（のけぞりフレーム数）
        // 古いデータもそのまま読めるように、硬直差が書かれていなければ
        // のけぞりフレーム数から逆算します。
        int remain = m.Active + m.Recovery;
        if (const Json* adv = obj.Find("hitAdvantage"); adv != nullptr) {
            m.HitAdvantage = adv->AsInt();
        } else {
            m.HitAdvantage = obj.GetInt("hitstun", remain) - remain;
        }
        if (const Json* adv = obj.Find("blockAdvantage"); adv != nullptr) {
            m.BlockAdvantage = adv->AsInt();
        } else {
            m.BlockAdvantage = obj.GetInt("blockstun", remain) - remain;
        }
        m.KnockdownFrames = obj.GetInt("knockdownFrames", 0);
        m.LandingRecovery = obj.GetInt("landingRecovery", 0);
        m.TotalFrame = m.TotalFrames();
        // ヒットストップ / ガードストップ。
        // 別名（hitStopFrames / blockStopFrames）でも書けます。
        // 2 つは完全に独立した値で、片方を変えてももう片方は変わりません。
        // ガードストップが書かれていない古いデータのときだけ、
        // ヒットストップから既定値を作ります。
        m.Hitstop = obj.GetInt("hitstop", obj.GetInt("hitStopFrames", 0));
        if (const Json* g = obj.Find("guardstop"); g != nullptr) {
            m.Guardstop = g->AsInt();
        } else if (const Json* g2 = obj.Find("blockStopFrames"); g2 != nullptr) {
            m.Guardstop = g2->AsInt();
        } else {
            m.Guardstop = DefaultGuardstop(m.Hitstop);
        }
        m.GuardType = obj.GetString("guardType", "High");
        m.ChipDamagePercent = obj.GetNumber("chipDamagePercent", 0.0);

        auto readHitbox = [](const Json& hb) {
            HitboxDef d;
            d.offsetX = hb.GetNumber("offsetX", 0.0);
            d.offsetY = hb.GetNumber("offsetY", 0.0);
            d.width = hb.GetNumber("width", 40.0);
            d.height = hb.GetNumber("height", 40.0);
            return d;
        };
        if (const Json* hitboxes = obj.Find("hitbox"); hitboxes && hitboxes->IsArray()) {
            for (const Json& hb : hitboxes->Items()) m.Hitboxes.push_back(readHitbox(hb));
        }

        // ---- 技ごとの食らい判定 ----
        // 2 通りの書き方を許しています。
        //   配列   : "hurtboxes": [ {部位}, {部位} ]        → そのまま使う
        //   オブジェクト: "hurtboxes": {"enabled":false, "parts":[...]}
        //                → 中身を残したまま、使う / 使わないを切り替えられる
        // 書かれていなければ、これまでどおり姿勢ごとの判定を使います。
        auto readHurtPart = [](const Json& pj) {
            HurtboxPart part;
            part.Name = pj.GetString("part", "body");
            part.Box = RectBox(pj.GetNumber("offsetX", 0.0), pj.GetNumber("offsetY", 0.0),
                               pj.GetNumber("width", 40.0), pj.GetNumber("height", 40.0));
            return part;
        };
        if (const Json* hurt = obj.Find("hurtboxes"); hurt != nullptr) {
            if (hurt->IsArray()) {
                for (const Json& pj : hurt->Items()) m.Hurtboxes.push_back(readHurtPart(pj));
                m.HurtboxOverrideEnabled = !m.Hurtboxes.empty();
            } else if (hurt->IsObject()) {
                if (const Json* parts = hurt->Find("parts"); parts && parts->IsArray()) {
                    for (const Json& pj : parts->Items()) m.Hurtboxes.push_back(readHurtPart(pj));
                }
                m.HurtboxOverrideEnabled = hurt->GetBool("enabled", !m.Hurtboxes.empty());
            }
        }

        if (const Json* frameBoxes = obj.Find("frameBoxes"); frameBoxes && frameBoxes->IsArray()) {
            for (const Json& fj : frameBoxes->Items()) {
                FrameBoxSet fb;
                fb.startFrame = fj.GetInt("startFrame", 0);
                fb.endFrame = fj.GetInt("endFrame", fb.startFrame);
                if (const Json* hurt = fj.Find("hurtboxes"); hurt && hurt->IsArray()) {
                    fb.hasHurtboxes = true;
                    // 部位の読み方は技全体のものと同じなので、同じ関数を使います。
                    for (const Json& pj : hurt->Items()) fb.hurtboxes.push_back(readHurtPart(pj));
                }
                if (const Json* pb = fj.Find("pushbox"); pb && pb->IsObject()) {
                    fb.hasPushbox = true;
                    fb.pushbox = RectBox(pb->GetNumber("offsetX", 0.0), pb->GetNumber("offsetY", 0.0),
                                         pb->GetNumber("width", 30.0), pb->GetNumber("height", 72.0));
                }
                if (const Json* hits = fj.Find("hitboxes"); hits && hits->IsArray()) {
                    fb.hasHitboxes = true;
                    for (const Json& hb : hits->Items()) fb.hitboxes.push_back(readHitbox(hb));
                }
                m.FrameBoxes.push_back(fb);
            }
        }

        m.ThrowRange = obj.GetNumber("throwRange", static_cast<double>(GameSpec::NormalThrowRange));
        m.KnockbackX = obj.GetNumber("knockbackX", 0.0);
        m.KnockbackY = obj.GetNumber("knockbackY", 0.0);
        m.HitOutcome = obj.GetString("hitOutcome", "Normal");
        m.MeterGain = obj.GetInt("meterGain", 0);
        m.MeterCost = obj.GetInt("meterCost", 0);

        // ---- キャンセル設定 ----
        // 新しい形式は "cancels": { "special": {...}, "super": {...} ... }。
        // 古いデータには "cancelStartFrame" / "cancelEndFrame" /
        // "cancelRoutes" しかないので、その場合は同じ内容になるよう
        // 4 種類ぶんの設定へ読み替えます（古いデータもそのまま動きます）。
        int legacyStart = obj.GetInt("cancelStartFrame", 0);
        int legacyEnd = obj.GetInt("cancelEndFrame", 0);
        std::vector<std::string> legacyRoutes;
        if (const Json* routes = obj.Find("cancelRoutes"); routes && routes->IsArray()) {
            for (const Json& r : routes->Items()) legacyRoutes.push_back(r.AsString());
        }

        const Json* cancels = obj.Find("cancels");
        if (cancels != nullptr && cancels->IsObject()) {
            for (int i = 0; i < CancelKindCount; ++i) {
                CancelKind kind = static_cast<CancelKind>(i);
                const Json* cj = cancels->Find(CancelKindKey(kind));
                if (cj == nullptr || !cj->IsObject()) continue;
                CancelRule& rule = m.Cancel(kind);
                rule.Enabled = cj->GetBool("enabled", false);
                rule.StartFrame = cj->GetInt("startFrame", 0);
                rule.EndFrame = cj->GetInt("endFrame", 0);
                rule.OnHit = cj->GetBool("onHit", true);
                rule.OnBlock = cj->GetBool("onBlock", true);
                rule.OnWhiff = cj->GetBool("onWhiff", false);
                if (const Json* allowed = cj->Find("allowedMoves");
                    allowed && allowed->IsArray()) {
                    for (const Json& a : allowed->Items()) rule.AllowedMoves.push_back(a.AsString());
                }
            }
        } else if (legacyStart > 0 || legacyEnd > 0) {
            // 昔の「1 つだけのキャンセル時間帯」を、必殺技・超必殺技・
            // ターゲットコンボの 3 種類に同じ内容で割り当てます。
            for (CancelKind kind : {CancelKind::Special, CancelKind::Super,
                                    CancelKind::TargetCombo}) {
                CancelRule& rule = m.Cancel(kind);
                rule.Enabled = true;
                rule.StartFrame = legacyStart;
                rule.EndFrame = legacyEnd;
                rule.OnHit = true;
                rule.OnBlock = true;
                rule.OnWhiff = false;
            }
            // 昔の許可リストはターゲットコンボの派生先として引き継ぎます。
            m.Cancel(CancelKind::TargetCombo).AllowedMoves = legacyRoutes;
        }

        // ---- 技中の空中判定 ----
        if (const Json* air = obj.Find("airborne"); air && air->IsObject()) {
            m.AirborneEnabled = air->GetBool("enabled", false);
            m.AirborneStart = air->GetInt("startFrame", 0);
            m.AirborneDuration = air->GetInt("durationFrames", 0);
            m.AirborneKind = air->GetString("mode", "FixedDuration") == "UntilLanding"
                                 ? AirborneMode::UntilLanding
                                 : AirborneMode::FixedDuration;
        }

        if (const Json* tags = obj.Find("tags"); tags && tags->IsArray()) {
            for (const Json& t : tags->Items()) m.Tags.push_back(t.AsString());
        }

        if (const Json* iv = obj.Find("invincibility"); iv && iv->IsObject()) {
            m.Inv.type = iv->GetString("type", "None");
            m.Inv.start_frame = iv->GetInt("start_frame", 0);
            m.Inv.end_frame = iv->GetInt("end_frame", 0);
        }

        m.InputCommand = obj.GetString("input", std::string());
        m.Button = obj.GetString("button", std::string());
        m.Stance = obj.GetString("stance", "stand");
        m.RequiresAir = obj.GetBool("requiresAir", false);

        if (const Json* p = obj.Find("projectile"); p && p->IsObject() && !p->Members().empty()) {
            m.Projectile.present = true;
            m.Projectile.speed = p->GetNumber("speed", 500.0);
            m.Projectile.lifetime = p->GetInt("lifetime", 90);
            m.Projectile.width = p->GetNumber("width", 30.0);
            m.Projectile.height = p->GetNumber("height", 30.0);
            m.Projectile.spawnOffsetX = p->GetNumber("spawnOffsetX", 40.0);
            m.Projectile.spawnOffsetY = p->GetNumber("spawnOffsetY", -40.0);
        }

        m.HasDynamicHitbox = obj.GetBool("hasDynamicHitbox", false);
        m.MotionImagePath = obj.GetString("motionImage", std::string());

        // CPU 用の間合い。JSON に書いていなければ、技の性質から推定します。
        // 弱攻撃は中心から約 34、強攻撃は約 48 まで届くので、その辺りの値。
        m.EffectiveRange = obj.GetNumber("effectiveRange", 0.0);
        if (m.EffectiveRange <= 0.0) {
            if (m.HasTag(Constants::TagProjectile)) m.EffectiveRange = 900.0; // 飛び道具は画面端まで
            else if (m.HasTag(Constants::TagThrow)) m.EffectiveRange = m.ThrowRange;
            else if (m.HasTag(Constants::TagHeavy)) m.EffectiveRange = 50.0;
            else if (m.HasTag(Constants::TagMedium)) m.EffectiveRange = 42.0;
            else m.EffectiveRange = 36.0;
        }
        return m;
    }

    // -----------------------------------------------------------------
    // JSON への書き出し
    // -----------------------------------------------------------------
    Json ToJson() const {
        Json j = Json::MakeObject();
        j.Set("id", Json(Id));
        j.Set("name", Json(Name));
        j.Set("startup", Json(Startup));
        j.Set("active", Json(Active));
        j.Set("recovery", Json(Recovery));
        j.Set("totalFrame", Json(TotalFrames()));
        j.Set("damage", Json(Damage));
        // 硬直差だけを保存します。のけぞり時間は硬直差から必ず
        // 計算し直されるので、保存すると二重管理になり食い違います。
        j.Set("hitAdvantage", Json(HitAdvantage));
        j.Set("blockAdvantage", Json(BlockAdvantage));
        if (KnockdownFrames > 0) j.Set("knockdownFrames", Json(KnockdownFrames));
        if (LandingRecovery > 0) j.Set("landingRecovery", Json(LandingRecovery));
        j.Set("hitstop", Json(Hitstop));
        j.Set("guardstop", Json(Guardstop));
        j.Set("guardType", Json(GuardType));
        j.Set("chipDamagePercent", Json(ChipDamagePercent));

        auto hitboxJson = [](const HitboxDef& hb) {
            Json o = Json::MakeObject();
            o.Set("offsetX", Json(hb.offsetX));
            o.Set("offsetY", Json(hb.offsetY));
            o.Set("width", Json(hb.width));
            o.Set("height", Json(hb.height));
            return o;
        };
        Json hitboxArr = Json::MakeArray();
        for (const auto& hb : Hitboxes) hitboxArr.Push(hitboxJson(hb));
        j.Set("hitbox", std::move(hitboxArr));

        // 部位 1 個ぶんを JSON にする（技全体の食らい判定と
        // frameBoxes の中で同じ形を使うので、関数にまとめます）。
        auto hurtPartJson = [](const HurtboxPart& part) {
            Json p = Json::MakeObject();
            p.Set("part", Json(part.Name));
            p.Set("offsetX", Json(part.Box.CenterX));
            p.Set("offsetY", Json(part.Box.CenterY));
            p.Set("width", Json(part.Box.Width));
            p.Set("height", Json(part.Box.Height));
            return p;
        };

        // ---- 技ごとの食らい判定 ----
        // 中身が無いときは、キーそのものを書きません。全部の技ファイルに
        // 空の "hurtboxes": [] が並ぶと、何も設定していないのか
        // 「わざと空にした」のか読み分けられなくなるためです。
        if (!Hurtboxes.empty()) {
            Json parts = Json::MakeArray();
            for (const auto& part : Hurtboxes) parts.Push(hurtPartJson(part));
            Json hurt = Json::MakeObject();
            hurt.Set("enabled", Json(HurtboxOverrideEnabled));
            hurt.Set("parts", std::move(parts));
            j.Set("hurtboxes", std::move(hurt));
        }

        if (!FrameBoxes.empty()) {
            Json arr = Json::MakeArray();
            for (const auto& fb : FrameBoxes) {
                Json fj = Json::MakeObject();
                fj.Set("startFrame", Json(fb.startFrame));
                fj.Set("endFrame", Json(fb.endFrame));
                if (fb.hasHurtboxes) {
                    Json parts = Json::MakeArray();
                    for (const auto& part : fb.hurtboxes) parts.Push(hurtPartJson(part));
                    fj.Set("hurtboxes", std::move(parts));
                }
                if (fb.hasPushbox) {
                    Json p = Json::MakeObject();
                    p.Set("offsetX", Json(fb.pushbox.CenterX));
                    p.Set("offsetY", Json(fb.pushbox.CenterY));
                    p.Set("width", Json(fb.pushbox.Width));
                    p.Set("height", Json(fb.pushbox.Height));
                    fj.Set("pushbox", std::move(p));
                }
                if (fb.hasHitboxes) {
                    Json hits = Json::MakeArray();
                    for (const auto& hb : fb.hitboxes) hits.Push(hitboxJson(hb));
                    fj.Set("hitboxes", std::move(hits));
                }
                arr.Push(std::move(fj));
            }
            j.Set("frameBoxes", std::move(arr));
        }

        if (GuardType == Constants::GuardThrow) j.Set("throwRange", Json(ThrowRange));
        j.Set("knockbackX", Json(KnockbackX));
        j.Set("knockbackY", Json(KnockbackY));
        j.Set("hitOutcome", Json(HitOutcome));
        j.Set("meterGain", Json(MeterGain));
        j.Set("meterCost", Json(MeterCost));

        Json cancels = Json::MakeObject();
        for (int i = 0; i < CancelKindCount; ++i) {
            CancelKind kind = static_cast<CancelKind>(i);
            const CancelRule& rule = Cancel(kind);
            Json cj = Json::MakeObject();
            cj.Set("enabled", Json(rule.Enabled));
            cj.Set("startFrame", Json(rule.StartFrame));
            cj.Set("endFrame", Json(rule.EndFrame));
            cj.Set("onHit", Json(rule.OnHit));
            cj.Set("onBlock", Json(rule.OnBlock));
            cj.Set("onWhiff", Json(rule.OnWhiff));
            if (!rule.AllowedMoves.empty()) {
                Json allowed = Json::MakeArray();
                for (const auto& id : rule.AllowedMoves) allowed.Push(Json(id));
                cj.Set("allowedMoves", std::move(allowed));
            }
            cancels.Set(CancelKindKey(kind), std::move(cj));
        }
        j.Set("cancels", std::move(cancels));

        if (AirborneEnabled) {
            Json air = Json::MakeObject();
            air.Set("enabled", Json(AirborneEnabled));
            air.Set("startFrame", Json(AirborneStart));
            air.Set("durationFrames", Json(AirborneDuration));
            air.Set("mode", Json(AirborneKind == AirborneMode::UntilLanding
                                     ? std::string("UntilLanding")
                                     : std::string("FixedDuration")));
            j.Set("airborne", std::move(air));
        }

        Json tags = Json::MakeArray();
        for (const auto& t : Tags) tags.Push(Json(t));
        j.Set("tags", std::move(tags));

        Json inv = Json::MakeObject();
        inv.Set("type", Json(Inv.type));
        inv.Set("start_frame", Json(Inv.start_frame));
        inv.Set("end_frame", Json(Inv.end_frame));
        j.Set("invincibility", std::move(inv));

        j.Set("input", Json(InputCommand));
        j.Set("button", Json(Button));
        j.Set("stance", Json(Stance));
        j.Set("requiresAir", Json(RequiresAir));

        Json proj = Json::MakeObject();
        if (Projectile.present) {
            proj.Set("speed", Json(Projectile.speed));
            proj.Set("lifetime", Json(Projectile.lifetime));
            proj.Set("width", Json(Projectile.width));
            proj.Set("height", Json(Projectile.height));
            proj.Set("spawnOffsetX", Json(Projectile.spawnOffsetX));
            proj.Set("spawnOffsetY", Json(Projectile.spawnOffsetY));
        }
        j.Set("projectile", std::move(proj));

        j.Set("effectiveRange", Json(EffectiveRange));
        j.Set("hasDynamicHitbox", Json(HasDynamicHitbox));
        j.Set("motionImage", Json(MotionImagePath));
        return j;
    }
};

} // namespace kakuge
