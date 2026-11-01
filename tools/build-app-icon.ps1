$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$full = (Resolve-Path 'res\logo.png').Path
$small = (Resolve-Path 'res\app-icon.png').Path
$frames = @(
    @{ Size = 16;  Source = $small },
    @{ Size = 20;  Source = $small },
    @{ Size = 24;  Source = $small },
    @{ Size = 32;  Source = $small },
    @{ Size = 40;  Source = $small },
    @{ Size = 48;  Source = $full },
    @{ Size = 64;  Source = $full },
    @{ Size = 96;  Source = $full },
    @{ Size = 128; Source = $full },
    @{ Size = 256; Source = $full }
)

$pngFrames = [System.Collections.Generic.List[byte[]]]::new()
foreach ($frame in $frames) {
    $source = [System.Drawing.Image]::FromFile($frame.Source)
    try {
        $bitmap = [System.Drawing.Bitmap]::new(
            [int]$frame.Size,
            [int]$frame.Size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($source, 0, 0, [int]$frame.Size, [int]$frame.Size)
            }
            finally {
                $graphics.Dispose()
            }

            $stream = [System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                $pngFrames.Add($stream.ToArray())
            }
            finally {
                $stream.Dispose()
            }
        }
        finally {
            $bitmap.Dispose()
        }
    }
    finally {
        $source.Dispose()
    }
}

$output = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$frames.Count)

    $offset = 6 + 16 * $frames.Count
    for ($i = 0; $i -lt $frames.Count; $i++) {
        $size = [int]$frames[$i].Size
        $dimension = if ($size -eq 256) { [byte]0 } else { [byte]$size }
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$pngFrames[$i].Length)
        $writer.Write([uint32]$offset)
        $offset += $pngFrames[$i].Length
    }

    foreach ($png in $pngFrames) {
        $writer.Write($png)
    }
    $writer.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path (Get-Location) 'res\app.ico'), $output.ToArray())
}
finally {
    $writer.Dispose()
    $output.Dispose()
}

$icon = Get-Item 'res\app.ico'
if ($icon.Length -lt 50000) { throw "Generated ICO is unexpectedly small: $($icon.Length) bytes" }
Write-Output ("Generated {0}: {1} frames, {2} bytes" -f $icon.FullName, $frames.Count, $icon.Length)
