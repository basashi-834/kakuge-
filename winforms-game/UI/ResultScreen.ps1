# UI/ResultScreen.ps1
# Result screen (section 4): PLAYER WIN / CPU WIN / DRAW, REMATCH / TITLE.
# $ResultData is a hashtable @{ winnerIsPlayer=[bool]; isDraw=[bool] }
# passed in by GameScreen via Main.ps1's navigation dispatch.
#
# Event handlers use .GetNewClosure() - see the note at the top of
# TitleScreen.ps1 for why this is required (confirmed by a real failure:
# a plain scriptblock invoked through .NET's Add_Click delegate machinery
# does not reliably see $Navigate otherwise). Layout/palette follow the
# same style as TitleScreen.ps1 (red accent banner box, like the
# reference mockup's "YOU WIN" panel).

function New-ResultScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate,
        [hashtable]$ResultData
    )
    $pal = Get-Palette

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $panel.BackColor = $pal.Bg

    $resultText = "DRAW"
    if (-not $ResultData.isDraw) {
        if ($ResultData.winnerIsPlayer) { $resultText = "YOU WIN" } else { $resultText = "CPU WIN" }
    }

    $banner = New-Object System.Windows.Forms.Panel
    $banner.Size = New-Object System.Drawing.Size(420, 90)
    $banner.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 420) / 2)), 150)
    $banner.BackColor = $pal.Accent
    $panel.Controls.Add($banner)

    $label = New-Object System.Windows.Forms.Label
    $label.Text = $resultText
    $label.Font = New-Object System.Drawing.Font("Segoe UI", 34, [System.Drawing.FontStyle]::Bold)
    $label.ForeColor = $pal.White
    $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $label.Dock = [System.Windows.Forms.DockStyle]::Fill
    $banner.Controls.Add($label)

    function New-ResultButton([string]$text, [int]$top, [bool]$primary) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 15, [System.Drawing.FontStyle]::Bold)
        $b.Size = New-Object System.Drawing.Size(340, 60)
        $b.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 340) / 2)), $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        if ($primary) {
            $b.FlatAppearance.BorderSize = 0
            $b.BackColor = $pal.Accent
        } else {
            $b.FlatAppearance.BorderSize = 2
            $b.FlatAppearance.BorderColor = $pal.MidGray
            $b.BackColor = $pal.PanelBg
        }
        $b.ForeColor = $pal.White
        $b.UseVisualStyleBackColor = $false
        $b.TabStop = $true
        return $b
    }

    $rematchButton = New-ResultButton "REMATCH" 320 $true
    $titleButton = New-ResultButton "TITLE" 396 $false

    $rematchButton.Add_Click({ & $Navigate "Game" $null }.GetNewClosure())
    $titleButton.Add_Click({ & $Navigate "Title" $null }.GetNewClosure())

    $panel.Controls.Add($rematchButton)
    $panel.Controls.Add($titleButton)

    $panel.Tag = @{
        Activate = { $rematchButton.Focus() }.GetNewClosure()
    }

    return $panel
}
