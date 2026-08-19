param (
    [ValidateSet(1, 4)]
    [int]$Scheme = 1
)

Write-Output "======================================================="
Write-Output ">> 正在装配 100% 高保真母版品牌方案 $Scheme (ICO + 纯透明托盘 + UI) ..."
Write-Output "======================================================="

# 1. 运行 Python 高保真母版处理管道
python "$PSScriptRoot\resources\build_master_production_icons.py" $Scheme

# 2. 强制刷新 RC 资源时间戳并清除旧的 RC 缓存编译产物
(Get-Item "$PSScriptRoot\src\EasyTools.rc").LastWriteTime = Get-Date
Get-ChildItem -Path "$PSScriptRoot\build" -Filter "*EasyTools.res*" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
Get-ChildItem -Path "$PSScriptRoot\build" -Filter "*EasyTools.rc.res*" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# 3. 重新编译前端与 C++ 主程序
& "$PSScriptRoot\deploy.ps1" -Quick

# 4. 同步至对应的独立测试目录
if ($Scheme -eq 1) {
    $targetDir = "$PSScriptRoot\deploy_schemes\Scheme1_AeroGlide"
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    Copy-Item -Path "$PSScriptRoot\deploy_dist\*" -Destination $targetDir -Recurse -Force
} else {
    $targetDir = "$PSScriptRoot\deploy_schemes\Scheme4_SonicLoop"
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    Copy-Item -Path "$PSScriptRoot\deploy_dist\*" -Destination $targetDir -Recurse -Force
}

Write-Output "======================================================="
Write-Output ">> 方案 $Scheme 已 100% 高保真同步至主程序及 $targetDir"
Write-Output "======================================================="
