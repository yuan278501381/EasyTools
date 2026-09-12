$ErrorActionPreference = 'Stop'

$pf86 = [System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::ProgramFilesX86)
if (-not $pf86) { $pf86 = [System.Environment]::GetEnvironmentVariable('ProgramFiles(x86)') }
if (-not $pf86) { $pf86 = [System.Environment]::GetEnvironmentVariable('ProgramFiles') }

$VsWhere = $null
$VsWhereCmd = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
if ($VsWhereCmd) {
    $VsWhere = $VsWhereCmd.Source
} elseif ($pf86) {
    $cand = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $cand) { $VsWhere = $cand }
}
if (-not $VsWhere) {
    $cand64 = 'C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $cand64) { $VsWhere = $cand64 }
}

if (-not $VsWhere -or -not (Test-Path -LiteralPath $VsWhere)) {
    throw 'ARM64 构建需要 Visual Studio C++ ARM64 工作负载，未找到 vswhere.exe。'
}
$InstancesJson = & $VsWhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -format json -utf8
if ($LASTEXITCODE -ne 0) { throw '无法查询 Visual Studio ARM64 工具链。' }
$Instances = @(($InstancesJson -join "`n") | ConvertFrom-Json)
if ($Instances.Count -eq 0) {
    throw '未安装 MSVC ARM64 工具链；请在 Visual Studio Installer 中添加 Microsoft.VisualStudio.Component.VC.Tools.ARM64。'
}
$Instance = $Instances[0]
$MajorVersion = ([version]$Instance.installationVersion).Major
$Generator = switch ($MajorVersion) {
    17 { 'Visual Studio 17 2022' }
    18 { 'Visual Studio 18 2026' }
    default { throw "ARM64 构建需要受支持的 Visual Studio 2022/2026，检测到版本 $MajorVersion。" }
}
[pscustomobject]@{
    InstallationPath = [string]$Instance.installationPath
    Generator = $Generator
}
