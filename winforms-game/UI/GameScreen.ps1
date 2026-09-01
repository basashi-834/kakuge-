# UI/GameScreen.ps1
# The battle screen: owns the render Panel, the fixed-timestep game loop
# Timer, and keyboard input capture. Combat logic itself lives entirely in
# BattleSystem/Fighter/CPUAI - this file only reads their PUBLIC state to
# draw and only writes raw input hashtables into Fighter.FrameStep(),
# never touching combat rules directly (section 38: UIと戦闘処理の分離).
#
# Cross-screen key routing note: PowerShell's `$script:` scope is tied to
# whichever .ps1 file a function was DEFINED in, which is NOT the same
# script-scope as Main.ps1 even though everything is dot-sourced together.
# To avoid that ambiguity, the "which screen currently owns the keyboard"
# flag is deliberately kept as $global:KakugeActiveGameState (set here,
# read by Main.ps1's single pair of Form-level KeyDown/KeyUp handlers).

function Get-P1RawInput([System.Collections.Generic.HashSet[System.Windows.Forms.Keys]]$heldKeys) {
    $input = New-RawInput
    $input.Left = $heldKeys.Contains([System.Windows.Forms.Keys]::A)
    $input.Right = $heldKeys.Contains([System.Windows.Forms.Keys]::D)
    $input.Down = $heldKeys.Contains([System.Windows.Forms.Keys]::S)
    $input.Up = $heldKeys.Contains([System.Windows.Forms.Keys]::Space)
    $input.ButtonsHeld.Light = $heldKeys.Contains([System.Windows.Forms.Keys]::J)
    $input.ButtonsHeld.Medium = $heldKeys.Contains([System.Windows.Forms.Keys]::K)
    $input.ButtonsHeld.Heavy = $heldKeys.Contains([System.Windows.Forms.Keys]::L)
    $input.ButtonsHeld.Special = $heldKeys.Contains([System.Windows.Forms.Keys]::U)
    $input.ButtonsHeld.Super = $heldKeys.Contains([System.Windows.Forms.Keys]::I)
    return $input
}

function New-GameScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate,
        [DataManager]$DataManager,
        [int]$RoundTimeSeconds,
        [string]$PlayerCharacterId,
        [string]$CpuCharacterId
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.BackColor = [System.Drawing.Color]::FromArgb(10, 10, 16)

    $renderPanel = New-Object System.Windows.Forms.Panel
    $renderPanel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $renderPanel.BackColor = [System.Drawing.Color]::FromArgb(23, 25, 40)
    # DoubleBuffered is declared on the base Control class (protected), so
    # the PropertyInfo must be looked up via [Control] itself, not via
    # $renderPanel.GetType() (Panel) - reflection's default NonPublic
    # search does not walk up to inherited protected members otherwise.
    $doubleBufferProp = [System.Windows.Forms.Control].GetProperty("DoubleBuffered", [System.Reflection.BindingFlags]"Instance,NonPublic")
    $doubleBufferProp.SetValue($renderPanel, $true, $null)
    $panel.Controls.Add($renderPanel)

    $bs = [BattleSystem]::new()
    $bs.StartMatch(
        $DataManager.GetCharacter($PlayerCharacterId), $DataManager.GetMoveset($PlayerCharacterId),
        $DataManager.GetCharacter($CpuCharacterId), $DataManager.GetMoveset($CpuCharacterId), $RoundTimeSeconds
    )

    $state = @{
        BattleSystem = $bs
        HeldKeys = New-Object 'System.Collections.Generic.HashSet[System.Windows.Forms.Keys]'
        DebugVisible = $false
        Effects = New-Object System.Collections.ArrayList
        Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        Accumulator = 0.0
        Dt = 1.0 / 60.0
        Finished = $false
    }

    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 15

    $timer.Add_Tick({
        if ($state.Finished) { return }

        $elapsed = $state.Stopwatch.Elapsed.TotalSeconds
        $state.Stopwatch.Restart()
        if ($elapsed -gt 0.25) { $elapsed = 0.25 } # clamp huge stalls (e.g. window drag)
        $state.Accumulator += $elapsed

        $maxSteps = 6
        $steps = 0
        while ($state.Accumulator -ge $state.Dt -and $steps -lt $maxSteps) {
            $p1Input = Get-P1RawInput $state.HeldKeys
            $state.BattleSystem.Update($state.Dt, $p1Input)
            foreach ($snd in $state.BattleSystem.AllSounds) { Play-Sound $snd }
            foreach ($fx in $state.BattleSystem.AllEffects) {
                [void]$state.Effects.Add(@{ kind = $fx.kind; x = $fx.x; y = $fx.y; age = 0.0 })
            }
            $state.Accumulator -= $state.Dt
            $steps++
        }

        $survivors = New-Object System.Collections.ArrayList
        foreach ($fx in $state.Effects) {
            $fx.age += $elapsed
            $style = Get-EffectStyle $fx.kind
            if ($fx.age -lt [double]$style.duration) { [void]$survivors.Add($fx) }
        }
        $state.Effects = $survivors

        $renderPanel.Invalidate()

        if (-not $state.BattleSystem.MatchActive -and -not $state.Finished) {
            $state.Finished = $true
            $timer.Stop()
            $global:KakugeActiveGameState = $null
            $resultData = @{
                winnerIsPlayer = ($state.BattleSystem.Winner -eq $state.BattleSystem.Player1)
                isDraw = $state.BattleSystem.IsDraw
            }
            & $Navigate "Result" $resultData
        }
    }.GetNewClosure())

    $renderPanel.Tag = $state
    # GetNewClosure() here too, even though this handler reads state via
    # $sender.Tag rather than an outer variable - it still references
    # $script:ScreenW, and the .NET Add_Paint delegate path has been
    # confirmed (on a real machine) not to reliably resolve anything from
    # the defining function's scope without it. Better safe than another
    # round-trip.
    $renderPanel.Add_Paint({
        param($sender, $e)
        $st = $sender.Tag
        $g = $e.Graphics
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.Clear([System.Drawing.Color]::FromArgb(23, 25, 40))

        $groundBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(46, 41, 36))
        $g.FillRectangle($groundBrush, 0, (ConvertTo-ScreenY 0), $script:ScreenW, 40)
        $groundBrush.Dispose()

        Draw-Fighter $g $st.BattleSystem.Player1
        Draw-Fighter $g $st.BattleSystem.Player2
        foreach ($proj in $st.BattleSystem.Projectiles) { Draw-Projectile $g $proj }
        foreach ($fx in $st.Effects) { Draw-Effect $g $fx }

        Draw-HUD $g $st.BattleSystem
        if ($st.DebugVisible) { Draw-DebugOverlay $g $st.BattleSystem }
    }.GetNewClosure())

    $panel.Add_VisibleChanged({
        if ($panel.Visible) {
            $global:KakugeActiveGameState = $state
            $renderPanel.Focus()
            $timer.Start()
        } else {
            $timer.Stop()
            if ($global:KakugeActiveGameState -eq $state) { $global:KakugeActiveGameState = $null }
        }
    }.GetNewClosure())

    return $panel
}
