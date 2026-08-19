param (
    [ValidateSet(1, 4)]
    [int]$Scheme = 1
)

Write-Output "======================================================="
Write-Output ">> 正在一键切换并装配 EasyTools 品牌方案 $Scheme ..."
Write-Output "======================================================="

# 1. 重新生成 ICO
& "$PSScriptRoot\resources\brand_schemes.ps1" -ApplyScheme $Scheme

# 2. 重新编译 C++ 主程序
& "$PSScriptRoot\deploy.ps1" -Quick

Write-Output "======================================================="
Write-Output ">> 品牌方案 $Scheme 已成功装配至 deploy_dist/EasyTools.exe 与安装包！"
Write-Output "======================================================="
