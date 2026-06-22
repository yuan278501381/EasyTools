# ─────────────────────────────────────────────────────────────────────────────
# generate_icon.ps1 — 生成 rc.exe 兼容的应用图标 (resources/app.ico)
#
# 关键: 必须输出「未压缩的 32 位 DIB」图标 (BITMAPINFOHEADER + BGRA + AND 掩码),
# 而非 PNG 压缩图标 —— rc.exe 对 64px PNG 图标会报 RC2176 "old DIB"。
# ─────────────────────────────────────────────────────────────────────────────

Add-Type -AssemblyName System.Drawing

$size = 64

# 1. 渲染图标位图: 蓝底白色 "E"
$bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$g.Clear([System.Drawing.Color]::FromArgb(255, 35, 116, 225))
$font = New-Object System.Drawing.Font("Segoe UI", 34, [System.Drawing.FontStyle]::Bold)
$brush = [System.Drawing.Brushes]::White
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = [System.Drawing.StringAlignment]::Center
$fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
$g.DrawString("E", $font, $brush, (New-Object System.Drawing.RectangleF(0, 0, $size, $size)), $fmt)
$g.Dispose()

# 2. 取出 BGRA 像素 (Format32bppArgb 在内存中即 B,G,R,A 字节序)
$rect = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$pixels = New-Object byte[] ($stride * $size)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
$bmp.UnlockBits($data)
$bmp.Dispose()

# 3. 组装 ICO 字节流
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)

$andRowBytes = [int][Math]::Floor((($size + 31) / 32)) * 4   # AND 掩码每行 4 字节对齐
$xorBytes = $size * $size * 4
$andBytes = $size * $andRowBytes
$imageBytes = 40 + $xorBytes + $andBytes                      # BITMAPINFOHEADER + XOR + AND

# ICONDIR
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]1)
# ICONDIRENTRY
$bw.Write([byte]$size); $bw.Write([byte]$size); $bw.Write([byte]0); $bw.Write([byte]0)
$bw.Write([uint16]1); $bw.Write([uint16]32)
$bw.Write([uint32]$imageBytes); $bw.Write([uint32]22)

# BITMAPINFOHEADER (biHeight = 2*size: XOR + AND)
$bw.Write([uint32]40); $bw.Write([int32]$size); $bw.Write([int32]($size * 2))
$bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)
$bw.Write([uint32]$xorBytes); $bw.Write([int32]0); $bw.Write([int32]0)
$bw.Write([uint32]0); $bw.Write([uint32]0)

# XOR 位图: 自底向上逐行写 BGRA
for ($y = $size - 1; $y -ge 0; $y--) {
    $bw.Write($pixels, $y * $stride, $size * 4)
}
# AND 掩码: 全 0 (全不透明, 由 alpha 决定显示)
$zeroRow = New-Object byte[] $andRowBytes
for ($y = 0; $y -lt $size; $y++) { $bw.Write($zeroRow, 0, $andRowBytes) }

$bw.Flush()
$outPath = Join-Path $PSScriptRoot "app.ico"
[System.IO.File]::WriteAllBytes($outPath, $ms.ToArray())
$ms.Dispose()

Write-Output "app.ico generated (32-bit DIB, $size x $size, $($imageBytes + 22) bytes)"
