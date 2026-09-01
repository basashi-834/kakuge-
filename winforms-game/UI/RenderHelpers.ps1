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

# ---------------------------------------------------------------------
# Shared color palette (red / white / charcoal - matches the reference
# mockups the user provided). Every screen pulls from here so the whole
# app reads as one consistent theme rather than each screen inventing
# its own colors.
# ---------------------------------------------------------------------
function Get-Palette {
    return @{
        Accent      = [System.Drawing.Color]::FromArgb(230, 51, 41)   # primary red
        AccentDark  = [System.Drawing.Color]::FromArgb(178, 34, 27)
        Bg          = [System.Drawing.Color]::FromArgb(18, 18, 20)     # near-black
        PanelBg     = [System.Drawing.Color]::FromArgb(30, 30, 34)
        PanelBg2    = [System.Drawing.Color]::FromArgb(40, 40, 46)
        White       = [System.Drawing.Color]::White
        LightGray   = [System.Drawing.Color]::FromArgb(205, 205, 210)
        MidGray     = [System.Drawing.Color]::FromArgb(120, 120, 128)
        HpEmpty     = [System.Drawing.Color]::FromArgb(55, 20, 20)
        GaugeEmpty  = [System.Drawing.Color]::FromArgb(35, 30, 45)
        Gauge       = [System.Drawing.Color]::FromArgb(150, 90, 230)
    }
}

# Rounded-rectangle helper (System.Drawing has no built-in one) used by
# the HUD bars/buttons to get the softer, segmented look from the
# reference mockups instead of hard rectangular WinForms defaults.
function New-RoundedRectPath([System.Drawing.Rectangle]$rect, [int]$radius) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    if ($d -gt $rect.Height) { $d = $rect.Height }
    if ($d -gt $rect.Width) { $d = $rect.Width }
    if ($d -le 1) {
        $path.AddRectangle($rect)
        return $path
    }
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

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

# Draws a simple line-art humanoid (head w/ headband, torso, two-segment
# arms and legs with a joint bend) rather than a bare rectangle - styled
# after the reference sketch (a fighter with a headband, visible elbow/
# knee bends, and a clear punch/kick reach on attacks). $sx/$sy is the
# feet-center (ground contact) position; still no art assets, just lines
# and ellipses (section 10/32).
function Draw-Humanoid {
    param(
        [System.Drawing.Graphics]$g,
        [int]$sx, [int]$sy,
        [System.Drawing.Color]$color,
        [double]$heightScale = 1.0,
        [int]$facing = 1,
        [double]$armReach = 0.0,     # >0 = front arm punches forward
        [double]$legKick = 0.0,      # >0 = front leg kicks forward/up
        [double]$leanBack = 0.0,
        [double]$guardRaise = 0.0    # >0 = both arms raised (blocking)
    )
    $s = $heightScale
    $legH = 46.0 * $s
    $torsoH = 38.0 * $s
    $headR = 12.0 * $s
    $hipY = $sy - $legH
    $shoulderY = $hipY - $torsoH
    $neckY = $shoulderY - 2
    $headCenterY = $neckY - $headR
    $cx = $sx + ($leanBack * 0.35)

    $bodyPen = New-Object System.Drawing.Pen $color, (3.2 * $s)
    $bodyPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $limbPen = New-Object System.Drawing.Pen $color, (5.5 * $s)
    $limbPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $limbPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $headPen = New-Object System.Drawing.Pen $color, (2.6 * $s)
    $bandColor = [System.Drawing.Color]::FromArgb(230, 51, 41)
    $bandPen = New-Object System.Drawing.Pen $bandColor, (3.0 * $s)

    # --- back leg (static stance) ---
    $backHipX = $cx - (5 * $s)
    $backKneeX = $backHipX - (2 * $s)
    $backKneeY = $hipY + ($legH * 0.55)
    $g.DrawLine($limbPen, $backHipX, $hipY, $backKneeX, $backKneeY)
    $g.DrawLine($limbPen, $backKneeX, $backKneeY, ($cx - 12*$s), $sy)

    # --- front leg (kicks forward/up when legKick > 0) ---
    $frontHipX = $cx + (5 * $s)
    if ($legKick -gt 0) {
        $kneeX = $frontHipX + ($facing * 10 * $s)
        $kneeY = $hipY + ($legH * 0.35)
        $footX = $frontHipX + ($facing * (16 + $legKick) * $s)
        $footY = $hipY - ([Math]::Min($legKick, 30) * 0.5 * $s)
        $g.DrawLine($limbPen, $frontHipX, $hipY, $kneeX, $kneeY)
        $g.DrawLine($limbPen, $kneeX, $kneeY, $footX, $footY)
    } else {
        $kneeX = $frontHipX + (2 * $s)
        $kneeY = $hipY + ($legH * 0.55)
        $g.DrawLine($limbPen, $frontHipX, $hipY, $kneeX, $kneeY)
        $g.DrawLine($limbPen, $kneeX, $kneeY, ($cx + 12*$s), $sy)
    }

    # --- torso (outline, slight taper) ---
    $torsoTopW = 24.0 * $s
    $torsoBotW = 20.0 * $s
    $pts = @(
        (New-Object System.Drawing.PointF(($cx - $torsoTopW/2), $shoulderY)),
        (New-Object System.Drawing.PointF(($cx + $torsoTopW/2), $shoulderY)),
        (New-Object System.Drawing.PointF(($cx + $torsoBotW/2), $hipY)),
        (New-Object System.Drawing.PointF(($cx - $torsoBotW/2), $hipY))
    )
    $g.DrawPolygon($bodyPen, $pts)

    # --- back arm (static, guard-ish) ---
    $backShoulderX = $cx - (10 * $s)
    $backElbowX = $backShoulderX - (3 * $s)
    $backElbowY = $shoulderY + (14 * $s)
    $g.DrawLine($limbPen, $backShoulderX, $shoulderY + (2*$s), $backElbowX, $backElbowY)
    $g.DrawLine($limbPen, $backElbowX, $backElbowY, ($backElbowX - 2*$s), ($backElbowY + 12*$s))

    # --- front arm (punches forward when armReach > 0, raised when guardRaise > 0) ---
    $frontShoulderX = $cx + (10 * $s)
    if ($armReach -gt 0) {
        $elbowX = $frontShoulderX + ($facing * 10 * $s)
        $elbowY = $shoulderY + (6 * $s)
        $handX = $frontShoulderX + ($facing * (14 + $armReach) * $s)
        $handY = $shoulderY + (2 * $s) - ([Math]::Min($armReach, 20) * 0.25 * $s)
        $g.DrawLine($limbPen, $frontShoulderX, $shoulderY + (2*$s), $elbowX, $elbowY)
        $g.DrawLine($limbPen, $elbowX, $elbowY, $handX, $handY)
    } elseif ($guardRaise -gt 0) {
        $elbowX = $frontShoulderX + ($facing * 4 * $s)
        $elbowY = $shoulderY + (2 * $s)
        $handX = $frontShoulderX + ($facing * 8 * $s)
        $handY = $shoulderY - (10 * $s)
        $g.DrawLine($limbPen, $frontShoulderX, $shoulderY + (2*$s), $elbowX, $elbowY)
        $g.DrawLine($limbPen, $elbowX, $elbowY, $handX, $handY)
    } else {
        $elbowX = $frontShoulderX + (3 * $s)
        $elbowY = $shoulderY + (14 * $s)
        $g.DrawLine($limbPen, $frontShoulderX, $shoulderY + (2*$s), $elbowX, $elbowY)
        $g.DrawLine($limbPen, $elbowX, $elbowY, ($elbowX + 2*$s), ($elbowY + 12*$s))
    }

    # --- head + headband (a nod to the reference sketch's karate look) ---
    $headRect = New-Object System.Drawing.RectangleF(($cx - $headR), ($headCenterY - $headR), ($headR*2), ($headR*2))
    $g.DrawEllipse($headPen, $headRect)
    $bandY = $headCenterY - ($headR * 0.15)
    $g.DrawLine($bandPen, ($cx - $headR + 1*$s), $bandY, ($cx + $headR - 1*$s), $bandY)
    $tailX = $cx - ($facing * $headR * 0.6)
    $g.DrawLine($bandPen, $tailX, $bandY, ($tailX - $facing * 8 * $s), ($bandY - 10 * $s))
    $g.DrawLine($bandPen, $tailX, $bandY, ($tailX - $facing * 4 * $s), ($bandY - 14 * $s))

    $bodyPen.Dispose(); $limbPen.Dispose(); $headPen.Dispose(); $bandPen.Dispose()
}

# Pose/color varies by state (section 32: Idle/Walk/Crouch/Jump/Attack/
# Hit/Knockdown/WakeUp/Block/Special/Super must be visually distinguishable).
function Draw-Fighter([System.Drawing.Graphics]$g, $fighter) {
    $bodyColor = [System.Drawing.Color]::FromArgb($fighter.Stats.ColorR, $fighter.Stats.ColorG, $fighter.Stats.ColorB)
    $sx = ConvertTo-ScreenX $fighter.PositionX
    $sy = ConvertTo-ScreenY $fighter.PositionY
    $facing = $fighter.Facing

    switch ($fighter.SM.CurrentState) {
        ([CharState]::Knockdown) {
            $c = [System.Drawing.Color]::FromArgb([Math]::Max(0,$bodyColor.R-60),[Math]::Max(0,$bodyColor.G-60),[Math]::Max(0,$bodyColor.B-60))
            $pen = New-Object System.Drawing.Pen $c, 3.5
            $g.DrawLine($pen, $sx - 44, $sy - 6, $sx + 44, $sy - 6)
            $g.DrawEllipse($pen, ($sx + ($facing * 40)), $sy - 24, 20, 20)
            $pen.Dispose()
        }
        ([CharState]::WakeUp) {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color $bodyColor -heightScale 0.75 -facing $facing
        }
        ([CharState]::Crouch) {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color $bodyColor -heightScale 0.72 -facing $facing
        }
        ([CharState]::Block) {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color ([System.Drawing.Color]::FromArgb(90,150,220)) -facing $facing -guardRaise 10
        }
        ([CharState]::Hitstun) {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color ([System.Drawing.Color]::FromArgb(240,90,90)) -facing $facing -leanBack (8 * $facing)
        }
        ([CharState]::Throw) {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color ([System.Drawing.Color]::FromArgb(210,70,70)) -heightScale 0.85 -facing $facing
        }
        ([CharState]::Dead) {
            $c = [System.Drawing.Color]::FromArgb(90,90,90)
            $pen = New-Object System.Drawing.Pen $c, 3.0
            $g.DrawLine($pen, $sx - 44, $sy - 4, $sx + 44, $sy - 4)
            $g.DrawEllipse($pen, ($sx + ($facing * 40)), $sy - 20, 18, 18)
            $pen.Dispose()
        }
        ([CharState]::Attack) {
            $tint = Get-MoveTint $fighter
            $isKick = ($null -ne $fighter.CurrentMoveData -and $fighter.CurrentMoveData.HasTag([Constants]::TagHeavy))
            if ($isKick) {
                Draw-Humanoid -g $g -sx $sx -sy $sy -color $tint -facing $facing -legKick 34
            } else {
                Draw-Humanoid -g $g -sx $sx -sy $sy -color $tint -facing $facing -armReach 34
            }
        }
        ([CharState]::Jump) {
            $c = [System.Drawing.Color]::FromArgb([Math]::Min(255,$bodyColor.R+25),[Math]::Min(255,$bodyColor.G+25),[Math]::Min(255,$bodyColor.B+25))
            Draw-Humanoid -g $g -sx $sx -sy $sy -color $c -heightScale 0.92 -facing $facing
        }
        default {
            Draw-Humanoid -g $g -sx $sx -sy $sy -color $bodyColor -facing $facing
        }
    }
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

# Rounded, right-to-left-aware bar (mirror=true drains from the right, for
# player 2's side) matching the reference HUD's red-fill-on-dark look.
function Draw-Bar {
    param(
        [System.Drawing.Graphics]$g, [int]$x, [int]$y, [int]$w, [int]$h,
        [double]$ratio, [System.Drawing.Color]$fillColor, [System.Drawing.Color]$emptyColor,
        [bool]$mirror = $false
    )
    if ($ratio -lt 0) { $ratio = 0 }
    if ($ratio -gt 1) { $ratio = 1 }
    $pal = Get-Palette
    $rect = New-Object System.Drawing.Rectangle($x, $y, $w, $h)
    $radius = [int]($h / 2)
    $path = New-RoundedRectPath $rect $radius
    $bg = New-Object System.Drawing.SolidBrush $emptyColor
    $g.FillPath($bg, $path)
    $bg.Dispose()

    $fillW = [int]($w * $ratio)
    if ($fillW -gt 0) {
        $oldClip = $g.Clip
        $g.SetClip($path, [System.Drawing.Drawing2D.CombineMode]::Replace)
        if ($mirror) { $fillRect = New-Object System.Drawing.Rectangle(($x + $w - $fillW), $y, $fillW, $h) }
        else { $fillRect = New-Object System.Drawing.Rectangle($x, $y, $fillW, $h) }
        $fg = New-Object System.Drawing.SolidBrush $fillColor
        $g.FillRectangle($fg, $fillRect)
        $fg.Dispose()
        $g.Clip = $oldClip
    }

    $pen = New-Object System.Drawing.Pen $pal.White, 2
    $g.DrawPath($pen, $path)
    $pen.Dispose()
    $path.Dispose()
}

function Draw-HPBar([System.Drawing.Graphics]$g, [int]$x, [int]$y, [int]$w, [int]$h, [double]$ratio, [bool]$mirror) {
    $pal = Get-Palette
    Draw-Bar -g $g -x $x -y $y -w $w -h $h -ratio $ratio -fillColor $pal.Accent -emptyColor $pal.HpEmpty -mirror $mirror
}

function Draw-GaugeBar([System.Drawing.Graphics]$g, [int]$x, [int]$y, [int]$w, [int]$h, [double]$ratio, [bool]$mirror) {
    $pal = Get-Palette
    Draw-Bar -g $g -x $x -y $y -w $w -h $h -ratio $ratio -fillColor $pal.Gauge -emptyColor $pal.GaugeEmpty -mirror $mirror
}

function Draw-HUD([System.Drawing.Graphics]$g, [BattleSystem]$bs) {
    $pal = Get-Palette
    $nameFont = New-Object System.Drawing.Font("Segoe UI", 12, [System.Drawing.FontStyle]::Bold)
    $labelFont = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $timerFont = New-Object System.Drawing.Font("Segoe UI", 24, [System.Drawing.FontStyle]::Bold)
    $whiteBrush = New-Object System.Drawing.SolidBrush $pal.White
    $grayBrush = New-Object System.Drawing.SolidBrush $pal.LightGray

    Draw-HPBar $g 24 40 420 26 ($bs.Player1.CurrentHP / [double]$bs.Player1.Stats.MaxHP) $false
    Draw-HPBar $g ($script:ScreenW - 444) 40 420 26 ($bs.Player2.CurrentHP / [double]$bs.Player2.Stats.MaxHP) $true
    $g.DrawString($bs.Player1.Stats.Name.ToUpper(), $nameFont, $whiteBrush, 24, 12)
    $sizeP2 = $g.MeasureString($bs.Player2.Stats.Name.ToUpper(), $nameFont)
    $g.DrawString($bs.Player2.Stats.Name.ToUpper(), $nameFont, $whiteBrush, ($script:ScreenW - 24 - $sizeP2.Width), 12)

    # Round timer: bold red rounded box, matching the reference mockup.
    $seconds = [int][Math]::Ceiling($bs.FramesLeft / 60.0)
    $timerText = [string]$seconds
    $boxW = 84; $boxH = 54
    $boxX = [int](($script:ScreenW - $boxW) / 2)
    $boxRect = New-Object System.Drawing.Rectangle($boxX, 14, $boxW, $boxH)
    $boxPath = New-RoundedRectPath $boxRect 8
    $accentBrush = New-Object System.Drawing.SolidBrush $pal.Accent
    $g.FillPath($accentBrush, $boxPath)
    $accentBrush.Dispose(); $boxPath.Dispose()
    $timerSize = $g.MeasureString($timerText, $timerFont)
    $g.DrawString($timerText, $timerFont, $whiteBrush, ($boxX + $boxW/2 - $timerSize.Width/2), (14 + $boxH/2 - $timerSize.Height/2))
    $roundSize = $g.MeasureString("ROUND 1", $labelFont)
    $g.DrawString("ROUND 1", $labelFont, $grayBrush, ($script:ScreenW/2 - $roundSize.Width/2), 74)

    Draw-GaugeBar $g 24 682 320 14 ($bs.Player1.Gauge.Value / [SuperGauge]::MaxValue) $false
    Draw-GaugeBar $g ($script:ScreenW - 344) 682 320 14 ($bs.Player2.Gauge.Value / [SuperGauge]::MaxValue) $true
    $g.DrawString("GAUGE", $labelFont, $grayBrush, 24, 665)
    $gaugeR = $g.MeasureString("GAUGE", $labelFont)
    $g.DrawString("GAUGE", $labelFont, $grayBrush, ($script:ScreenW - 24 - $gaugeR.Width), 665)

    $whiteBrush.Dispose(); $grayBrush.Dispose(); $nameFont.Dispose(); $labelFont.Dispose(); $timerFont.Dispose()
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
