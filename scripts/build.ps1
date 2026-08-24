#!/usr/bin/env pwsh
# ─────────────────────────────────────────────────────────────────────────────
# EasyTools CI/CD Build & Deploy Script
#
# 支持:
#   - x64 和 ARM64 双架构交叉编译
#   - Debug / Release / RelWithDebInfo 构建类型
#   - UI (Vite) 自动构建
#   - 自动打包 ZIP 发布包
#   - vcpkg 依赖管理
#
# 用法:
#   .\scripts\build.ps1 -Arch x64 -Config Release
#   .\scripts\build.ps1 -Arch arm64 -Config Release -Package
#   .\scripts\build.ps1 -All    # 构建 x64 + ARM64
# ─────────────────────────────────────────────────────────────────────────────

param(
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",

    [switch]$Package,   # 是否打包 ZIP
    [switch]$All,       # 构建所有架构
    [switch]$SkipUI,    # 跳过 UI 构建
    [switch]$Clean      # 清理构建目录
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$UIDir = Join-Path $ProjectRoot "ui"
$VersionFile = Join-Path $ProjectRoot "VERSION"
if (-not (Test-Path -LiteralPath $VersionFile)) {
    throw "缺少唯一版本源: $VersionFile"
}
$Version = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION 必须是稳定 SemVer（例如 1.2.3），当前值: $Version"
}

Write-Host "╔═══════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  EasyTools v$Version - Build Script                           ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan

# ── 辅助函数 ──────────────────────────────────────────────────────────────────

function Write-Step {
    param([string]$Message)
    Write-Host "`n▶ $Message" -ForegroundColor Green
}

function Write-Error-Exit {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
    exit 1
}

function Invoke-BuildArch {
    param(
        [string]$TargetArch,
        [string]$BuildConfig
    )

    $BuildDir = Join-Path $ProjectRoot "build-$TargetArch"

    # 清理
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Step "清理构建目录: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    # 创建构建目录
    if (!(Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    # ── CMake Configure ──────────────────────────────────────────────────
    Write-Step "CMake Configure [$TargetArch / $BuildConfig]"

    $CmakeArgs = @(
        "-B", $BuildDir,
        "-S", $ProjectRoot,
        "-G", "Visual Studio 17 2022",
        "-A", $(if ($TargetArch -eq "arm64") { "ARM64" } else { "x64" }),
        "-DCMAKE_BUILD_TYPE=$BuildConfig",
        "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    )

    if ($TargetArch -eq "arm64") {
        $CmakeArgs += "-DVCPKG_TARGET_TRIPLET=arm64-windows"
    } else {
        $CmakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
    }

    cmake @CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Exit "CMake Configure 失败"
    }

    # ── CMake Build ──────────────────────────────────────────────────────
    Write-Step "CMake Build [$TargetArch / $BuildConfig]"

    cmake --build $BuildDir --config $BuildConfig --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Exit "CMake Build 失败"
    }

    Write-Host "✓ 构建完成: $TargetArch / $BuildConfig" -ForegroundColor Green

    # ── 打包 ─────────────────────────────────────────────────────────────
    if ($Package) {
        Write-Step "打包发布包 [$TargetArch]"

        $OutDir = Join-Path $BuildDir "bin" $BuildConfig
        if (!(Test-Path $OutDir)) {
            # 某些生成器不会创建 Config 子目录
            $OutDir = Join-Path $BuildDir "bin"
        }

        $PackageName = "EasyTools-v${Version}-${TargetArch}"
        $PackageDir = Join-Path $ProjectRoot "dist" $PackageName
        $ZipPath = Join-Path $ProjectRoot "dist" "${PackageName}.zip"

        # 创建发布目录
        if (Test-Path $PackageDir) {
            Remove-Item -Recurse -Force $PackageDir
        }
        New-Item -ItemType Directory -Path $PackageDir | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $PackageDir "plugins") | Out-Null

        # 复制文件
        Copy-Item (Join-Path $OutDir "EasyTools.exe") $PackageDir
        Copy-Item (Join-Path $OutDir "EasyTools_Service.exe") $PackageDir
        Copy-Item (Join-Path $OutDir "EasyCore.dll") $PackageDir

        # 插件
        $PluginsDir = Join-Path $OutDir "plugins"
        if (Test-Path $PluginsDir) {
            Copy-Item (Join-Path $PluginsDir "*") (Join-Path $PackageDir "plugins") -Recurse
        }

        # UI 资产
        $UIDist = Join-Path $OutDir "ui"
        if (Test-Path $UIDist) {
            Copy-Item $UIDist (Join-Path $PackageDir "ui") -Recurse
        }

        # 资源文件
        $ResourcesDir = Join-Path $ProjectRoot "resources"
        if (Test-Path $ResourcesDir) {
            Copy-Item $ResourcesDir (Join-Path $PackageDir "resources") -Recurse
        }

        # 压缩
        if (Test-Path $ZipPath) { Remove-Item $ZipPath }
        Compress-Archive -Path $PackageDir -DestinationPath $ZipPath
        Write-Host "✓ 发布包: $ZipPath" -ForegroundColor Green
    }
}

# ── UI 构建 ───────────────────────────────────────────────────────────────────

if (!$SkipUI) {
    Write-Step "构建 WebView2 UI (Vite)"

    Push-Location $UIDir
    try {
        if (!(Test-Path "node_modules")) {
            Write-Host "  安装依赖..."
            npm ci --prefer-offline
            if ($LASTEXITCODE -ne 0) {
                Write-Error-Exit "npm ci 失败"
            }
        }

        npm run build
        if ($LASTEXITCODE -ne 0) {
            Write-Error-Exit "UI 构建失败"
        }
        Write-Host "✓ UI 构建完成" -ForegroundColor Green
    } finally {
        Pop-Location
    }
}

# ── 执行构建 ──────────────────────────────────────────────────────────────────

if ($All) {
    Write-Step "构建所有架构"
    Invoke-BuildArch -TargetArch "x64" -BuildConfig $Config
    Invoke-BuildArch -TargetArch "arm64" -BuildConfig $Config
} else {
    Invoke-BuildArch -TargetArch $Arch -BuildConfig $Config
}

# ── 完成 ──────────────────────────────────────────────────────────────────────
Write-Host "`n═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  构建全部完成!" -ForegroundColor Green
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
