// =====================================================================
// engine/MoveExecutor.h - 技が「今どの段階か」を計算する
// =====================================================================
// 技のデータ（MoveData）と、経過フレーム数の 2 つだけを受け取って、
// 「今は発生中か、持続中か、硬直中か、もう終わったか」を答えます。
//
// このクラスは状態を一切持ちません（すべて static 関数）。
// 状態を持たないことには大きな利点があります。
// StateMachine の CurrentFrame から毎回計算し直すので、
// 「実際のフレーム数と、技の進行状況がずれる」というバグが
// 原理的に起こりません。
//
// フレームの数え方（このゲーム全体で統一）
// ---------------------------------------
// 技のフレームは 1 から数えます。技を出したフレームが 1F 目です。
// 「発生 4F」は「4F 目に最初の攻撃判定が出る」という意味であって、
// 「4F 待ってから 5F 目に出る」ではありません。
//
//   発生 (Startup)  … frame が [1, Startup-1]        … まだ当たらない
//   持続 (Active)   … frame が [Startup, Startup+Active-1] … 当たる
//   硬直 (Recovery) … frame が [Startup+Active, Startup+Active+Recovery-1]
//   終了 (Done)     … frame が Startup+Active+Recovery 以降
//
//   例: 発生 4F / 持続 3F / 硬直 7F
//       1-3F   発生前
//       4-6F   攻撃判定（＝持続 3 フレームぶん）
//       7-13F  硬直（＝硬直 7 フレームぶん）
//       14F    行動可能（この 14F 目から次の技を出せます）
//
// 「硬直 7F」は「7F 目だけ硬直する」ではなく、
// 「持続が終わったあと 7 フレームぶん硬直が続く」という意味です。
// =====================================================================
#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "engine/Boxes.h"
#include "engine/Constants.h"
#include "engine/MoveData.h"

namespace kakuge {

enum class MovePhase { Startup, Active, Recovery, Done };

struct MoveExecutor {
    // frame は 1 始まり（技を出したフレームが 1）。
    static MovePhase GetPhase(const MoveData& move, int frame) {
        if (frame < move.Startup) return MovePhase::Startup;
        if (frame < move.Startup + move.Active) return MovePhase::Active;
        if (frame < move.Startup + move.Active + move.Recovery) return MovePhase::Recovery;
        return MovePhase::Done;
    }

    // 技が終わって行動可能になるフレーム番号（この番号のフレームから動ける）。
    //   発生 4 / 持続 3 / 硬直 7 なら 14。
    static int ActionableFrame(const MoveData& move) {
        return move.Startup + move.Active + move.Recovery;
    }

    // 今このフレームで無敵かどうか。
    // kind には "Strike"（打撃）や "Throw"（投げ）を渡します。
    // 技の無敵種別が "Full" なら何に対しても無敵、
    // "Strike" なら打撃だけ無敵（投げは食らう）です。
    static bool IsInvincible(const MoveData& move, int frame, const std::string& kind) {
        const auto& inv = move.Inv;
        if (inv.type == Constants::InvincibleNone) return false;
        if (frame < inv.start_frame || frame > inv.end_frame) return false;
        if (kind.empty()) return true;
        if (inv.type == Constants::InvincibleFull) return true;
        return inv.type == kind;
    }

    // 今のフレームで、何らかのキャンセルができるか。
    // contact は「今出している技が当たったか / ガードされたか / 空振りか」。
    static bool CanCancel(const MoveData& move, int frame, MoveContact contact) {
        return move.IsCancelWindowOpen(frame, contact);
    }

    // 「この技へ」キャンセルできるか。技の種類（必殺技 / 超必殺技 /
    // 通常技）に対応する設定を見ます。
    static bool CanCancelInto(const MoveData& move, int frame, MoveContact contact,
                              const MoveData& target) {
        return move.AllowsCancel(target.CancelKindAsTarget(), frame, contact, target.Id);
    }

    // -----------------------------------------------------------------
    // 今このフレームに出ている攻撃判定を、ワールド座標の四角形で返す
    // -----------------------------------------------------------------
    // 判定の出どころは 2 つあり、上のほうが優先されます。
    //   1. frameBoxes の "hitboxes" 指定（フレーム単位の上書き）。
    //      持続フレームかどうかに関係なく効きます。空の配列を書けば
    //      「このフレームは判定なし」という指定にもなります。
    //   2. 上書きが無ければ、持続フレーム中だけ move.Hitboxes 全部。
    //
    // 投げは当たり判定を使わないので実際には当たりませんが、
    // デバッグ表示で投げ間合いを描けるように、判定自体は返します。
    static std::vector<RectBox> GetActiveHitboxRects(const MoveData& move, int frame, int facing,
                                                     double originX, double originY) {
        std::vector<RectBox> out;
        const std::vector<HitboxDef>* source = nullptr;
        if (const FrameBoxSet* fb = move.FrameBoxesAt(frame); fb != nullptr && fb->hasHitboxes) {
            source = &fb->hitboxes;
        } else if (GetPhase(move, frame) == MovePhase::Active) {
            source = &move.Hitboxes;
        }
        if (source == nullptr) return out;

        // 原点を整数に丸める理由は Boxes.h の PlaceParts と同じです
        //（描画と判定の位置を必ず一致させるため）。
        double ox = std::round(originX), oy = std::round(originY);
        int f = facing < 0 ? -1 : 1;
        for (const auto& box : *source) {
            out.emplace_back(ox + box.offsetX * f, oy + box.offsetY, box.width, box.height);
        }
        return out;
    }

    // このフレームに攻撃判定が出ているか（当たり得るか）だけを調べる、
    // 軽い版。座標計算をしないので毎フレーム呼んでも安いです。
    static bool HasLiveHitboxes(const MoveData& move, int frame) {
        if (const FrameBoxSet* fb = move.FrameBoxesAt(frame); fb != nullptr && fb->hasHitboxes) {
            return !fb->hitboxes.empty();
        }
        return GetPhase(move, frame) == MovePhase::Active && !move.Hitboxes.empty();
    }
};

} // namespace kakuge
