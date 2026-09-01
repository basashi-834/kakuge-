# UI/TitleScreen.ps1
# Title screen (section 4): GAME START / CHARACTER EDIT / EXIT.
# $Navigate is a scriptblock: { param($screen, $data) ... } provided by
# Main.ps1 to switch screens - kept as an explicit parameter (rather than
# an ambient closure) so this file has no hidden dependency on how Main.ps1
# is structured.

function New-TitleScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.BackColor = [System.Drawing.Color]::FromArgb(14, 14, 22)

    $title = New-Object System.Windows.Forms.Label
    $title.Text = "KAKUGE`n2D FIGHTING GAME"
    $title.Font = New-Object System.Drawing.Font("Segoe UI", 30, [System.Drawing.FontStyle]::Bold)
    $title.ForeColor = [System.Drawing.Color]::White
    $title.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $title.Dock = [System.Windows.Forms.DockStyle]::Top
    $title.Height = 160
    $panel.Controls.Add($title)

    $buttonPanel = New-Object System.Windows.Forms.Panel
    $buttonPanel.Dock = [System.Windows.Forms.DockStyle]::Fill
    $panel.Controls.Add($buttonPanel)

    function New-MenuButton([string]$text, [int]$top) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 14)
        $b.Size = New-Object System.Drawing.Size(300, 56)
        $b.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 300) / 2)), $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.BackColor = [System.Drawing.Color]::FromArgb(40, 40, 60)
        $b.ForeColor = [System.Drawing.Color]::White
        return $b
    }

    $startButton = New-MenuButton "GAME START" 60
    $editButton  = New-MenuButton "CHARACTER EDIT" 140
    $exitButton  = New-MenuButton "EXIT" 220

    $startButton.Add_Click({ & $Navigate "Game" $null })
    $editButton.Add_Click({ & $Navigate "Editor" $null })
    $exitButton.Add_Click({ [System.Windows.Forms.Application]::Exit() })

    $buttonPanel.Controls.Add($startButton)
    $buttonPanel.Controls.Add($editButton)
    $buttonPanel.Controls.Add($exitButton)

    $panel.Add_VisibleChanged({ if ($panel.Visible) { $startButton.Focus() } })
    return $panel
}
