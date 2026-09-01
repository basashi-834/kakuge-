# UI/RenderHelpers.ps1
# Shared GDI+ drawing helpers + the world-space -> screen-space mapping
# used by GameScreen. Kept separate from BattleSystem so combat logic
# never depends on System.Drawing (section 38: UI と戦闘処理の分離).
# No art assets: everything is drawn as simple rectangles/ellipses
# (section 10/32 - "アニメーション素材がない場合は長方形・円...で構いません").

$script:ScreenW = 1280
$script:ScreenH = 720
$script:OriginX = 640.0
$script:OriginY = 520.0

function ConvertTo-ScreenX([double]$worldX) { return [int]($script:OriginX + $worldX) }
function ConvertTo-ScreenY([double]$worldY) { return [int]($script:OriginY + $worldY) }

function Get-TagColor([string]$tag) {
    switch ($tag) {
        "Light"   { return [System.Drawing.Color]::FromArgb(242, 217, 51) }
        "Medium"  { return [System.Drawing.Color]::FromArgb(242, 140, 38) }
        "Heavy"   { return [System.Drawing.Color]::FromArgb(230, 51, 38) }
        "Special" { return [System.Drawing.Color]::FromArgb(153, 64, 242) }
        "Super"   { return [System.Drawing.Color]::FromArgb(255, 38, 140) }
        default   { return [System.Drawing.Color]::White }
    }
}

function Get-MoveTint($fighter) {
    if ($null -ne $fighter.CurrentMoveData) {
        foreach ($t in @("Super", "Special", "Heavy", "Medium", "Light")) {
            if ($fighter.CurrentMoveData.HasTag($t)) { return Get-TagColor $t }
        }
    }
    return [System.Drawing.Color]::FromArgb($fighter.Stats.ColorR, $fighter.Stats.ColorG, $fighter.Stats.ColorB)
}

# Draws one fighter as simple procedural shapes, pose/color varying by
# state (section 32: Idle/Walk/Crouch/Jump/Attack/Hit/Knockdown/WakeUp/
# Block/Special/Super must be visually distinguishable).
function Draw-Fighter([System.Drawing.Graphics]$g, $fighter) {
    $bodyColor = [System.Drawing.Color]::FromArgb($fighter.Stats.ColorR, $fighter.Stats.ColorG, $fighter.Stats.ColorB)
    $bw = 46.0; $bh = 100.0; $ch = 62.0
    $sx = ConvertTo-ScreenX $fighter.PositionX
    $sy = ConvertTo-ScreenY $fighter.PositionY
    $brush = $null

    switch ($fighter.SM.CurrentState) {
        ([CharState]::Knockdown) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb([Math]::Max(0,$bodyColor.R-60),[Math]::Max(0,$bodyColor.G-60),[Math]::Max(0,$bodyColor.B-60)))
            $g.FillRectangle($brush, $sx - 50, $sy - 22, 100, 22)
        }
        ([CharState]::WakeUp) {
            $brush = New-Object System.Drawing.SolidBrush $bodyColor
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]($ch*0.7), [int]$bw, [int]($ch*0.7))
        }
        ([CharState]::Crouch) {
            $brush = New-Object System.Drawing.SolidBrush $bodyColor
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$ch, [int]$bw, [int]$ch)
        }
        ([CharState]::Block) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90,150,220))
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$bh, [int]$bw, [int]$bh)
        }
        ([CharState]::Hitstun) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,220,220))
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$bh, [int]$bw, [int]$bh)
        }
        ([CharState]::Throw) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200,60,60))
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]($bh*0.8), [int]$bw, [int]($bh*0.8))
        }
        ([CharState]::Dead) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70,70,70))
            $g.FillRectangle($brush, $sx - 50, $sy - 14, 100, 14)
        }
        ([CharState]::Attack) {
            $tint = Get-MoveTint $fighter
            $brush = New-Object System.Drawing.SolidBrush $tint
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$bh, [int]$bw, [int]$bh)
            $limbX = $sx + ($fighter.Facing * 8)
            $limbW = $fighter.Facing * 46
            $rx = [Math]::Min($limbX, $limbX + $limbW)
            $g.FillRectangle($brush, $rx, $sy - [int]($bh*0.65), [Math]::Abs($limbW), 16)
        }
        ([CharState]::Jump) {
            $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb([Math]::Min(255,$bodyColor.R+25),[Math]::Min(255,$bodyColor.G+25),[Math]::Min(255,$bodyColor.B+25)))
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$bh, [int]$bw, [int]$bh)
        }
        default {
            $brush = New-Object System.Drawing.SolidBrush $bodyColor
            $g.FillRectangle($brush, $sx - [int]($bw/2), $sy - [int]$bh, [int]$bw, [int]$bh)
        }
    }
    if ($null -ne $brush) { $brush.Dispose() }

    # facing indicator
    $noseBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb([Math]::Min(255,$bodyColor.R+60),[Math]::Min(255,$bodyColor.G+60),[Math]::Min(255,$bodyColor.B+60)))
    $noseX = $sx + $fighter.Facing * ([int]($bw/2) + 6)
    $g.FillEllipse($noseBrush, $noseX - 4, $sy - [int]($bh*0.85) - 4, 8, 8)
    $noseBrush.Dispose()
}

function Draw-Projectile([System.Drawing.Graphics]$g, $proj) {
    $sx = ConvertTo-ScreenX $proj.PositionX
    $sy = ConvertTo-ScreenY $proj.PositionY
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(230,150,40))
    $g.FillEllipse($brush, $sx - 16, $sy - 16, 32, 32)
    $brush.Dispose()
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255,255,150)), 3
    $g.DrawEllipse($pen, $sx - 16, $sy - 16, 32, 32)
    $pen.Dispose()
}

# Transient hit/guard/special/super effect (section 33). $effect is a
# hashtable @{ kind=; x=; y=; age=; duration= }.
function Get-EffectStyle([string]$kind) {
    switch ($kind) {
        "hit"       { return @{ color = [System.Drawing.Color]::FromArgb(255,255,80); radius = 16.0; duration = 0.14 } }
        "heavy_hit" { return @{ color = [System.Drawing.Color]::FromArgb(255,130,25); radius = 26.0; duration = 0.18 } }
        "guard"     { return @{ color = [System.Drawing.Color]::FromArgb(100,180,255); radius = 20.0; duration = 0.14 } }
        "special"   { return @{ color = [System.Drawing.Color]::FromArgb(155,50,255); radius = 30.0; duration = 0.22 } }
        "super"     { return @{ color = [System.Drawing.Color]::FromArgb(255,50,50); radius = 46.0; duration = 0.32 } }
        default     { return @{ color = [System.Drawing.Color]::White; radius = 16.0; duration = 0.14 } }
    }
}

function Draw-Effect([System.Drawing.Graphics]$g, [hashtable]$effect) {
    $style = Get-EffectStyle $effect.kind
    $t = [Math]::Min(1.0, $effect.age / [double]$style.duration)
    $r = [double]$style.radius * (0.4 + $t * 1.1)
    $alpha = [int](255 * (1.0 - $t))
    if ($alpha -le 0) { return }
    $c = $style.color
    $sx = ConvertTo-ScreenX $effect.x
    $sy = ConvertTo-ScreenY $effect.y
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($alpha, $c.R, $c.G, $c.B))
    $g.FillEllipse($brush, [int]($sx - $r), [int]($sy - $r), [int]($r*2), [int]($r*2))
    $brush.Dispose()
}

function Get-ScreenRect([RectBox]$rect) {
    $x = ConvertTo-ScreenX $rect.Left()
    $y = ConvertTo-ScreenY $rect.Top()
    $w = [int]$rect.Width
    $h = [int]$rect.Height
    return New-Object System.Drawing.Rectangle ($x, $y, $w, $h)
}

function Draw-HPBar([System.Drawing.Graphics]$g, [int]$x, [int]$y, [int]$w, [int]$h, [double]$ratio) {
    if ($ratio -lt 0) { $ratio = 0 }
    if ($ratio -gt 1) { $ratio = 1 }
    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(60, 15, 15))
    $g.FillRectangle($bg, $x, $y, $w, $h)
    $bg.Dispose()
    $fillColor = [System.Drawing.Color]::FromArgb(70, 210, 90)
    if ($ratio -lt 0.5) { $fillColor = [System.Drawing.Color]::FromArgb(230, 190, 40) }
    if ($ratio -lt 0.25) { $fillColor = [System.Drawing.Color]::FromArgb(220, 60, 50) }
    $fg = New-Object System.Drawing.SolidBrush $fillColor
    $g.FillRectangle($fg, $x, $y, [int]($w * $ratio), $h)
    $fg.Dispose()
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 2
    $g.DrawRectangle($pen, $x, $y, $w, $h)
    $pen.Dispose()
}

function Draw-GaugeBar([System.Drawing.Graphics]$g, [int]$x, [int]$y, [int]$w, [int]$h, [double]$ratio) {
    if ($ratio -lt 0) { $ratio = 0 }
    if ($ratio -gt 1) { $ratio = 1 }
    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(20, 30, 55))
    $g.FillRectangle($bg, $x, $y, $w, $h)
    $bg.Dispose()
    $fg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(80, 170, 255))
    $g.FillRectangle($fg, $x, $y, [int]($w * $ratio), $h)
    $fg.Dispose()
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(150,150,180)), 1
    $g.DrawRectangle($pen, $x, $y, $w, $h)
    $pen.Dispose()
}

function Draw-HUD([System.Drawing.Graphics]$g, [BattleSystem]$bs) {
    $font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
    $bigFont = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
    $whiteBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)

    Draw-HPBar $g 24 34 400 24 ($bs.Player1.CurrentHP / [double]$bs.Player1.Stats.MaxHP)
    Draw-HPBar $g ($script:ScreenW - 424) 34 400 24 ($bs.Player2.CurrentHP / [double]$bs.Player2.Stats.MaxHP)
    $g.DrawString($bs.Player1.Stats.Name, $font, $whiteBrush, 24, 10)
    $sizeP2 = $g.MeasureString($bs.Player2.Stats.Name, $font)
    $g.DrawString($bs.Player2.Stats.Name, $font, $whiteBrush, ($script:ScreenW - 24 - $sizeP2.Width), 10)

    $seconds = [int][Math]::Ceiling($bs.FramesLeft / 60.0)
    $timerText = [string]$seconds
    $timerSize = $g.MeasureString($timerText, $bigFont)
    $g.DrawString($timerText, $bigFont, $whiteBrush, ($script:ScreenW/2 - $timerSize.Width/2), 30)
    $roundSize = $g.MeasureString("ROUND 1", $font)
    $g.DrawString("ROUND 1", $font, $whiteBrush, ($script:ScreenW/2 - $roundSize.Width/2), 10)

    Draw-GaugeBar $g 24 682 300 14 ($bs.Player1.Gauge.Value / [SuperGauge]::MaxValue)
    Draw-GaugeBar $g ($script:ScreenW - 324) 682 300 14 ($bs.Player2.Gauge.Value / [SuperGauge]::MaxValue)

    $whiteBrush.Dispose(); $font.Dispose(); $bigFont.Dispose()
}

function Draw-DebugOverlay([System.Drawing.Graphics]$g, [BattleSystem]$bs) {
    $pushPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(60,255,60)), 2
    $hurtPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(70,130,255)), 2
    $hitPen  = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255,40,40)), 3
    $font = New-Object System.Drawing.Font("Consolas", 9)
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)

    foreach ($f in @($bs.Player1, $bs.Player2)) {
        $g.DrawRectangle($pushPen, (Get-ScreenRect $f.PushboxRect()))
        $g.DrawRectangle($hurtPen, (Get-ScreenRect $f.HurtboxRect()))
        if ($null -ne $f.ActiveHitboxRect) {
            $g.DrawRectangle($hitPen, (Get-ScreenRect $f.ActiveHitboxRect))
        }
        $info = $f.DebugInfo()
        $lines = @(
            ("state={0} move={1} frame={2}" -f $info.state, $info.move, $info.frame),
            ("hp={0} gauge={1:N0}" -f $info.hp, $info.gauge),
            ("vel=({0:N0},{1:N0})" -f $info.velocityX, $info.velocityY),
            ("hitstun={0} blockstun={1} hitstop={2}" -f $info.hitstun, $info.blockstun, $info.hitstop)
        )
        $tx = (ConvertTo-ScreenX $f.PositionX) - 80
        $ty = (ConvertTo-ScreenY $f.PositionY) - 190
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $g.DrawString($lines[$i], $font, $brush, $tx, $ty + ($i * 14))
        }
    }

    $pushPen.Dispose(); $hurtPen.Dispose(); $hitPen.Dispose(); $font.Dispose(); $brush.Dispose()
}
