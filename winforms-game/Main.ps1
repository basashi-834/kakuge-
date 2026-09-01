# Main.ps1
# Entry point: loads every module in dependency order, wires up screen
# navigation, and starts the WinForms message loop. Launched via Launch.bat
# (double-click) so execution-policy restrictions never block it.
#
# Uses ONLY assemblies that ship with Windows PowerShell 5.1 / .NET
# Framework by default (System.Windows.Forms, System.Drawing) - no NuGet,
# no external module install.

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if ([string]::IsNullOrEmpty($root)) { $root = Split-Path -Parent $MyInvocation.MyCommand.Path }

$script:LogPath = Join-Path $root "error.log"
function Write-KakugeLog([string]$text) {
    try {
        $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $text
        Add-Content -LiteralPath $script:LogPath -Value $line -Encoding UTF8
    } catch { }
}

try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    # ---- dot-source every module (order matters: a class referencing
    # another class's TYPE must have that type already defined) ----
    . (Join-Path $root "Data\JsonHelpers.ps1")
    . (Join-Path $root "Character\Constants.ps1")
    . (Join-Path $root "Character\Boxes.ps1")
    . (Join-Path $root "MoveData\MoveData.ps1")
    . (Join-Path $root "MoveData\MoveExecutor.ps1")
    . (Join-Path $root "Character\CharacterStats.ps1")
    . (Join-Path $root "InputSystem\InputSystem.ps1")
    . (Join-Path $root "Character\StateMachine.ps1")
    . (Join-Path $root "Character\SuperGauge.ps1")
    . (Join-Path $root "Character\Fighter.ps1")
    . (Join-Path $root "BattleSystem\Projectile.ps1")
    . (Join-Path $root "CPUAI\CPUAI.ps1")
    . (Join-Path $root "BattleSystem\BattleSystem.ps1")
    . (Join-Path $root "Data\DataManager.ps1")
    . (Join-Path $root "UI\RenderHelpers.ps1")
    . (Join-Path $root "UI\AudioHelper.ps1")
    . (Join-Path $root "UI\TitleScreen.ps1")
    . (Join-Path $root "UI\GameScreen.ps1")
    . (Join-Path $root "UI\ResultScreen.ps1")
    . (Join-Path $root "CharacterEditor\CharacterEditorScreen.ps1")
}
catch {
    $msg = "起動時にエラーが発生しました (モジュール読み込み失敗):`n`n$($_.Exception.Message)`n`n$($_.ScriptStackTrace)"
    try { Write-KakugeLog $msg } catch { }
    try { Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.MessageBox]::Show($msg, "Kakuge - 起動エラー") | Out-Null } catch { Write-Host $msg }
    exit 1
}

try {
    # ---- match rules (section 35) ----
    $script:RoundTimeSeconds = 99
    $rulesPath = Join-Path $root "Data\match_rules.json"
    if (Test-Path -LiteralPath $rulesPath) {
        $rules = Read-JsonFile -Path $rulesPath
        if ($null -ne $rules) { $script:RoundTimeSeconds = [int](Get-JsonProp $rules 'roundTimeSeconds' 99) }
    }

    # ---- data (base data ships next to the launcher; edits saved under
    # %APPDATA%\Kakuge so they survive a restart - section 4/31) ----
    $userDir = Join-Path $env:APPDATA "Kakuge"
    $script:DM = [DataManager]::new((Join-Path $root "Data"), $userDir)
    $script:DM.ReloadAll()
    if ($script:DM.GetCharacterIds().Count -eq 0) {
        [System.Windows.Forms.MessageBox]::Show("Data\characters にキャラクターデータが見つかりませんでした。`nフォルダごとコピーされているか確認してください。", "Kakuge - データエラー") | Out-Null
        exit 1
    }

    Initialize-Audio (Join-Path $root "Audio")

    $script:PlayerCharId = "ryu"
    $script:CpuCharId = "ryu"

    # ---- main window ----
    $script:MainForm = New-Object System.Windows.Forms.Form
    $script:MainForm.Text = "Kakuge - 2D Fighting Game"
    $script:MainForm.ClientSize = New-Object System.Drawing.Size($script:ScreenW, $script:ScreenH)
    $script:MainForm.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $script:MainForm.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedSingle
    $script:MainForm.MaximizeBox = $false
    $script:MainForm.KeyPreview = $true
    $script:MainForm.BackColor = [System.Drawing.Color]::Black

    # Which screen currently "owns" the keyboard - deliberately $global:
    # (not $script:) because GameScreen.ps1's functions live in a
    # different dot-sourced file and PowerShell's $script: scope is tied
    # to the FILE a function was defined in, not the caller, so a $script:
    # variable here would NOT be the same variable GameScreen.ps1 sees.
    $global:KakugeActiveGameState = $null

    $script:MainForm.Add_KeyDown({
        param($sender, $e)
        if ($null -eq $global:KakugeActiveGameState) { return }
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::F1) {
            $global:KakugeActiveGameState.DebugVisible = -not $global:KakugeActiveGameState.DebugVisible
            return
        }
        [void]$global:KakugeActiveGameState.HeldKeys.Add($e.KeyCode)
    })
    $script:MainForm.Add_KeyUp({
        param($sender, $e)
        if ($null -eq $global:KakugeActiveGameState) { return }
        [void]$global:KakugeActiveGameState.HeldKeys.Remove($e.KeyCode)
    })

    # Show-Screen is a genuine FUNCTION (not a scriptblock variable) so
    # there is no self-reference/closure ordering problem when a screen
    # calls back into it - PowerShell functions are looked up by name and
    # always resolve $script: to the file that defines them (this file),
    # regardless of who calls them or when.
    function Show-Screen {
        param([string]$ScreenName, $Data)
        $script:MainForm.SuspendLayout()
        $script:MainForm.Controls.Clear()
        $newPanel = $null
        switch ($ScreenName) {
            "Title" {
                $newPanel = New-TitleScreen -MainForm $script:MainForm -Navigate ${function:Show-Screen}
            }
            "Game" {
                $gameScreenArgs = @{
                    MainForm = $script:MainForm
                    Navigate = ${function:Show-Screen}
                    DataManager = $script:DM
                    RoundTimeSeconds = $script:RoundTimeSeconds
                    PlayerCharacterId = $script:PlayerCharId
                    CpuCharacterId = $script:CpuCharId
                }
                $newPanel = New-GameScreen @gameScreenArgs
            }
            "Result" {
                $newPanel = New-ResultScreen -MainForm $script:MainForm -Navigate ${function:Show-Screen} -ResultData $Data
            }
            "Editor" {
                $newPanel = New-CharacterEditorScreen -MainForm $script:MainForm -Navigate ${function:Show-Screen} -DataManager $script:DM
            }
        }
        if ($null -ne $newPanel) {
            $newPanel.Dock = [System.Windows.Forms.DockStyle]::Fill
            $script:MainForm.Controls.Add($newPanel)
        }
        $script:MainForm.ResumeLayout()
    }

    # ---- route in-game-loop exceptions (Timer ticks / Paint / clicks) to
    # a visible error dialog + log file instead of a silent crash or the
    # OS "stopped working" dialog - critical since this can't be debugged
    # interactively; the user reporting the exact message is how a fix
    # happens. ----
    [System.Windows.Forms.Application]::SetUnhandledExceptionMode([System.Windows.Forms.UnhandledExceptionMode]::CatchException)
    [System.Windows.Forms.Application]::add_ThreadException({
        param($sender, $e)
        $msg = "実行中にエラーが発生しました:`n`n$($e.Exception.Message)`n`n$($e.Exception.StackTrace)"
        Write-KakugeLog $msg
        [System.Windows.Forms.MessageBox]::Show($msg, "Kakuge - 実行時エラー") | Out-Null
    })

    Show-Screen "Title" $null
    [System.Windows.Forms.Application]::Run($script:MainForm)
}
catch {
    $msg = "起動処理中にエラーが発生しました:`n`n$($_.Exception.Message)`n`n$($_.ScriptStackTrace)"
    Write-KakugeLog $msg
    try { [System.Windows.Forms.MessageBox]::Show($msg, "Kakuge - 起動エラー") | Out-Null } catch { Write-Host $msg }
    exit 1
}
