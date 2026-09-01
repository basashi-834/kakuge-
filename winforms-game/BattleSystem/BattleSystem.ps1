# BattleSystem/BattleSystem.ps1
# Per-match orchestrator. The ONLY place that calls Fighter.FrameStep(), so
# hit detection / pushbox resolution / projectile updates always run in one
# deterministic order every fixed 60Hz logic tick (section 11/38 - mirrors
# MatchController from the earlier Godot prototype).

class BattleSystem {
    [Fighter]$Player1
    [Fighter]$Player2
    [CPUAI]$CpuAI
    [System.Collections.ArrayList]$Projectiles = [System.Collections.ArrayList]::new()

    [int]$FramesLeft = 0
    [bool]$MatchActive = $false
    $Winner = $null
    [bool]$IsDraw = $false

    [System.Collections.ArrayList]$AllEffects = [System.Collections.ArrayList]::new()
    [System.Collections.ArrayList]$AllSounds = [System.Collections.ArrayList]::new()

    static [double] $StageMinX = -560.0
    static [double] $StageMaxX = 560.0

    [void] StartMatch([CharacterStats]$p1Stats, [hashtable]$p1Moves, [CharacterStats]$p2Stats, [hashtable]$p2Moves, [int]$roundTimeSeconds) {
        $this.Player1 = [Fighter]::new()
        $this.Player2 = [Fighter]::new()
        $this.Player1.Setup($p1Stats, $p1Moves)
        $this.Player2.Setup($p2Stats, $p2Moves)
        $this.Player1.Opponent = $this.Player2
        $this.Player2.Opponent = $this.Player1
        foreach ($p in @($this.Player1, $this.Player2)) {
            $p.StageMinX = [BattleSystem]::StageMinX
            $p.StageMaxX = [BattleSystem]::StageMaxX
        }
        $this.Player1.PositionX = -220.0; $this.Player1.PositionY = 0.0
        $this.Player2.PositionX = 220.0; $this.Player2.PositionY = 0.0
        $this.Player1.Facing = [Constants]::FacingRight
        $this.Player2.Facing = [Constants]::FacingLeft

        $this.CpuAI = [CPUAI]::new($this.Player2, $this.Player1)
        $this.Projectiles = [System.Collections.ArrayList]::new()
        $this.FramesLeft = $roundTimeSeconds * [Constants]::Fps
        $this.MatchActive = $true
        $this.Winner = $null
        $this.IsDraw = $false
    }

    # One fixed 60Hz logic tick. $p1RawInput is real keyboard state from
    # GameScreen; the CPU's input is produced internally via CpuAI.
    [void] Update([double]$dt, [hashtable]$p1RawInput) {
        if (-not $this.MatchActive) { return }
        $this.AllEffects.Clear()
        $this.AllSounds.Clear()

        $p2Input = $this.CpuAI.Decide()
        $this.Player1.FrameStep($dt, $p1RawInput)
        $this.Player2.FrameStep($dt, $p2Input)

        $this.ResolvePushboxes()
        $this.ResolveCombat($this.Player1, $this.Player2)
        $this.ResolveCombat($this.Player2, $this.Player1)
        $this.UpdateProjectiles($dt)

        $this.DrainFighterEvents($this.Player1)
        $this.DrainFighterEvents($this.Player2)

        if ($this.Player1.IsDead -or $this.Player2.IsDead) {
            $this.EndByKO()
            return
        }

        $this.FramesLeft -= 1
        if ($this.FramesLeft -le 0) { $this.EndByTimeout() }
    }

    [void] ResolvePushboxes() {
        $r1 = $this.Player1.PushboxRect()
        $r2 = $this.Player2.PushboxRect()
        if (-not $r1.Intersects($r2)) { return }
        $overlapX = [Math]::Min($r1.Right(), $r2.Right()) - [Math]::Max($r1.Left(), $r2.Left())
        if ($overlapX -le 0) { return }
        $dir = 1.0
        if ($this.Player1.PositionX -lt $this.Player2.PositionX) { $dir = -1.0 }
        $push = $overlapX / 2.0
        $this.Player1.PositionX += $push * $dir
        $this.Player2.PositionX -= $push * $dir
        $this.Player1.ClampToStage()
        $this.Player2.ClampToStage()
    }

    [void] ResolveCombat([Fighter]$attacker, [Fighter]$defender) {
        if ($null -eq $attacker.ActiveHitboxRect -or $null -eq $attacker.CurrentMoveData) { return }
        if ($defender.IsDead) { return }
        if ($attacker.AlreadyHit.Contains($defender)) { return }
        $hurtRect = $defender.HurtboxRect()
        if (-not $attacker.ActiveHitboxRect.Intersects($hurtRect)) { return }

        [void]$attacker.AlreadyHit.Add($defender)
        $move = $attacker.CurrentMoveData
        $result = $defender.ReceiveHit($move, $attacker)
        if ($move.Hitstop -gt $attacker.HitstopTimer) { $attacker.HitstopTimer = $move.Hitstop }
        $gain = $move.MeterGain
        if ([bool]$result.blocked) { $gain = $gain * 0.5 }
        $attacker.Gauge.Add($gain)
    }

    [void] UpdateProjectiles([double]$dt) {
        $survivors = New-Object System.Collections.ArrayList
        foreach ($proj in $this.Projectiles) {
            $alive = $proj.FrameStep($dt)
            if ($alive) {
                $target = $this.Player1
                if ($proj.Owner -eq $this.Player1) { $target = $this.Player2 }
                if (-not $proj.HasHit -and -not $target.IsDead) {
                    if ($proj.HitboxRect().Intersects($target.HurtboxRect())) {
                        $proj.HasHit = $true
                        [void]$target.ReceiveHit($proj.Move, $proj.Owner)
                        $alive = $false
                    }
                }
            }
            if ($alive) { [void]$survivors.Add($proj) }
        }
        $this.Projectiles = $survivors
    }

    [void] DrainFighterEvents([Fighter]$fighter) {
        foreach ($e in $fighter.PendingEffects) { [void]$this.AllEffects.Add($e) }
        $fighter.PendingEffects.Clear()
        foreach ($s in $fighter.PendingSounds) { [void]$this.AllSounds.Add($s) }
        $fighter.PendingSounds.Clear()
        if ($null -ne $fighter.PendingProjectileRequest) {
            $req = $fighter.PendingProjectileRequest
            $proj = [Projectile]::new()
            $proj.StageMinX = [BattleSystem]::StageMinX
            $proj.StageMaxX = [BattleSystem]::StageMaxX
            $proj.Setup($req.move, $fighter, $req.x, $req.y, $req.facing)
            [void]$this.Projectiles.Add($proj)
            $fighter.PendingProjectileRequest = $null
        }
    }

    [void] EndByKO() {
        if (-not $this.MatchActive) { return }
        $this.MatchActive = $false
        if ($this.Player1.IsDead -and $this.Player2.IsDead) {
            $this.IsDraw = $true
            $this.Winner = $null
        }
        elseif ($this.Player1.IsDead) {
            $this.Winner = $this.Player2
        }
        else {
            $this.Winner = $this.Player1
        }
    }

    [void] EndByTimeout() {
        if (-not $this.MatchActive) { return }
        $this.MatchActive = $false
        if ($this.Player1.CurrentHP -eq $this.Player2.CurrentHP) {
            $this.IsDraw = $true
            $this.Winner = $null
        }
        elseif ($this.Player1.CurrentHP -gt $this.Player2.CurrentHP) {
            $this.Winner = $this.Player1
        }
        else {
            $this.Winner = $this.Player2
        }
    }
}
