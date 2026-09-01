# BattleSystem/Projectile.ps1
# Standalone object (section 26) - deliberately NOT a Fighter, just a
# moving rectangle with a lifetime and a reference to the move that
# defines its damage/hitstun/etc.

class Projectile {
    $Move
    [Fighter]$Owner
    [double]$PositionX = 0.0
    [double]$PositionY = 0.0
    [double]$Speed = 500.0
    [int]$LifetimeFrames = 90
    [int]$Facing = 1
    [double]$Width = 30.0
    [double]$Height = 30.0
    [bool]$HasHit = $false
    [double]$StageMinX = -600.0
    [double]$StageMaxX = 600.0

    [void] Setup($move, [Fighter]$owner, [double]$spawnX, [double]$spawnY, [int]$facing) {
        $this.Move = $move
        $this.Owner = $owner
        $this.Facing = $facing
        $pdata = $move.Projectile
        $this.Speed = 500.0
        $this.LifetimeFrames = 90
        $this.Width = 30.0
        $this.Height = 30.0
        $offsetX = 40.0
        $offsetY = -40.0
        if ($pdata -is [hashtable]) {
            if ($pdata.ContainsKey('speed')) { $this.Speed = [double]$pdata['speed'] }
            if ($pdata.ContainsKey('lifetime')) { $this.LifetimeFrames = [int]$pdata['lifetime'] }
            if ($pdata.ContainsKey('width')) { $this.Width = [double]$pdata['width'] }
            if ($pdata.ContainsKey('height')) { $this.Height = [double]$pdata['height'] }
            if ($pdata.ContainsKey('spawnOffsetX')) { $offsetX = [double]$pdata['spawnOffsetX'] }
            if ($pdata.ContainsKey('spawnOffsetY')) { $offsetY = [double]$pdata['spawnOffsetY'] }
        }
        $this.PositionX = $spawnX + ($offsetX * $facing)
        $this.PositionY = $spawnY + $offsetY
    }

    # Returns $false once the projectile should be removed.
    [bool] FrameStep([double]$dt) {
        $this.PositionX += $this.Speed * $this.Facing * $dt
        $this.LifetimeFrames -= 1
        if ($this.LifetimeFrames -le 0) { return $false }
        if ($this.PositionX -lt ($this.StageMinX - 100) -or $this.PositionX -gt ($this.StageMaxX + 100)) { return $false }
        return $true
    }

    [RectBox] HitboxRect() {
        return [RectBox]::new($this.PositionX, $this.PositionY, $this.Width, $this.Height)
    }
}
