# Character/Fighter.ps1
# The character controller. Owns physics + delegates to small focused
# helpers (StateMachine, MoveExecutor, InputBuffer, CommandParser,
# SuperGauge) rather than doing everything itself (section 38).
#
# Unlike the original Godot prototype, Fighter never reads a keyboard
# itself: BattleSystem.Update() calls FrameStep($dt, $rawInput) once per
# fixed 60Hz logic tick for player1 then player2, passing in already-
# resolved input (real keys for the human, CPUAI's synthesized input for
# the CPU) - this keeps InputSystem fully decoupled from Character
# (section 38: "CPU AIをCharacterから分離する" / InputSystem separation).

class Fighter {
    [CharacterStats]$Stats
    [hashtable]$Moveset = @{}
    [StateMachine]$SM = [StateMachine]::new()
    [InputBuffer]$InputBuf = [InputBuffer]::new()
    [SuperGauge]$Gauge = [SuperGauge]::new()
    [HurtboxSet]$Hurtboxes = [HurtboxSet]::new()

    [int]$CurrentHP = 1000
    [int]$Facing = 1
    [bool]$FacingLocked = $false
    [bool]$IsDead = $false
    $Opponent = $null

    [double]$StageMinX = -560.0
    [double]$StageMaxX = 560.0

    [double]$PositionX = 0.0
    [double]$PositionY = 0.0
    [double]$VelocityX = 0.0
    [double]$VelocityY = 0.0

    $CurrentMoveData = $null
    [bool]$ProjectileSpawnedThisActivation = $false

    [int]$HitstunTimer = 0
    [int]$BlockstunTimer = 0
    [int]$HitstopTimer = 0
    [int]$KnockdownTimer = 0
    [int]$WakeupTimer = 0
    [int]$ThrownTimer = 0
    [int]$DashTimer = 0
    [bool]$IsCrouchingGuard = $false
    [int]$FrameCounter = 0

    [double]$PushboxHalfWidth = 28.0
    [double]$PushboxHalfHeight = 55.0
    [RectBox]$ActiveHitboxRect = $null
    [System.Collections.ArrayList]$AlreadyHit = [System.Collections.ArrayList]::new()

    [System.Collections.ArrayList]$PendingEffects = [System.Collections.ArrayList]::new()
    [System.Collections.ArrayList]$PendingSounds = [System.Collections.ArrayList]::new()
    $PendingProjectileRequest = $null
    [bool]$LastHitBlocked = $false

    $HeldButtonsPrev = @{ Light = $false; Medium = $false; Heavy = $false; Special = $false; Super = $false; Throw = $false }
    [int]$LastForwardTapFrame = -999

    static [double] $GroundY = 0.0
    static [int] $DashInputWindow = 14
    static [int] $DashDuration = 14
    static [int] $KnockdownFrames = 40
    static [int] $HardKnockdownFrames = 60
    static [int] $WakeupFrames = 14
    static [int] $ThrownLockFrames = 20

    [void] Setup([CharacterStats]$stats, [hashtable]$moveset) {
        $this.Stats = $stats
        $this.Moveset = $moveset
        $this.ResetForRound()
    }

    [void] ResetForRound() {
        $this.CurrentHP = $this.Stats.MaxHP
        $this.IsDead = $false
        $this.CurrentMoveData = $null
        $this.HitstunTimer = 0
        $this.BlockstunTimer = 0
        $this.HitstopTimer = 0
        $this.KnockdownTimer = 0
        $this.WakeupTimer = 0
        $this.ThrownTimer = 0
        $this.DashTimer = 0
        $this.Gauge.Value = 0.0
        $this.InputBuf.Clear()
        $this.SM.ChangeState([CharState]::Idle, "")
        $this.ActiveHitboxRect = $null
        $this.AlreadyHit.Clear()
        $this.PendingEffects.Clear()
        $this.PendingSounds.Clear()
        $this.PendingProjectileRequest = $null
        $this.VelocityX = 0.0
        $this.VelocityY = 0.0
    }

    # -----------------------------------------------------------------
    # Main per-frame entry point (called by BattleSystem)
    # -----------------------------------------------------------------
    [void] FrameStep([double]$dt, [hashtable]$raw) {
        if ($this.IsDead) {
            $this.VelocityY += $this.Stats.Gravity * $dt
            $this.PositionX += $this.VelocityX * $dt
            $this.PositionY += $this.VelocityY * $dt
            if ($this.PositionY -gt [Fighter]::GroundY) { $this.PositionY = [Fighter]::GroundY; $this.VelocityY = 0.0 }
            return
        }

        $this.FrameCounter += 1
        $digit = [InputBuffer]::ComputeDigit($raw.Left, $raw.Right, $raw.Down, $raw.Up, $this.Facing)
        $pressed = $this.NewlyPressedButtons($raw.ButtonsHeld)
        # Synthetic "Throw" input: Light+Medium held together (no dedicated
        # throw key in the section 29 control scheme).
        if ($raw.ButtonsHeld.Light -and $raw.ButtonsHeld.Medium -and (($pressed -contains "Light") -or ($pressed -contains "Medium")) -and -not ($pressed -contains "Throw")) {
            $pressed += "Throw"
        }
        $this.InputBuf.RecordFrame($this.FrameCounter, $digit, $pressed)

        if ($this.HitstopTimer -gt 0) {
            $this.HitstopTimer -= 1
            return
        }

        $this.SM.Tick()
        $this.HandleStateLogic($raw, $pressed)
        $this.ApplyPhysics($dt)
        $this.ClampToStage()
        $this.UpdateFacing()
    }

    [string[]] NewlyPressedButtons([hashtable]$held) {
        $result = New-Object System.Collections.Generic.List[string]
        foreach ($key in @("Light", "Medium", "Heavy", "Special", "Super")) {
            $isHeld = [bool]$held[$key]
            $wasHeld = [bool]$this.HeldButtonsPrev[$key]
            if ($isHeld -and -not $wasHeld) { $result.Add($key) }
        }
        $this.HeldButtonsPrev = @{
            Light = [bool]$held.Light; Medium = [bool]$held.Medium; Heavy = [bool]$held.Heavy
            Special = [bool]$held.Special; Super = [bool]$held.Super; Throw = $false
        }
        return $result.ToArray()
    }

    [bool] IsHoldingBack([hashtable]$raw) {
        if ($raw.Left -and $this.Facing -eq [Constants]::FacingRight) { return $true }
        if ($raw.Right -and $this.Facing -eq [Constants]::FacingLeft) { return $true }
        return $false
    }

    [bool] IsHoldingForward([hashtable]$raw) {
        if ($raw.Right -and $this.Facing -eq [Constants]::FacingRight) { return $true }
        if ($raw.Left -and $this.Facing -eq [Constants]::FacingLeft) { return $true }
        return $false
    }

    # -----------------------------------------------------------------
    # State logic
    # -----------------------------------------------------------------
    [void] HandleStateLogic([hashtable]$raw, [string[]]$pressed) {
        switch ($this.SM.CurrentState) {
            ([CharState]::Hitstun) {
                $this.HitstunTimer -= 1
                if ($this.HitstunTimer -le 0) { $this.SM.ChangeState([CharState]::Idle, "") }
                $this.VelocityX = $this.MoveToward($this.VelocityX, 0.0, 900.0 / [Constants]::Fps)
            }
            ([CharState]::Block) {
                $this.BlockstunTimer -= 1
                $this.VelocityX = $this.MoveToward($this.VelocityX, 0.0, 900.0 / [Constants]::Fps)
                if ($this.BlockstunTimer -le 0) { $this.SM.ChangeState([CharState]::Idle, "") }
            }
            ([CharState]::Throw) {
                $this.ThrownTimer -= 1
                if ($this.ThrownTimer -le 0) { $this.EnterKnockdown($false, 0) }
            }
            ([CharState]::Knockdown) {
                $this.KnockdownTimer -= 1
                $this.VelocityX = $this.MoveToward($this.VelocityX, 0.0, 1200.0 / [Constants]::Fps)
                if ($this.KnockdownTimer -le 0) {
                    $this.WakeupTimer = [Fighter]::WakeupFrames
                    $this.SM.ChangeState([CharState]::WakeUp, "")
                }
            }
            ([CharState]::WakeUp) {
                $this.WakeupTimer -= 1
                if ($this.WakeupTimer -le 0) { $this.SM.ChangeState([CharState]::Idle, "") }
            }
            ([CharState]::Attack) {
                [void]$this.TryStartMove($raw, $pressed)
                $this.ProgressMove()
            }
            ([CharState]::Jump) {
                if (-not $this.TryStartMove($raw, $pressed)) {
                    $vx = 0.0
                    if ($raw.Right) { $vx = $this.Stats.WalkForwardSpeed }
                    if ($raw.Left) { $vx = $vx - $this.Stats.WalkForwardSpeed }
                    $this.VelocityX = $vx
                }
                if ($this.PositionY -ge [Fighter]::GroundY -and $this.VelocityY -ge 0) {
                    $this.PositionY = [Fighter]::GroundY
                    $this.VelocityY = 0
                    if ($raw.Down) { $this.SM.ChangeState([CharState]::Crouch, "") }
                    else { $this.SM.ChangeState([CharState]::Idle, "") }
                }
            }
            default {
                if ($this.DashTimer -gt 0) { $this.DashTimer -= 1 }
                if (-not $this.TryStartMove($raw, $pressed)) {
                    $this.HandleGroundMovement($raw)
                }
            }
        }
    }

    [double] MoveToward([double]$current, [double]$target, [double]$step) {
        if ($current -lt $target) { return [Math]::Min($current + $step, $target) }
        if ($current -gt $target) { return [Math]::Max($current - $step, $target) }
        return $target
    }

    [void] HandleGroundMovement([hashtable]$raw) {
        $this.IsCrouchingGuard = ($raw.Down -and $this.IsHoldingBack($raw))

        if ($raw.Up -and -not $raw.Down) {
            $this.VelocityY = $this.Stats.JumpVelocity
            $this.SM.ChangeState([CharState]::Jump, "")
            if ($this.IsHoldingForward($raw)) { $this.VelocityX = $this.Stats.WalkForwardSpeed }
            elseif ($this.IsHoldingBack($raw)) { $this.VelocityX = -$this.Stats.WalkForwardSpeed }
            else { $this.VelocityX = 0.0 }
            return
        }

        if ($raw.Down) {
            if ($this.IsHoldingBack($raw)) { $this.SM.ChangeState([CharState]::Block, "") }
            else { $this.SM.ChangeState([CharState]::Crouch, "") }
            $this.VelocityX = 0.0
            return
        }

        if ($this.IsHoldingForward($raw)) {
            if (($this.FrameCounter - $this.LastForwardTapFrame) -le [Fighter]::DashInputWindow -and $this.DashTimer -le 0) {
                $this.DashTimer = [Fighter]::DashDuration
            }
            $this.LastForwardTapFrame = $this.FrameCounter
            $spd = $this.Stats.WalkForwardSpeed
            if ($this.DashTimer -gt 0) { $spd = $this.Stats.DashSpeed }
            $this.VelocityX = $spd * $this.Facing
            $this.SM.ChangeState([CharState]::WalkForward, "")
        }
        elseif ($this.IsHoldingBack($raw)) {
            $this.VelocityX = -$this.Stats.WalkBackwardSpeed * $this.Facing
            if ($null -ne $this.Opponent -and $this.Opponent.SM.CurrentState -eq [CharState]::Attack) {
                $this.SM.ChangeState([CharState]::Block, "")
            } else {
                $this.SM.ChangeState([CharState]::WalkBackward, "")
            }
        }
        else {
            $this.VelocityX = 0.0
            $this.SM.ChangeState([CharState]::Idle, "")
        }
    }

    # -----------------------------------------------------------------
    # Moves
    # -----------------------------------------------------------------
    [bool] TryStartMove([hashtable]$raw, [string[]]$pressed) {
        if ($null -eq $pressed -or $pressed.Count -eq 0) { return $false }
        $stance = $this.CurrentStance()

        $superCandidates = New-Object System.Collections.Generic.List[object]
        $specialCandidates = New-Object System.Collections.Generic.List[object]
        $normalCandidates = New-Object System.Collections.Generic.List[object]

        foreach ($move in $this.Moveset.Values) {
            if ($move.InputCommand -ne "" -and [CommandParser]::Matches($this.InputBuf, $move.InputCommand, $move.Button, [Constants]::CommandWindow)) {
                if ($move.HasTag([Constants]::TagSuper)) { $superCandidates.Add($move) }
                elseif ($move.HasTag([Constants]::TagSpecial)) { $specialCandidates.Add($move) }
                else { $normalCandidates.Add($move) }
            }
        }
        foreach ($btn in $pressed) {
            foreach ($move in $this.Moveset.Values) {
                if ($move.InputCommand -eq "" -and $move.Button -eq $btn -and $move.Stance -eq $stance) {
                    $normalCandidates.Add($move)
                }
            }
        }

        foreach ($group in @($superCandidates, $specialCandidates, $normalCandidates)) {
            foreach ($move in $group) {
                if ($this.CanStart($move)) {
                    $this.StartMove($move)
                    return $true
                }
            }
        }
        return $false
    }

    [string] CurrentStance() {
        if ($this.SM.CurrentState -eq [CharState]::Jump) { return "air" }
        if ($this.SM.CurrentState -eq [CharState]::Crouch) { return "crouch" }
        return "stand"
    }

    [bool] CanStart($move) {
        if (($move.HasTag([Constants]::TagSuper) -or $move.MeterCost -gt 0) -and -not $this.Gauge.CanSpend($move.MeterCost)) {
            return $false
        }
        if ($this.SM.CurrentState -eq [CharState]::Attack) {
            if ($null -eq $this.CurrentMoveData) { return $false }
            return ([MoveExecutor]::CanCancel($this.CurrentMoveData, $this.SM.CurrentFrame) -and $this.CurrentMoveData.CanCancelInto($move.Id))
        }
        return $true
    }

    [void] StartMove($move) {
        if ($move.MeterCost -gt 0) { [void]$this.Gauge.Spend($move.MeterCost) }
        $this.CurrentMoveData = $move
        $this.ProjectileSpawnedThisActivation = $false
        $this.FacingLocked = $true
        $this.SM.ChangeState([CharState]::Attack, $move.Id)
        [void]$this.PendingSounds.Add("attack")
    }

    [void] ProgressMove() {
        if ($null -eq $this.CurrentMoveData) { $this.SM.ChangeState([CharState]::Idle, ""); return }
        $frame = $this.SM.CurrentFrame
        $move = $this.CurrentMoveData
        $phase = [MoveExecutor]::GetPhase($move, $frame)

        if ($phase -eq [MoveExecutor]::PhaseActive) {
            if ($null -eq $this.ActiveHitboxRect) {
                $this.ActiveHitboxRect = [MoveExecutor]::GetActiveHitboxRect($move, $frame, $this.Facing, $this.PositionX, $this.PositionY)
                $this.AlreadyHit.Clear()
            }
        } else {
            $this.ActiveHitboxRect = $null
        }

        if ($phase -eq [MoveExecutor]::PhaseActive -and $move.HasTag([Constants]::TagProjectile) -and -not $this.ProjectileSpawnedThisActivation) {
            $this.ProjectileSpawnedThisActivation = $true
            $this.PendingProjectileRequest = @{ move = $move; x = $this.PositionX; y = $this.PositionY; facing = $this.Facing }
        }

        if ($phase -eq [MoveExecutor]::PhaseDone) {
            $this.ActiveHitboxRect = $null
            $this.FacingLocked = $false
            $wasAir = $this.PositionY -lt ([Fighter]::GroundY - 1.0)
            $this.CurrentMoveData = $null
            if ($wasAir) { $this.SM.ChangeState([CharState]::Jump, "") }
            else { $this.SM.ChangeState([CharState]::Idle, "") }
        }
    }

    # -----------------------------------------------------------------
    # Combat resolution (called by BattleSystem)
    # -----------------------------------------------------------------
    [bool] IsInvincibleAgainst([string]$kind) {
        if ($this.SM.CurrentState -eq [CharState]::Attack -and $null -ne $this.CurrentMoveData) {
            return [MoveExecutor]::IsInvincible($this.CurrentMoveData, $this.SM.CurrentFrame, $kind)
        }
        return $false
    }

    [hashtable] ReceiveHit($move, [Fighter]$attacker) {
        if ($this.IsDead) { return @{ blocked = $false; whiffed = $true } }
        $invKind = [Constants]::InvincibleStrike
        if ($move.GuardType -eq [Constants]::GuardThrow) { $invKind = [Constants]::InvincibleThrow }
        if ($this.IsInvincibleAgainst($invKind)) { return @{ blocked = $false; whiffed = $true } }

        $blocked = $false
        if ($move.GuardType -ne [Constants]::GuardThrow) {
            $blocked = $this.CheckGuard($move)
        }

        $this.HitstopTimer = $move.Hitstop
        if ($blocked) {
            $chip = [int][Math]::Round($move.Damage * $move.ChipDamagePercent)
            $this.CurrentHP = [Math]::Max(0, $this.CurrentHP - $chip)
            $this.BlockstunTimer = $move.Blockstun
            $this.SM.ChangeState([CharState]::Block, "")
            $this.Gauge.Add($move.MeterGain * 0.5)
            $this.ApplyKnockback($move, $attacker, $true)
            [void]$this.PendingEffects.Add(@{ kind = "guard"; x = $this.PositionX; y = $this.PositionY })
            [void]$this.PendingSounds.Add("block")
        }
        else {
            $this.CurrentHP = [Math]::Max(0, $this.CurrentHP - $move.Damage)
            $this.Gauge.Add($move.MeterGain)
            $this.ApplyKnockback($move, $attacker, $false)
            if ($move.GuardType -eq [Constants]::GuardThrow) {
                $this.ThrownTimer = [Fighter]::ThrownLockFrames
                $this.SM.ChangeState([CharState]::Throw, "")
            }
            elseif ($move.HitOutcome -eq [Constants]::HitNormal) {
                $this.HitstunTimer = $move.Hitstun
                $this.SM.ChangeState([CharState]::Hitstun, "")
            }
            else {
                $this.EnterKnockdown(($move.HitOutcome -eq [Constants]::HitHardKnockdown), $move.Hitstun)
            }
            $fx = "hit"
            if ($move.HasTag([Constants]::TagSuper)) { $fx = "super" }
            elseif ($move.HasTag([Constants]::TagSpecial)) { $fx = "special" }
            elseif ($move.HasTag([Constants]::TagHeavy)) { $fx = "heavy_hit" }
            [void]$this.PendingEffects.Add(@{ kind = $fx; x = $this.PositionX; y = $this.PositionY })
            [void]$this.PendingSounds.Add("hit")
        }

        if ($this.CurrentHP -le 0 -and -not $this.IsDead) {
            $this.IsDead = $true
            $this.SM.ChangeState([CharState]::Dead, "")
            [void]$this.PendingSounds.Add("ko")
        }
        $this.LastHitBlocked = $blocked
        return @{ blocked = $blocked; whiffed = $false }
    }

    [void] EnterKnockdown([bool]$hard, [int]$customFrames) {
        if ($customFrames -gt 0) { $this.KnockdownTimer = $customFrames }
        elseif ($hard) { $this.KnockdownTimer = [Fighter]::HardKnockdownFrames }
        else { $this.KnockdownTimer = [Fighter]::KnockdownFrames }
        $this.SM.ChangeState([CharState]::Knockdown, "")
    }

    [bool] CheckGuard($move) {
        # NOTE: guard is decided from THIS FRAME's own most-recent recorded
        # input (the last entry in InputBuf), since Fighter no longer reads
        # a live keyboard itself - BattleSystem always calls FrameStep()
        # (which records input) before resolving hits for that same tick.
        if ($this.InputBuf.History.Count -eq 0) { return $false }
        $lastEntry = $this.InputBuf.History[$this.InputBuf.History.Count - 1]
        $digit = [int]$lastEntry.digit
        $holdingBack = $digit -in @(1, 4, 7)
        $inGuardPosture = ($this.SM.CurrentState -eq [CharState]::Block) -or ($this.SM.IsActionable() -and $holdingBack)
        if (-not $inGuardPosture -or -not $holdingBack) { return $false }

        if ($move.GuardType -eq [Constants]::GuardHigh) { return $true }
        if ($move.GuardType -eq [Constants]::GuardLow) {
            return ($this.IsCrouchingGuard -or $this.SM.CurrentState -eq [CharState]::Crouch)
        }
        if ($move.GuardType -eq [Constants]::GuardOverhead) {
            return -not ($this.IsCrouchingGuard -or $this.SM.CurrentState -eq [CharState]::Crouch)
        }
        return $false
    }

    [void] ApplyKnockback($move, [Fighter]$attacker, [bool]$isBlock) {
        $dir = - $attacker.Facing
        $kx = $move.KnockbackX
        if ($isBlock) { $kx = $kx * 0.4 }
        $this.VelocityX = $kx * $dir
        if (-not $isBlock -and $move.KnockbackY -ne 0.0) {
            $this.VelocityY = - [Math]::Abs($move.KnockbackY)
        }
    }

    # -----------------------------------------------------------------
    # Physics / misc
    # -----------------------------------------------------------------
    [void] ApplyPhysics([double]$dt) {
        if ($this.SM.CurrentState -eq [CharState]::Jump -or $this.PositionY -lt ([Fighter]::GroundY - 0.01)) {
            $this.VelocityY += $this.Stats.Gravity * $dt
        } else {
            $this.VelocityY = 0.0
        }
        $this.PositionX += $this.VelocityX * $dt
        $this.PositionY += $this.VelocityY * $dt
        if ($this.PositionY -gt [Fighter]::GroundY) {
            $this.PositionY = [Fighter]::GroundY
            $this.VelocityY = 0.0
        }
        # keep the active hitbox glued to the attacker while it's live
        if ($null -ne $this.ActiveHitboxRect -and $null -ne $this.CurrentMoveData) {
            $this.ActiveHitboxRect = [MoveExecutor]::GetActiveHitboxRect($this.CurrentMoveData, $this.SM.CurrentFrame, $this.Facing, $this.PositionX, $this.PositionY)
        }
    }

    [void] ClampToStage() {
        if ($this.PositionX -lt $this.StageMinX) { $this.PositionX = $this.StageMinX }
        if ($this.PositionX -gt $this.StageMaxX) { $this.PositionX = $this.StageMaxX }
    }

    [void] UpdateFacing() {
        if ($this.FacingLocked -or $null -eq $this.Opponent) { return }
        if (-not $this.SM.IsActionable()) { return }
        if ($this.Opponent.PositionX -ge $this.PositionX) { $this.Facing = [Constants]::FacingRight }
        else { $this.Facing = [Constants]::FacingLeft }
    }

    [string] Stance() {
        if ($this.SM.CurrentState -eq [CharState]::Jump -or $this.PositionY -lt ([Fighter]::GroundY - 0.01)) { return "air" }
        if ($this.SM.CurrentState -eq [CharState]::Crouch -or $this.IsCrouchingGuard) { return "crouch" }
        return "stand"
    }

    [RectBox] HurtboxRect() {
        return $this.Hurtboxes.ForStance($this.Stance(), $this.PositionX, $this.PositionY)
    }

    [RectBox] PushboxRect() {
        return [RectBox]::new($this.PositionX, $this.PositionY - $this.PushboxHalfHeight, $this.PushboxHalfWidth * 2.0, $this.PushboxHalfHeight * 2.0)
    }

    [object] GetMove([string]$id) {
        if ($this.Moveset.ContainsKey($id)) { return $this.Moveset[$id] }
        return $null
    }

    [hashtable] DebugInfo() {
        return @{
            state = $this.SM.CurrentState.ToString()
            move = $this.SM.CurrentMove
            frame = $this.SM.CurrentFrame
            hp = $this.CurrentHP
            gauge = $this.Gauge.Value
            velocityX = $this.VelocityX
            velocityY = $this.VelocityY
            hitstun = $this.HitstunTimer
            blockstun = $this.BlockstunTimer
            hitstop = $this.HitstopTimer
        }
    }
}
