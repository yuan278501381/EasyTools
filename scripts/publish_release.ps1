param(
    [string]$Tag = "v1.0.7",
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

- ⚡ **DirectComposition 硬件合成加速**：手势轨迹与 Toast HUD 原生引入 DirectComposition 硬件合成渲染管线，在高刷屏（120Hz/144Hz/240Hz）与多显示器环境下彻底消除撕裂与延迟；
- 🛡️ **以管理员身份运行与 UAC 提权联动**：通用设置新增「以管理员身份运行」配置开关，开启后立即弹出 UAC 提权并平滑重启，全面解决管理员窗口上手势与快捷键失效的问题；
- 🎙️ **录屏专属热键动态武装（HotkeyArmScope）**：录屏暂停快捷键（默认 Ctrl+Shift+P）改为仅在录屏会话期间按需武装，日常状态自动释放，彻底解决与 VS Code / Cursor 等编辑器命令面板的快捷键冲突；
- 🔔 **启动与就绪 Toast 全面优化**：浮窗迁移至工作区底部居中（避免遮挡应用顶部标题栏与系统任务栏），配合白色高反差加粗边框与即时清爽退出（hideNow）机制；
- 🖥️ **High-DPI 渲染与 WebView2 物理像素矫正**：全面解决 Windows 150%/200% 缩放下界面发糊与膨胀的底层 Bug，原生启用 RAW_PIXELS 物理像素映射，字字锐利清晰；
- 🔍 **普通权限 SCM 搜索降级**：非管理员运行因权限不足无法启动系统服务时，自动平滑降级为便携索引引擎，彻底保障全盘搜索开箱即用；
- 🎯 **手势目标窗口穿透与智能关窗兜底**：重构覆盖层命中测试与 Z-Order 避让机制，并针对 CEF/Qt 等单窗口应用提供智能升格关窗与 Alt+F4 稳健回退；
- 🎨 **全新极客品牌视觉资产体系**：主程序与托盘图标全面重构为高定原画级流线型微标识，采用深空蓝超椭圆底座与柔和悬浮光影；
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
