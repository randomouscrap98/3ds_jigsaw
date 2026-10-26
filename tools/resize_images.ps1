# Resize pictures for 3DS Jigsaw on Windows (same job as resize_images.sh, no extra tools:
# uses the image support built into Windows). Every picture's long side is made at most
# 1024 pixels (never enlarged), any EXIF rotation is applied (the game ignores it), and the
# result is written as PNG. JPG/PNG/BMP files that are already fine are copied as they are.
# Usually run through resize_images.bat (drag a folder onto it).
#   powershell -ExecutionPolicy Bypass -File resize_images.ps1 INFOLDER [OUTFOLDER]
# OUTFOLDER defaults to INFOLDER\resized.
param(
  [Parameter(Mandatory = $true)][string]$InDir,
  [string]$OutDir
)
Add-Type -AssemblyName System.Drawing
$Max = 1024

if (-not (Test-Path -LiteralPath $InDir -PathType Container)) {
  Write-Error "Not a folder: $InDir"
  exit 1
}
if (-not $OutDir) { $OutDir = Join-Path $InDir 'resized' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$readable = '.jpg', '.jpeg', '.png', '.bmp', '.gif', '.tif', '.tiff'
$gameFormats = '.jpg', '.jpeg', '.png', '.bmp'
# EXIF orientation -> the turn/flip that puts the picture upright
$upright = @{
  2 = 'RotateNoneFlipX'; 3 = 'Rotate180FlipNone'; 4 = 'Rotate180FlipX'; 5 = 'Rotate90FlipX'
  6 = 'Rotate90FlipNone'; 7 = 'Rotate270FlipX'; 8 = 'Rotate270FlipNone'
}

Get-ChildItem -LiteralPath $InDir -File | Where-Object { $readable -contains $_.Extension.ToLower() } | ForEach-Object {
  $file = $_
  try {
    $img = [System.Drawing.Image]::FromFile($file.FullName)
  } catch {
    Write-Warning "Skipped $($file.Name): can't read it"
    return
  }
  try {
    $orient = 1
    if ($img.PropertyIdList -contains 0x0112) { $orient = [int]$img.GetPropertyItem(0x0112).Value[0] }
    if ($upright.ContainsKey($orient)) { $img.RotateFlip([System.Drawing.RotateFlipType]$upright[$orient]) }
    $w = $img.Width
    $h = $img.Height
    if ($w -le $Max -and $h -le $Max -and $orient -le 1 -and $gameFormats -contains $file.Extension.ToLower()) {
      Copy-Item -LiteralPath $file.FullName -Destination $OutDir
      Write-Host "copied   $($file.Name)"
      return
    }
    $scale = [Math]::Min(1.0, $Max / [Math]::Max($w, $h))
    $nw = [Math]::Max(1, [int][Math]::Round($w * $scale))
    $nh = [Math]::Max(1, [int][Math]::Round($h * $scale))
    $bmp = New-Object System.Drawing.Bitmap $nw, $nh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    # Mirror the edges while sampling, so the border doesn't fade
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $dest = New-Object System.Drawing.Rectangle 0, 0, $nw, $nh
    $g.DrawImage($img, $dest, 0, 0, $w, $h, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    $g.Dispose()
    $out = Join-Path $OutDir ($file.BaseName + '.png')
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "resized  $($file.Name) -> $($file.BaseName).png ($nw x $nh)"
  } finally {
    $img.Dispose()
  }
}
Write-Host "Done. Pictures are in $OutDir"
