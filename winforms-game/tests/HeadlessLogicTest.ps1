# tests/HeadlessLogicTest.ps1
# Engine-agnostic regression check for the core fight loop, runnable with
# plain PowerShell (Windows PowerShell 5.1 OR PowerShell 7) - it never
# touches System.Windows.Forms/System.Drawing, only Character/MoveData/
# BattleSystem/InputSystem/CPUAI/Data, so it can be exercised in CI or on a
# dev machine without any GUI. Mirrors the Godot prototype's own headless
# test suite (HeadlessSmokeTest / GuardRulesTest / CommandAndCancelTest).
#
# Run with: pwsh -NoProfile -File tests/HeadlessLogicTest.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($root)) { $root = (Get-Item (Join-Path $PSScriptRoot "..")).FullName }

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

$script:Passed = 0
$script:Failed = 0
function Check([string]$label, [bool]$cond) {
    if ($cond) { $script:Passed++; Write-Host "[OK]   $label" }
    else { $script:Failed++; Write-Host "[FAIL] $label" -ForegroundColor Red }
}

# Use a throwaway user-data folder so this test never touches a real
# %APPDATA% installation and is repeatable.
$tempUserDir = Join-Path ([System.IO.Path]::GetTempPath()) ("KakugeTest_" + [guid]::NewGuid().ToString("N"))
$dm = [DataManager]::new((Join-Path $root "Data"), $tempUserDir)
$dm.ReloadAll()

Write-Host "=== DataManager load ==="
Check "loaded character 'ryu'" ($null -ne $dm.GetCharacter("ryu"))
$ryuMoves = $dm.GetMoveset("ryu")
Check "loaded 11 ryu moves" ($ryuMoves.Count -eq 11)

# ---------------------------------------------------------------------
# 1) Full simulated match: Player1 idle (no input), CPU-controlled
#    Player2 attacking - verifies state machine, hitbox/hurtbox, hit
#    resolution, knockdown/wakeup, gauge, KO all work end-to-end.
# ---------------------------------------------------------------------
Write-Host "`n=== Full match simulation (P1 idle vs CPU) ==="
$bs = [BattleSystem]::new()
$bs.StartMatch($dm.GetCharacter("ryu"), $dm.GetMoveset("ryu"), $dm.GetCharacter("ryu"), $dm.GetMoveset("ryu"), 99)

$dt = 1.0 / 60.0
$neutral = New-RawInput
$sawHitboxActive = $false
$sawHpDrop = $false
$maxFrames = 12000
$frame = 0
while ($bs.MatchActive -and $frame -lt $maxFrames) {
    $bs.Update($dt, $neutral)
    if ($null -ne $bs.Player2.ActiveHitboxRect) { $sawHitboxActive = $true }
    if ($bs.Player1.CurrentHP -lt $bs.Player1.Stats.MaxHP) { $sawHpDrop = $true }
    $frame++
    if ($frame % 1800 -eq 0) {
        Write-Host ("  t={0}s P1hp={1} P2hp={2} P1st={3} P2st={4}/{5} P1g={6:N0} P2g={7:N0}" -f `
            [int]($frame/60), $bs.Player1.CurrentHP, $bs.Player2.CurrentHP, $bs.Player1.SM.CurrentState, `
            $bs.Player2.SM.CurrentState, $bs.Player2.SM.CurrentMove, $bs.Player1.Gauge.Value, $bs.Player2.Gauge.Value)
    }
}
Check "loop ended before frame cap ($frame frames)" ($frame -lt $maxFrames)
Check "hitbox became active at least once" $sawHitboxActive
Check "player1 HP dropped at least once" $sawHpDrop
Check "match ended via KO or timeout" (-not $bs.MatchActive)
Check "a winner or draw was recorded" ($bs.IsDraw -or $null -ne $bs.Winner)
Write-Host ("  final: P1hp={0} P2hp={1} draw={2} winner={3}" -f $bs.Player1.CurrentHP, $bs.Player2.CurrentHP, $bs.IsDraw, $(if ($bs.Winner) { $bs.Winner.Stats.Name } else { "none" }))

# ---------------------------------------------------------------------
# 2) Guard rule table (section 14)
# ---------------------------------------------------------------------
Write-Host "`n=== Guard rule table ==="
function Test-Guard([string]$label, [string]$moveId, $crouchGuard, [bool]$expectBlocked) {
    $attacker = [Fighter]::new()
    $defender = [Fighter]::new()
    $attacker.Setup($dm.GetCharacter("ryu"), $dm.GetMoveset("ryu"))
    $defender.Setup($dm.GetCharacter("ryu"), $dm.GetMoveset("ryu"))
    $attacker.PositionX = -40; $defender.PositionX = 40
    $attacker.Facing = [Constants]::FacingRight
    $defender.Facing = [Constants]::FacingLeft
    $attacker.Opponent = $defender; $defender.Opponent = $attacker

    if ($null -eq $crouchGuard) {
        $defender.SM.ChangeState([CharState]::Idle, "")
        $defender.InputBuf.RecordFrame(1, 5, @())
    }
    elseif ($crouchGuard) {
        $defender.SM.ChangeState([CharState]::Crouch, "")
        $defender.IsCrouchingGuard = $true
        $defender.InputBuf.RecordFrame(1, 1, @()) # digit 1 = down+back, facing left -> right+down held
    }
    else {
        $defender.SM.ChangeState([CharState]::Block, "")
        $defender.InputBuf.RecordFrame(1, 4, @()) # digit 4 = back only
    }

    $move = $attacker.Moveset[$moveId]
    $result = $defender.ReceiveHit($move, $attacker)
    Check "$label (blocked=$($result.blocked))" ([bool]$result.blocked -eq $expectBlocked)
}

Test-Guard "High vs standing block -> BLOCKED"       "standing_medium" $false $true
Test-Guard "High vs crouching block -> BLOCKED"       "standing_medium" $true  $true
Test-Guard "Low vs crouching block -> BLOCKED"        "crouch_light"    $true  $true
Test-Guard "Low vs standing block -> HIT"             "crouch_light"    $false $false
Test-Guard "Overhead vs standing block -> BLOCKED"    "jump_attack"     $false $true
Test-Guard "Overhead vs crouching block -> HIT"       "jump_attack"     $true  $false
Test-Guard "Throw vs standing block -> HIT (unblockable)" "standing_throw" $false $false
Test-Guard "Throw vs crouching block -> HIT (unblockable)" "standing_throw" $true $false
Test-Guard "High vs no guard input -> HIT"            "standing_medium" $null  $false

# ---------------------------------------------------------------------
# 3) Command parser + cancel window
# ---------------------------------------------------------------------
Write-Host "`n=== Command parser + cancel window ==="
$buf = [InputBuffer]::new()
$f = 0
foreach ($d in @(2, 2, 3, 3, 6)) { $f++; $buf.RecordFrame($f, $d, @()) }
$f++; $buf.RecordFrame($f, 6, @("Special"))
Check "236+Special recognized" ([CommandParser]::Matches($buf, "236", "Special", [Constants]::CommandWindow))
Check "214 NOT recognized from a 236 buffer" (-not [CommandParser]::Matches($buf, "214", "Special", [Constants]::CommandWindow))

$buf2 = [InputBuffer]::new()
$f = 0
foreach ($d in @(2, 3, 6)) { $f++; $buf2.RecordFrame($f, $d, @()) }
for ($i = 0; $i -lt 20; $i++) { $f++; $buf2.RecordFrame($f, 5, @()) }
$f++; $buf2.RecordFrame($f, 5, @("Special"))
Check "236+Special NOT recognized when button comes too late" (-not [CommandParser]::Matches($buf2, "236", "Special", [Constants]::CommandWindow))

$fighter = [Fighter]::new()
$fighter.Setup($dm.GetCharacter("ryu"), $dm.GetMoveset("ryu"))
$fighter.Opponent = $fighter
$neutral2 = New-RawInput
$lightInput = New-RawInput; $lightInput.ButtonsHeld.Light = $true
$fighter.FrameStep($dt, $lightInput)
Check "Light press starts standing_light" ($fighter.SM.CurrentMove -eq "standing_light")

for ($i = 0; $i -lt 4; $i++) { $fighter.FrameStep($dt, $neutral2) }
Check "still in standing_light at cancel window open, frame=$($fighter.SM.CurrentFrame)" `
    ($fighter.SM.CurrentMove -eq "standing_light" -and $fighter.SM.CurrentFrame -eq 4)

$heavyInput = New-RawInput; $heavyInput.ButtonsHeld.Heavy = $true
$fighter.FrameStep($dt, $heavyInput)
Check "Heavy cancels standing_light into standing_heavy inside the window" `
    ($fighter.SM.CurrentMove -eq "standing_heavy" -and $fighter.SM.CurrentFrame -eq 0)

# ---------------------------------------------------------------------
# 4) DataManager save/reload persistence (character editor path)
# ---------------------------------------------------------------------
Write-Host "`n=== DataManager save/reload persistence ==="
$statsCopy = $dm.GetCharacter("ryu")
$originalHp = $statsCopy.MaxHP
$statsCopy.MaxHP = $originalHp + 250
$statsCopy.Name = "RYU-TEST"
$dm.SaveCharacter($statsCopy)

$moveCopy = $dm.GetMove("ryu", "standing_light")
$originalDamage = $moveCopy.Damage
$moveCopy.Damage = $originalDamage + 17
$dm.SaveMove("ryu", $moveCopy)

$dm2 = [DataManager]::new((Join-Path $root "Data"), $tempUserDir)
$dm2.ReloadAll()
Check "reloaded maxHP matches the edit" ($dm2.GetCharacter("ryu").MaxHP -eq ($originalHp + 250))
Check "reloaded name matches the edit" ($dm2.GetCharacter("ryu").Name -eq "RYU-TEST")
Check "reloaded move damage matches the edit" ($dm2.GetMove("ryu", "standing_light").Damage -eq ($originalDamage + 17))

Remove-Item -LiteralPath $tempUserDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "`n=== RESULT: $script:Passed passed, $script:Failed failed ==="
if ($script:Failed -gt 0) { exit 1 } else { exit 0 }
