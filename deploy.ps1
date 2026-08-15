<#
.SYNOPSIS
EasyTools CI/CD 自动化部署脚本 (Idempotent Deployment Script)

.DESCRIPTION
此脚本一键完成环境检测、依赖安装、前端构建、C++ 编译和成品打包，完全幂等。支持 sccache 极速编译与 vcpkg 二进制缓存。

.EXAMPLE
.\deploy.ps1 -Configuration Release
.\deploy.ps1 -Quick
#>

param (
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$VcpkgRoot = "C:\vcpkg",
    [switch]$Quick = $false,             # 极速增量开发模式 (跳过 npm ci，复用 node_modules 直奔编译与测试)
    [switch]$SkipTests = $false,         # 跳过 CTest 单元测试
    [switch]$SkipInstaller = $false,     # 跳过 Inno Setup 安装包生成
    [string]$BinaryCacheDir = ""         # 自定义 vcpkg 二进制包缓存目录
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$CMakeText = Get-Content (Join-Path $ScriptDir "CMakeLists.txt") -Raw
if ($CMakeText -notmatch '(?s)project\s*\(\s*EasyTools\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "无法从 CMakeLists.txt 读取 EasyTools 版本号"
}
$ProjectVersion = $Matches[1]
$WebView2Version = "1.0.4022.49"

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
        $UiPackage = Get-Content "package.json" -Raw | ConvertFrom-Json
        if ($UiPackage.version -ne $ProjectVersion) {
            throw "前端版本 $($UiPackage.version) 与项目版本 $ProjectVersion 不一致"
        }

        if (-not $Quick -or -not (Test-Path "node_modules")) {
            Write-Log "执行 npm ci (锁定依赖)..."
            npm ci --prefer-offline --no-audit
            if ($LASTEXITCODE -ne 0) { throw "npm ci 失败，退出码: $LASTEXITCODE" }
        } else {
            Write-Log "⚡ 极速模式: 复用本地 node_modules 依赖" "INFO"
        }

        foreach ($Command in @("lint", "i18n-check", "build")) {
            Write-Log "执行 npm run $Command..."
            npm run $Command
            if ($LASTEXITCODE -ne 0) {
                throw "npm run $Command 失败，退出码: $LASTEXITCODE"
            }
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
# 2. Vcpkg 依赖安装与二进制归档缓存 (Binary Cache Acceleration)
# ------------------------------------------------------------------------------
Write-Log "检查 Vcpkg 依赖环境与二进制缓存配置..."
if (-not (Test-Path $VcpkgRoot)) {
    Write-Log "未检测到 Vcpkg ($VcpkgRoot)。开始克隆并安装 Vcpkg..." "WARN"
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    Push-Location $VcpkgRoot
    .\bootstrap-vcpkg.bat
    Pop-Location
} else {
    Write-Log "发现 Vcpkg 安装在: $VcpkgRoot"
}

# 自动激活本地/CI vcpkg 二进制包归档缓存 (按 ABI 哈希缓存，彻底免除重编译)
if ([string]::IsNullOrEmpty($BinaryCacheDir)) {
    $BinaryCacheDir = Join-Path $env:LOCALAPPDATA "vcpkg\archives"
}
if (-not (Test-Path $BinaryCacheDir)) {
    New-Item -ItemType Directory -Path $BinaryCacheDir -Force | Out-Null
}
$env:VCPKG_DEFAULT_BINARY_CACHE = $BinaryCacheDir
Write-Log "⚡ 已激活 Vcpkg 二进制归档加速缓存: $BinaryCacheDir" "SUCCESS"

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

$WebView2TargetDir = Join-Path $PackagesDir "Microsoft.Web.WebView2.$WebView2Version"
if (-not (Test-Path (Join-Path $WebView2TargetDir "build\native\include\WebView2.h"))) {
    Write-Log "未发现固定版本 WebView2 SDK $WebView2Version，开始通过 nuget.exe 下载..." "WARN"

    $NugetPath = Join-Path $ScriptDir "nuget.exe"
    if (-not (Test-Path $NugetPath)) {
        Write-Log "下载 nuget.exe..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile $NugetPath
    }

    & $NugetPath install Microsoft.Web.WebView2 -Version $WebView2Version -OutputDirectory $PackagesDir
    if ($LASTEXITCODE -ne 0) {
        throw "WebView2 SDK 下载失败！退出码: $LASTEXITCODE"
    }
    Write-Log "WebView2 SDK 安装成功。" "SUCCESS"
} else {
    Write-Log "已就绪 WebView2 SDK: $WebView2TargetDir"
}

# ------------------------------------------------------------------------------
# 4. CMake 构建 C++ 核心与插件 (含 sccache 编译器级缓存)
# ------------------------------------------------------------------------------
Write-Log "准备 C++ 构建环境..."

# 动态挂载 VS 开发环境工具链
if (-not (Get-Command "cmake" -ErrorAction SilentlyContinue) -or -not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
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

$CMakeExtraArgs = @()
if (Get-Command "sccache" -ErrorAction SilentlyContinue) {
    Write-Log "⚡ 检测到 sccache 编译器缓存，自动启用 C/C++ 极速编译加速..." "SUCCESS"
    $CMakeExtraArgs += "-DCMAKE_C_COMPILER_LAUNCHER=sccache"
    $CMakeExtraArgs += "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
}

Write-Log "执行 CMake Configure..."
$BuildDir = Join-Path $ScriptDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" -DVCPKG_TARGET_TRIPLET="x64-windows" @CMakeExtraArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake 配置失败！退出码: $LASTEXITCODE"
}

$CpuCount = [Environment]::ProcessorCount
Write-Log "执行 CMake Build ($Configuration, 并发核心数: $CpuCount)..."
cmake --build build --config $Configuration --parallel $CpuCount

if ($LASTEXITCODE -ne 0) {
    throw "C++ 编译失败！退出码: $LASTEXITCODE"
}
Write-Log "C++ 编译完成。" "SUCCESS"

# ------------------------------------------------------------------------------
# 4.5 运行单元测试 (失败则中断流水线)
# ------------------------------------------------------------------------------
if (-not $SkipTests) {
    Write-Log "运行 CTest 测试套件..."
    ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "测试失败！退出码: $LASTEXITCODE"
    }
    Write-Log "测试套件通过。" "SUCCESS"
} else {
    Write-Log "已指定 -SkipTests，跳过单测执行。" "WARN"
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

    $ExeDir = Split-Path $ExePath -Parent
    $ServicePath = Join-Path $ExeDir "EasyTools_Service.exe"
    if (-not (Test-Path $ServicePath)) {
        throw "找不到编译后的 EasyTools_Service.exe"
    }
    Copy-Item $ServicePath -Destination $StagingDir
    Write-Log "已复制文件索引服务: EasyTools_Service.exe"
    
    # 复制所有同一目录下的 DLL 文件 (vcpkg 的依赖如 spdlog.dll 等)
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

# MSVC 动态运行库。EasyTools 与 vcpkg x64-windows 依赖均使用动态 CRT；
# 便携包不能假设目标机器预装了 Visual C++ Redistributable。优先使用当前
# DevShell 精确对应的 Redist 目录，CI 中再通过 vswhere 定位同一工具链。
$VcRuntimeNames = @(
    "msvcp140.dll", "msvcp140_atomic_wait.dll",
    "vcruntime140.dll", "vcruntime140_1.dll"
)
$VcCrtDir = $null
$VcRedistRoots = @()
if ($env:VCToolsRedistDir -and (Test-Path $env:VCToolsRedistDir)) {
    $VcRedistRoots += $env:VCToolsRedistDir
}

$RedistVsPath = $vsPath
if (-not $RedistVsPath) {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $RedistVsPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Workload.VCTools -property installationPath
    }
}
if ($RedistVsPath) {
    $MsVcRedistRoot = Join-Path $RedistVsPath "VC\Redist\MSVC"
    if (Test-Path $MsVcRedistRoot) {
        $VcRedistRoots += Get-ChildItem $MsVcRedistRoot -Directory |
            Sort-Object Name -Descending | ForEach-Object { $_.FullName }
    }
}

foreach ($Root in $VcRedistRoots | Select-Object -Unique) {
    $X64Root = Join-Path $Root "x64"
    if (-not (Test-Path $X64Root)) { continue }
    $Candidate = Get-ChildItem $X64Root -Directory -Filter "Microsoft.VC*.CRT" |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($Candidate) {
        $MissingRuntime = @($VcRuntimeNames | Where-Object {
            -not (Test-Path (Join-Path $Candidate.FullName $_))
        })
        if ($MissingRuntime.Count -eq 0) {
            $VcCrtDir = $Candidate.FullName
            break
        }
    }
}
if (-not $VcCrtDir) {
    throw "找不到与 MSVC 工具链匹配的 x64 Visual C++ Runtime 可再发行文件"
}
Copy-Item (Join-Path $VcCrtDir "*.dll") -Destination $StagingDir
Write-Log "已复制 Visual C++ Runtime: $VcCrtDir"

# WebView2Loader.dll
$WebView2PackageDir = Get-Item $WebView2TargetDir -ErrorAction SilentlyContinue
if ($WebView2PackageDir) {
    $LoaderPath = Join-Path $WebView2PackageDir.FullName "build\native\x64\WebView2Loader.dll"
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
if (Test-Path "LICENSE") { Copy-Item "LICENSE" -Destination $StagingDir }

$RequiredArtifacts = @(
    "EasyTools.exe", "EasyTools_Service.exe", "EasyCore.dll",
    "ui\index.html", "plugins\Plugin_Gesture.dll", "plugins\Plugin_Capture.dll",
    "plugins\Plugin_Keycast.dll", "plugins\Plugin_Search.dll"
) + $VcRuntimeNames
foreach ($Artifact in $RequiredArtifacts) {
    if (-not (Test-Path (Join-Path $StagingDir $Artifact))) {
        throw "发布产物不完整，缺少: $Artifact"
    }
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

try {
    Rename-Item -Path $StagingDir -NewName "deploy_dist"
} catch {
    if ((Test-Path $BackupDir) -and -not (Test-Path $DeployDir)) {
        Rename-Item -Path $BackupDir -NewName "deploy_dist"
    }
    throw
}
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
        & $ISCC "/DEasyToolsVersion=$ProjectVersion" $InstallerScript
        if ($LASTEXITCODE -eq 0) {
            Write-Log "安装包已成功生成到: $OutputInstallerDir" "SUCCESS"
        } else {
            throw "安装包编译失败！退出码: $LASTEXITCODE"
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

# 新版、测试与安装包都已完成后才删除回滚副本。流程中途失败时保留该目录，
# 便于人工恢复；成功流程不在工作区遗留一整份过期发布物。
if (Test-Path $BackupDir) {
    Remove-Item -Recurse -Force $BackupDir
    Write-Log "旧版本回滚副本已清理。"
}
Write-Log "直接双击运行 deploy_dist/EasyTools.exe 即可启动工具。"
Write-Log "======================================================="
