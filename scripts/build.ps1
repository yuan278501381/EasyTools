#!/usr/bin/env pwsh
param(
    [ValidateSet("x64", "arm64")]
    [string]$Arch = "x64",

    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",

    [switch]$Package,
    [switch]$All,
    [switch]$SkipUI,
    [switch]$Clean
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

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  EasyTools v$Version - Build Script" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

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

    $BuildDir = Join-Path $ProjectRoot "build"

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Step "清理构建目录: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    if (!(Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    $cmakeExe = "cmake.exe"
    if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
        $candidates = @(
            "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        )
        foreach ($cand in $candidates) {
            if (Test-Path $cand) {
                $cmakeExe = $cand
                break
            }
        }
    }

    Write-Step "CMake Build [$TargetArch / $BuildConfig]"

    & $cmakeExe --build $BuildDir --config $BuildConfig --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Error-Exit "CMake Build 失败"
    }

    Write-Host "✓ 构建完成: $TargetArch / $BuildConfig" -ForegroundColor Green

    if ($Package) {
        Write-Step "打包发布包 [$TargetArch]"

        $OutDir = Join-Path $BuildDir "bin" $BuildConfig
        if (!(Test-Path $OutDir)) {
            $OutDir = Join-Path $BuildDir "bin"
        }

        $PackageName = "EasyTools-v${Version}-${TargetArch}"
        $PackageDir = Join-Path $ProjectRoot "dist" $PackageName
        $ZipPath = Join-Path $ProjectRoot "dist" "${PackageName}.zip"

        if (Test-Path $PackageDir) {
            Remove-Item -Recurse -Force $PackageDir
        }
        New-Item -ItemType Directory -Path $PackageDir | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $PackageDir "plugins") | Out-Null

        Copy-Item (Join-Path $OutDir "EasyTools.exe") $PackageDir -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $OutDir "EasyTools_Service.exe") $PackageDir -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $OutDir "EasyCore.dll") $PackageDir -ErrorAction SilentlyContinue

        $PluginsDir = Join-Path $OutDir "plugins" $BuildConfig
        if (-not (Test-Path $PluginsDir)) {
            $PluginsDir = Join-Path $OutDir "plugins"
        }
        if (Test-Path $PluginsDir) {
            Copy-Item (Join-Path $PluginsDir "*.dll") (Join-Path $PackageDir "plugins") -Force -ErrorAction SilentlyContinue
            Copy-Item (Join-Path $PluginsDir "*.json") (Join-Path $PackageDir "plugins") -Force -ErrorAction SilentlyContinue
        }

        $UIDist = Join-Path $OutDir "ui"
        if (Test-Path $UIDist) {
            Copy-Item $UIDist (Join-Path $PackageDir "ui") -Recurse -Force -ErrorAction SilentlyContinue
        }

        if (Test-Path $ZipPath) {
            Remove-Item -Force $ZipPath
        }
        Compress-Archive -Path "$PackageDir\*" -DestinationPath $ZipPath
        Write-Host "✓ 发布包生成: $ZipPath" -ForegroundColor Green
    }
}

if (-not $SkipUI) {
    Write-Step "构建前端 UI (Vite)"
    Push-Location $UIDir
    try {
        npm run build
        if ($LASTEXITCODE -ne 0) {
            Write-Error-Exit "UI 构建失败"
        }
        Write-Host "✓ UI 构建完成" -ForegroundColor Green
    } finally {
        Pop-Location
    }
}

if ($All) {
    Invoke-BuildArch "x64" $Config
    Invoke-BuildArch "arm64" $Config
} else {
    Invoke-BuildArch $Arch $Config
}

Write-Host "全部构建任务已完成！" -ForegroundColor Green
