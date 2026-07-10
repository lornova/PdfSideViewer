# Generates app\res\app.ico: two side-by-side pages with an accent line at the same height
# (the synchronized-scrolling cue). Deterministic System.Drawing rendering, so the icon is
# reproducible from source; rerun after changing the artwork and commit the resulting .ico.
#
#   powershell scripts\make-icon.ps1
#
# The container is written by hand (System.Drawing.Icon cannot author multi-frame icons):
# classic 32bpp DIB frames up to 128 px for maximum tool compatibility (GDI+ itself cannot
# read PNG frames), PNG only for the 256 px frame as is conventional.

param(
    [string]$OutFile = (Join-Path $PSScriptRoot '..\app\res\app.ico')
)

Set-StrictMode -Version 3
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256

function New-Frame([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.Clear([System.Drawing.Color]::Transparent)

        $borderCol = [System.Drawing.Color]::FromArgb(255, 43, 58, 85)    # dark slate
        $paperCol  = [System.Drawing.Color]::FromArgb(255, 251, 251, 252)
        $foldCol   = [System.Drawing.Color]::FromArgb(255, 208, 214, 224)
        $grayCol   = [System.Drawing.Color]::FromArgb(255, 158, 168, 181)
        $accentCol = [System.Drawing.Color]::FromArgb(255, 47, 111, 222)  # sync-blue

        $bw = [Math]::Max(1.0, $s * 0.05)   # page border width
        $lw = [Math]::Max(1.0, $s * 0.05)   # content line width

        $borderPen = New-Object System.Drawing.Pen($borderCol, [float]$bw)
        $borderPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $paperBrush = New-Object System.Drawing.SolidBrush($paperCol)
        $foldBrush = New-Object System.Drawing.SolidBrush($foldCol)
        $grayPen = New-Object System.Drawing.Pen($grayCol, [float]$lw)
        $accentPen = New-Object System.Drawing.Pen($accentCol, [float]$lw)

        # Two pages, geometry in fractions of s so one routine renders every size.
        $y0 = 0.10 * $s
        $ph = 0.80 * $s
        $pw = 0.40 * $s
        $inset = $bw / 2.0
        foreach ($x0 in (0.06 * $s), (0.54 * $s)) {
            $x = $x0 + $inset; $y = $y0 + $inset
            $w = $pw - $bw;    $h = $ph - $bw
            $fold = if ($s -ge 24) { 0.28 * $w } else { 0.0 }

            # Page outline with a cut top-right corner (dog-ear when large enough).
            $pts = @(
                (New-Object System.Drawing.PointF([float]$x, [float]$y)),
                (New-Object System.Drawing.PointF([float]($x + $w - $fold), [float]$y)),
                (New-Object System.Drawing.PointF([float]($x + $w), [float]($y + $fold))),
                (New-Object System.Drawing.PointF([float]($x + $w), [float]($y + $h))),
                (New-Object System.Drawing.PointF([float]$x, [float]($y + $h)))
            )
            $g.FillPolygon($paperBrush, $pts)
            if ($fold -gt 0) {
                $tri = @(
                    (New-Object System.Drawing.PointF([float]($x + $w - $fold), [float]$y)),
                    (New-Object System.Drawing.PointF([float]($x + $w - $fold), [float]($y + $fold))),
                    (New-Object System.Drawing.PointF([float]($x + $w), [float]($y + $fold)))
                )
                $g.FillPolygon($foldBrush, $tri)
                $g.DrawPolygon($borderPen, $tri)
            }
            $g.DrawPolygon($borderPen, $pts)

            # Content lines; the accent sits at the same height on both pages.
            if ($s -ge 20) {
                $pad = 0.18 * $w
                foreach ($fy in 0.34, 0.52, 0.70) {
                    $ly = $y + $fy * $h
                    $pen = if ($fy -eq 0.52) { $accentPen } else { $grayPen }
                    $g.DrawLine($pen, [float]($x + $pad), [float]$ly, [float]($x + $w - $pad), [float]$ly)
                }
            }
        }

        $borderPen.Dispose(); $paperBrush.Dispose(); $foldBrush.Dispose()
        $grayPen.Dispose(); $accentPen.Dispose()
    } finally {
        $g.Dispose()
    }
    return $bmp
}

# ICO DIB frame: BITMAPINFOHEADER (height doubled for the AND mask), bottom-up BGRA pixels,
# then an all-zero 1bpp AND mask (the alpha channel drives transparency on 32bpp icons).
function Get-DibBytes([System.Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $rect = New-Object System.Drawing.Rectangle(0, 0, $s, $s)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $px = New-Object byte[] ($data.Stride * $s)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $px, 0, $px.Length)
    $stride = $data.Stride
    $bmp.UnlockBits($data)

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter($ms)
    $bw.Write([uint32]40)                 # biSize
    $bw.Write([int32]$s)                  # biWidth
    $bw.Write([int32]($s * 2))            # biHeight: XOR + AND blocks
    $bw.Write([uint16]1)                  # biPlanes
    $bw.Write([uint16]32)                 # biBitCount
    $bw.Write([uint32]0)                  # biCompression = BI_RGB
    $bw.Write([uint32]($s * $s * 4))      # biSizeImage
    $bw.Write([int32]0); $bw.Write([int32]0); $bw.Write([uint32]0); $bw.Write([uint32]0)
    for ($row = $s - 1; $row -ge 0; $row--) { $bw.Write($px, $row * $stride, $s * 4) }
    $maskRow = New-Object byte[] ([int]([Math]::Floor(($s + 31) / 32) * 4))
    for ($row = 0; $row -lt $s; $row++) { $bw.Write($maskRow) }
    $bw.Flush()
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return , $bytes
}

$frames = foreach ($s in $sizes) {
    $bmp = New-Frame $s
    if ($s -ge 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $ms.ToArray()
        $ms.Dispose()
    } else {
        $bytes = Get-DibBytes $bmp
    }
    $bmp.Dispose()
    , $bytes
}

$outDir = Split-Path -Parent $OutFile
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# ICONDIR + ICONDIRENTRY[] + payloads. Width/height bytes are 0 for the 256 px frame.
$fs = [System.IO.File]::Create($OutFile)
try {
    $bin = New-Object System.IO.BinaryWriter($fs)
    $bin.Write([uint16]0)                 # reserved
    $bin.Write([uint16]1)                 # type: icon
    $bin.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
        $bin.Write([byte]$dim)            # width
        $bin.Write([byte]$dim)            # height
        $bin.Write([byte]0)               # color count (true color)
        $bin.Write([byte]0)               # reserved
        $bin.Write([uint16]1)             # planes
        $bin.Write([uint16]32)            # bit count
        $bin.Write([uint32]$frames[$i].Length)
        $bin.Write([uint32]$offset)
        $offset += $frames[$i].Length
    }
    foreach ($frame in $frames) { $bin.Write($frame) }
    $bin.Flush()
} finally {
    $fs.Dispose()
}

Write-Host "Wrote $OutFile ($((Get-Item $OutFile).Length) bytes, $($sizes.Count) frames: $($sizes -join ', '))"
