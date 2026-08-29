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
    [switch]$Coverage = $false,          # 启用 C++ 代码覆盖率分析与防回退门禁 (OpenCppCoverage)
    [switch]$StaticAnalysis = $false,    # 对核心、搜索插件和索引服务运行 MSVC /analyze
    [switch]$Install = $false,           # 构建完成后立即通过 CLI 执行静默安装与启动
    [string]$BinaryCacheDir = ""         # 自定义 vcpkg 二进制包缓存目录
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$VersionFile = Join-Path $ScriptDir "VERSION"
if (-not (Test-Path -LiteralPath $VersionFile)) {
    throw "缺少唯一版本源: $VersionFile"
}
$ProjectVersion = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($ProjectVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION 必须是稳定 SemVer（例如 1.2.3），当前值: $ProjectVersion"
}
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
Write-Log "======================================================="

# ------------------------------------------------------------------------------
# 1. 前端构建 (React + Vite)
# ------------------------------------------------------------------------------
Write-Log "检查前端环境 (ui/)..."
if (Test-Path "ui/package.json") {
    Push-Location ui
    try {
        if (-not $Quick -or -not (Test-Path "node_modules")) {
            Write-Log "执行 npm ci (锁定依赖)..."
            npm ci --prefer-offline --no-audit
            if ($LASTEXITCODE -ne 0) { throw "npm ci 失败，退出码: $LASTEXITCODE" }
        } else {
            Write-Log "⚡ 极速模式: 复用本地 node_modules 依赖" "INFO"
        }

        foreach ($Command in @("lint", "i18n-check", "css-check", "typography-check", "trim-workingset-check", "test", "build")) {
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
if ($StaticAnalysis) {
    $CMakeExtraArgs += "-DEASYTOOLS_ENABLE_MSVC_ANALYSIS=ON"
} else {
    $CMakeExtraArgs += "-DEASYTOOLS_ENABLE_MSVC_ANALYSIS=OFF"
}
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

# 确保全套多分辨率 Windows 图标与托盘图标存在，并清理旧版 .res 确保资源强制重新链接
if (-not (Test-Path "$ScriptDir\resources\app.ico") -or -not (Test-Path "$ScriptDir\resources\tray.ico") -or -not (Test-Path "$ScriptDir\resources\tray_dark.ico")) {
    Write-Log "检测到图标未初始化，调用母版管道生成 (resources/build_master_production_icons.py)..."
    python "$ScriptDir\resources\build_master_production_icons.py" 1
}
Get-ChildItem -Path $BuildDir -Filter "*EasyTools*.res" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$CpuCount = [Environment]::ProcessorCount
Write-Log "执行 CMake Build ($Configuration, 并发核心数: $CpuCount)..."
cmake --build build --config $Configuration --parallel $CpuCount

if ($LASTEXITCODE -ne 0) {
    throw "C++ 编译失败！退出码: $LASTEXITCODE"
}
Write-Log "C++ 编译完成。" "SUCCESS"

# ------------------------------------------------------------------------------
# 4.5 运行单元测试与代码覆盖率防回退分析 (失败则中断流水线)
# ------------------------------------------------------------------------------
if (-not $SkipTests) {
    $OpenCppCoverageExe = $null
    if (Get-Command "OpenCppCoverage" -ErrorAction SilentlyContinue) {
        $OpenCppCoverageExe = "OpenCppCoverage"
    } elseif (Test-Path "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe") {
        $OpenCppCoverageExe = "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe"
    }

    if ($Coverage -or $OpenCppCoverageExe) {
        Write-Log "运行 C++ 单元测试与代码覆盖率防回退分析..."
        $CoverageReportDir = Join-Path $ScriptDir "coverage_report"
        if (-not (Test-Path $CoverageReportDir)) { New-Item -ItemType Directory -Path $CoverageReportDir | Out-Null }

        $TestExe = Join-Path $BuildDir "bin\$Configuration\EasyToolsTests.exe"
        if (-not (Test-Path $TestExe)) { $TestExe = Join-Path $BuildDir "bin\EasyToolsTests.exe" }

        if ((Test-Path $TestExe) -and $OpenCppCoverageExe) {
            & $OpenCppCoverageExe --sources "$ScriptDir\src" `
                                  --excluded_sources "$ScriptDir\build" `
                                  --excluded_sources "$ScriptDir\packages" `
                                  --excluded_sources "$ScriptDir\tests" `
                                  --export_type "html:$CoverageReportDir" `
                                  --export_type "cobertura:$CoverageReportDir\cobertura.xml" `
                                  -- $TestExe --gtest_output="xml:$CoverageReportDir\junit.xml"
            if ($LASTEXITCODE -ne 0) {
                throw "单元测试与代码覆盖率分析执行失败！退出码: $LASTEXITCODE"
            }
            $CoberturaPath = Join-Path $CoverageReportDir "cobertura.xml"
            if (Test-Path $CoberturaPath) {
                [xml]$CoverageXml = Get-Content $CoberturaPath
                $LineRate = [double]$CoverageXml.coverage.'line-rate'
                if ($LineRate -lt 0.32) {
                    throw "C++ 行覆盖率 $([math]::Round($LineRate * 100, 2))% 低于 32% 防回退门禁"
                }
                Write-Log "C++ 行覆盖率: $([math]::Round($LineRate * 100, 2))% (门禁 >= 32%)" "SUCCESS"
            }
            Write-Log "代码覆盖率报告与 GTest JUnit 报表已生成: $CoverageReportDir" "SUCCESS"
        } else {
            ctest --test-dir $BuildDir -C $Configuration --output-on-failure
            if ($LASTEXITCODE -ne 0) {
                throw "测试失败！退出码: $LASTEXITCODE"
            }
        }
    } else {
        Write-Log "运行 CTest 测试套件..."
        ctest --test-dir $BuildDir -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            throw "测试失败！退出码: $LASTEXITCODE"
        }
    }
    Write-Log "测试套件通过。" "SUCCESS"
} else {
    Write-Log "已指定 -SkipTests，跳过单测执行。" "WARN"
}

# ------------------------------------------------------------------------------
# 5. 打包输出物 (Deploy & Artifact Purification)
# ------------------------------------------------------------------------------
Write-Log "开始提纯输出物并写入暂存区..."
$DeployDir = Join-Path $ScriptDir "deploy_dist"
$StagingDir = Join-Path $ScriptDir "deploy_dist_staging_$TraceID"
$SymbolsDir = Join-Path $ScriptDir "deploy_symbols"

if (Test-Path $StagingDir) {
    Remove-Item -Recurse -Force $StagingDir
}
New-Item -ItemType Directory -Path $StagingDir | Out-Null

# 符号隔离归档 (Symbol Stripping & Independent Archive)
if (-not (Test-Path $SymbolsDir)) {
    New-Item -ItemType Directory -Path $SymbolsDir | Out-Null
}
$SymbolsZip = Join-Path $SymbolsDir "EasyTools-Symbols-v$ProjectVersion.zip"
$PdbFiles = Get-ChildItem -Path (Join-Path $BuildDir "bin") -Filter "*.pdb" -Recurse -ErrorAction SilentlyContinue
if ($PdbFiles) {
    Write-Log "正在将 $($PdbFiles.Count) 个调试符号 (*.pdb) 归档到: $SymbolsZip..."
    if (Test-Path $SymbolsZip) { Remove-Item -Force $SymbolsZip -ErrorAction SilentlyContinue }
    Compress-Archive -Path $PdbFiles.FullName -DestinationPath $SymbolsZip -Force
    Write-Log "调试符号已安全剥离并独立归档至 deploy_symbols/。" "SUCCESS"
}

# 二进制主文件
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
    
    # 复制所有同一目录下的运行库 DLL 文件 (排除 gtest, gmock 等测试库)
    $DllFiles = Get-ChildItem -Path $ExeDir -Filter "*.dll" | Where-Object {
        $_.Name -notlike "gtest*.dll" -and $_.Name -notlike "gmock*.dll"
    }
    foreach ($dll in $DllFiles) {
        Copy-Item $dll.FullName -Destination $StagingDir
    }
    Write-Log "已复制生产运行库 DLL 文件 (已排除测试库 gtest)"
    
    # 复制插件目录 (仅复制插件 DLL 与 plugin.json，严禁复制 pdb 及重复三方库)
    $PluginsDir = Join-Path $BuildDir "bin\plugins\$Configuration"
    if (Test-Path $PluginsDir) {
        $TargetPluginsDir = Join-Path $StagingDir "plugins"
        New-Item -ItemType Directory -Path $TargetPluginsDir -ErrorAction SilentlyContinue | Out-Null
        $PluginFiles = Get-ChildItem -Path $PluginsDir | Where-Object {
            $_.Name -like "Plugin_*.dll" -or $_.Name -like "*.plugin.json"
        }
        foreach ($pf in $PluginFiles) {
            Copy-Item $pf.FullName -Destination $TargetPluginsDir
        }
        Write-Log "已复制纯净插件集合 (plugins/)，无冗余 DLL 与 PDB"

        # 补充收集插件所需但主程序未直接引用的第三方运行库 (如 opencv_photo4.dll 等) 到根运行目录
        $PluginDependencyDlls = Get-ChildItem -Path $PluginsDir -Filter "*.dll" | Where-Object {
            $_.Name -notlike "Plugin_*.dll" -and $_.Name -notlike "gtest*.dll" -and $_.Name -notlike "gmock*.dll"
        }
        foreach ($pDll in $PluginDependencyDlls) {
            $destFile = Join-Path $StagingDir $pDll.Name
            if (-not (Test-Path $destFile)) {
                Copy-Item $pDll.FullName -Destination $StagingDir
                Write-Log "补充复制插件依赖运行库到根目录: $($pDll.Name)"
            }
        }
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
    $pf86 = ${env:ProgramFiles(x86)}
    if (-not $pf86) { $pf86 = $env:ProgramFiles }
    $vswhere = Join-Path $pf86 "Microsoft Visual Studio\Installer\vswhere.exe"
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

# 白名单资产注入 (resources/)
$TargetResourcesDir = Join-Path $StagingDir "resources"
New-Item -ItemType Directory -Path $TargetResourcesDir -ErrorAction SilentlyContinue | Out-Null

$WhitelistedResourceFiles = @("app.ico", "tray.ico", "tray_dark.ico", "app_icon_hires.png")
foreach ($rFile in $WhitelistedResourceFiles) {
    $srcPath = Join-Path "resources" $rFile
    if (Test-Path $srcPath) {
        Copy-Item $srcPath -Destination $TargetResourcesDir
    }
}
if (Test-Path "resources\scripts") {
    Copy-Item "resources\scripts" -Destination $TargetResourcesDir -Recurse
}
Write-Log "已通过白名单注入运行必需资源 (ico, png, scripts/)"

if (Test-Path "ui/dist") {
    $UiDeployDir = Join-Path $StagingDir "ui"
    New-Item -ItemType Directory -Path $UiDeployDir | Out-Null
    Copy-Item "ui/dist\*" -Destination $UiDeployDir -Recurse
    # 排除测试用 demo 网页
    if (Test-Path (Join-Path $UiDeployDir "theme_demo.html")) {
        Remove-Item -Force (Join-Path $UiDeployDir "theme_demo.html")
    }
    # 确保保留 Logo_Origin.png 和 Logo_Origin2.png
    if ((Test-Path "ui/Logo_Origin.png") -and (-not (Test-Path (Join-Path $UiDeployDir "Logo_Origin.png")))) {
        Copy-Item "ui/Logo_Origin.png" -Destination $UiDeployDir
    }
    if ((Test-Path "ui/Logo_Origin2.png") -and (-not (Test-Path (Join-Path $UiDeployDir "Logo_Origin2.png")))) {
        Copy-Item "ui/Logo_Origin2.png" -Destination $UiDeployDir
    }
    Write-Log "已复制纯净前端产物 (ui/) 并保留原始 Logo"
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
        foreach ($p in $remaining) {
            try {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            } catch { }
        }
        Start-Sleep -Seconds 1
    }
}

$BackupDir = Join-Path $ScriptDir "deploy_dist_backup"
taskkill /F /T /IM EasyTools.exe 2>$null | Out-Null
taskkill /F /T /IM EasyTools_Service.exe 2>$null | Out-Null
Get-Process -Name "EasyTools*", "EasyTools_Service*" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
if (Test-Path $DeployDir) {
    if (Test-Path $BackupDir) {
        Remove-Item -Recurse -Force $BackupDir -ErrorAction SilentlyContinue
    }
    $renamed = $false
    for ($i = 0; $i -lt 5; $i++) {
        try {
            Rename-Item -Path $DeployDir -NewName "deploy_dist_backup" -ErrorAction Stop
            $renamed = $true
            break
        } catch {
            Start-Sleep -Milliseconds 600
        }
    }
    if ($renamed) {
        Rename-Item -Path $StagingDir -NewName "deploy_dist"
        Write-Log "旧版本已安全备份到: deploy_dist_backup"
    } else {
        # 若 Windows 锁住父级目录名，则执行安全原子覆盖
        Write-Log "目录重命名受阻，执行原子文件集快速覆盖..." "WARN"
        Copy-Item -Path "$StagingDir\*" -Destination $DeployDir -Recurse -Force
        Remove-Item -Recurse -Force $StagingDir -ErrorAction SilentlyContinue
    }
} else {
    Rename-Item -Path $StagingDir -NewName "deploy_dist"
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
        $TargetSetupFile = Join-Path $OutputInstallerDir "EasyTools-Setup.exe"
        if (Test-Path $TargetSetupFile) {
            Remove-Item -Force $TargetSetupFile -ErrorAction SilentlyContinue
        }
        & $ISCC "/DEasyToolsVersion=$ProjectVersion" $InstallerScript
        if ($LASTEXITCODE -eq 0) {
            if (Test-Path $TargetSetupFile) {
                $now = Get-Date
                (Get-Item $TargetSetupFile).CreationTime = $now
                (Get-Item $TargetSetupFile).LastWriteTime = $now
            }
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

# ------------------------------------------------------------------------------
# 7. 全功能端到端生命周期与防死锁自动化审计门禁 (DevOps Lifecycle Gate)
# ------------------------------------------------------------------------------
if (-not $SkipTests) {
    $LifecycleScript = Join-Path $ScriptDir "build\verify_lifecycle.ps1"
    if (Test-Path $LifecycleScript) {
        Write-Log "🚀 正在执行全模块生命周期与防死锁自动化端到端审计 (verify_lifecycle.ps1)..." "INFO"
        & pwsh.exe -File $LifecycleScript
        if ($LASTEXITCODE -ne 0) {
            throw "全模块生命周期自动化端到端审计未通过！退出码: $LASTEXITCODE"
        }
        Write-Log "全模块生命周期自动化审计 100% 通过。" "SUCCESS"
    }
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

if ($Install) {
    Write-Log "正在执行自动化 CLI 安装..." "INFO"
    & pwsh.exe -File (Join-Path $ScriptDir "install.ps1") -Silent -Launch
}
