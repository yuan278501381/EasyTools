param (
    [ValidateSet(1, 4)]
    [int]$Scheme = 1
)

Write-Output "======================================================="
Write-Output ">> 正在装配 100% 高保真母版品牌方案 $Scheme (ICO + 纯透明托盘 + UI) ..."
Write-Output "======================================================="

# 1. 运行 Python 高保真母版处理管道
python "$PSScriptRoot\resources\build_master_production_icons.py" $Scheme

# 2. 重新编译前端与 C++ 主程序
& "$PSScriptRoot\deploy.ps1" -Quick

Write-Output "======================================================="
Write-Output ">> 方案 $Scheme 已 100% 高保真同步至 deploy_dist/EasyTools.exe 与安装包！"
Write-Output "======================================================="
