# UI/TitleScreen.ps1
# Title screen (section 4): GAME START / CHARACTER EDIT / EXIT.
# $Navigate is a scriptblock: { param($screen, $data) ... } provided by
# Main.ps1 to switch screens - kept as an explicit parameter (rather than
# an ambient closure) so this file has no hidden dependency on how Main.ps1
# is structured.
#
# IMPORTANT: every WinForms event handler here uses .GetNewClosure().
# Confirmed by an actual failure on a real Windows machine: a plain `{...}`
# scriptblock attached via .Add_Click()/.Add_VisibleChanged() is invoked
# through .NET's delegate machinery (ScriptBlock.InvokeAsDelegateHelper),
# not through a normal PowerShell call - and that path does NOT reliably
# resolve a parent function's local variables (like $Navigate) unless the
# scriptblock was created with GetNewClosure(). Without it, `& $Navigate`
# inside the handler fails with "the expression after '&' created an
# invalid object" the first time the button is actually clicked.
#
# Buttons are placed directly on the root panel (no intermediate Dock=Fill
# sub-panel) using absolute coordinates against $script:ScreenW/H, which
# are fixed constants matching the Form's ClientSize set before any screen
# is ever shown - this avoids depending on a sub-panel's own size being
# resolved by layout timing.

function New-TitleScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate
    )

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $panel.BackColor = [System.Drawing.Color]::FromArgb(14, 14, 22)

    $title = New-Object System.Windows.Forms.Label
    $title.Text = "KAKUGE"
    $title.Font = New-Object System.Drawing.Font("Segoe UI", 40, [System.Drawing.FontStyle]::Bold)
    $title.ForeColor = [System.Drawing.Color]::White
    $title.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $title.Location = New-Object System.Drawing.Point(0, 70)
    $title.Size = New-Object System.Drawing.Size($script:ScreenW, 70)
    $panel.Controls.Add($title)

    $subtitle = New-Object System.Windows.Forms.Label
    $subtitle.Text = "2D FIGHTING GAME"
    $subtitle.Font = New-Object System.Drawing.Font("Segoe UI", 20, [System.Drawing.FontStyle]::Bold)
    $subtitle.ForeColor = [System.Drawing.Color]::White
    $subtitle.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $subtitle.Location = New-Object System.Drawing.Point(0, 150)
    $subtitle.Size = New-Object System.Drawing.Size($script:ScreenW, 50)
    $panel.Controls.Add($subtitle)

    function New-MenuButton([string]$text, [int]$top) {
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

    $startButton = New-MenuButton "GAME START" 280
    $editButton  = New-MenuButton "CHARACTER EDIT" 370
    $exitButton  = New-MenuButton "EXIT" 460

    $startButton.Add_Click({ & $Navigate "Game" $null }.GetNewClosure())
    $editButton.Add_Click({ & $Navigate "Editor" $null }.GetNewClosure())
    $exitButton.Add_Click({ [System.Windows.Forms.Application]::Exit() }.GetNewClosure())

    $panel.Controls.Add($startButton)
    $panel.Controls.Add($editButton)
    $panel.Controls.Add($exitButton)

    # See the note at the top of GameScreen.ps1 / Main.ps1's Show-Screen:
    # Activate is called explicitly by Show-Screen right after this panel
    # is added to the form, rather than relying on Add_VisibleChanged.
    $panel.Tag = @{
        Activate = { $startButton.Focus() }.GetNewClosure()
    }

    return $panel
}
