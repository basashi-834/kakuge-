// =====================================================================
// engine/StateMachine.h - 「今どの状態で、その状態に入って何フレーム目か」
// =====================================================================
// 格闘ゲームのキャラクターの動きは、次の 2 つの情報でほぼ表せます。
//   1. 今どの状態か（立ち / しゃがみ / 攻撃 / のけぞり ...）
//   2. その状態に入ってから何フレーム経ったか
// このクラスはその 2 つ（＋出している技の ID）だけを覚えます。
//
// ここには「何をすべきか」の判断は一切書きません。それは Fighter.h と
// MoveExecutor.h の仕事です。役割を分けておくと、
// 「フレーム数が進まない」バグを探すときにこの短いファイルだけ見れば
// 済むので、原因追跡がとても楽になります。
// =====================================================================
#pragma once
#include <string>

#include "engine/Constants.h"

namespace kakuge {

class StateMachine {
public:
    CharState CurrentState = CharState::Idle;  // 今の状態
    CharState PreviousState = CharState::Idle; // 直前の状態（演出などに使える）
    int CurrentFrame = 0;                      // 今の状態に入ってからの経過フレーム
    std::string CurrentMove;                   // 出している技の ID（技以外では空）

    void ChangeState(CharState newState) { ChangeState(newState, ""); }

    // 状態を切り替える。フレーム数は 0 に戻ります。
    //
    // 重要: 同じ状態・同じ技への切り替えは「何もしない」で抜けます。
    // 例えば前進中は毎フレーム ChangeState(WalkForward) が呼ばれますが、
    // ここで毎回リセットしてしまうと CurrentFrame が永遠に 0 のままになり、
    // アニメーションが 1 コマ目で止まってしまいます。
    void ChangeState(CharState newState, const std::string& moveId) {
        if (newState == CurrentState && moveId == CurrentMove) return;
        PreviousState = CurrentState;
        CurrentState = newState;
        CurrentMove = moveId;
        CurrentFrame = 0;
    }

    // 1 フレーム進める（毎フレーム 1 回だけ呼ぶ）。
    void Tick() { CurrentFrame += 1; }

    bool IsAttacking() const { return CurrentState == CharState::Attack; }

    // 「今プレイヤーの操作を受け付けられるか」。
    // のけぞり中・ダウン中・技の最中は false になり、新しい行動を
    // 始められません。これが格闘ゲームの「硬直」の正体です。
    bool IsActionable() const {
        return CurrentState == CharState::Idle ||
               CurrentState == CharState::WalkForward ||
               CurrentState == CharState::WalkBackward ||
               CurrentState == CharState::Crouch;
    }
};

} // namespace kakuge
