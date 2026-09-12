param(
    [switch]$NoRun,
    [string]$Target = "",
    [string]$Config = "Release",
    [int]$Parallel = 16
)

$ErrorActionPreference = "Stop"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

# 安全锚定当前工作目录至仓库根目录，支持从任意子路径（如 scripts/）直接运行
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) { $ScriptDir = $PSScriptRoot }
$RepoRoot = if ($ScriptDir) { Split-Path -Parent $ScriptDir } else { (Get-Location).Path }
Set-Location $RepoRoot

Write-Host "⚡ [Tools3000 Rapid Dev] 正在准备极速增量编译..." -ForegroundColor Cyan

# 1. 寻找 CMake 路径及开发环境 (安全解析，杜绝 ${env:ProgramFiles(x86)} 命令解析漏洞)
$cmakeCmd = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCmd) { $cmakeCmd.Source } else { $null }

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

# 2. 杀掉运行中的 Tools3000 并同步等待句柄完全释放
$running = Get-Process -Name "Tools3000*", "Tools3000*" -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force -ErrorAction SilentlyContinue
    $stopTimeoutMs = 3000
    $waitSw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($waitSw.ElapsedMilliseconds -lt $stopTimeoutMs) {
        $remaining = Get-Process -Name "Tools3000*", "Tools3000*" -ErrorAction SilentlyContinue
        if (-not $remaining) { break }
        Start-Sleep -Milliseconds 50
    }
    $remaining = Get-Process -Name "Tools3000*", "Tools3000*" -ErrorAction SilentlyContinue
    if ($remaining) {
        Write-Host "❌ 无法终止旧的 Tools3000 进程 ($($remaining.Name))，文件句柄仍被占用！" -ForegroundColor Red
        exit 1
    }
    # 额外微量缓冲以保证 Windows 内核解除 EXE/DLL Section 映射
    Start-Sleep -Milliseconds 50
}

# 3. 增量编译 C++ (仅编译修改的模块，毫秒级)
$buildArgs = @("--build", "build", "--config", $Config)
if ($Parallel -gt 0) {
    $buildArgs += @("--parallel", "$Parallel")
}
if ($Target) {
    $buildArgs += @("--target", $Target)
}

& $cmakeExe @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 增量编译失败！" -ForegroundColor Red
    exit 1
}

# 4. 快速同步二进制到 deploy_dist (0.1秒)
$distDir = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "deploy_dist"))
if (-not (Test-Path $distDir)) {
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null
}
$pluginsDist = Join-Path $distDir "plugins"
if (-not (Test-Path $pluginsDist)) {
    New-Item -ItemType Directory -Path $pluginsDist -Force | Out-Null
}

$binDir = Join-Path $RepoRoot "build\bin\$Config"
$primaryExe = "Tools3000.exe"
$primaryService = "Tools3000_Service.exe"

$exePath = Join-Path $binDir $primaryExe
if (Test-Path -LiteralPath $exePath) {
    Copy-Item $exePath (Join-Path $distDir "Tools3000.exe") -Force
}
$servicePath = Join-Path $binDir $primaryService
if (Test-Path -LiteralPath $servicePath) {
    Copy-Item $servicePath (Join-Path $distDir "Tools3000_Service.exe") -Force
}
$coreDll = Join-Path $binDir "Tools3000Core.dll"
if (Test-Path -LiteralPath $coreDll) {
    Copy-Item $coreDll (Join-Path $distDir "Tools3000Core.dll") -Force
}
$pluginsBin = Join-Path $binDir "plugins\$Config"
if (-not (Test-Path $pluginsBin)) {
    $pluginsBin = Join-Path $RepoRoot "build\bin\plugins\$Config"
}
if (Test-Path -LiteralPath $pluginsBin) {
    Copy-Item (Join-Path $pluginsBin "*.dll") $pluginsDist -Force
}

$sw.Stop()
Write-Host "✅ [增量编译就绪] 耗时: $($sw.ElapsedMilliseconds) ms" -ForegroundColor Green

if (-not $NoRun -and (-not $Target -or $Target -eq "Tools3000")) {
    $targetExe = Join-Path $distDir "Tools3000.exe"
    if (Test-Path -LiteralPath $targetExe) {
        Write-Host "🚀 正在启动 Tools3000 桌面客户端..." -ForegroundColor Yellow
        Start-Process -FilePath $targetExe -WorkingDirectory $distDir
    }
}
