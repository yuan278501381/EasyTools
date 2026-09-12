param(
    [string]$Target = "Tools3000Tests",
    [string]$Config = "Release",
    [int]$Parallel = 16,
    [switch]$Run,
    [string]$Filter = ""
)

$ErrorActionPreference = "Stop"

# 安全锚定当前工作目录至仓库根目录，支持从任意子路径（如 scripts/）直接运行
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) { $ScriptDir = $PSScriptRoot }
$RepoRoot = if ($ScriptDir) { Split-Path -Parent $ScriptDir } else { (Get-Location).Path }
Set-Location $RepoRoot

# 安全定位 ProgramFiles(x86) 与 vswhere，规避 ${env:ProgramFiles(x86)} 在嵌套命令/多层引号下的语法解析漏洞
$pf86 = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::ProgramFilesX86)
if (-not $pf86) { $pf86 = [System.Environment]::GetEnvironmentVariable('ProgramFiles(x86)') }
if (-not $pf86) { $pf86 = [System.Environment]::GetEnvironmentVariable('ProgramFiles') }

$vswhere = $null
$vswhereCmd = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
if ($vswhereCmd) {
    $vswhere = $vswhereCmd.Source
} elseif ($pf86) {
    $cand = Join-Path $pf86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $cand) { $vswhere = $cand }
}
if (-not $vswhere) {
    $cand64 = "C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $cand64) { $vswhere = $cand64 }
}

$vsPath = $null
if ($vswhere -and (Test-Path -LiteralPath $vswhere)) {
    $rawVsPath = & $vswhere -latest -products * -property installationPath
    if ($rawVsPath) {
        $vsPath = ($rawVsPath | Where-Object { $_ -and -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
        if ($vsPath) { $vsPath = $vsPath.Trim() }
    }
}

# 挂载 VS 编译环境（若尚未挂载 cl.exe）
if ($vsPath -and (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue))) {
    $devShell = Join-Path ([string]$vsPath) 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (Test-Path -LiteralPath $devShell) {
        try {
            Import-Module $devShell -ErrorAction SilentlyContinue
            if (Get-Command "Enter-VsDevShell" -ErrorAction SilentlyContinue) {
                Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' -ErrorAction SilentlyContinue | Out-Null
            }
        } catch {
            Write-Warning "挂载 Visual Studio 开发环境时发生异常: $_"
        }
    }
}

$cmakeCmd = Get-Command "cmake" -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCmd) { $cmakeCmd.Source } else { $null }

if (-not $cmakeExe -and $vsPath) {
    $vsCMake = Join-Path ([string]$vsPath) "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $vsCMake) { $cmakeExe = $vsCMake }
}

if (-not $cmakeExe) {
    $candidates = @()
    if ($pf86) {
        $candidates += Join-Path $pf86 "Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        $candidates += Join-Path $pf86 "Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    $candidates += @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($cand in $candidates) {
        if (Test-Path -LiteralPath $cand) {
            $cmakeExe = $cand
            break
        }
    }
}

if (-not $cmakeExe) {
    $cmakeExe = "cmake.exe"
}

$buildDir = Join-Path $RepoRoot "build"
if (-not (Test-Path -LiteralPath $buildDir)) {
    Write-Host "❌ 尚未初始化构建目录: $buildDir`n请先在仓库根目录运行 .\deploy.ps1 进行完整环境配置。" -ForegroundColor Red
    exit 1
}

$buildArgs = @("--build", "build", "--config", $Config)
if ($Target) {
    $buildArgs += @("--target", $Target)
}
if ($Parallel -gt 0) {
    $buildArgs += @("--parallel", "$Parallel")
}
$buildArgs += @("--", "/nr:false")
$env:MSBUILDDISABLENODEREUSE = "1"

& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($Run) {
    $candidatePaths = @(
        (Join-Path $RepoRoot "build\bin\$Config\$Target.exe"),
        (Join-Path $RepoRoot "build\bin\$Target.exe"),
        (Join-Path $RepoRoot "build\$Config\$Target.exe")
    )
    $testExe = $null
    foreach ($cand in $candidatePaths) {
        if (Test-Path -LiteralPath $cand) {
            $testExe = $cand
            break
        }
    }
    if ($testExe) {
        Write-Host "🚀 正在运行测试目标: $testExe" -ForegroundColor Cyan
        $runArgs = @("--gtest_brief=1")
        if ($Filter) {
            $runArgs += @("--gtest_filter=$Filter")
        }
        & $testExe @runArgs
        exit $LASTEXITCODE
    } else {
        Write-Host "❌ 未找到目标可执行文件: $Target.exe" -ForegroundColor Red
        exit 1
    }
}
