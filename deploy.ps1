<#
.SYNOPSIS
EasyTools CI/CD 自动化部署脚本 (Idempotent Deployment Script)

.DESCRIPTION
此脚本一键完成环境检测、依赖安装、前端构建、C++ 编译和成品打包，完全幂等。

.EXAMPLE
.\deploy.ps1 -Configuration Release
#>

param (
    [string]$Configuration = "Release",
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

# 统一日志函数
function Write-Log ($Message, $Level = "INFO") {
    $TimeStamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $Color = switch ($Level) {
        "INFO" { "Cyan" }
        "WARN" { "Yellow" }
        "ERROR" { "Red" }
        "SUCCESS" { "Green" }
        default { "White" }
    }
    Write-Host "[$TimeStamp] [$Level] $Message" -ForegroundColor $Color
}

Write-Log "======================================================="
Write-Log "启动 EasyTools 一键幂等部署流程"
Write-Log "配置环境: $Configuration"
Write-Log "======================================================="

# ------------------------------------------------------------------------------
# 1. 前端构建 (React + Vite)
# ------------------------------------------------------------------------------
Write-Log "检查前端环境 (ui/)..."
if (Test-Path "ui/package.json") {
    Push-Location ui
    try {
        # 检查是否需要安装 node_modules
        if (-not (Test-Path "node_modules")) {
            Write-Log "执行 npm install..."
            npm install
        } else {
            Write-Log "前端依赖已存在，跳过 npm install (如果需要强制更新请手动删除 node_modules)"
        }

        # 始终确保打包产物最新
        Write-Log "执行 npm run build..."
        npm run build
        Write-Log "前端构建完成。" "SUCCESS"
    } catch {
        Write-Log "前端构建失败: $_" "ERROR"
        throw
    } finally {
        Pop-Location
    }
} else {
    Write-Log "未发现前端工程 (ui/package.json)，跳过前端构建。" "WARN"
}

# ------------------------------------------------------------------------------
# 2. Vcpkg 依赖安装 (Manifest Mode)
# ------------------------------------------------------------------------------
Write-Log "检查 Vcpkg 依赖环境..."
if (-not (Test-Path $VcpkgRoot)) {
    Write-Log "未检测到 Vcpkg ($VcpkgRoot)。开始克隆并安装 Vcpkg..." "WARN"
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    Push-Location $VcpkgRoot
    .\bootstrap-vcpkg.bat
    Pop-Location
} else {
    Write-Log "发现 Vcpkg 安装在: $VcpkgRoot"
}

$VcpkgToolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $VcpkgToolchain)) {
    throw "Vcpkg Toolchain 文件丢失: $VcpkgToolchain"
}

# ------------------------------------------------------------------------------
# 3. WebView2 SDK NuGet 包下载
# ------------------------------------------------------------------------------
Write-Log "检查 WebView2 SDK..."
$PackagesDir = Join-Path $ScriptDir "packages"
if (-not (Test-Path $PackagesDir)) {
    New-Item -ItemType Directory -Path $PackagesDir | Out-Null
}

$WebView2Dirs = Get-ChildItem -Path $PackagesDir -Filter "Microsoft.Web.WebView2.*" -Directory
if ($WebView2Dirs.Count -eq 0) {
    Write-Log "未发现 WebView2 SDK，开始通过 nuget.exe 下载..." "WARN"
    
    $NugetExe = Join-Path $PackagesDir "nuget.exe"
    if (-not (Test-Path $NugetExe)) {
        Write-Log "下载 nuget.exe..."
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $NugetExe
    }

    Start-Process -FilePath $NugetExe -ArgumentList "install Microsoft.Web.WebView2 -OutputDirectory `"$PackagesDir`"" -Wait -NoNewWindow
    Write-Log "WebView2 SDK 下载完成。" "SUCCESS"
} else {
    Write-Log "WebView2 SDK 已存在，跳过下载。"
}

# ------------------------------------------------------------------------------
# 4. CMake 编译 (含 vcpkg 依赖)
# ------------------------------------------------------------------------------
Write-Log "执行 CMake Configure..."
$BuildDir = Join-Path $ScriptDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" -DVCPKG_TARGET_TRIPLET="x64-windows"

Write-Log "执行 CMake Build ($Configuration)..."
cmake --build build --config $Configuration

if ($LASTEXITCODE -ne 0) {
    throw "C++ 编译失败！退出码: $LASTEXITCODE"
}
Write-Log "C++ 编译完成。" "SUCCESS"

# ------------------------------------------------------------------------------
# 5. 打包输出物 (Deploy)
# ------------------------------------------------------------------------------
Write-Log "开始提纯输出物..."
$DeployDir = Join-Path $ScriptDir "deploy_dist"
if (Test-Path $DeployDir) {
    Remove-Item -Recurse -Force $DeployDir
}
New-Item -ItemType Directory -Path $DeployDir | Out-Null

# 二进制文件
$ExePath = Join-Path $BuildDir "bin\$Configuration\EasyTools.exe"
if (-not (Test-Path $ExePath)) {
    # 尝试查找根级 bin
    $ExePath = Join-Path $BuildDir "bin\EasyTools.exe"
}

if (Test-Path $ExePath) {
    Copy-Item $ExePath -Destination $DeployDir
    Write-Log "已复制可执行文件: EasyTools.exe"
} else {
    throw "找不到编译后的 EasyTools.exe"
}

# WebView2Loader.dll
$WebView2TargetDir = Get-ChildItem -Path $PackagesDir -Filter "Microsoft.Web.WebView2.*" -Directory | Select-Object -First 1
if ($WebView2TargetDir) {
    $LoaderPath = Join-Path $WebView2TargetDir.FullName "build\native\x64\WebView2Loader.dll"
    if (Test-Path $LoaderPath) {
        Copy-Item $LoaderPath -Destination $DeployDir
        Write-Log "已复制 WebView2Loader.dll"
    }
}

# 拷贝资源文件和 UI
if (Test-Path "resources") {
    Copy-Item "resources" -Destination $DeployDir -Recurse
    Write-Log "已复制资源文件 (resources/)"
}

if (Test-Path "ui/dist") {
    $UiDeployDir = Join-Path $DeployDir "ui"
    New-Item -ItemType Directory -Path $UiDeployDir | Out-Null
    Copy-Item "ui/dist\*" -Destination $UiDeployDir -Recurse
    Write-Log "已复制前端产物 (ui/)"
}

Write-Log "======================================================="
Write-Log "EasyTools 一键部署成功！" "SUCCESS"
Write-Log "您的纯净发布版位于: $DeployDir"
Write-Log "直接双击运行 deploy_dist/EasyTools.exe 即可启动工具。"
Write-Log "======================================================="
