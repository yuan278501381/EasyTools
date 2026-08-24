<#
.SYNOPSIS
EasyTools 官方 CLI 安装管理工具 (Official CLI Installer & Lifecycle Manager)

.DESCRIPTION
支持一键静默安装、自定义路径安装、开机自启配置、便携版运行与静默卸载。

.EXAMPLE
.\install.ps1                          # 默认极速静默安装并自动启动
.\install.ps1 -Silent                  # 静默安装
.\install.ps1 -Dir "D:\Apps\EasyTools" # 安装到指定目录
.\install.ps1 -Uninstall               # 静默卸载
.\install.ps1 -Portable                # 直接以绿色便携版运行
.\install.ps1 -Rebuild                 # 重新编译后立即静默安装并启动
#>

param (
    [switch]$Silent = $true,              # 静默安装模式 (默认开启)
    [switch]$VerySilent = $true,          # 完全无感静默安装 (无弹窗打扰)
    [switch]$Launch = $true,              # 安装完成后自动启动应用 (默认开启)
    [string]$Dir = "",                    # 自定义安装路径 (留空默认: C:\Program Files\EasyTools)
    [switch]$DesktopIcon = $false,        # 创建桌面图标
    [switch]$Uninstall = $false,          # 执行静默卸载流程
    [switch]$Portable = $false,           # 运行绿色便携版
    [switch]$Rebuild = $false             # 先执行增量编译打包再安装
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$TraceID = [guid]::NewGuid().ToString("N").Substring(0, 8)
$SetupExe = Join-Path $ScriptDir "Output\EasyTools-Setup.exe"
$DeployDistExe = Join-Path $ScriptDir "deploy_dist\EasyTools.exe"

function Write-CliLog ($Message, $Level = "INFO") {
    $TimeStamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $Color = switch ($Level) {
        "INFO" { "Cyan" }
        "WARN" { "Yellow" }
        "ERROR" { "Red" }
        "SUCCESS" { "Green" }
        default { "White" }
    }
    Write-Host "[$TimeStamp] [$TraceID] [$Level] $Message" -ForegroundColor $Color
}

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "   EasyTools Official CLI Installer & Manager (2026)   " -ForegroundColor Cyan
Write-Host "   Copyright (c) 2026 Yy1 (@yuan278501381)             " -ForegroundColor DarkGray
Write-Host "=======================================================" -ForegroundColor Cyan

# 1. 便携版模式
if ($Portable) {
    if (-not (Test-Path $DeployDistExe)) {
        Write-CliLog "便携版未就绪，正在快速构建..." "WARN"
        & pwsh.exe -File (Join-Path $ScriptDir "deploy.ps1") -Quick -SkipInstaller
    }
    Write-CliLog "正在以绿色便携版启动 EasyTools..." "INFO"
    Start-Process -FilePath $DeployDistExe
    Write-CliLog "EasyTools 便携版已启动！" "SUCCESS"
    exit 0
}

# 2. 卸载模式
if ($Uninstall) {
    Write-CliLog "正在检索系统中的 EasyTools 安装实例..." "INFO"
    $UninstallKeys = @(
        "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EasyTools_is1",
        "HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\EasyTools_is1",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\EasyTools_is1"
    )
    $UninstallerPath = ""
    foreach ($k in $UninstallKeys) {
        if (Test-Path $k) {
            $val = (Get-ItemProperty -Path $k -ErrorAction SilentlyContinue).UninstallString
            if ($val) {
                $UninstallerPath = $val.Trim('"')
                break
            }
        }
    }

    if (-not $UninstallerPath -or -not (Test-Path $UninstallerPath)) {
        $DefaultUninstaller = "C:\Program Files\EasyTools\unins000.exe"
        if (Test-Path $DefaultUninstaller) {
            $UninstallerPath = $DefaultUninstaller
        }
    }

    if ($UninstallerPath -and (Test-Path $UninstallerPath)) {
        Write-CliLog "发现卸载程序: $UninstallerPath" "INFO"
        Write-CliLog "正在执行静默卸载并清理后台服务..." "INFO"
        $UninstArgs = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"
        $proc = Start-Process -FilePath $UninstallerPath -ArgumentList $UninstArgs -Wait -PassThru
        if ($proc.ExitCode -eq 0) {
            Write-CliLog "EasyTools 已成功卸载并清理完毕！" "SUCCESS"
        } else {
            Write-CliLog "卸载退出代码: $($proc.ExitCode)" "WARN"
        }
    } else {
        Write-CliLog "未在系统中检测到已安装的 EasyTools。" "WARN"
    }
    exit 0
}

# 3. 重新构建请求
if ($Rebuild -or -not (Test-Path $SetupExe)) {
    Write-CliLog "安装包不存在或指定了 -Rebuild，开始执行增量编译与打包..." "INFO"
    & pwsh.exe -File (Join-Path $ScriptDir "deploy.ps1") -Quick
}

if (-not (Test-Path $SetupExe)) {
    Write-CliLog "未找到安装包文件: $SetupExe" "ERROR"
    exit 1
}

# 4. 执行 CLI 静默安装
Write-CliLog "正在执行 EasyTools CLI 自动化安装..." "INFO"
$InstallArgs = @()
if ($VerySilent) {
    $InstallArgs += "/VERYSILENT"
    $InstallArgs += "/SUPPRESSMSGBOXES"
} elseif ($Silent) {
    $InstallArgs += "/SILENT"
    $InstallArgs += "/SUPPRESSMSGBOXES"
}

$InstallArgs += "/NORESTART"
$InstallArgs += "/CLOSEAPPLICATIONS"
$InstallArgs += "/FORCECLOSEAPPLICATIONS"

if ($Dir) {
    $InstallArgs += "/DIR=""$Dir"""
    Write-CliLog "自定义安装目录: $Dir" "INFO"
}

if ($DesktopIcon) {
    $InstallArgs += "/TASKS=""desktopicon"""
}

$LogFilePath = Join-Path $ScriptDir "deploy_logs\install_cli_$TraceID.log"
$InstallArgs += "/LOG=""$LogFilePath"""

Write-CliLog "启动安装进程: $SetupExe" "INFO"
Write-CliLog "参数: $($InstallArgs -join ' ')" "INFO"

# 执行安装
$pinfo = New-Object System.Diagnostics.ProcessStartInfo
$pinfo.FileName = $SetupExe
$pinfo.Arguments = $InstallArgs -join " "
$pinfo.Verb = "runas"
$pinfo.UseShellExecute = $true

try {
    $p = [System.Diagnostics.Process]::Start($pinfo)
    $p.WaitForExit()
    if ($p.ExitCode -eq 0) {
        Write-CliLog "=======================================================" "SUCCESS"
        Write-CliLog "EasyTools CLI 安装成功！" "SUCCESS"
        Write-CliLog "安装日志: $LogFilePath" "INFO"

        if ($Launch) {
            $TargetExe = if ($Dir) { Join-Path $Dir "EasyTools.exe" } else { "C:\Program Files\EasyTools\EasyTools.exe" }
            if (Test-Path $TargetExe) {
                Write-CliLog "正在启动应用: $TargetExe" "INFO"
                $appInfo = New-Object System.Diagnostics.ProcessStartInfo
                $appInfo.FileName = $TargetExe
                $appInfo.Verb = "runas"
                $appInfo.UseShellExecute = $true
                try {
                    [System.Diagnostics.Process]::Start($appInfo) | Out-Null
                } catch {
                    Start-Process -FilePath $TargetExe
                }
                Write-CliLog "EasyTools 已成功启动并就绪！" "SUCCESS"
            }
        }
        Write-Host "=======================================================" -ForegroundColor Green
    } else {
        Write-CliLog "安装程序退出，错误码: $($p.ExitCode)" "ERROR"
        exit $p.ExitCode
    }
} catch {
    Write-CliLog "安装执行失败: $_" "ERROR"
    exit 1
}
