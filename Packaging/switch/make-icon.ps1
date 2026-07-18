$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$size = 256
$bmp = New-Object System.Drawing.Bitmap($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias

# --- background: near-black with a faint warm vignette ---
$g.Clear([System.Drawing.Color]::FromArgb(255, 7, 5, 7))
$bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$bgPath.AddEllipse(-80, -60, 416, 400)
$bgBrush = New-Object System.Drawing.Drawing2D.PathGradientBrush($bgPath)
$bgBrush.CenterColor = [System.Drawing.Color]::FromArgb(255, 34, 8, 10)
$bgBrush.SurroundColors = @([System.Drawing.Color]::FromArgb(255, 6, 4, 6))
$bgBrush.CenterPoint = New-Object System.Drawing.PointF(128, 150)
$g.FillRectangle($bgBrush, 0, 0, $size, $size)

# --- pointed (ogive) arch path builder ---
function New-ArchPath([float]$cx, [float]$w, [float]$top, [float]$bottom) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $x1 = $cx - $w / 2; $x2 = $cx + $w / 2
    $shoulder = $top + ($bottom - $top) * 0.42
    $p.StartFigure()
    $p.AddLine($x1, $bottom, $x1, $shoulder)
    $p.AddBezier($x1, $shoulder, $x1, $top + 14, ($cx - $w * 0.16), $top, $cx, $top)
    $p.AddBezier($cx, $top, ($cx + $w * 0.16), $top, $x2, $top + 14, $x2, $shoulder)
    $p.AddLine($x2, $shoulder, $x2, $bottom)
    $p.CloseFigure()
    return $p
}

$cx = 128.0
$bottom = 218.0

# --- outer glow: layered strokes around the portal, wide+faint to tight+strong ---
$glowArch = New-ArchPath $cx 118 44 $bottom
$glows = @(
    @{ w = 34; a = 22 }, @{ w = 24; a = 38 }, @{ w = 16; a = 60 }, @{ w = 9; a = 95 }
)
foreach ($gl in $glows) {
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb($gl.a, 225, 40, 28)), ([float]$gl.w)
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $glowArch)
    $pen.Dispose()
}

# --- stone frame: two strokes, dark outer + lit inner edge ---
$framePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 52, 46, 50)), 11.0
$framePen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$g.DrawPath($framePen, $glowArch)
$framePen.Dispose()
$edgePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 96, 78, 76)), 3.0
$edgePen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$g.DrawPath($edgePen, $glowArch)
$edgePen.Dispose()

# --- portal body: radial red gradient inside the arch ---
$arch = New-ArchPath $cx 112 48 $bottom
$portalBrush = New-Object System.Drawing.Drawing2D.PathGradientBrush($arch)
$portalBrush.CenterColor = [System.Drawing.Color]::FromArgb(255, 236, 74, 40)
$portalBrush.SurroundColors = @([System.Drawing.Color]::FromArgb(255, 96, 10, 16))
$portalBrush.CenterPoint = New-Object System.Drawing.PointF(128, 150)
$g.FillPath($portalBrush, $arch)

# --- inner energy: nested brighter arches suggest depth/vortex ---
$inner1 = New-ArchPath $cx 74 78 $bottom
$b1 = New-Object System.Drawing.Drawing2D.PathGradientBrush($inner1)
$b1.CenterColor = [System.Drawing.Color]::FromArgb(255, 252, 132, 64)
$b1.SurroundColors = @([System.Drawing.Color]::FromArgb(60, 160, 24, 20))
$b1.CenterPoint = New-Object System.Drawing.PointF(128, 162)
$g.FillPath($b1, $inner1)

$inner2 = New-ArchPath $cx 40 112 $bottom
$b2 = New-Object System.Drawing.Drawing2D.PathGradientBrush($inner2)
$b2.CenterColor = [System.Drawing.Color]::FromArgb(255, 255, 210, 150)
$b2.SurroundColors = @([System.Drawing.Color]::FromArgb(40, 240, 90, 40))
$b2.CenterPoint = New-Object System.Drawing.PointF(128, 175)
$g.FillPath($b2, $inner2)

# --- steps under the portal ---
$stepColor = [System.Drawing.Color]::FromArgb(255, 58, 50, 54)
$stepLit = [System.Drawing.Color]::FromArgb(255, 120, 52, 44)
$sb = New-Object System.Drawing.SolidBrush $stepColor
$g.FillRectangle($sb, 52, 218, 152, 10); $sb.Dispose()
$sb = New-Object System.Drawing.SolidBrush $stepLit
$g.FillRectangle($sb, 52, 218, 152, 3); $sb.Dispose()
$sb = New-Object System.Drawing.SolidBrush $stepColor
$g.FillRectangle($sb, 38, 228, 180, 12); $sb.Dispose()

# --- floating embers ---
$rng = New-Object System.Random 7
for ($i = 0; $i -lt 26; $i++) {
    $ex = 40 + $rng.NextDouble() * 176
    $ey = 30 + $rng.NextDouble() * 170
    $er = 1 + $rng.NextDouble() * 2.2
    $ea = 60 + $rng.Next(140)
    $eb = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($ea, 255, (120 + $rng.Next(100)), 60))
    $g.FillEllipse($eb, [float]($ex - $er), [float]($ey - $er), [float](2 * $er), [float](2 * $er))
    $eb.Dispose()
}

$g.Dispose()

# --- save as high-quality JPEG (Switch nro icon format) ---
$out = Join-Path $PSScriptRoot 'icon.jpg'
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
$ep = New-Object System.Drawing.Imaging.EncoderParameters 1
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 95L
$bmp.Save($out, $codec, $ep)
$bmp.Dispose()
Write-Host "saved: $out"
