# ─────────────────────────────────────────────────────────────────────────────
# generate_icon.ps1 — 生成 EasyTools 全套 Windows 规范应用图标与纯白托盘图标
#
# 规范:
#   1. app.ico (用于任务栏、任务管理器、Alt+Tab、桌面快捷方式、安装包)
#      • 现代 Windows 11 Fluent 科技极速蓝紫渐变圆角底座 (Squircle) + 饱满纯白疾速闪电
#   2. tray.ico (用于 Windows 系统托盘区 Notification Area)
#      • 纯透明底色 + 永远纯白疾速闪电 (Pure White Speed Lightning)，占满视区，饱满粗壮，极高辨识度
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

# 绘制圆角矩形辅助函数
function Add-RoundedRectPath {
    param ($path, $rect, $radius)
    $diameter = $radius * 2
    $arc = New-Object System.Drawing.RectangleF($rect.X, $rect.Y, $diameter, $diameter)
    
    $path.AddArc($arc, 180, 90)
    $arc.X = $rect.Right - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $rect.Bottom - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $rect.X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
}

# 绘制经典 6 点极速闪电矢量坐标 (饱满锋利无缺角)
function Get-LightningPoints {
    param ([float]$w, [float]$h, [float]$padX = 0, [float]$padY = 0)
    $availW = $w - ($padX * 2)
    $availH = $h - ($padY * 2)

    return @(
        (New-Object System.Drawing.PointF ($padX + $availW * 0.60), ($padY + $availH * 0.00)), # 顶部尖峰
        (New-Object System.Drawing.PointF ($padX + $availW * 0.12), ($padY + $availH * 0.54)), # 左外折角
        (New-Object System.Drawing.PointF ($padX + $availW * 0.46), ($padY + $availH * 0.54)), # 中腰内折
        (New-Object System.Drawing.PointF ($padX + $availW * 0.40), ($padY + $availH * 1.00)), # 底部尖峰
        (New-Object System.Drawing.PointF ($padX + $availW * 0.88), ($padY + $availH * 0.46)), # 右外折角
        (New-Object System.Drawing.PointF ($padX + $availW * 0.54), ($padY + $availH * 0.46))  # 中腰内折
    )
}

# ── 1. 生成 app.ico (Fluent 蓝紫渐变底座 + 纯白疾速闪电) ────────────
$appIcoPath = Join-Path $PSScriptRoot "app.ico"
Build-IcoFile -OutputPath $appIcoPath -RenderCallback {
    param ($g, $sz)

    $margin = [float]($sz * 0.04)
    $rect = New-Object System.Drawing.RectangleF($margin, $margin, ($sz - $margin * 2), ($sz - $margin * 2))
    $radius = [float]($sz * 0.22)

    $bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-RoundedRectPath $bgPath $rect $radius

    # 极速科技蓝紫渐变底座
    $bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF $rect.Left, $rect.Top),
        (New-Object System.Drawing.PointF $rect.Right, $rect.Bottom),
        [System.Drawing.Color]::FromArgb(255, 37, 99, 235),    # Royal Blue #2563eb
        [System.Drawing.Color]::FromArgb(255, 79, 70, 229)     # Hyper Indigo #4f46e5
    )
    $g.FillPath($bgBrush, $bgPath)
    $bgBrush.Dispose()

    # 细微高光边框
    if ($sz -ge 32) {
        $strokePen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(80, 255, 255, 255), 1.0)
        $g.DrawPath($strokePen, $bgPath)
        $strokePen.Dispose()
    }
    $bgPath.Dispose()

    # 居中纯白疾速闪电 (饱满且居中，占 76% 高度)
    $padX = [float]($sz * 0.16)
    $padY = [float]($sz * 0.12)
    $pts = Get-LightningPoints $sz $sz $padX $padY
    $whiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPolygon($whiteBrush, $pts)
    $whiteBrush.Dispose()
}

# ── 2. 生成 tray.ico (纯透明底色 + 永远纯白疾速闪电) ─────────────────
$trayIcoPath = Join-Path $PSScriptRoot "tray.ico"
Build-IcoFile -OutputPath $trayIcoPath -RenderCallback {
    param ($g, $sz)

    # 托盘专属：无厚重底座，纯白极简前冲闪电，占满 92% 高度与宽度
    $padX = [float]($sz * 0.08)
    $padY = [float]($sz * 0.04)
    $pts = Get-LightningPoints $sz $sz $padX $padY
    
    $whiteBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPolygon($whiteBrush, $pts)
    $whiteBrush.Dispose()
}
