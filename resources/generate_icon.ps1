# ─────────────────────────────────────────────────────────────────────────────
# generate_icon.ps1 — 生成 EasyTools 全套 Windows 规范应用图标与纯白托盘图标
#
# 规范:
#   1. app.ico (用于任务栏、任务管理器、Alt+Tab、桌面快捷方式、安装包)
#      • 经典原版蓝底 RGB(35, 116, 225) + 经典纯白闪电 (替代原字母 "E")
#   2. tray.ico (用于 Windows 系统托盘区 Notification Area)
#      • 纯透明底色 + 经典纯白闪电 (Pure White Lightning)，保持与原版视觉一致且托盘纯白
#   3. 输出未压缩 32 位 DIB 多分辨率 (16, 20, 24, 32, 40, 48, 64, 128, 256)，100% 兼容 rc.exe / High-DPI
# ─────────────────────────────────────────────────────────────────────────────

Add-Type -AssemblyName System.Drawing

function Build-IcoFile {
    param (
        [string]$OutputPath,
        [scriptblock]$RenderCallback,
        [int[]]$Sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    )

    $imagesData = @()
    $headerSize = 6 + ($Sizes.Length * 16)
    $currentOffset = $headerSize

    foreach ($sz in $Sizes) {
        $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)

        & $RenderCallback $g $sz

        $g.Dispose()

        # 提取 32 位 BGRA 像素
        $rect = New-Object System.Drawing.Rectangle(0, 0, $sz, $sz)
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = $data.Stride
        $pixels = New-Object byte[] ($stride * $sz)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
        $bmp.UnlockBits($data)
        $bmp.Dispose()

        $andRowBytes = [int][Math]::Floor((($sz + 31) / 32)) * 4
        $xorBytes = $sz * $sz * 4
        $andBytes = $sz * $andRowBytes
        $imageBytes = 40 + $xorBytes + $andBytes

        # 序列化单张 DIB 数据块
        $msImg = New-Object System.IO.MemoryStream
        $bwImg = New-Object System.IO.BinaryWriter($msImg)

        # BITMAPINFOHEADER (biHeight = 2*sz)
        $bwImg.Write([uint32]40)
        $bwImg.Write([int32]$sz)
        $bwImg.Write([int32]($sz * 2))
        $bwImg.Write([uint16]1)
        $bwImg.Write([uint16]32)
        $bwImg.Write([uint32]0)
        $bwImg.Write([uint32]$xorBytes)
        $bwImg.Write([int32]0)
        $bwImg.Write([int32]0)
        $bwImg.Write([uint32]0)
        $bwImg.Write([uint32]0)

        # XOR 位图 (自底向上 BGRA)
        for ($y = $sz - 1; $y -ge 0; $y--) {
            $bwImg.Write($pixels, $y * $stride, $sz * 4)
        }

        # AND 掩码 (全 0 透明通道由 alpha 决定)
        $zeroRow = New-Object byte[] $andRowBytes
        for ($y = 0; $y -lt $sz; $y++) {
            $bwImg.Write($zeroRow, 0, $andRowBytes)
        }

        $bwImg.Flush()
        $dibBytes = $msImg.ToArray()
        $msImg.Dispose()

        $imagesData += [PSCustomObject]@{
            Size = $sz
            ImageBytes = $imageBytes
            Offset = $currentOffset
            Data = $dibBytes
        }

        $currentOffset += $imageBytes
    }

    # 组装完整 ICO 文件
    $msFile = New-Object System.IO.MemoryStream
    $bwFile = New-Object System.IO.BinaryWriter($msFile)

    # ICONDIR
    $bwFile.Write([uint16]0)
    $bwFile.Write([uint16]1)
    $bwFile.Write([uint16]$Sizes.Length)

    # ICONDIRENTRY[]
    foreach ($entry in $imagesData) {
        $wByte = if ($entry.Size -eq 256) { [byte]0 } else { [byte]$entry.Size }
        $hByte = if ($entry.Size -eq 256) { [byte]0 } else { [byte]$entry.Size }
        $bwFile.Write($wByte)
        $bwFile.Write($hByte)
        $bwFile.Write([byte]0) # ColorCount
        $bwFile.Write([byte]0) # Reserved
        $bwFile.Write([uint16]1) # Planes
        $bwFile.Write([uint16]32) # BitCount
        $bwFile.Write([uint32]$entry.ImageBytes)
        $bwFile.Write([uint32]$entry.Offset)
    }

    # DIB Blocks
    foreach ($entry in $imagesData) {
        $bwFile.Write($entry.Data, 0, $entry.Data.Length)
    }

    $bwFile.Flush()
    [System.IO.File]::WriteAllBytes($OutputPath, $msFile.ToArray())
    $msFile.Dispose()

    Write-Output "Generated: $OutputPath ($($Sizes.Length) sizes: $($Sizes -join ', ') px)"
}

# 绘制经典闪电点阵
function Get-ClassicLightningPoints {
    param ([float]$w, [float]$h, [float]$padX = 0, [float]$padY = 0)
    $bw = $w - ($padX * 2)
    $bh = $h - ($padY * 2)

    return @(
        (New-Object System.Drawing.PointF ($padX + $bw * (13.0 / 24.0)), ($padY + $bh * (2.0 / 24.0))),
        (New-Object System.Drawing.PointF ($padX + $bw * (3.0 / 24.0)),  ($padY + $bh * (14.0 / 24.0))),
        (New-Object System.Drawing.PointF ($padX + $bw * (12.0 / 24.0)), ($padY + $bh * (14.0 / 24.0))),
        (New-Object System.Drawing.PointF ($padX + $bw * (11.0 / 24.0)), ($padY + $bh * (22.0 / 24.0))),
        (New-Object System.Drawing.PointF ($padX + $bw * (21.0 / 24.0)), ($padY + $bh * (10.0 / 24.0))),
        (New-Object System.Drawing.PointF ($padX + $bw * (12.0 / 24.0)), ($padY + $bh * (10.0 / 24.0)))
    )
}

# ── 1. 生成 app.ico (原版蓝底 RGB(35, 116, 225) + 纯白经典闪电) ────────────
$appIcoPath = Join-Path $PSScriptRoot "app.ico"
Build-IcoFile -OutputPath $appIcoPath -RenderCallback {
    param ($g, $sz)

    # 原版纯正经典蓝底色 RGB(35, 116, 225)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 35, 116, 225))

    # 居中纯白闪电 (替代原本的 "E")
    $padX = [float]($sz * 0.15)
    $padY = [float]($sz * 0.12)
    $pts = Get-ClassicLightningPoints $sz $sz $padX $padY

    $whiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPolygon($whiteBrush, $pts)
    $whiteBrush.Dispose()
}

# ── 2. 生成 tray.ico (纯透明底色 + 永远纯白经典闪电) ─────────────────
$trayIcoPath = Join-Path $PSScriptRoot "tray.ico"
Build-IcoFile -OutputPath $trayIcoPath -RenderCallback {
    param ($g, $sz)

    # 托盘专属：无背景色，纯透明底，纯白经典闪电
    $padX = [float]($sz * 0.08)
    $padY = [float]($sz * 0.06)
    $pts = Get-ClassicLightningPoints $sz $sz $padX $padY
    
    $whiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPolygon($whiteBrush, $pts)
    $whiteBrush.Dispose()
}
