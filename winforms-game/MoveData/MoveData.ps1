# MoveData/MoveData.ps1
# Pure data description of a single move (normal/special/super). Mirrors
# the schema used by the earlier Godot prototype 1:1 so the same JSON files
# under Data/moves/<character>/ work unchanged (section 8/31/38: "技をコード
# に直接大量記述せず、可能な限りデータとして管理する").

class MoveData {
    [string]$Id = ""
    [string]$Name = ""

    [int]$Startup = 1
    [int]$Active = 1
    [int]$Recovery = 1
    [int]$TotalFrame = 0

    [int]$Damage = 0
    [int]$Hitstun = 0
    [int]$Blockstun = 0
    [int]$Hitstop = 0
    [string]$GuardType = "High"
    [double]$ChipDamagePercent = 0.0

    # Each entry: @{ offsetX=; offsetY=; width=; height= }
    [System.Collections.ArrayList]$Hitboxes = [System.Collections.ArrayList]::new()

    [double]$KnockbackX = 0.0
    [double]$KnockbackY = 0.0
    [string]$HitOutcome = "Normal"

    [int]$MeterGain = 0
    [int]$MeterCost = 0

    [System.Collections.ArrayList]$CancelRoutes = [System.Collections.ArrayList]::new()
    [int]$CancelStartFrame = 0
    [int]$CancelEndFrame = 0

    [System.Collections.ArrayList]$Tags = [System.Collections.ArrayList]::new()

    # @{ type = "None"/"Full"/"Strike"/"Throw"; start_frame=; end_frame= }
    $Invincibility = @{ type = "None"; start_frame = 0; end_frame = 0 }

    [string]$InputCommand = ""
    [string]$Button = ""
    [string]$Stance = "stand"
    [bool]$RequiresAir = $false

    $Projectile = @{}
    [double]$EffectiveRange = 0.0

    [bool] HasTag([string]$tag) {
        return $this.Tags.Contains($tag)
    }

    [bool] CanCancelInto([string]$moveId) {
        return $this.CancelRoutes.Contains($moveId)
    }

    [bool] IsCancelWindowOpen([int]$frame) {
        if ($this.CancelStartFrame -le 0 -and $this.CancelEndFrame -le 0) { return $false }
        return ($frame -ge $this.CancelStartFrame -and $frame -le $this.CancelEndFrame)
    }

    # Frame advantage helpers (section 16), used by the Character Editor.
    [int] OnHitAdvantage()   { return $this.Hitstun - $this.Recovery }
    [int] OnBlockAdvantage() { return $this.Blockstun - $this.Recovery }

    static [MoveData] FromObject($obj) {
        $m = [MoveData]::new()
        $m.Id   = [string](Get-JsonProp $obj 'id' "")
        $m.Name = [string](Get-JsonProp $obj 'name' $m.Id)
        $m.Startup  = [int](Get-JsonProp $obj 'startup' 1)
        $m.Active   = [int](Get-JsonProp $obj 'active' 1)
        $m.Recovery = [int](Get-JsonProp $obj 'recovery' 1)
        $tf = [int](Get-JsonProp $obj 'totalFrame' 0)
        if ($tf -gt 0) { $m.TotalFrame = $tf } else { $m.TotalFrame = $m.Startup + $m.Active + $m.Recovery }
        $m.Damage    = [int](Get-JsonProp $obj 'damage' 0)
        $m.Hitstun   = [int](Get-JsonProp $obj 'hitstun' 0)
        $m.Blockstun = [int](Get-JsonProp $obj 'blockstun' 0)
        $m.Hitstop   = [int](Get-JsonProp $obj 'hitstop' 0)
        $m.GuardType = [string](Get-JsonProp $obj 'guardType' "High")
        $m.ChipDamagePercent = [double](Get-JsonProp $obj 'chipDamagePercent' 0.0)

        $hitboxArr = ConvertTo-PSArray (Get-JsonProp $obj 'hitbox' @())
        foreach ($hb in $hitboxArr) {
            [void]$m.Hitboxes.Add(@{
                offsetX = [double](Get-JsonProp $hb 'offsetX' 0.0)
                offsetY = [double](Get-JsonProp $hb 'offsetY' 0.0)
                width   = [double](Get-JsonProp $hb 'width' 40.0)
                height  = [double](Get-JsonProp $hb 'height' 40.0)
            })
        }

        $m.KnockbackX = [double](Get-JsonProp $obj 'knockbackX' 0.0)
        $m.KnockbackY = [double](Get-JsonProp $obj 'knockbackY' 0.0)
        $m.HitOutcome = [string](Get-JsonProp $obj 'hitOutcome' "Normal")
        $m.MeterGain  = [int](Get-JsonProp $obj 'meterGain' 0)
        $m.MeterCost  = [int](Get-JsonProp $obj 'meterCost' 0)

        foreach ($r in (ConvertTo-PSArray (Get-JsonProp $obj 'cancelRoutes' @()))) {
            [void]$m.CancelRoutes.Add([string]$r)
        }
        $m.CancelStartFrame = [int](Get-JsonProp $obj 'cancelStartFrame' 0)
        $m.CancelEndFrame   = [int](Get-JsonProp $obj 'cancelEndFrame' 0)

        foreach ($t in (ConvertTo-PSArray (Get-JsonProp $obj 'tags' @()))) {
            [void]$m.Tags.Add([string]$t)
        }

        $invObj = Get-JsonProp $obj 'invincibility' $null
        if ($null -ne $invObj) {
            $m.Invincibility = @{
                type = [string](Get-JsonProp $invObj 'type' "None")
                start_frame = [int](Get-JsonProp $invObj 'start_frame' 0)
                end_frame = [int](Get-JsonProp $invObj 'end_frame' 0)
            }
        }

        $m.InputCommand = [string](Get-JsonProp $obj 'input' "")
        $m.Button = [string](Get-JsonProp $obj 'button' "")
        $m.Stance = [string](Get-JsonProp $obj 'stance' "stand")
        $m.RequiresAir = [bool](Get-JsonProp $obj 'requiresAir' $false)

        $projObj = Get-JsonProp $obj 'projectile' $null
        if ($null -ne $projObj) {
            $proj = @{}
            foreach ($p in $projObj.PSObject.Properties) { $proj[$p.Name] = $p.Value }
            $m.Projectile = $proj
        }

        $m.EffectiveRange = [double](Get-JsonProp $obj 'effectiveRange' 0.0)
        if ($m.EffectiveRange -le 0.0) {
            if ($m.HasTag([Constants]::TagProjectile)) { $m.EffectiveRange = 900.0 }
            elseif ($m.HasTag([Constants]::TagThrow))  { $m.EffectiveRange = 55.0 }
            elseif ($m.HasTag([Constants]::TagHeavy))  { $m.EffectiveRange = 100.0 }
            elseif ($m.HasTag([Constants]::TagMedium)) { $m.EffectiveRange = 85.0 }
            else { $m.EffectiveRange = 70.0 }
        }
        return $m
    }

    [hashtable] ToHashtable() {
        $hitboxOut = @()
        foreach ($hb in $this.Hitboxes) { $hitboxOut += , $hb }
        return [ordered]@{
            id = $this.Id
            name = $this.Name
            startup = $this.Startup
            active = $this.Active
            recovery = $this.Recovery
            totalFrame = $this.TotalFrame
            damage = $this.Damage
            hitstun = $this.Hitstun
            blockstun = $this.Blockstun
            hitstop = $this.Hitstop
            guardType = $this.GuardType
            chipDamagePercent = $this.ChipDamagePercent
            hitbox = @($hitboxOut)
            knockbackX = $this.KnockbackX
            knockbackY = $this.KnockbackY
            hitOutcome = $this.HitOutcome
            meterGain = $this.MeterGain
            meterCost = $this.MeterCost
            cancelRoutes = @($this.CancelRoutes)
            cancelStartFrame = $this.CancelStartFrame
            cancelEndFrame = $this.CancelEndFrame
            tags = @($this.Tags)
            invincibility = $this.Invincibility
            input = $this.InputCommand
            button = $this.Button
            stance = $this.Stance
            requiresAir = $this.RequiresAir
            projectile = $this.Projectile
            effectiveRange = $this.EffectiveRange
        }
    }
}
