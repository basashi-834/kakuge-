# CharacterEditor/CharacterEditorScreen.ps1
# In-game Character Editor (section 4/31). Builds its form controls in
# code (no separate designer file) so adding a new editable field is one
# call to New-FormRow. Edits are held in the loaded MoveData/CharacterStats
# objects until SAVE, then written via DataManager to the per-user JSON
# folder so they survive a restart (section 4: "編集内容は保存できるように").

function New-FormRow {
    param(
        [System.Windows.Forms.Control]$Parent,
        [int]$Y,
        [string]$LabelText,
        [System.Windows.Forms.Control]$Control,
        [int]$LabelWidth = 190,
        [int]$ControlX = 200,
        [int]$ControlWidth = 190,
        [int]$LabelX = 4
    )
    # BUG FIX: LabelX used to be hardcoded to 4 for every call, so the
    # stats column's labels and the move column's labels (a separate call
    # site further right) both landed in the exact same x position and
    # overlapped each other - only the non-overlapping tail of whichever
    # list was longer stayed readable. Callers now pass their own LabelX.
    $label = New-Object System.Windows.Forms.Label
    $label.Text = $LabelText
    $label.Location = New-Object System.Drawing.Point($LabelX, ($Y + 3))
    $label.Size = New-Object System.Drawing.Size($LabelWidth, 22)
    $label.ForeColor = [System.Drawing.Color]::White
    $Parent.Controls.Add($label)

    $Control.Location = New-Object System.Drawing.Point($ControlX, $Y)
    $Control.Width = $ControlWidth
    $Parent.Controls.Add($Control)
    return ($Y + 28)
}

function New-NumUpDown([double]$min, [double]$max, [double]$value, [int]$decimals) {
    $n = New-Object System.Windows.Forms.NumericUpDown
    $n.Minimum = [decimal]$min
    $n.Maximum = [decimal]$max
    $n.DecimalPlaces = $decimals
    $n.Value = [Math]::Max($min, [Math]::Min($max, $value))
    return $n
}

# NumericUpDown.Value THROWS if assigned outside [Minimum,Maximum] - hand-
# edited JSON could contain out-of-range numbers, so every load-time
# assignment goes through this clamp instead of setting .Value directly.
function Set-NumSafe([System.Windows.Forms.NumericUpDown]$control, [double]$value) {
    $clamped = [Math]::Max([double]$control.Minimum, [Math]::Min([double]$control.Maximum, $value))
    $control.Value = [decimal]$clamped
}

function New-CharacterEditorScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate,
        [DataManager]$DataManager
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.BackColor = [System.Drawing.Color]::FromArgb(18, 18, 28)

    # ---- Top bar -------------------------------------------------------
    # NOTE: buttons here explicitly set FlatStyle/BackColor/ForeColor - the
    # default "System" button style barely shows custom colors against a
    # dark background on some Windows themes, which is why button text was
    # unreadable before this fix.
    $topBar = New-Object System.Windows.Forms.Panel
    $topBar.Dock = [System.Windows.Forms.DockStyle]::Top
    $topBar.Height = 46
    $topBar.BackColor = [System.Drawing.Color]::FromArgb(24, 24, 36)
    $panel.Controls.Add($topBar)

    $charCombo = New-Object System.Windows.Forms.ComboBox
    $charCombo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
    $charCombo.Location = New-Object System.Drawing.Point(8, 10)
    $charCombo.Width = 150
    $charCombo.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 55)
    $charCombo.ForeColor = [System.Drawing.Color]::White
    $topBar.Controls.Add($charCombo)

    $moveCombo = New-Object System.Windows.Forms.ComboBox
    $moveCombo.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
    $moveCombo.Location = New-Object System.Drawing.Point(166, 10)
    $moveCombo.Width = 200
    $moveCombo.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 55)
    $moveCombo.ForeColor = [System.Drawing.Color]::White
    $topBar.Controls.Add($moveCombo)

    function New-BarButton([string]$text, [int]$x, [int]$width) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Location = New-Object System.Drawing.Point($x, 8)
        $b.Size = New-Object System.Drawing.Size($width, 30)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.BackColor = [System.Drawing.Color]::FromArgb(50, 50, 70)
        $b.ForeColor = [System.Drawing.Color]::White
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
        return $b
    }

    $newMoveButton = New-BarButton "New Move" 374 90
    $saveButton = New-BarButton "SAVE" 470 80
    $backButton = New-BarButton "TITLE" 556 80
    $topBar.Controls.Add($newMoveButton)
    $topBar.Controls.Add($saveButton)
    $topBar.Controls.Add($backButton)

    $statusLabel = New-Object System.Windows.Forms.Label
    $statusLabel.Location = New-Object System.Drawing.Point(646, 14)
    $statusLabel.Width = 600
    $statusLabel.ForeColor = [System.Drawing.Color]::LightGreen
    $topBar.Controls.Add($statusLabel)

    # ---- Scrollable body: two columns ----------------------------------
    $scroll = New-Object System.Windows.Forms.Panel
    $scroll.Dock = [System.Windows.Forms.DockStyle]::Fill
    $scroll.AutoScroll = $true
    $panel.Controls.Add($scroll)

    $statsHeader = New-Object System.Windows.Forms.Label
    $statsHeader.Text = "基本性能 (Base Stats)"
    $statsHeader.Font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold)
    $statsHeader.ForeColor = [System.Drawing.Color]::White
    $statsHeader.Location = New-Object System.Drawing.Point(16, 10)
    $statsHeader.Width = 380
    $scroll.Controls.Add($statsHeader)

    $moveHeader = New-Object System.Windows.Forms.Label
    $moveHeader.Text = "技データ (Move Data)"
    $moveHeader.Font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold)
    $moveHeader.ForeColor = [System.Drawing.Color]::White
    $moveHeader.Location = New-Object System.Drawing.Point(440, 10)
    $moveHeader.Width = 380
    $scroll.Controls.Add($moveHeader)

    # -- Stats fields
    $sf = @{}
    $y = 44
    $sf.Name = New-Object System.Windows.Forms.TextBox
    $y = New-FormRow $scroll $y "キャラクター名" $sf.Name 190 200 220
    $sf.MaxHP = New-NumUpDown 1 99999 1000 0
    $y = New-FormRow $scroll $y "最大HP" $sf.MaxHP
    $sf.WalkForwardSpeed = New-NumUpDown 0 3000 220 1
    $y = New-FormRow $scroll $y "前進速度" $sf.WalkForwardSpeed
    $sf.WalkBackwardSpeed = New-NumUpDown 0 3000 170 1
    $y = New-FormRow $scroll $y "後退速度" $sf.WalkBackwardSpeed
    $sf.DashSpeed = New-NumUpDown 0 4000 420 1
    $y = New-FormRow $scroll $y "ダッシュ速度" $sf.DashSpeed
    $sf.JumpVelocity = New-NumUpDown -4000 0 -900 1
    $y = New-FormRow $scroll $y "ジャンプ力(負の値)" $sf.JumpVelocity
    $sf.Gravity = New-NumUpDown 0 8000 2400 1
    $y = New-FormRow $scroll $y "重力" $sf.Gravity
    foreach ($ctrl in $sf.Values) { $ctrl.BackColor = [System.Drawing.Color]::FromArgb(40,40,55); $ctrl.ForeColor = [System.Drawing.Color]::White }

    # -- Move fields
    $mf = @{}
    $my = 44
    $moveColX = 440
    $moveCtrlX = 640
    $mf.Name = New-Object System.Windows.Forms.TextBox
    $my = New-FormRow $scroll $my "技名" $mf.Name 190 $moveCtrlX 200 $moveColX
    $mf.Startup = New-NumUpDown 1 999 4 0
    $my = New-FormRow $scroll $my "発生フレーム" $mf.Startup 190 $moveCtrlX 190 $moveColX
    $mf.Active = New-NumUpDown 1 999 3 0
    $my = New-FormRow $scroll $my "持続フレーム" $mf.Active 190 $moveCtrlX 190 $moveColX
    $mf.Recovery = New-NumUpDown 0 999 7 0
    $my = New-FormRow $scroll $my "硬直フレーム" $mf.Recovery 190 $moveCtrlX 190 $moveColX
    $mf.Damage = New-NumUpDown 0 9999 30 0
    $my = New-FormRow $scroll $my "ダメージ" $mf.Damage 190 $moveCtrlX 190 $moveColX
    $mf.Hitstun = New-NumUpDown 0 999 12 0
    $my = New-FormRow $scroll $my "ヒット硬直" $mf.Hitstun 190 $moveCtrlX 190 $moveColX
    $mf.Blockstun = New-NumUpDown 0 999 8 0
    $my = New-FormRow $scroll $my "ガード硬直" $mf.Blockstun 190 $moveCtrlX 190 $moveColX
    $mf.Hitstop = New-NumUpDown 0 60 4 0
    $my = New-FormRow $scroll $my "ヒットストップ" $mf.Hitstop 190 $moveCtrlX 190 $moveColX
    $mf.KnockbackX = New-NumUpDown -3000 3000 120 1
    $my = New-FormRow $scroll $my "ノックバックX" $mf.KnockbackX 190 $moveCtrlX 190 $moveColX
    $mf.KnockbackY = New-NumUpDown -3000 3000 0 1
    $my = New-FormRow $scroll $my "ノックバックY" $mf.KnockbackY 190 $moveCtrlX 190 $moveColX
    $mf.OffsetX = New-NumUpDown -500 500 35 1
    $my = New-FormRow $scroll $my "Hitbox offsetX" $mf.OffsetX 190 $moveCtrlX 190 $moveColX
    $mf.OffsetY = New-NumUpDown -500 500 -70 1
    $my = New-FormRow $scroll $my "Hitbox offsetY" $mf.OffsetY 190 $moveCtrlX 190 $moveColX
    $mf.Width = New-NumUpDown 1 500 36 1
    $my = New-FormRow $scroll $my "Hitbox width" $mf.Width 190 $moveCtrlX 190 $moveColX
    $mf.Height = New-NumUpDown 1 500 22 1
    $my = New-FormRow $scroll $my "Hitbox height" $mf.Height 190 $moveCtrlX 190 $moveColX

    $mf.GuardType = New-Object System.Windows.Forms.ComboBox
    $mf.GuardType.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
    [void]$mf.GuardType.Items.AddRange(@([Constants]::GuardHigh, [Constants]::GuardLow, [Constants]::GuardOverhead, [Constants]::GuardThrow))
    $my = New-FormRow $scroll $my "ガード属性" $mf.GuardType 190 $moveCtrlX 190 $moveColX

    $mf.Cancelable = New-Object System.Windows.Forms.CheckBox
    $my = New-FormRow $scroll $my "キャンセル可否" $mf.Cancelable 190 $moveCtrlX 190 $moveColX

    $mf.CancelRoutes = New-Object System.Windows.Forms.TextBox
    $my = New-FormRow $scroll $my "キャンセル先(カンマ区切り)" $mf.CancelRoutes 190 $moveCtrlX 190 $moveColX

    foreach ($ctrl in $mf.Values) {
        if ($ctrl -isnot [System.Windows.Forms.CheckBox]) {
            $ctrl.BackColor = [System.Drawing.Color]::FromArgb(40,40,55)
            $ctrl.ForeColor = [System.Drawing.Color]::White
        }
    }

    $advantageLabel = New-Object System.Windows.Forms.Label
    $advantageLabel.Location = New-Object System.Drawing.Point($moveColX, ($my + 8))
    $advantageLabel.Width = 380
    $advantageLabel.Height = 22
    $advantageLabel.ForeColor = [System.Drawing.Color]::Yellow
    $scroll.Controls.Add($advantageLabel)

    # ---------------------------------------------------------------
    # State + wiring
    # ---------------------------------------------------------------
    $ctx = @{
        CurrentCharId = ""
        CurrentMoveId = ""
        CurrentMoveset = @{}
    }

    # NOTE: these are SCRIPTBLOCK VARIABLES, not `function` definitions.
    # PowerShell nested functions only exist for the lifetime of the
    # enclosing function call - once New-CharacterEditorScreen returns,
    # a later event (a button click, a ValueChanged) can no longer see a
    # nested `function Foo {}` at all (confirmed empirically: it fails
    # with "term not recognized"), even from a scriptblock that appears
    # to be defined right next to it. A scriptblock stored in a variable
    # does not have that problem - GetNewClosure() correctly snapshots it
    # like any other captured variable, so it's called as `& $FillMoveForm`.

    $UpdateAdvantagePreview = {
        $startup = [int]$mf.Startup.Value
        $active = [int]$mf.Active.Value
        $recovery = [int]$mf.Recovery.Value
        $hitstun = [int]$mf.Hitstun.Value
        $blockstun = [int]$mf.Blockstun.Value
        $onHit = $hitstun - $recovery
        $onBlock = $blockstun - $recovery
        $advantageLabel.Text = "On Hit: {0:+#;-#;0}   On Block: {1:+#;-#;0}   TotalFrame: {2}" -f $onHit, $onBlock, ($startup + $active + $recovery)
    }.GetNewClosure()

    $FillStatsForm = {
        param($stats)
        $sf.Name.Text = $stats.Name
        Set-NumSafe $sf.MaxHP $stats.MaxHP
        Set-NumSafe $sf.WalkForwardSpeed $stats.WalkForwardSpeed
        Set-NumSafe $sf.WalkBackwardSpeed $stats.WalkBackwardSpeed
        Set-NumSafe $sf.DashSpeed $stats.DashSpeed
        Set-NumSafe $sf.JumpVelocity $stats.JumpVelocity
        Set-NumSafe $sf.Gravity $stats.Gravity
    }.GetNewClosure()

    $FillMoveForm = {
        param($move)
        $mf.Name.Text = $move.Name
        Set-NumSafe $mf.Startup $move.Startup
        Set-NumSafe $mf.Active $move.Active
        Set-NumSafe $mf.Recovery $move.Recovery
        Set-NumSafe $mf.Damage $move.Damage
        Set-NumSafe $mf.Hitstun $move.Hitstun
        Set-NumSafe $mf.Blockstun $move.Blockstun
        Set-NumSafe $mf.Hitstop $move.Hitstop
        Set-NumSafe $mf.KnockbackX $move.KnockbackX
        Set-NumSafe $mf.KnockbackY $move.KnockbackY
        $box = @{ offsetX = 0.0; offsetY = 0.0; width = 40.0; height = 40.0 }
        if ($move.Hitboxes.Count -gt 0) { $box = $move.Hitboxes[0] }
        Set-NumSafe $mf.OffsetX $box.offsetX
        Set-NumSafe $mf.OffsetY $box.offsetY
        Set-NumSafe $mf.Width $box.width
        Set-NumSafe $mf.Height $box.height
        $mf.GuardType.SelectedItem = $move.GuardType
        $mf.Cancelable.Checked = ($move.CancelEndFrame -gt $move.CancelStartFrame)
        $mf.CancelRoutes.Text = ($move.CancelRoutes -join ",")
        & $UpdateAdvantagePreview
    }.GetNewClosure()

    $ApplyStatsForm = {
        param($stats)
        $stats.Name = $sf.Name.Text
        $stats.MaxHP = [int]$sf.MaxHP.Value
        $stats.WalkForwardSpeed = [double]$sf.WalkForwardSpeed.Value
        $stats.WalkBackwardSpeed = [double]$sf.WalkBackwardSpeed.Value
        $stats.DashSpeed = [double]$sf.DashSpeed.Value
        $stats.JumpVelocity = [double]$sf.JumpVelocity.Value
        $stats.Gravity = [double]$sf.Gravity.Value
    }.GetNewClosure()

    $ApplyMoveForm = {
        param($move)
        $move.Name = $mf.Name.Text
        $move.Startup = [int]$mf.Startup.Value
        $move.Active = [int]$mf.Active.Value
        $move.Recovery = [int]$mf.Recovery.Value
        $move.TotalFrame = $move.Startup + $move.Active + $move.Recovery
        $move.Damage = [int]$mf.Damage.Value
        $move.Hitstun = [int]$mf.Hitstun.Value
        $move.Blockstun = [int]$mf.Blockstun.Value
        $move.Hitstop = [int]$mf.Hitstop.Value
        $move.KnockbackX = [double]$mf.KnockbackX.Value
        $move.KnockbackY = [double]$mf.KnockbackY.Value
        $move.Hitboxes = [System.Collections.ArrayList]::new()
        [void]$move.Hitboxes.Add(@{ offsetX = [double]$mf.OffsetX.Value; offsetY = [double]$mf.OffsetY.Value; width = [double]$mf.Width.Value; height = [double]$mf.Height.Value })
        if ($null -ne $mf.GuardType.SelectedItem) { $move.GuardType = [string]$mf.GuardType.SelectedItem }
        if ($mf.Cancelable.Checked) {
            $move.CancelStartFrame = $move.Startup
            $move.CancelEndFrame = $move.Startup + $move.Active + $move.Recovery
        } else {
            $move.CancelStartFrame = 0
            $move.CancelEndFrame = 0
        }
        $routesText = $mf.CancelRoutes.Text
        $move.CancelRoutes = [System.Collections.ArrayList]::new()
        if (-not [string]::IsNullOrWhiteSpace($routesText)) {
            foreach ($r in ($routesText -split ",")) {
                $trimmed = $r.Trim()
                if ($trimmed -ne "") { [void]$move.CancelRoutes.Add($trimmed) }
            }
        }
    }.GetNewClosure()

    $PopulateMoveList = {
        $moveCombo.Items.Clear()
        $ids = @($ctx.CurrentMoveset.Keys) | Sort-Object
        foreach ($id in $ids) { [void]$moveCombo.Items.Add($id) }
        if ($ids.Count -gt 0) { $moveCombo.SelectedIndex = 0 }
    }.GetNewClosure()

    $LoadCharacter = {
        param([string]$charId)
        $ctx.CurrentCharId = $charId
        $stats = $DataManager.GetCharacter($charId)
        $ctx.CurrentMoveset = $DataManager.GetMoveset($charId)
        & $FillStatsForm $stats
        & $PopulateMoveList
    }.GetNewClosure()

    $charCombo.Add_SelectedIndexChanged({
        if ($null -ne $charCombo.SelectedItem) { & $LoadCharacter ([string]$charCombo.SelectedItem) }
    }.GetNewClosure())

    $moveCombo.Add_SelectedIndexChanged({
        if ($null -ne $moveCombo.SelectedItem) {
            $ctx.CurrentMoveId = [string]$moveCombo.SelectedItem
            & $FillMoveForm $ctx.CurrentMoveset[$ctx.CurrentMoveId]
        }
    }.GetNewClosure())

    foreach ($ctrl in @($mf.Startup, $mf.Active, $mf.Recovery, $mf.Hitstun, $mf.Blockstun)) {
        $ctrl.Add_ValueChanged({ & $UpdateAdvantagePreview }.GetNewClosure())
    }

    $newMoveButton.Add_Click({
        if ($ctx.CurrentCharId -eq "") { return }
        $baseId = "new_move"
        $newId = $baseId
        $n = 1
        while ($ctx.CurrentMoveset.ContainsKey($newId)) { $newId = "${baseId}_$n"; $n++ }
        $m = [MoveData]::new()
        $m.Id = $newId
        $m.Name = "New Move"
        [void]$m.Tags.Add([Constants]::TagNormal)
        [void]$m.Tags.Add([Constants]::TagLight)
        $m.Button = "Light"
        $m.Stance = "stand"
        $m.GuardType = [Constants]::GuardHigh
        $ctx.CurrentMoveset[$newId] = $m
        & $PopulateMoveList
        $moveCombo.SelectedItem = $newId
        $statusLabel.Text = "新しい技 '$newId' を追加しました (SAVEで保存)"
    }.GetNewClosure())

    $saveButton.Add_Click({
        if ($ctx.CurrentCharId -eq "") { return }
        $stats = $DataManager.GetCharacter($ctx.CurrentCharId)
        & $ApplyStatsForm $stats
        $DataManager.SaveCharacter($stats)
        if ($ctx.CurrentMoveId -ne "" -and $ctx.CurrentMoveset.ContainsKey($ctx.CurrentMoveId)) {
            $move = $ctx.CurrentMoveset[$ctx.CurrentMoveId]
            & $ApplyMoveForm $move
            $DataManager.SaveMove($ctx.CurrentCharId, $move)
        }
        $statusLabel.Text = "保存しました: " + (Get-Date -Format "HH:mm:ss")
    }.GetNewClosure())

    $backButton.Add_Click({ & $Navigate "Title" $null }.GetNewClosure())

    # NOTE: initialization used to run on Add_VisibleChanged, but that
    # event is not guaranteed to fire just from Controls.Add() (Visible
    # already defaults to true, so nothing may actually "change"). Show-
    # Screen in Main.ps1 now calls this Activate scriptblock explicitly,
    # right after adding the panel to the form - guaranteed to run exactly
    # once per screen switch.
    $panel.Tag = @{
        Activate = {
            $charCombo.Items.Clear()
            $ids = @($DataManager.GetCharacterIds()) | Sort-Object
            foreach ($id in $ids) { [void]$charCombo.Items.Add($id) }
            if ($ids.Count -gt 0) { $charCombo.SelectedIndex = 0 }
        }.GetNewClosure()
    }

    return $panel
}
