# UI/ResultScreen.ps1
# Result screen (section 4): PLAYER WIN / CPU WIN / DRAW, REMATCH / TITLE.
# $ResultData is a hashtable @{ winnerIsPlayer=[bool]; isDraw=[bool] }
# passed in by GameScreen via Main.ps1's navigation dispatch.

function New-ResultScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate,
        [hashtable]$ResultData
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.BackColor = [System.Drawing.Color]::FromArgb(14, 14, 22)

    $resultText = "DRAW"
    if (-not $ResultData.isDraw) {
        if ($ResultData.winnerIsPlayer) { $resultText = "PLAYER WIN" } else { $resultText = "CPU WIN" }
    }

    $label = New-Object System.Windows.Forms.Label
    $label.Text = $resultText
    $label.Font = New-Object System.Drawing.Font("Segoe UI", 40, [System.Drawing.FontStyle]::Bold)
    $label.ForeColor = [System.Drawing.Color]::White
    $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $label.Dock = [System.Windows.Forms.DockStyle]::Top
    $label.Height = 220
    $panel.Controls.Add($label)

    $buttonPanel = New-Object System.Windows.Forms.Panel
    $buttonPanel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.Controls.Add($buttonPanel)

    $rematchButton = New-Object System.Windows.Forms.Button
    $rematchButton.Text = "REMATCH"
    $rematchButton.Font = New-Object System.Drawing.Font("Segoe UI", 14)
    $rematchButton.Size = New-Object System.Drawing.Size(300, 56)
    $rematchButton.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 300) / 2)), 40)
    $rematchButton.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $rematchButton.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 60)
    $rematchButton.ForeColor = [System.Drawing.Color]::White

    $titleButton = New-Object System.Windows.Forms.Button
    $titleButton.Text = "TITLE"
    $titleButton.Font = New-Object System.Drawing.Font("Segoe UI", 14)
    $titleButton.Size = New-Object System.Drawing.Size(300, 56)
    $titleButton.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 300) / 2)), 120)
    $titleButton.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
    $titleButton.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 60)
    $titleButton.ForeColor = [System.Drawing.Color]::White

    $rematchButton.Add_Click({ & $Navigate "Game" $null })
    $titleButton.Add_Click({ & $Navigate "Title" $null })

    $buttonPanel.Controls.Add($rematchButton)
    $buttonPanel.Controls.Add($titleButton)

    $panel.Add_VisibleChanged({ if ($panel.Visible) { $rematchButton.Focus() } })
    return $panel
}
