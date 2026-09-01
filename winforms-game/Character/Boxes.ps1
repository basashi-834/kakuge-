# Character/Boxes.ps1
# Hitbox / Hurtbox / Pushbox are plain axis-aligned rectangles (world-space,
# centered on X, top-left/height style on Y) kept as a small value-ish
# class so BattleSystem can intersect them without any dependency on
# System.Drawing/WinForms - this file is pure logic and is exercised by
# tests/HeadlessLogicTest.ps1 without loading any GUI assembly.

class RectBox {
    [double]$CenterX = 0
    [double]$CenterY = 0
    [double]$Width = 0
    [double]$Height = 0

    RectBox() {}
    RectBox([double]$cx, [double]$cy, [double]$w, [double]$h) {
        $this.CenterX = $cx
        $this.CenterY = $cy
        $this.Width = $w
        $this.Height = $h
    }

    [double] Left()   { return $this.CenterX - $this.Width / 2.0 }
    [double] Right()  { return $this.CenterX + $this.Width / 2.0 }
    [double] Top()    { return $this.CenterY - $this.Height / 2.0 }
    [double] Bottom() { return $this.CenterY + $this.Height / 2.0 }

    [bool] Intersects([RectBox]$other) {
        if ($null -eq $other) { return $false }
        if ($this.Right() -le $other.Left())  { return $false }
        if ($this.Left()  -ge $other.Right()) { return $false }
        if ($this.Bottom() -le $other.Top())  { return $false }
        if ($this.Top()  -ge $other.Bottom()) { return $false }
        return $true
    }
}

# Per-stance Hurtbox shapes (section 7: 立ち/しゃがみ/ジャンプで形状変化).
# Y is measured with 0 = ground, negative = up (matches the Godot port's
# convention so JSON hitbox offsets carry over unchanged).
class HurtboxSet {
    [RectBox]$Stand  = [RectBox]::new(0, -50, 46, 100)
    [RectBox]$Crouch = [RectBox]::new(0, -31, 46, 62)
    [RectBox]$Air    = [RectBox]::new(0, -50, 46, 100)

    [RectBox] ForStance([string]$stance, [double]$originX, [double]$originY) {
        $src = $this.Stand
        if ($stance -eq "crouch") { $src = $this.Crouch }
        elseif ($stance -eq "air") { $src = $this.Air }
        return [RectBox]::new($originX + $src.CenterX, $originY + $src.CenterY, $src.Width, $src.Height)
    }
}
