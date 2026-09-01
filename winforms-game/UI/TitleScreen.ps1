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
#
# Palette/typography follow the reference mockup the user shared: bold
# condensed title, a red accent rule, one solid red primary action and
# outlined secondary actions.

function New-TitleScreen {
    param(
        [System.Windows.Forms.Form]$MainForm,
        [scriptblock]$Navigate
    )
    $pal = Get-Palette

    $panel = New-Object System.Windows.Forms.Panel
    $panel.Size = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $panel.BackColor = $pal.Bg

    $eyebrow = New-Object System.Windows.Forms.Label
    $eyebrow.Text = "2D FIGHTING GAME"
    $eyebrow.Font = New-Object System.Drawing.Font("Segoe UI", 12, [System.Drawing.FontStyle]::Bold)
    $eyebrow.ForeColor = $pal.Accent
    $eyebrow.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $eyebrow.Location = New-Object System.Drawing.Point(0, 96)
    $eyebrow.Size = New-Object System.Drawing.Size($script:ScreenW, 28)
    $panel.Controls.Add($eyebrow)

    $title = New-Object System.Windows.Forms.Label
    $title.Text = "KAKUGE"
    $title.Font = New-Object System.Drawing.Font("Segoe UI", 56, [System.Drawing.FontStyle]::Bold)
    $title.ForeColor = $pal.White
    $title.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $title.Location = New-Object System.Drawing.Point(0, 124)
    $title.Size = New-Object System.Drawing.Size($script:ScreenW, 90)
    $panel.Controls.Add($title)

    $rule = New-Object System.Windows.Forms.Panel
    $rule.BackColor = $pal.Accent
    $rule.Size = New-Object System.Drawing.Size(120, 4)
    $rule.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 120) / 2)), 216)
    $panel.Controls.Add($rule)

    function New-PrimaryButton([string]$text, [int]$top) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 16, [System.Drawing.FontStyle]::Bold)
        $b.Size = New-Object System.Drawing.Size(340, 64)
        $b.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 340) / 2)), $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.FlatAppearance.BorderSize = 0
        $b.BackColor = $pal.Accent
        $b.ForeColor = $pal.White
        $b.UseVisualStyleBackColor = $false
        $b.TabStop = $true
        return $b
    }
    function New-SecondaryButton([string]$text, [int]$top) {
        $b = New-Object System.Windows.Forms.Button
        $b.Text = $text
        $b.Font = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
        $b.Size = New-Object System.Drawing.Size(340, 56)
        $b.Location = New-Object System.Drawing.Point(([int](($script:ScreenW - 340) / 2)), $top)
        $b.FlatStyle = [System.Windows.Forms.FlatStyle]::Flat
        $b.FlatAppearance.BorderSize = 2
        $b.FlatAppearance.BorderColor = $pal.MidGray
        $b.BackColor = $pal.PanelBg
        $b.ForeColor = $pal.White
        $b.UseVisualStyleBackColor = $false
        $b.TabStop = $true
        return $b
    }

    $startButton = New-PrimaryButton "GAME START" 280
    $editButton  = New-SecondaryButton "CHARACTER EDIT" 364
    $exitButton  = New-SecondaryButton "EXIT" 434

    $startButton.Add_Click({ & $Navigate "Game" $null }.GetNewClosure())
    $editButton.Add_Click({ & $Navigate "Editor" $null }.GetNewClosure())
    $exitButton.Add_Click({ [System.Windows.Forms.Application]::Exit() }.GetNewClosure())

    $panel.Controls.Add($startButton)
    $panel.Controls.Add($editButton)
    $panel.Controls.Add($exitButton)

    $footer = New-Object System.Windows.Forms.Label
    $footer.Text = "A / D move   S crouch   Space jump   J K L attack   U special   I super   Esc pause"
    $footer.Font = New-Object System.Drawing.Font("Segoe UI", 9)
    $footer.ForeColor = $pal.MidGray
    $footer.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
    $footer.Location = New-Object System.Drawing.Point(0, ($script:ScreenH - 40))
    $footer.Size = New-Object System.Drawing.Size($script:ScreenW, 24)
    $panel.Controls.Add($footer)

    # See the note at the top of GameScreen.ps1 / Main.ps1's Show-Screen:
    # Activate is called explicitly by Show-Screen right after this panel
    # is added to the form, rather than relying on Add_VisibleChanged.
    $panel.Tag = @{
        Activate = { $startButton.Focus() }.GetNewClosure()
    }

    return $panel
}
