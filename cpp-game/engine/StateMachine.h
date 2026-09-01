// engine/StateMachine.h
// Pure state-tracking. Only remembers CurrentState / CurrentMove /
// CurrentFrame - all "what should happen" logic lives in Fighter /
// MoveExecutor. 1:1 port of Character/StateMachine.ps1.
#pragma once
#include <string>
#include "Constants.h"

namespace kakuge {

class StateMachine {
public:
    CharState CurrentState = CharState::Idle;
    CharState PreviousState = CharState::Idle;
    int CurrentFrame = 0;
    std::string CurrentMove;

    void ChangeState(CharState newState) { ChangeState(newState, ""); }

    void ChangeState(CharState newState, const std::string& moveId) {
        if (newState == CurrentState && moveId == CurrentMove) return;
        PreviousState = CurrentState;
        CurrentState = newState;
        CurrentMove = moveId;
        CurrentFrame = 0;
    }

    void Tick() { CurrentFrame += 1; }

    bool IsAttacking() const { return CurrentState == CharState::Attack; }

    bool IsActionable() const {
        return CurrentState == CharState::Idle || CurrentState == CharState::WalkForward ||
               CurrentState == CharState::WalkBackward || CurrentState == CharState::Crouch;
    }
};

} // namespace kakuge
