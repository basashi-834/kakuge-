# UI/ResultScreen.ps1
# Result screen (section 4): PLAYER WIN / CPU WIN / DRAW, REMATCH / TITLE.
# $ResultData is a hashtable @{ winnerIsPlayer=[bool]; isDraw=[bool] }
# passed in by GameScreen via Main.ps1's navigation dispatch.
#
# Event handlers use .GetNewClosure() - see the note at the top of
# TitleScreen.ps1 for why this is required (confirmed by a real failure:
# a plain scriptblock invoked through .NET's Add_Click delegate machinery
# does not reliably see $Navigate otherwise). Layout follows the same
# "no nested Dock=Fill sub-panel" approach as TitleScreen.ps1.

function New-ResultScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate,
        [hashtable]$ResultData
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $panel.BackColor = [System.Drawing.Color]::FromArgb(14, 14, 22)

    $resultText = "DRAW"
    if (-not $ResultData.isDraw) {
        if ($ResultData.winnerIsPlayer) { $resultText = "PLAYER WIN" } else { $resultText = "CPU WIN" }
    }

    $label = New-Object System.Windows.Forms.Label
    $label.Text = $resultText
    $label.Font = New-Object System.Drawing.Font("Segoe UI", 44, [System.Drawing.FontStyle]::Bold)
    $label.ForeColor = [System.Drawing.Color]::White
    $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $label.Location = New-Object System.Drawing.Point(0, 160)
    $label.Size = New-Object System.Drawing.Size($script:ScreenW, 100)
    $panel.Controls.Add($label)

    function New-ResultButton([string]$text, [int]$top) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
        $b.Size = New-Object System.Drawing.Size(340, 64)
        $b.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 340) / 2)), $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.FlatAppearance.BorderSize = 2
        $b.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(120, 120, 160)
        $b.BackColor = [System.Drawing.Color]::FromArgb(45, 45, 70)
        $b.ForeColor = [System.Drawing.Color]::White
        $b.UseVisualStyleBackColor = $false
        $b.TabStop = $true
        return $b
    }

    $rematchButton = New-ResultButton "REMATCH" 340
    $titleButton = New-ResultButton "TITLE" 430

    $rematchButton.Add_Click({ & $Navigate "Game" $null }.GetNewClosure())
    $titleButton.Add_Click({ & $Navigate "Title" $null }.GetNewClosure())

    $panel.Controls.Add($rematchButton)
    $panel.Controls.Add($titleButton)

    $panel.Tag = @{
        Activate = { $rematchButton.Focus() }.GetNewClosure()
    }

    return $panel
}
