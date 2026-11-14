<#
.SYNOPSIS
    Build a multi-size Windows icon (.ico with PNG entries) from a 256x256 source.
    (ASCII only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.)

    The 256 px image is kept byte-for-byte; smaller sizes are high-quality bicubic
    downscales so the title bar / taskbar / Explorer icons stay sharp.

.EXAMPLE
    .\tools\make_icon.ps1 -Source C:\path\HUST.ico -Out res\HUSTpdf.ico
#>
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Out,
    [int[]]$Sizes = @(16, 20, 24, 32, 40, 48, 64, 128)
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Source: .ico (first PNG entry) or .png.
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Source))
if ($bytes[0] -eq 0 -and $bytes[2] -eq 1) {
    $size = [BitConverter]::ToUInt32($bytes, 14)
    $off = [BitConverter]::ToUInt32($bytes, 18)
    $png256 = New-Object byte[] $size
    [Array]::Copy($bytes, $off, $png256, 0, $size)
} else {
    $png256 = $bytes
}
$ms = New-Object System.IO.MemoryStream(, $png256)
$src = [System.Drawing.Image]::FromStream($ms)
if ($src.Width -ne 256 -or $src.Height -ne 256) { throw "Source must be 256x256 (got $($src.Width)x$($src.Height))" }

$images = @()
foreach ($s in ($Sizes | Sort-Object)) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src, 0, 0, $s, $s)
    $g.Dispose()
    $pngStream = New-Object System.IO.MemoryStream
    $bmp.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $images += , @($s, $pngStream.ToArray())
}
$images += , @(256, $png256)

# ICO: header, directory entries, PNG payloads.
$file = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($file)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($img in $images) {
    $dim = if ($img[0] -ge 256) { 0 } else { $img[0] }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$img[1].Length); $w.Write([uint32]$offset)
    $offset += $img[1].Length
}
foreach ($img in $images) { $w.Write([byte[]]$img[1]) }
$w.Flush()
$dir = Split-Path $Out -Parent
if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
[System.IO.File]::WriteAllBytes($Out, $file.ToArray())
Write-Host ("Wrote {0}: {1} images ({2}), {3:N0} bytes" -f $Out, $images.Count, (($images | ForEach-Object { $_[0] }) -join ', '), $file.Length)
