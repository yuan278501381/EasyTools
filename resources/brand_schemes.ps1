param (
    [int]$ApplyScheme = 1
)

# ─────────────────────────────────────────────────────────────────────────────
# brand_schemes.ps1 — 生成 方案 1 (Aero-Glide E) 与 方案 4 (Sonic Loop e) 官方资产
# ─────────────────────────────────────────────────────────────────────────────

Add-Type -AssemblyName System.Drawing

function Build-IcoFileFromRenderer {
    param (
        [string]$OutputPath,
        [scriptblock]$RenderCallback,
        [int[]]$Sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
    )

    $imagesData = @()
    $headerSize = 6 + ($Sizes.Length * 16)
    $currentOffset = $headerSize

    foreach ($sz in $Sizes) {
        $scale = 4
        $canvasSz = $sz * $scale
        $bmpHi = New-Object System.Drawing.Bitmap($canvasSz, $canvasSz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmpHi)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)

        & $RenderCallback $g $canvasSz

        $g.Dispose()

        # 4x 超采样下采样回目标尺寸
        $finalBmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $gFinal = [System.Drawing.Graphics]::FromImage($finalBmp)
        $gFinal.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $gFinal.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $gFinal.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $gFinal.DrawImage($bmpHi, (New-Object System.Drawing.Rectangle(0, 0, $sz, $sz)), 0, 0, $canvasSz, $canvasSz, [System.Drawing.GraphicsUnit]::Pixel)
        $gFinal.Dispose()
        $bmpHi.Dispose()

        $rect = New-Object System.Drawing.Rectangle(0, 0, $sz, $sz)
        $data = $finalBmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = $data.Stride
        $pixels = New-Object byte[] ($stride * $sz)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
        $finalBmp.UnlockBits($data)
        $finalBmp.Dispose()

        $andRowBytes = [int][Math]::Floor((($sz + 31) / 32)) * 4
        $xorBytes = $sz * $sz * 4
        $andBytes = $sz * $andRowBytes
        $imageBytes = 40 + $xorBytes + $andBytes

        $msImg = New-Object System.IO.MemoryStream
        $bwImg = New-Object System.IO.BinaryWriter($msImg)

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

        for ($y = $sz - 1; $y -ge 0; $y--) {
            $bwImg.Write($pixels, $y * $stride, $sz * 4)
        }

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

    $msFile = New-Object System.IO.MemoryStream
    $bwFile = New-Object System.IO.BinaryWriter($msFile)

    $bwFile.Write([uint16]0)
    $bwFile.Write([uint16]1)
    $bwFile.Write([uint16]$Sizes.Length)

    foreach ($entry in $imagesData) {
        $wByte = if ($entry.Size -eq 256) { [byte]0 } else { [byte]$entry.Size }
        $hByte = if ($entry.Size -eq 256) { [byte]0 } else { [byte]$entry.Size }
        $bwFile.Write($wByte)
        $bwFile.Write($hByte)
        $bwFile.Write([byte]0)
        $bwFile.Write([byte]0)
        $bwFile.Write([uint16]1)
        $bwFile.Write([uint16]32)
        $bwFile.Write([uint32]$entry.ImageBytes)
        $bwFile.Write([uint32]$entry.Offset)
    }

    foreach ($entry in $imagesData) {
        $bwFile.Write($entry.Data, 0, $entry.Data.Length)
    }

    $bwFile.Flush()
    [System.IO.File]::WriteAllBytes($OutputPath, $msFile.ToArray())
    $msFile.Dispose()

    Write-Output "ICO Generated: $OutputPath ($($Sizes.Length) sizes)"
}

function Add-SquircleRect {
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

# ─────────────────────────────────────────────────────────────────────────────
# 方案 1: 【超速滑翔光翼 "E" · Aero-Glide 'E'】
# ─────────────────────────────────────────────────────────────────────────────
function Get-AeroGlidePath {
    param ([float]$sz, [float]$padX = 0, [float]$padY = 0)
    $bw = $sz - ($padX * 2)
    $bh = $sz - ($padY * 2)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath

    # 1. 顶层滑翔翼 (Top Wing)
    $pTop = New-Object System.Drawing.Drawing2D.GraphicsPath
    [System.Drawing.PointF[]]$topPts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.38), ($padY + $bh * 0.16)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.72), ($padY + $bh * 0.16)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.64), ($padY + $bh * 0.34)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.32), ($padY + $bh * 0.34))
    )
    $pTop.AddPolygon($topPts)

    # 2. 中层穿梭翼 (Middle Wing)
    $pMid = New-Object System.Drawing.Drawing2D.GraphicsPath
    [System.Drawing.PointF[]]$midPts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.30), ($padY + $bh * 0.44)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.68), ($padY + $bh * 0.44)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.62), ($padY + $bh * 0.60)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.24), ($padY + $bh * 0.60))
    )
    $pMid.AddPolygon($midPts)

    # 3. 底层滑行翼 (Bottom Wing)
    $pBot = New-Object System.Drawing.Drawing2D.GraphicsPath
    [System.Drawing.PointF[]]$botPts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.22), ($padY + $bh * 0.70)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.68), ($padY + $bh * 0.70)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.60), ($padY + $bh * 0.86)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.14), ($padY + $bh * 0.86))
    )
    $pBot.AddPolygon($botPts)

    # 4. 倾斜动能主脊梁 (Inclined Spine)
    $pSpine = New-Object System.Drawing.Drawing2D.GraphicsPath
    [System.Drawing.PointF[]]$spinePts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.38), ($padY + $bh * 0.16)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.48), ($padY + $bh * 0.16)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.24), ($padY + $bh * 0.86)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.14), ($padY + $bh * 0.86))
    )
    $pSpine.AddPolygon($spinePts)

    $path.AddPath($pTop, $false)
    $path.AddPath($pMid, $false)
    $path.AddPath($pBot, $false)
    $path.AddPath($pSpine, $false)
    return $path
}

# 渲染 方案 1 App
function Render-Scheme1-App {
    param ($g, $sz)
    $margin = [float]($sz * 0.04)
    $rect = New-Object System.Drawing.RectangleF($margin, $margin, ($sz - $margin * 2), ($sz - $margin * 2))
    $radius = [float]($sz * 0.22)

    $bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-SquircleRect $bgPath $rect $radius
    $bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF $rect.Left, $rect.Top),
        (New-Object System.Drawing.PointF $rect.Right, $rect.Bottom),
        [System.Drawing.Color]::FromArgb(255, 56, 189, 248),  # #38BDF8 天空蓝
        [System.Drawing.Color]::FromArgb(255, 99, 102, 241)   # #6366F1 极光紫
    )
    $g.FillPath($bgBrush, $bgPath)
    $bgBrush.Dispose()

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(60, 255, 255, 255), [float]($sz * 0.015))
    $g.DrawPath($pen, $bgPath)
    $pen.Dispose()
    $bgPath.Dispose()

    # 主体 E
    $padX = [float]($sz * 0.16)
    $padY = [float]($sz * 0.12)
    $pE = Get-AeroGlidePath $sz $padX $padY

    # 3D 柔和微投影
    $matrix = New-Object System.Drawing.Drawing2D.Matrix
    $matrix.Translate(0, [float]($sz * 0.015))
    $pShadow = $pE.Clone()
    $pShadow.Transform($matrix)
    $sBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(40, 0, 0, 0))
    $g.FillPath($sBrush, $pShadow)
    $sBrush.Dispose()
    $pShadow.Dispose()
    $matrix.Dispose()

    # 纯白主体
    $wBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPath($wBrush, $pE)
    $wBrush.Dispose()
    $pE.Dispose()
}

# 渲染 方案 1 Tray
function Render-Scheme1-Tray {
    param ($g, $sz)
    $padX = [float]($sz * 0.04)
    $padY = [float]($sz * 0.04)
    $pE = Get-AeroGlidePath $sz $padX $padY
    $wBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPath($wBrush, $pE)
    $wBrush.Dispose()
    $pE.Dispose()
}

# ─────────────────────────────────────────────────────────────────────────────
# 方案 4: 【莫比乌斯流体尾迹 "e" · Sonic Loop 'e'】
# ─────────────────────────────────────────────────────────────────────────────
function Get-SonicLoopPath {
    param ([float]$sz, [float]$padX = 0, [float]$padY = 0)
    $bw = $sz - ($padX * 2)
    $bh = $sz - ($padY * 2)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath

    # 绘制连续环绕的动能流体 e 路径
    # 外环与飞扬尾迹 (Outer loop & flying checkmark tail)
    [System.Drawing.PointF[]]$pts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.50), ($padY + $bh * 0.12)), # 顶部中心
        (New-Object System.Drawing.PointF ($padX + $bw * 0.20), ($padY + $bh * 0.30)), # 左上弧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.16), ($padY + $bh * 0.55)), # 左腰
        (New-Object System.Drawing.PointF ($padX + $bw * 0.28), ($padY + $bh * 0.82)), # 左下弧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.55), ($padY + $bh * 0.88)), # 底部回旋
        (New-Object System.Drawing.PointF ($padX + $bw * 0.88), ($padY + $bh * 0.32)), # 飞扬破空尾尖
        (New-Object System.Drawing.PointF ($padX + $bw * 0.72), ($padY + $bh * 0.65)), # 尾翼内侧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.50), ($padY + $bh * 0.72)), # 内底弧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.34), ($padY + $bh * 0.58)), # 内左弧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.76), ($padY + $bh * 0.44)), # 中梁右端
        (New-Object System.Drawing.PointF ($padX + $bw * 0.72), ($padY + $bh * 0.26)), # 右上弧
        (New-Object System.Drawing.PointF ($padX + $bw * 0.50), ($padY + $bh * 0.12))  # 闭合回顶
    )
    $path.AddClosedCurve($pts, 0.4)

    # 减去中央眼眶 (Eye inner hole)
    [System.Drawing.PointF[]]$holePts = @(
        (New-Object System.Drawing.PointF ($padX + $bw * 0.50), ($padY + $bh * 0.25)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.36), ($padY + $bh * 0.35)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.36), ($padY + $bh * 0.45)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.62), ($padY + $bh * 0.45)),
        (New-Object System.Drawing.PointF ($padX + $bw * 0.60), ($padY + $bh * 0.32))
    )
    $holePath = New-Object System.Drawing.Drawing2D.GraphicsPath
    $holePath.AddClosedCurve($holePts, 0.3)
    $path.AddPath($holePath, $false)
    $holePath.Dispose()

    return $path
}

# 渲染 方案 4 App
function Render-Scheme4-App {
    param ($g, $sz)
    $margin = [float]($sz * 0.04)
    $rect = New-Object System.Drawing.RectangleF($margin, $margin, ($sz - $margin * 2), ($sz - $margin * 2))
    $radius = [float]($sz * 0.22)

    $bgPath = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-SquircleRect $bgPath $rect $radius
    $bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush (
        (New-Object System.Drawing.PointF $rect.Left, $rect.Top),
        (New-Object System.Drawing.PointF $rect.Right, $rect.Bottom),
        [System.Drawing.Color]::FromArgb(255, 56, 189, 248),  # #38BDF8 天空蓝
        [System.Drawing.Color]::FromArgb(255, 124, 58, 237)  # #7C3AED 极光紫
    )
    $g.FillPath($bgBrush, $bgPath)
    $bgBrush.Dispose()

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(60, 255, 255, 255), [float]($sz * 0.015))
    $g.DrawPath($pen, $bgPath)
    $pen.Dispose()
    $bgPath.Dispose()

    $padX = [float]($sz * 0.14)
    $padY = [float]($sz * 0.12)
    $pe = Get-SonicLoopPath $sz $padX $padY

    # 3D 投影
    $matrix = New-Object System.Drawing.Drawing2D.Matrix
    $matrix.Translate(0, [float]($sz * 0.015))
    $pShadow = $pe.Clone()
    $pShadow.Transform($matrix)
    $sBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(40, 0, 0, 0))
    $g.FillPath($sBrush, $pShadow)
    $sBrush.Dispose()
    $pShadow.Dispose()
    $matrix.Dispose()

    $wBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPath($wBrush, $pe)
    $wBrush.Dispose()
    $pe.Dispose()
}

# 渲染 方案 4 Tray
function Render-Scheme4-Tray {
    param ($g, $sz)
    $padX = [float]($sz * 0.03)
    $padY = [float]($sz * 0.03)
    $pe = Get-SonicLoopPath $sz $padX $padY
    $wBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 255, 255, 255))
    $g.FillPath($wBrush, $pe)
    $wBrush.Dispose()
    $pe.Dispose()
}

# ─────────────────────────────────────────────────────────────────────────────
# 执行分发生成
# ─────────────────────────────────────────────────────────────────────────────
if ($ApplyScheme -eq 1) {
    Write-Output ">> 正在装配 方案 1 (Aero-Glide E)..."
    Build-IcoFileFromRenderer (Join-Path $PSScriptRoot "app.ico") ${function:Render-Scheme1-App}
    Build-IcoFileFromRenderer (Join-Path $PSScriptRoot "tray.ico") ${function:Render-Scheme1-Tray}
} elseif ($ApplyScheme -eq 4) {
    Write-Output ">> 正在装配 方案 4 (Sonic Loop e)..."
    Build-IcoFileFromRenderer (Join-Path $PSScriptRoot "app.ico") ${function:Render-Scheme4-App}
    Build-IcoFileFromRenderer (Join-Path $PSScriptRoot "tray.ico") ${function:Render-Scheme4-Tray}
}
