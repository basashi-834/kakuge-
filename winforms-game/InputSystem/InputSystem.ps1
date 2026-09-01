# InputSystem/InputSystem.ps1
# Raw input snapshot + rolling history + motion-command recognition
# (sections 23/24). Raw per-frame input is passed around as a hashtable
# with keys Left/Right/Down/Up (bool) and ButtonsHeld (hashtable of
# Light/Medium/Heavy/Special/Super -> bool) - this is intentionally the
# same shape whether it comes from real keyboard state (GameScreen) or
# CPUAI's synthesized input, so Fighter never has to know which.

function New-RawInput {
    return @{
        Left = $false; Right = $false; Down = $false; Up = $false
        ButtonsHeld = @{ Light = $false; Medium = $false; Heavy = $false; Special = $false; Super = $false; Throw = $false }
    }
}

class InputBuffer {
    [System.Collections.ArrayList]$History = [System.Collections.ArrayList]::new()
    static [int] $Length = 20

    static [int] ComputeDigit([bool]$left, [bool]$right, [bool]$down, [bool]$up, [int]$facing) {
        $forward = $right
        $back = $left
        if ($facing -eq [Constants]::FacingLeft) { $forward = $left; $back = $right }
        if ($down -and $forward) { return 3 }
        if ($down -and $back) { return 1 }
        if ($up -and $forward) { return 9 }
        if ($up -and $back) { return 7 }
        if ($down) { return 2 }
        if ($up) { return 8 }
        if ($forward) { return 6 }
        if ($back) { return 4 }
        return 5
    }

    [void] RecordFrame([int]$frameNumber, [int]$digit, [string[]]$buttonsPressed) {
        [void]$this.History.Add(@{ frame = $frameNumber; digit = $digit; buttons = [string[]]$buttonsPressed })
        while ($this.History.Count -gt [InputBuffer]::Length) { $this.History.RemoveAt(0) }
    }

    [void] Clear() {
        $this.History.Clear()
    }
}

class CommandParser {
    static [hashtable] $Motions = @{
        "236"    = @(2, 3, 6)
        "214"    = @(2, 1, 4)
        "623"    = @(6, 2, 3)
        "236236" = @(2, 3, 6, 2, 3, 6)
    }

    static [bool] Matches([InputBuffer]$buffer, [string]$inputCommand, [string]$button, [int]$window) {
        if ([string]::IsNullOrEmpty($inputCommand)) { return $false }
        if (-not [CommandParser]::Motions.ContainsKey($inputCommand)) { return $false }
        $digits = [CommandParser]::Motions[$inputCommand]
        if ($buffer.History.Count -eq 0) { return $false }

        $lastFrame = [int]$buffer.History[$buffer.History.Count - 1].frame
        $relevant = @()
        foreach ($entry in $buffer.History) {
            if (($lastFrame - [int]$entry.frame) -le $window) { $relevant += $entry }
        }

        $ptr = 0
        $matchFrame = -1
        foreach ($entry in $relevant) {
            if ($ptr -lt $digits.Count -and [int]$entry.digit -eq $digits[$ptr]) {
                $ptr += 1
                $matchFrame = [int]$entry.frame
                if ($ptr -ge $digits.Count) { break }
            }
        }
        if ($ptr -lt $digits.Count) { return $false }

        $buttonGrace = 8
        foreach ($entry in $relevant) {
            if ([int]$entry.frame -ge $matchFrame -and ([int]$entry.frame - $matchFrame) -le $buttonGrace) {
                if ($entry.buttons -contains $button) { return $true }
            }
        }
        return $false
    }
}
