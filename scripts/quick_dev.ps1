param(
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

Write-Host "⚡ [EasyTools Rapid Dev] 正在准备极速增量编译..." -ForegroundColor Cyan

# 寻找 CMake 路径
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

# 1. 杀掉运行中的 EasyTools
Get-Process -Name "EasyTools*" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# 2. 增量编译 C++ (仅编译修改的模块，毫秒级)
& $cmakeExe --build build --config Release --parallel 16
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ 增量编译失败！" -ForegroundColor Red
    exit 1
}

# 3. 快速同步二进制到 deploy_dist (0.1秒)
Copy-Item "build\bin\Release\EasyTools.exe" "deploy_dist\EasyTools.exe" -Force -ErrorAction SilentlyContinue
Copy-Item "build\bin\Release\EasyTools_Service.exe" "deploy_dist\EasyTools_Service.exe" -Force -ErrorAction SilentlyContinue
Copy-Item "build\bin\Release\EasyCore.dll" "deploy_dist\EasyCore.dll" -Force -ErrorAction SilentlyContinue
if (Test-Path "build\bin\plugins\Release") {
    Copy-Item "build\bin\plugins\Release\*.dll" "deploy_dist\plugins\" -Force -ErrorAction SilentlyContinue
}

$sw.Stop()
Write-Host "✅ [增量编译就绪] 耗时: $($sw.ElapsedMilliseconds) ms" -ForegroundColor Green

if (-not $NoRun) {
    Write-Host "🚀 正在启动 EasyTools 实时调试控制台..." -ForegroundColor Yellow
    Start-Process -FilePath "$PSScriptRoot\..\deploy_dist\EasyTools.exe" -ArgumentList "--debug"
}
