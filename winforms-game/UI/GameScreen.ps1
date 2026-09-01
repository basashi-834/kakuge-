# UI/GameScreen.ps1
# The battle screen: owns the render Panel, the fixed-timestep game loop
# Timer, and keyboard input capture. Combat logic itself lives entirely in
# BattleSystem/Fighter/CPUAI - this file only reads their PUBLIC state to
# draw and only writes raw input hashtables into Fighter.FrameStep(),
# never touching combat rules directly (section 38: UIと戦闘処理の分離).
#
# Lifecycle note: starting the Timer / registering this screen as the
# keyboard owner used to happen on Add_VisibleChanged, which is NOT
# guaranteed to fire just from Controls.Add() (a Control's Visible
# property already defaults to true, so nothing may actually "change").
# That was the root cause of "the character doesn't respond to input at
# all" on a real machine - the timer never started. Activation/
# deactivation is now done explicitly by Show-Screen (Main.ps1), via the
# scriptblocks stored on $panel.Tag, guaranteed to run exactly once per
# screen switch.
#
# Cross-screen key routing: PowerShell's `$script:` scope is tied to
# whichever .ps1 file a function was DEFINED in, which is NOT the same
# script-scope as Main.ps1 even though everything is dot-sourced together.
# To avoid that ambiguity, the "which screen currently owns the keyboard"
# flag is deliberately kept as $global:KakugeActiveGameState (set here,
# read by Main.ps1's single pair of Form-level KeyDown/KeyUp handlers,
# which also call $global:KakugeActiveGameState.TogglePause on Escape).

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
    $panel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $panel.BackColor = [System.Drawing.Color]::FromArgb(10, 10, 16)

    $renderPanel = New-Object System.Windows.Forms.Panel
    $renderPanel.Location = New-Object System.Drawing.Point(0, 0)
    $renderPanel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $renderPanel.BackColor = [System.Drawing.Color]::FromArgb(23, 25, 40)
    # DoubleBuffered is declared on the base Control class (protected), so
    # the PropertyInfo must be looked up via [Control] itself, not via
    # $renderPanel.GetType() (Panel) - reflection's default NonPublic
    # search does not walk up to inherited protected members otherwise.
    $doubleBufferProp = [System.Windows.Forms.Control].GetProperty("DoubleBuffered", [System.Reflection.BindingFlags]"Instance,NonPublic")
    $doubleBufferProp.SetValue($renderPanel, $true, $null)
    $panel.Controls.Add($renderPanel)

    # ---- Pause overlay (section: "戻れるようにポーズメニューを追加") ------
    $pausePanel = New-Object System.Windows.Forms.Panel
    $pausePanel.Size = New-Object System.Drawing.Size(420, 260)
    $pausePanel.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 420)/2)), ([int](($script:ScreenH - 260)/2)))
    $pausePanel.BackColor = [System.Drawing.Color]::FromArgb(20, 20, 30)
    $pausePanel.Visible = $false
    $pausePanel.BorderStyle = [System.Windows.Forms.BorderStyle]::FixedSingle

    $pauseLabel = New-Object System.Windows.Forms.Label
    $pauseLabel.Text = "PAUSED"
    $pauseLabel.Font = New-Object System.Drawing.Font("Segoe UI", 22, [System.Drawing.FontStyle]::Bold)
    $pauseLabel.ForeColor = [System.Drawing.Color]::White
    $pauseLabel.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $pauseLabel.Location = New-Object System.Drawing.Point(0, 20)
    $pauseLabel.Size = New-Object System.Drawing.Size(420, 50)
    $pausePanel.Controls.Add($pauseLabel)

    function New-PauseButton([string]$text, [int]$top) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold)
        $b.Size = New-Object System.Drawing.Size(300, 50)
        $b.Location = New-Object System.Drawing.Point(60, $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.FlatAppearance.BorderSize = 2
        $b.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(120, 120, 160)
        $b.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 70)
        $b.ForeColor = [System.Drawing.Color]::White
        $b.UseVisualStyleBackColor = $false
        return $b
    }
    $resumeButton = New-PauseButton "RESUME (Esc)" 90
    $pauseTitleButton = New-PauseButton "GIVE UP -> TITLE" 160
    $pausePanel.Controls.Add($resumeButton)
    $pausePanel.Controls.Add($pauseTitleButton)
    $panel.Controls.Add($pausePanel)
    $pausePanel.BringToFront()

    $bs = [BattleSystem]::new()
    $bs.StartMatch(
        $DataManager.GetCharacter($PlayerCharacterId), $DataManager.GetMoveset($PlayerCharacterId),
        $DataManager.GetCharacter($CpuCharacterId), $DataManager.GetMoveset($CpuCharacterId), $RoundTimeSeconds
    )

    $state = @{
        BattleSystem = $bs
        HeldKeys = New-Object 'System.Collections.Generic.HashSet[System.Windows.Forms.Keys]'
        DebugVisible = $false
        Paused = $false
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

        if (-not $state.Paused) {
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
        }

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

    $togglePause = {
        $state.Paused = -not $state.Paused
        $pausePanel.Visible = $state.Paused
        if ($state.Paused) {
            $state.HeldKeys.Clear() # don't keep moving once resumed from a stuck key
            $pausePanel.BringToFront()
            $resumeButton.Focus()
        } else {
            $renderPanel.Focus()
        }
    }.GetNewClosure()
    $state.TogglePause = $togglePause

    $resumeButton.Add_Click({ & $togglePause }.GetNewClosure())
    $pauseTitleButton.Add_Click({
        $timer.Stop()
        $global:KakugeActiveGameState = $null
        & $Navigate "Title" $null
    }.GetNewClosure())

    $renderPanel.Tag = $state
    # GetNewClosure() here too, even though this handler reads state via
    # $sender.Tag rather than an outer variable - it still references
    # $script:ScreenW, and the .NET Add_Paint delegate path has been
    # confirmed (on a real machine) not to reliably resolve anything from
    # the defining function's scope without it.
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

    $panel.Tag = @{
        Activate = {
            $global:KakugeActiveGameState = $state
            $state.Stopwatch.Restart()
            $renderPanel.Focus()
            $timer.Start()
        }.GetNewClosure()
        Deactivate = {
            $timer.Stop()
            if ($global:KakugeActiveGameState -eq $state) { $global:KakugeActiveGameState = $null }
        }.GetNewClosure()
    }

    return $panel
}
