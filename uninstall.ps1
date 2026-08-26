<#
.SYNOPSIS
EasyTools 官方 CLI 静默卸载工具 (Official CLI Uninstaller)

.DESCRIPTION
检索系统注册表与标准安装路径，自动优雅停止运行实例，并以 /VERYSILENT 模式彻底清理安装目录与服务。

.EXAMPLE
.\uninstall.ps1
.\uninstall.cmd
.\uninstall.ps1 -KeepPersonalData
#>

param (
    [switch]$KeepPersonalData = $false,   # 保留配置、日志、转储、索引、历史、截图和录屏
    [switch]$Force = $false               # 强制杀死残留进程
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

$TraceID = [guid]::NewGuid().ToString("N").Substring(0, 8)

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
Write-Host "   EasyTools Official CLI Uninstaller (2026)           " -ForegroundColor Cyan
Write-Host "   Copyright (c) 2026 Yy1 (@yuan278501381)             " -ForegroundColor DarkGray
Write-Host "=======================================================" -ForegroundColor Cyan

# 1. 优雅停止运行中的 EasyTools 进程
Write-CliLog "正在检测运行中的 EasyTools 进程..." "INFO"
$Processes = Get-Process -Name "EasyTools", "EasyTools_Service" -ErrorAction SilentlyContinue
if ($Processes) {
    Write-CliLog "正在退出运行中的 EasyTools 实例..." "INFO"
    foreach ($p in $Processes) {
        try {
            $p.CloseMainWindow() | Out-Null
            Start-Sleep -Milliseconds 300
            if (-not $p.HasExited) {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            }
        } catch {}
    }
}

# 2. 检索卸载程序路径
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
    Write-CliLog "定位到卸载程序: $UninstallerPath" "INFO"
    Write-CliLog "正在执行一键静默无感卸载..." "INFO"
    
    $pinfo = New-Object System.Diagnostics.ProcessStartInfo
    $pinfo.FileName = $UninstallerPath
    $pinfo.Arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"
    if ($KeepPersonalData) {
        $pinfo.Arguments += " /KEEPPERSONALDATA"
    }
    $pinfo.Verb = "runas"
    $pinfo.UseShellExecute = $true

    try {
        $p = [System.Diagnostics.Process]::Start($pinfo)
        $p.WaitForExit()
        if ($p.ExitCode -eq 0) {
            Write-CliLog "EasyTools 主程序与系统服务已成功卸载！" "SUCCESS"
        } else {
            Write-CliLog "卸载退出代码: $($p.ExitCode)" "WARN"
        }
    } catch {
        Write-CliLog "卸载调用失败: $_" "ERROR"
        exit 1
    }
} else {
    Write-CliLog "未在注册表或默认路径检测到 EasyTools 安装记录。" "WARN"
}

# 3. 默认清理全部个人数据；可用 -KeepPersonalData 明确保留
if (-not $KeepPersonalData) {
    $PersonalDataDirs = @(
        (Join-Path $env:LOCALAPPDATA "EasyTools"),
        (Join-Path $env:APPDATA "EasyTools"),
        (Join-Path $env:ProgramData "EasyTools")
    )
    foreach ($dir in $PersonalDataDirs) {
        if (Test-Path -LiteralPath $dir) {
            Write-CliLog "正在清理个人数据: $dir" "INFO"
            Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    Write-CliLog "个人数据已清理完毕！" "SUCCESS"
}

Write-Host "=======================================================" -ForegroundColor Green
Write-Host "EasyTools CLI 卸载流程执行完毕。" -ForegroundColor Green
Write-Host "=======================================================" -ForegroundColor Green
