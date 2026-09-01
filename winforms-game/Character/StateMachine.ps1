# Character/StateMachine.ps1
# Pure state-tracking (section 6). Only remembers CurrentState / CurrentMove
# / CurrentFrame - all "what should happen" logic lives in Fighter /
# MoveExecutor, matching the Godot prototype's separation.

class StateMachine {
    [CharState]$CurrentState = [CharState]::Idle
    [CharState]$PreviousState = [CharState]::Idle
    [int]$CurrentFrame = 0
    [string]$CurrentMove = ""

    [void] ChangeState([CharState]$newState) {
        $this.ChangeState($newState, "")
    }

    [void] ChangeState([CharState]$newState, [string]$moveId) {
        if ($newState -eq $this.CurrentState -and $moveId -eq $this.CurrentMove) { return }
        $this.PreviousState = $this.CurrentState
        $this.CurrentState = $newState
        $this.CurrentMove = $moveId
        $this.CurrentFrame = 0
    }

    [void] Tick() {
        $this.CurrentFrame += 1
    }

    [bool] IsAttacking() {
        return $this.CurrentState -eq [CharState]::Attack
    }

    [bool] IsActionable() {
        return $this.CurrentState -in @([CharState]::Idle, [CharState]::WalkForward, [CharState]::WalkBackward, [CharState]::Crouch)
    }
}
