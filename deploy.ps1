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

$TraceID = [guid]::NewGuid().ToString("N").Substring(0, 8)
$LogDir = Join-Path $ScriptDir "deploy_logs"
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }
$LogFile = Join-Path $LogDir "deploy_$(Get-Date -Format 'yyyyMMdd').log"

# 统一日志函数
function Write-Log ($Message, $Level = "INFO") {
    $TimeStamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $LogStr = "[$TimeStamp] [$TraceID] [$Level] $Message"
    $Color = switch ($Level) {
        "INFO" { "Cyan" }
        "WARN" { "Yellow" }
        "ERROR" { "Red" }
        "SUCCESS" { "Green" }
        default { "White" }
    }
    Write-Host "[$TimeStamp] [$Level] $Message" -ForegroundColor $Color
    Add-Content -Path $LogFile -Value $LogStr -Encoding UTF8
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
        if ($LASTEXITCODE -ne 0) {
            throw "npm run build 失败，退出码: $LASTEXITCODE"
        }
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
# 4. CMake 编译 (带 vcpkg 依赖)
# ------------------------------------------------------------------------------
# 如果环境中没有 cmake，则尝试自动装载 VS 开发者环境
if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
    Write-Log "未在 PATH 中找到 CMake，尝试自动挂载 Visual Studio 开发者环境..."
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Workload.VCTools -property installationPath
        if ($vsPath) {
            $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
            if (Test-Path $devShell) {
                Import-Module $devShell
                Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64" | Out-Null
                Write-Log "✅ 成功挂载 VS 编译环境 ($vsPath)!"
            }
        }
    }
}

if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue)) {
    throw "仍然无法找到 CMake！请确保已安装 C++ 桌面开发工作负载，或尝试在 'Developer PowerShell for VS' 窗口中运行此脚本。"
}

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
# 4.5 运行单元测试 (失败则中断流水线)
# ------------------------------------------------------------------------------
$TestExe = Join-Path $BuildDir "bin\$Configuration\EasyToolsTests.exe"
if (-not (Test-Path $TestExe)) {
    $TestExe = Join-Path $BuildDir "bin\EasyToolsTests.exe"
}
if (Test-Path $TestExe) {
    Write-Log "运行单元测试..."
    & $TestExe
    if ($LASTEXITCODE -ne 0) {
        throw "单元测试失败！退出码: $LASTEXITCODE"
    }
    Write-Log "单元测试通过。" "SUCCESS"
} else {
    Write-Log "未找到单元测试可执行文件 ($TestExe)，跳过。" "WARN"
}

# ------------------------------------------------------------------------------
# 5. 打包输出物 (Deploy)
# ------------------------------------------------------------------------------
Write-Log "开始提纯输出物并写入暂存区..."
$DeployDir = Join-Path $ScriptDir "deploy_dist"
$StagingDir = Join-Path $ScriptDir "deploy_dist_staging_$TraceID"

if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
New-Item -ItemType Directory -Path $StagingDir | Out-Null

# 二进制文件
$ExePath = Join-Path $BuildDir "bin\$Configuration\EasyTools.exe"
if (-not (Test-Path $ExePath)) {
    # 尝试查找根级 bin
    $ExePath = Join-Path $BuildDir "bin\EasyTools.exe"
}

if (Test-Path $ExePath) {
    Copy-Item $ExePath -Destination $StagingDir
    Write-Log "已复制可执行文件: EasyTools.exe"
    
    # 复制所有同一目录下的 DLL 文件 (vcpkg 的依赖如 spdlog.dll 等)
    $ExeDir = Split-Path $ExePath -Parent
    $DllFiles = Join-Path $ExeDir "*.dll"
    if (Test-Path $DllFiles) {
        Copy-Item $DllFiles -Destination $StagingDir
        Write-Log "已复制依赖库 DLL 文件"
    }
    
    # 复制插件目录
    $PluginsDir = Join-Path $BuildDir "bin\plugins\$Configuration"
    if (Test-Path $PluginsDir) {
        $TargetPluginsDir = Join-Path $StagingDir "plugins"
        New-Item -ItemType Directory -Path $TargetPluginsDir -ErrorAction SilentlyContinue | Out-Null
        Copy-Item "$PluginsDir\*" -Destination $TargetPluginsDir -Recurse
        Write-Log "已复制插件与相关依赖 (plugins/)"
    } else {
        Write-Log "未找到插件构建目录: $PluginsDir" "WARN"
    }
} else {
    throw "找不到编译后的 EasyTools.exe"
}

# WebView2Loader.dll
$WebView2TargetDir = Get-ChildItem -Path $PackagesDir -Filter "Microsoft.Web.WebView2.*" -Directory | Select-Object -First 1
if ($WebView2TargetDir) {
    $LoaderPath = Join-Path $WebView2TargetDir.FullName "build\native\x64\WebView2Loader.dll"
    if (Test-Path $LoaderPath) {
        Copy-Item $LoaderPath -Destination $StagingDir
        Write-Log "已复制 WebView2Loader.dll"
    }
}

# 拷贝资源文件和 UI
if (Test-Path "resources") {
    Copy-Item "resources" -Destination $StagingDir -Recurse
    Write-Log "已复制资源文件 (resources/)"
}

if (Test-Path "ui/dist") {
    $UiDeployDir = Join-Path $StagingDir "ui"
    New-Item -ItemType Directory -Path $UiDeployDir | Out-Null
    Copy-Item "ui/dist\*" -Destination $UiDeployDir -Recurse
    Write-Log "已复制前端产物 (ui/)"
}

# ------------------------------------------------------------------------------
# 6. 优雅关闭及原子交换 (Atomic Swap)
# ------------------------------------------------------------------------------
Write-Log "开始执行原子目录交换..."

# 优雅停止可能正在运行的 EasyTools 进程
$runningProcesses = Get-Process -Name "EasyTools*" -ErrorAction SilentlyContinue
if ($runningProcesses) {
    Write-Log "检测到 EasyTools 进程正在运行，尝试发送优雅关闭信号 (CloseMainWindow)..." "WARN"
    foreach ($p in $runningProcesses) {
        try {
            $p.CloseMainWindow() | Out-Null
        } catch { }
    }
    
    # 等待最多 3 秒让其优雅退出
    $waited = 0
    while ((Get-Process -Name "EasyTools*" -ErrorAction SilentlyContinue) -and $waited -lt 3) {
        Start-Sleep -Seconds 1
        $waited++
    }
    
    $remaining = Get-Process -Name "EasyTools*" -ErrorAction SilentlyContinue
    if ($remaining) {
        Write-Log "进程未在规定时间内退出，执行强制关闭 (Force Stop)..." "WARN"
        $remaining | Stop-Process -Force
        Start-Sleep -Seconds 1
    }
}

# 备份旧版并上线新版
$BackupDir = Join-Path $ScriptDir "deploy_dist_backup"
if (Test-Path $DeployDir) {
    if (Test-Path $BackupDir) {
        Remove-Item -Recurse -Force $BackupDir
    }
    Rename-Item -Path $DeployDir -NewName "deploy_dist_backup"
    Write-Log "旧版本已安全备份到: deploy_dist_backup"
}

Rename-Item -Path $StagingDir -NewName "deploy_dist"
Write-Log "新版本秒级切换上线完成。" "SUCCESS"

# ------------------------------------------------------------------------------
# 7. 生成 Windows 安装程序 (Inno Setup)
# ------------------------------------------------------------------------------
Write-Log "检查是否可以生成安装程序 (Inno Setup)..."
$InnoSetupDirs = @(
    "C:\Program Files (x86)\Inno Setup 6",
    "C:\Program Files\Inno Setup 6",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6"
)
$ISCC = $null
foreach ($dir in $InnoSetupDirs) {
    if (Test-Path "$dir\ISCC.exe") {
        $ISCC = "$dir\ISCC.exe"
        break
    }
}

if ($ISCC) {
    Write-Log "找到 Inno Setup 编译器: $ISCC"
    $InstallerScript = Join-Path $ScriptDir "installer.iss"
    $OutputInstallerDir = Join-Path $ScriptDir "Output"
    
    if (Test-Path $InstallerScript) {
        Write-Log "正在编译安装包 (EasyTools-Setup.exe)..."
        & $ISCC $InstallerScript
        if ($LASTEXITCODE -eq 0) {
            Write-Log "安装包已成功生成到: $OutputInstallerDir" "SUCCESS"
        } else {
            Write-Log "安装包编译失败！" "WARN"
        }
    } else {
        Write-Log "未找到安装脚本 $InstallerScript" "WARN"
    }
} else {
    Write-Log "未找到 Inno Setup 编译器，跳过安装包生成步骤。" "WARN"
}

Write-Log "======================================================="
Write-Log "EasyTools 一键原子部署成功！" "SUCCESS"
Write-Log "您的纯净发布版位于: $DeployDir"
if (Test-Path (Join-Path $ScriptDir "Output\EasyTools-Setup.exe")) {
    Write-Log "您的安装包位于: $(Join-Path $ScriptDir "Output\EasyTools-Setup.exe")"
}
Write-Log "全链路 TraceID: $TraceID (详见 deploy_logs)"
Write-Log "直接双击运行 deploy_dist/EasyTools.exe 即可启动工具。"
Write-Log "======================================================="
