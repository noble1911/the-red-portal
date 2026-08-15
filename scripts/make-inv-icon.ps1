# [RUNS MOD] Build a DXGM inventory-icon pack from ordinary image files.
#
# DXGM is the little container the mod already uses for gem art (assets/data/inv/gems.dat,
# read by LoadGemArt in Source/runs_gems.cpp). It stores true RGB plus 1-bit transparency;
# the game quantises the colours against whatever palette is live, because Diablo loads a
# different palette per dungeon type and cycles indices 1-31 for lava/water.
#
# Layout:
#   'D' 'X' 'G' 'M'  version(1)  spriteCount  colourCount  pad(0)
#   colourCount x (R, G, B)
#   per sprite: width(1)  height(1)  width*height index bytes, row-major,
#               0 = transparent, otherwise colour-table index + 1
#
# Inventory icons must be an exact multiple of 28px on both axes: an item's slot footprint
# is derived from its bitmap (GetInventorySize in Source/inv.cpp divides by 28), not declared.
#
# Sources may be any format Windows can decode. Where the decoder drops an alpha channel
# (it does for WebP), transparency is rebuilt by flood-filling the flat background inward
# from the border, so the subject must not touch all four edges.
#
# Usage:
#   scripts\make-inv-icon.ps1 -In art\scroll.webp -Out assets\data\inv\runicons.dat -PreviewDir out
#
# Windows-only (uses WIC via PresentationCore). This is a dev-time asset tool; nothing in
# the game build depends on it.
param(
    [Parameter(Mandatory = $true)][string[]]$In,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$Size = 28,
    [int]$Tolerance = 14,
    # Art drawn over a transparent backdrop often carries a drop shadow, which a decoder
    # that discards alpha flattens into a dark halo the plain colour key can't reach. The
    # shadow is neutral grey while the subject is coloured, so -KeyShadow additionally
    # clears border-connected pixels that are near-greyscale AND no brighter than the
    # background. Bright near-white highlights survive on the luminance test, dark
    # coloured areas on the chroma test.
    # The shadow is only ever a little darker than the backdrop it was flattened onto, so
    # the test is bounded below by -ShadowDepth. Without that bound it also swallows dark
    # near-neutral parts of the subject itself (on the scroll art, the rolled end).
    [switch]$KeyShadow,
    [int]$ChromaTolerance = 12,
    [int]$LumaSlack = 14,
    [int]$ShadowDepth = 45,
    [int]$AlphaThreshold = 128,
    [string]$PreviewDir,
    [int]$Zoom = 8
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName PresentationCore

function Save-Png([byte[]]$pixels, [int]$pw, [int]$ph, [string]$path) {
    $bmp = [System.Windows.Media.Imaging.BitmapSource]::Create(
        $pw, $ph, 96, 96, [System.Windows.Media.PixelFormats]::Bgra32, $null, $pixels, $pw * 4)
    $enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bmp))
    $fs = [System.IO.File]::Create($path)
    $enc.Save($fs)
    $fs.Close()
}

# Decode to Bgra32, rebuild alpha if needed, area-downscale into $Size x $Size.
# Returns a Bgra32 byte[] of that size.
function Convert-Icon([string]$path) {
    $stream = [System.IO.File]::OpenRead($path)
    $decoder = [System.Windows.Media.Imaging.BitmapDecoder]::Create($stream,
        [System.Windows.Media.Imaging.BitmapCreateOptions]::None,
        [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
    $conv = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap
    $conv.BeginInit()
    $conv.Source = $decoder.Frames[0]
    $conv.DestinationFormat = [System.Windows.Media.PixelFormats]::Bgra32
    $conv.EndInit()
    $w = $conv.PixelWidth
    $h = $conv.PixelHeight
    $stride = $w * 4
    $src = New-Object byte[] ($stride * $h)
    $conv.CopyPixels($src, $stride, 0)
    $stream.Close()

    # Does the source carry usable alpha of its own?
    $hasAlpha = $false
    for ($i = 3; $i -lt $src.Length; $i += 4) {
        if ($src[$i] -ne 255) { $hasAlpha = $true; break }
    }

    $isBg = New-Object bool[] ($w * $h)
    if ($hasAlpha) {
        for ($i = 0; $i -lt ($w * $h); $i++) { $isBg[$i] = ($src[($i * 4) + 3] -lt $AlphaThreshold) }
        # Write-Host, not Write-Output: this function returns the pixel buffer, and
        # anything written to the success stream would be prepended to it.
        Write-Host "  $([System.IO.Path]::GetFileName($path)): ${w}x${h}, using source alpha"
    } else {
        # Flood-fill the background inward from the border. Only background-coloured
        # pixels reachable from an edge are cleared, so matching pixels inside the
        # subject survive.
        $ring = @{}
        for ($x = 0; $x -lt $w; $x++) {
            foreach ($y in @(0, ($h - 1))) {
                $o = ($y * $stride) + ($x * 4)
                $k = "$($src[$o]) $($src[$o+1]) $($src[$o+2])"
                $ring[$k] = 1 + $ring[$k]
            }
        }
        $bgParts = (($ring.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key) -split ' '
        $bgB = [int]$bgParts[0]; $bgG = [int]$bgParts[1]; $bgR = [int]$bgParts[2]
        Write-Host "  $([System.IO.Path]::GetFileName($path)): ${w}x${h}, no alpha - keying BGR $bgB,$bgG,$bgR"

        $stack = New-Object System.Collections.Generic.Stack[int]
        for ($x = 0; $x -lt $w; $x++) {
            foreach ($y in @(0, ($h - 1))) { $stack.Push(($y * $w) + $x) }
        }
        for ($y = 0; $y -lt $h; $y++) {
            foreach ($x in @(0, ($w - 1))) { $stack.Push(($y * $w) + $x) }
        }
        $bgLuma = (($bgR * 2) + ($bgG * 5) + $bgB) / 8
        while ($stack.Count -gt 0) {
            $idx = $stack.Pop()
            if ($isBg[$idx]) { continue }
            $o = $idx * 4
            $b = [int]$src[$o]; $g = [int]$src[$o + 1]; $r = [int]$src[$o + 2]
            $d = [Math]::Max([Math]::Abs($b - $bgB),
                 [Math]::Max([Math]::Abs($g - $bgG), [Math]::Abs($r - $bgR)))
            $clear = ($d -le $Tolerance)
            if (-not $clear -and $KeyShadow) {
                $chroma = [Math]::Max($r, [Math]::Max($g, $b)) - [Math]::Min($r, [Math]::Min($g, $b))
                $luma = (($r * 2) + ($g * 5) + $b) / 8
                $clear = ($chroma -le $ChromaTolerance) `
                    -and ($luma -le ($bgLuma + $LumaSlack)) `
                    -and ($luma -ge ($bgLuma - $ShadowDepth))
            }
            if (-not $clear) { continue }
            $isBg[$idx] = $true
            $x = $idx % $w
            $y = [int](($idx - $x) / $w)
            if ($x -gt 0)        { $stack.Push($idx - 1) }
            if ($x -lt ($w - 1)) { $stack.Push($idx + 1) }
            if ($y -gt 0)        { $stack.Push($idx - $w) }
            if ($y -lt ($h - 1)) { $stack.Push($idx + $w) }
        }
    }

    # Content bounding box, so the subject fills the icon instead of inheriting the
    # source's padding.
    $minX = $w; $minY = $h; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            if (-not $isBg[($y * $w) + $x]) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { throw "$path is entirely background" }
    $bw = $maxX - $minX + 1
    $bh = $maxY - $minY + 1

    # Area downscale, aspect preserved and centred. Colour is averaged over subject
    # pixels only, so the keyed background never bleeds into the edges.
    $scale = [Math]::Max($bw, $bh) / [double]$Size
    $dstW = [Math]::Max(1, [int][Math]::Round($bw / $scale))
    $dstH = [Math]::Max(1, [int][Math]::Round($bh / $scale))
    $offX = [int](($Size - $dstW) / 2)
    $offY = [int](($Size - $dstH) / 2)
    Write-Host "    content ${bw}x${bh} -> ${dstW}x${dstH} centred at +$offX,+$offY"

    $dstStride = $Size * 4
    $dst = New-Object byte[] ($dstStride * $Size)
    for ($dy = 0; $dy -lt $dstH; $dy++) {
        $sy0 = $minY + [int][Math]::Floor($dy * $bh / $dstH)
        $sy1 = $minY + [int][Math]::Ceiling(($dy + 1) * $bh / $dstH)
        if ($sy1 -le $sy0) { $sy1 = $sy0 + 1 }
        for ($dx = 0; $dx -lt $dstW; $dx++) {
            $sx0 = $minX + [int][Math]::Floor($dx * $bw / $dstW)
            $sx1 = $minX + [int][Math]::Ceiling(($dx + 1) * $bw / $dstW)
            if ($sx1 -le $sx0) { $sx1 = $sx0 + 1 }
            $inside = 0; $total = 0; $sb = 0; $sg = 0; $sr = 0
            for ($sy = $sy0; $sy -lt $sy1; $sy++) {
                for ($sx = $sx0; $sx -lt $sx1; $sx++) {
                    $total++
                    $i = ($sy * $w) + $sx
                    if (-not $isBg[$i]) {
                        $inside++
                        $o = $i * 4
                        $sb += $src[$o]; $sg += $src[$o + 1]; $sr += $src[$o + 2]
                    }
                }
            }
            $o = (($dy + $offY) * $dstStride) + (($dx + $offX) * 4)
            if ($inside -gt 0) {
                $dst[$o]     = [byte][int][Math]::Round($sb / $inside)
                $dst[$o + 1] = [byte][int][Math]::Round($sg / $inside)
                $dst[$o + 2] = [byte][int][Math]::Round($sr / $inside)
                $dst[$o + 3] = [byte][int][Math]::Round(255.0 * $inside / $total)
            }
        }
    }
    return , $dst
}

$icons = @()
foreach ($path in $In) { $icons += , (Convert-Icon $path) }

# DXGM has 1-bit transparency (0 = clear), matching how the engine's own item sprites
# work, so soft edges from the downscale are thresholded here.
$dstStride = $Size * 4
$freq = @{}
foreach ($icon in $icons) {
    for ($i = 0; $i -lt ($Size * $Size); $i++) {
        if ($icon[($i * 4) + 3] -ge $AlphaThreshold) {
            $o = $i * 4
            $k = "$($icon[$o+2]),$($icon[$o+1]),$($icon[$o])" # R,G,B
            $freq[$k] = 1 + $freq[$k]
        }
    }
}
# The colour count is a single byte, and the runtime re-quantises to the game palette
# anyway, so trimming to the 255 most-used colours costs nothing visible.
$table = @($freq.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 255 | ForEach-Object { $_.Key })
Write-Output "colour table: $($freq.Keys.Count) distinct -> $($table.Count) stored"

$tableRgb = @()
foreach ($k in $table) { $p = $k -split ','; $tableRgb += , @([int]$p[0], [int]$p[1], [int]$p[2]) }
$index = @{}
for ($i = 0; $i -lt $table.Count; $i++) { $index[$table[$i]] = $i }

function Get-ColourIndex([int]$r, [int]$g, [int]$b) {
    $k = "$r,$g,$b"
    if ($index.ContainsKey($k)) { return $index[$k] }
    $best = 0; $bestD = [int]::MaxValue
    for ($i = 0; $i -lt $tableRgb.Count; $i++) {
        $dr = $r - $tableRgb[$i][0]; $dg = $g - $tableRgb[$i][1]; $db = $b - $tableRgb[$i][2]
        $d = ($dr * $dr * 2) + ($dg * $dg * 4) + ($db * $db)
        if ($d -lt $bestD) { $bestD = $d; $best = $i }
    }
    $index[$k] = $best
    return $best
}

$bytes = New-Object System.Collections.Generic.List[byte]
foreach ($c in [byte[]][char[]]'DXGM') { $bytes.Add($c) }
$bytes.Add(1)
$bytes.Add([byte]$icons.Count)
$bytes.Add([byte]$table.Count)
$bytes.Add(0)
foreach ($c in $tableRgb) { $bytes.Add([byte]$c[0]); $bytes.Add([byte]$c[1]); $bytes.Add([byte]$c[2]) }
foreach ($icon in $icons) {
    $bytes.Add([byte]$Size)
    $bytes.Add([byte]$Size)
    for ($y = 0; $y -lt $Size; $y++) {
        for ($x = 0; $x -lt $Size; $x++) {
            $o = ($y * $dstStride) + ($x * 4)
            if ($icon[$o + 3] -lt $AlphaThreshold) {
                $bytes.Add(0)
            } else {
                $bytes.Add([byte]((Get-ColourIndex $icon[$o + 2] $icon[$o + 1] $icon[$o]) + 1))
            }
        }
    }
}

$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Force $outDir | Out-Null }
[System.IO.File]::WriteAllBytes($Out, $bytes.ToArray())
Write-Output "wrote $Out ($($bytes.Count) bytes, $($icons.Count) sprite(s) of ${Size}x${Size})"

if ($PreviewDir) {
    if (-not (Test-Path $PreviewDir)) { New-Item -ItemType Directory -Force $PreviewDir | Out-Null }
    for ($n = 0; $n -lt $icons.Count; $n++) {
        $icon = $icons[$n]
        $pw = $Size * $Zoom
        $pStride = $pw * 4
        $prev = New-Object byte[] ($pStride * $pw)
        for ($y = 0; $y -lt $pw; $y++) {
            for ($x = 0; $x -lt $pw; $x++) {
                $so = ([int]($y / $Zoom) * $dstStride) + ([int]($x / $Zoom) * 4)
                # Show it thresholded, over a dark backdrop like the inventory panel.
                $po = ($y * $pStride) + ($x * 4)
                if ($icon[$so + 3] -ge $AlphaThreshold) {
                    $prev[$po] = $icon[$so]; $prev[$po + 1] = $icon[$so + 1]; $prev[$po + 2] = $icon[$so + 2]
                } else {
                    $prev[$po] = 24; $prev[$po + 1] = 20; $prev[$po + 2] = 16
                }
                $prev[$po + 3] = 255
            }
        }
        $p = Join-Path $PreviewDir ("icon{0}-preview.png" -f $n)
        Save-Png $prev $pw $pw $p
        Write-Output "wrote $p"
    }
}
