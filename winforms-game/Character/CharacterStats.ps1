# Character/CharacterStats.ps1
# Base performance data for a character (section 5/31). Pure data - runtime
# fields (current HP, position, velocity, facing) live on Fighter and are
# re-initialized from this every round.

class CharacterStats {
    [string]$Id = ""
    [string]$Name = "Fighter"
    [int]$MaxHP = 1000
    [double]$WalkForwardSpeed = 220.0
    [double]$WalkBackwardSpeed = 170.0
    [double]$DashSpeed = 420.0
    [double]$JumpVelocity = -900.0
    [double]$Gravity = 2400.0
    [int]$ColorR = 200
    [int]$ColorG = 50
    [int]$ColorB = 45
    [System.Collections.ArrayList]$MoveIds = [System.Collections.ArrayList]::new()

    static [CharacterStats] FromObject($obj) {
        $s = [CharacterStats]::new()
        $s.Id = [string](Get-JsonProp $obj 'id' "")
        $s.Name = [string](Get-JsonProp $obj 'name' $s.Id)
        $s.MaxHP = [int](Get-JsonProp $obj 'maxHP' 1000)
        $s.WalkForwardSpeed = [double](Get-JsonProp $obj 'walkForwardSpeed' 220.0)
        $s.WalkBackwardSpeed = [double](Get-JsonProp $obj 'walkBackwardSpeed' 170.0)
        $s.DashSpeed = [double](Get-JsonProp $obj 'dashSpeed' 420.0)
        $s.JumpVelocity = [double](Get-JsonProp $obj 'jumpVelocity' -900.0)
        $s.Gravity = [double](Get-JsonProp $obj 'gravity' 2400.0)
        $colorArr = ConvertTo-PSArray (Get-JsonProp $obj 'color' @(0.78, 0.2, 0.18))
        if ($colorArr.Count -ge 3) {
            $s.ColorR = [int]([double]$colorArr[0] * 255)
            $s.ColorG = [int]([double]$colorArr[1] * 255)
            $s.ColorB = [int]([double]$colorArr[2] * 255)
        }
        foreach ($mid in (ConvertTo-PSArray (Get-JsonProp $obj 'moves' @()))) {
            [void]$s.MoveIds.Add([string]$mid)
        }
        return $s
    }

    [hashtable] ToHashtable() {
        return [ordered]@{
            id = $this.Id
            name = $this.Name
            maxHP = $this.MaxHP
            walkForwardSpeed = $this.WalkForwardSpeed
            walkBackwardSpeed = $this.WalkBackwardSpeed
            dashSpeed = $this.DashSpeed
            jumpVelocity = $this.JumpVelocity
            gravity = $this.Gravity
            color = @(($this.ColorR / 255.0), ($this.ColorG / 255.0), ($this.ColorB / 255.0))
            moves = @($this.MoveIds)
        }
    }
}
