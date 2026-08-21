param(
    [string]$Tag = "v1.0.6",
    [switch]$ForceRebuild = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
Set-Location $ScriptDir

$CleanVersion = $Tag.TrimStart('v', 'V')

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "发布 EasyTools $Tag (版本号: $CleanVersion) 到 GitHub Releases" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. 自动化版本号级联对齐 (SSOT: 确保 CMakeLists.txt / ui/package.json / installer.iss 严格一致)
$CMakeFile = Join-Path $ScriptDir "CMakeLists.txt"
$CMakeContent = Get-Content $CMakeFile -Raw
if ($CMakeContent -match 'project\s*\(\s*EasyTools\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    $CurrentCMakeVer = $Matches[1]
    if ($CurrentCMakeVer -ne $CleanVersion) {
        Write-Host "[AUTO-SYNC] 更新 CMakeLists.txt 版本: $CurrentCMakeVer -> $CleanVersion" -ForegroundColor Yellow
        $CMakeContent = $CMakeContent -replace 'project\s*\(\s*EasyTools\s+VERSION\s+[0-9]+\.[0-9]+\.[0-9]+', "project(EasyTools`n    VERSION $CleanVersion"
        $CMakeContent | Out-File $CMakeFile -Encoding utf8
        $ForceRebuild = $true
    }
}

$UiPackageFile = Join-Path $ScriptDir "ui\package.json"
if (Test-Path $UiPackageFile) {
    $UiPkg = Get-Content $UiPackageFile -Raw | ConvertFrom-Json
    if ($UiPkg.version -ne $CleanVersion) {
        Write-Host "[AUTO-SYNC] 更新 ui/package.json 版本: $($UiPkg.version) -> $CleanVersion" -ForegroundColor Yellow
        $UiPkgText = Get-Content $UiPackageFile -Raw
        $UiPkgText = $UiPkgText -replace '"version":\s*"[^"]+"', "`"version`": `"$CleanVersion`""
        $UiPkgText | Out-File $UiPackageFile -Encoding utf8
        $ForceRebuild = $true
    }
}

$InstallerFile = Join-Path $ScriptDir "installer.iss"
if (Test-Path $InstallerFile) {
    $IssContent = Get-Content $InstallerFile -Raw
    if ($IssContent -match '#define EasyToolsVersion "([^"]+)"') {
        if ($Matches[1] -ne $CleanVersion) {
            Write-Host "[AUTO-SYNC] 更新 installer.iss 版本: $($Matches[1]) -> $CleanVersion" -ForegroundColor Yellow
            $IssContent = $IssContent -replace '#define EasyToolsVersion "[^"]+"', "#define EasyToolsVersion `"$CleanVersion`""
            $IssContent | Out-File $InstallerFile -Encoding utf8
        }
    }
}

# 2. 如果版本有更新或强制重构，触发 deploy.ps1 重新打包
if ($ForceRebuild -or -not (Test-Path "Output\EasyTools-Setup.exe")) {
    Write-Host "[BUILD] 触发全量一键构建部署流水线..." -ForegroundColor Cyan
    & pwsh -ExecutionPolicy Bypass -File .\deploy.ps1 -Quick
    if ($LASTEXITCODE -ne 0) {
        throw "构建部署失败，终止发布！"
    }
}

$ReleaseDir = "release_assets"
if (Test-Path $ReleaseDir) {
    Remove-Item -Recurse -Force $ReleaseDir
}
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null

$SetupExe = "Output\EasyTools-Setup.exe"
if (-not (Test-Path $SetupExe)) {
    throw "未找到安装包文件: $SetupExe"
}

$TargetSetup = "$ReleaseDir\EasyTools-$Tag-Setup.exe"
Copy-Item $SetupExe $TargetSetup
Write-Host "[OK] 安装包已就绪: $TargetSetup" -ForegroundColor Green

$TempPortable = Join-Path $env:TEMP "EasyTools-$Tag"
if (Test-Path $TempPortable) {
    Remove-Item -Recurse -Force $TempPortable
}
Copy-Item -Recurse "deploy_dist" $TempPortable
$TargetZip = "$ReleaseDir\EasyTools-$Tag-win-x64-portable.zip"
Compress-Archive -Path $TempPortable -DestinationPath $TargetZip -CompressionLevel Optimal
Remove-Item -Recurse -Force $TempPortable
Write-Host "[OK] 绿色便携包已压缩: $TargetZip" -ForegroundColor Green

$Checksums = @()
Get-ChildItem -Path $ReleaseDir -File | ForEach-Object {
    $Hash = (Get-FileHash -Path $_.FullName -Algorithm SHA256).Hash.ToLower()
    $Checksums += "$Hash  $($_.Name)"
}
$ChecksumFile = "$ReleaseDir\SHA256SUMS.txt"
$Checksums | Out-File -FilePath $ChecksumFile -Encoding utf8
Write-Host "[OK] 校验和文件已生成: $ChecksumFile" -ForegroundColor Green

git tag $Tag -f
git push origin $Tag -f

$Notes = @"
### EasyTools $Tag 发布摘要

- ⚡ **手势识别精度与抗倾斜增强**：优化基准方向磁吸区容差判定与连续折线拟合算法，彻底解决画「下」手势轻微左斜被误判为「下-左」组合手势的痛点；
- 🎯 **手势目标窗口穿透与避让**：重构覆盖层命中测试与 Z-Order 避让机制，确保快捷键与窗口指令 100% 精准投递至光标下方的真实外部应用；
- 🔔 **启动状态微反馈**：程序初始化就绪后通过原生 Toast 提供轻量优雅的就绪提示，克制且不夺取前台焦点；
- 🎨 **全新极客品牌视觉资产体系**：主程序与托盘图标全面重构为高定原画级流线型微标识，采用深空蓝超椭圆（Midnight Squircle）底座与大半径柔和悬浮光影；
- 🖼️ **About 关于页与品牌大展台 (Grand Showcase)**：关于页面新增专属 C 位特写展台，配备动态能量光晕、缓动悬浮呼吸动画与钛金质感排版；
- 📐 **适度黄金比例默认窗口尺寸**：设置窗口恢复为经典的 1100x750 舒适默认尺寸，消除在高分屏与多屏环境下的全屏压迫感，并增加自适应平滑居中；
- 🎵 **全局多媒体控制支持**：新增上一曲、下一曲、播放/暂停、静音、音量调节等内置全局多媒体命令。
"@

$NotesFile = "$ReleaseDir\RELEASE_NOTES.md"
$Notes | Out-File -FilePath $NotesFile -Encoding utf8

gh release create $Tag $TargetSetup $TargetZip $ChecksumFile --title "EasyTools $Tag (Windows x64)" --notes-file $NotesFile --latest

Write-Host "=========================================" -ForegroundColor Green
Write-Host "EasyTools $Tag 发布成功！" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
