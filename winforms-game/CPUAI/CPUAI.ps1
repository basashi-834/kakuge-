# CPUAI/CPUAI.ps1
# CPU opponent "brain" - deliberately separate from Fighter (section 27/38:
# "CPU AIをCharacterから分離する"). Reads public state off both fighters and
# returns a synthetic raw-input hashtable that Fighter.FrameStep() consumes
# exactly like real keyboard input.

class CPUAI {
    [Fighter]$Self
    [Fighter]$Opp
    [System.Random]$Rng = [System.Random]::new()

    [System.Collections.Generic.Queue[hashtable]]$PendingSequence = [System.Collections.Generic.Queue[hashtable]]::new()
    $CurrentInput = $null
    [int]$DecisionCooldown = 0

    static [double] $CloseRange = 80.0   # tuned to sit inside normal-attack reach (~76-90px)
    static [double] $MidRange = 420.0
    static [double] $AntiAirRange = 260.0
    static [double] $LowHpRatio = 0.25

    CPUAI([Fighter]$self, [Fighter]$opp) {
        $this.Self = $self
        $this.Opp = $opp
        $this.CurrentInput = New-RawInput
    }

    [hashtable] Decide() {
        if ($this.PendingSequence.Count -gt 0) { return $this.PendingSequence.Dequeue() }
        if ($this.Self.IsDead -or $this.Opp.IsDead) { return New-RawInput }

        $dx = $this.Opp.PositionX - $this.Self.PositionX
        $dist = [Math]::Abs($dx)
        $dirToOpp = 1
        if ($dx -lt 0) { $dirToOpp = -1 }

        if ($this.Opp.SM.CurrentState -eq [CharState]::Attack -and $dist -lt 160.0 -and $this.Self.SM.IsActionable() -and $this.Rng.NextDouble() -lt 0.7) {
            return $this.HoldBack()
        }

        if ($this.DecisionCooldown -gt 0) {
            $this.DecisionCooldown -= 1
            return $this.CurrentInput
        }

        $this.CurrentInput = $this.Plan($dist, $dirToOpp)
        $this.DecisionCooldown = $this.Rng.Next(8, 19)
        return $this.CurrentInput
    }

    [hashtable] Plan([double]$dist, [int]$dirToOpp) {
        if (-not $this.Self.SM.IsActionable() -and $this.Self.SM.CurrentState -ne [CharState]::Jump) {
            return New-RawInput
        }

        $lowHp = $this.Self.CurrentHP -lt ($this.Self.Stats.MaxHP * [CPUAI]::LowHpRatio)
        $oppAirborne = $this.Opp.SM.CurrentState -eq [CharState]::Jump

        if ($oppAirborne -and $dist -lt [CPUAI]::AntiAirRange) {
            $antiAir = $this.FindMove({ param($m) $m.HasTag([Constants]::TagAntiAir) })
            if ($null -ne $antiAir) { return $this.UseMove($antiAir) }
        }

        if ($dist -lt 300.0 -and $this.Rng.NextDouble() -lt 0.5) {
            $superMove = $this.FindMove({ param($m) $m.HasTag([Constants]::TagSuper) -and $this.Self.Gauge.CanSpend($m.MeterCost) })
            if ($null -ne $superMove) { return $this.UseMove($superMove) }
        }

        if ($dist -lt [CPUAI]::CloseRange) {
            if ($lowHp -and $this.Rng.NextDouble() -lt 0.5) { return $this.HoldBack() }
            if ($this.Rng.NextDouble() -lt 0.55) {
                $atk = $this.PickCloseAttack()
                if ($null -ne $atk) { return $this.UseMove($atk) }
            }
            return $this.MoveDir($dirToOpp) # keep tightening spacing so attacks actually reach
        }
        elseif ($dist -lt [CPUAI]::MidRange) {
            if ($lowHp -and $this.Rng.NextDouble() -lt 0.35) { return $this.MoveDir(-$dirToOpp) }
            if ($this.Rng.NextDouble() -lt 0.6) { return $this.MoveDir($dirToOpp) }
            return New-RawInput
        }
        else {
            if (-not $lowHp -and $this.Rng.NextDouble() -lt 0.6) {
                $proj = $this.FindMove({ param($m) $m.HasTag([Constants]::TagProjectile) })
                if ($null -ne $proj) { return $this.UseMove($proj) }
            }
            return $this.MoveDir($dirToOpp)
        }
    }

    [object] PickCloseAttack() {
        $pool = New-Object System.Collections.Generic.List[object]
        foreach ($m in $this.Self.Moveset.Values) {
            if ($m.HasTag([Constants]::TagNormal) -and $m.Stance -eq "stand" -and -not $m.HasTag([Constants]::TagThrow)) {
                $pool.Add($m)
            }
        }
        if ($pool.Count -eq 0) { return $null }
        return $pool[$this.Rng.Next(0, $pool.Count)]
    }

    [object] FindMove([scriptblock]$predicate) {
        $pool = New-Object System.Collections.Generic.List[object]
        foreach ($m in $this.Self.Moveset.Values) {
            if ([bool](& $predicate $m)) { $pool.Add($m) }
        }
        if ($pool.Count -eq 0) { return $null }
        return $pool[$this.Rng.Next(0, $pool.Count)]
    }

    [hashtable] MoveDir([int]$dir) {
        $input = New-RawInput
        $facing = $this.Self.Facing
        if ($dir -eq $facing) {
            $input.Right = ($facing -eq 1)
            $input.Left = ($facing -eq -1)
        } else {
            $input.Right = ($facing -eq -1)
            $input.Left = ($facing -eq 1)
        }
        return $input
    }

    [hashtable] HoldBack() {
        $input = New-RawInput
        $facing = $this.Self.Facing
        $backIsRight = ($facing -eq -1)
        $input.Right = $backIsRight
        $input.Left = -not $backIsRight
        if ($this.Rng.NextDouble() -lt 0.3) { $input.Down = $true }
        return $input
    }

    [hashtable] UseMove($move) {
        if ($move.InputCommand -eq "") {
            $input = New-RawInput
            $input.ButtonsHeld[$move.Button] = $true
            return $input
        }
        if (-not [CommandParser]::Motions.ContainsKey($move.InputCommand)) { return New-RawInput }
        $digits = [CommandParser]::Motions[$move.InputCommand]
        $this.PendingSequence.Clear()
        foreach ($d in $digits) {
            $raw = $this.DigitToRaw($d, $this.Self.Facing)
            $this.PendingSequence.Enqueue($raw)
            $this.PendingSequence.Enqueue($this.CloneInput($raw))
        }
        $finalRaw = $this.DigitToRaw($digits[$digits.Count - 1], $this.Self.Facing)
        $finalRaw.ButtonsHeld[$move.Button] = $true
        $this.PendingSequence.Enqueue($finalRaw)
        $this.PendingSequence.Enqueue((New-RawInput))
        return $this.PendingSequence.Dequeue()
    }

    [hashtable] CloneInput([hashtable]$src) {
        return @{
            Left = $src.Left; Right = $src.Right; Down = $src.Down; Up = $src.Up
            ButtonsHeld = @{
                Light = $src.ButtonsHeld.Light; Medium = $src.ButtonsHeld.Medium; Heavy = $src.ButtonsHeld.Heavy
                Special = $src.ButtonsHeld.Special; Super = $src.ButtonsHeld.Super; Throw = $src.ButtonsHeld.Throw
            }
        }
    }

    [hashtable] DigitToRaw([int]$digit, [int]$facing) {
        $input = New-RawInput
        $forwardIsRight = ($facing -eq 1)
        $forward = $digit -in @(3, 6, 9)
        $back = $digit -in @(1, 4, 7)
        $down = $digit -in @(1, 2, 3)
        $up = $digit -in @(7, 8, 9)
        if ($forward) { $input.Right = $forwardIsRight; $input.Left = -not $forwardIsRight }
        elseif ($back) { $input.Right = -not $forwardIsRight; $input.Left = $forwardIsRight }
        $input.Down = $down
        $input.Up = $up
        return $input
    }
}
