# MoveData/MoveExecutor.ps1
# Frame management for whichever move is currently playing (section 11).
# Stateless: phase is derived purely from (move, currentFrame), so it can
# never desync from StateMachine.CurrentFrame, which is the single source
# of truth for "how many frames have we been in this move".
#
#   Startup:  frame in [1, startup-1]
#   Active:   frame in [startup, startup+active-1]
#   Recovery: frame in [startup+active, startup+active+recovery-1]
#   Done:     frame >= startup+active+recovery

class MoveExecutor {
    static [string] $PhaseStartup  = "startup"
    static [string] $PhaseActive   = "active"
    static [string] $PhaseRecovery = "recovery"
    static [string] $PhaseDone     = "done"

    static [string] GetPhase($move, [int]$frame) {
        if ($frame -lt $move.Startup) { return [MoveExecutor]::PhaseStartup }
        elseif ($frame -lt ($move.Startup + $move.Active)) { return [MoveExecutor]::PhaseActive }
        elseif ($frame -lt ($move.Startup + $move.Active + $move.Recovery)) { return [MoveExecutor]::PhaseRecovery }
        return [MoveExecutor]::PhaseDone
    }

    static [bool] IsInvincible($move, [int]$frame, [string]$kind) {
        $inv = $move.Invincibility
        $itype = [string]$inv.type
        if ($itype -eq [Constants]::InvincibleNone) { return $false }
        $start = [int]$inv.start_frame
        $end = [int]$inv.end_frame
        if ($frame -lt $start -or $frame -gt $end) { return $false }
        if ([string]::IsNullOrEmpty($kind)) { return $true }
        if ($itype -eq [Constants]::InvincibleFull) { return $true }
        return ($itype -eq $kind)
    }

    static [bool] CanCancel($move, [int]$frame) {
        return $move.IsCancelWindowOpen($frame)
    }

    # Returns the currently active hitbox as a RectBox (world-space, already
    # facing-flipped), or $null if no hitbox should be live this frame.
    static [RectBox] GetActiveHitboxRect($move, [int]$frame, [int]$facing, [double]$originX, [double]$originY) {
        $phase = [MoveExecutor]::GetPhase($move, $frame)
        if ($phase -ne [MoveExecutor]::PhaseActive) { return $null }
        if ($move.Hitboxes.Count -eq 0) { return $null }
        $box = $move.Hitboxes[0]
        $offsetX = [double]$box.offsetX * $facing
        $offsetY = [double]$box.offsetY
        return [RectBox]::new($originX + $offsetX, $originY + $offsetY, [double]$box.width, [double]$box.height)
    }
}
