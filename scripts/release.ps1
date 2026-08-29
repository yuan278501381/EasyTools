<#
.SYNOPSIS
    EasyTools 世界级全自动发版流水线 (DevOps Master Release Pipeline)
.DESCRIPTION
    一键完成：版本自增 +1 -> 提交 Release 说明 -> 非快进合并至 main -> 全量编译与 7 重端到端测试门禁 -> 
    EXE 版本强校验 -> 资产打包与 SHA256 校验 -> Git 打 Tag 与全量推送 -> GitHub Release 官方发布 -> 自动检出下一阶段特性分支。
.EXAMPLE
    pwsh scripts/release.ps1                 # 默认补丁版本自增 +1 (如 1.0.5 -> 1.0.6)
    pwsh scripts/release.ps1 -Bump Minor     # 次版本自增 +1 (如 1.0.5 -> 1.1.0)
    pwsh scripts/release.ps1 -Version 1.0.6  # 显式指定目标版本
#>
param(
    [ValidateSet("Patch", "Minor", "Major")]
    [string]$Bump = "Patch",
    [string]$Version = "",
    [string]$NextFeatureBranch = "",
    [switch]$SkipGitHubPublish = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
Set-Location $ScriptDir

Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host " 🚀 EasyTools DevOps 一键全自动发版总控流水线" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

# 1. 检查 Git 工作树与分支状态
$CurrentBranch = (git branch --show-current).Trim()
if ([string]::IsNullOrWhiteSpace($CurrentBranch)) {
    throw "无法获取当前 Git 分支，请检查 Git 环境！"
}

$DirtyFiles = @(git status --porcelain --untracked-files=all)
if ($DirtyFiles.Count -gt 0) {
    Write-Host "[ERROR] 检测到工作区有未提交的改动，请先提交或暂存：" -ForegroundColor Red
    $DirtyFiles | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    throw "发版流水线要求干净的工作区！"
}

# 2. 版本计算与唯一事实源更新
$VersionFile = Join-Path $ScriptDir "VERSION"
if (-not (Test-Path -LiteralPath $VersionFile)) {
    throw "缺少版本单一事实源: $VersionFile"
}
$CurrentVersion = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($CurrentVersion -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    throw "当前 VERSION 格式不合规: $CurrentVersion"
}
$Major = [int]$Matches[1]
$Minor = [int]$Matches[2]
$Patch = [int]$Matches[3]

$TargetVersion = ""
if (-not [string]::IsNullOrWhiteSpace($Version)) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') {
        throw "指定的版本号格式必须为 X.Y.Z，当前值: $Version"
    }
    $TargetVersion = $Version
} else {
    switch ($Bump) {
        "Major" { $Major++; $Minor = 0; $Patch = 0 }
        "Minor" { $Minor++; $Patch = 0 }
        "Patch" { $Patch++ }
    }
    $TargetVersion = "$Major.$Minor.$Patch"
}

$TargetTag = "v$TargetVersion"
Write-Host "[INFO] 当前版本: v$CurrentVersion" -ForegroundColor Yellow
Write-Host "[INFO] 发行版本: $TargetTag (版本号: $TargetVersion)" -ForegroundColor Green

# 3. 检查并生成 Release Notes 模板
$NotesFile = Join-Path $ScriptDir "docs\RELEASE_NOTES_$TargetTag.md"
if (-not (Test-Path $NotesFile)) {
    Write-Host "[INFO] 生成 Release Notes 模板: $NotesFile" -ForegroundColor Cyan
    @"
# EasyTools $TargetTag 官方正式版发布说明

欢迎体验 **EasyTools $TargetTag**！本次更新专注于解决日常使用中的体感痛点，带来更稳定、丝滑的极客桌面效率体验。

---

## 🌟 本次重点更新与体验改进

### 1. 核心功能演进与体验提升
- 优化日常使用体感与操作响应速度；
- 修复已知使用问题与边缘交互缺陷。

### 2. 系统稳定性与架构加固
- 全模块生命周期与防死锁端到端保障；
- 跨系统（Windows 10/11/Server 2025）通用圆角与纯净渲染支持。
"@ | Set-Content -Path $NotesFile -Encoding utf8
}

# 4. 更新 VERSION 文件
Set-Content -Path $VersionFile -Value $TargetVersion -NoNewline -Encoding utf8
Write-Host "[OK] VERSION 已更新为: $TargetVersion" -ForegroundColor Green

# 5. 在开发分支提交版本升级 commit
git add VERSION docs/RELEASE_NOTES_$TargetTag.md CHANGELOG.md
$HasStaged = @(git diff --cached --name-only)
if ($HasStaged.Count -gt 0) {
    git commit -m "chore(release): 升级项目版本至 $TargetTag 并同步官方发布日志与版本元数据`n`n💼 业务角度：`n- 发布 EasyTools $TargetTag 官方正式版；`n- 同步生成以用户体感与痛点解决为视角的官方 Release Notes。`n`n🔧 技术角度：`n- 升级唯一事实源 VERSION 至 $TargetVersion；`n- 准备主分支发布与资产打包。"
    Write-Host "[OK] 版本元数据已在当前分支提交" -ForegroundColor Green
}

# 6. 切换至 main 分支并执行非快进合并
Write-Host "[GIT] 切换至 main 分支并执行 --no-ff 显式合并..." -ForegroundColor Cyan
git checkout main
if ($LASTEXITCODE -ne 0) { throw "切换至 main 分支失败！" }

git merge --no-ff $CurrentBranch -m "merge($CurrentBranch): 合并 $CurrentBranch 分支至 main 分支，发布 $TargetTag 正式版"
if ($LASTEXITCODE -ne 0) { throw "合并至 main 分支失败！" }
Write-Host "[OK] main 分支已成功完成显式非快进合并" -ForegroundColor Green

# 7. 全量清理并执行本地编译构建与 7 重端到端测试门禁
Write-Host "[BUILD] 执行全量编译构建与生命周期测试门禁..." -ForegroundColor Cyan
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue Output, deploy_dist, ui\dist, release_assets

$cmakeDir = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if (Test-Path $cmakeDir) { $env:PATH = "$cmakeDir;$env:PATH" }

& pwsh -ExecutionPolicy Bypass -File .\deploy.ps1 -Quick
if ($LASTEXITCODE -ne 0) { throw "构建部署门禁失败，终止发版！" }

# 8. EXE 二进制版本强校验门禁
$BuiltExe = "deploy_dist\EasyTools.exe"
if (-not (Test-Path $BuiltExe)) { throw "未找到编译产物: $BuiltExe" }
$ExeVersion = (Get-Item $BuiltExe).VersionInfo.ProductVersion
if ($ExeVersion -ne $TargetVersion) {
    throw "版本强校验失败！编译产物版本 ($ExeVersion) 与目标发版版本 ($TargetVersion) 不匹配！"
}
Write-Host "[OK] 二进制版本强校验通过: EasyTools.exe ProductVersion = $ExeVersion" -ForegroundColor Green

# 9. 资产归档与 SHA256 校验和生成
$ReleaseDir = "release_assets"
if (Test-Path $ReleaseDir) { Remove-Item -Recurse -Force $ReleaseDir }
New-Item -ItemType Directory -Path $ReleaseDir | Out-Null

$SetupExe = "Output\EasyTools-Setup.exe"
if (-not (Test-Path $SetupExe)) { throw "未找到安装包: $SetupExe" }
$TargetSetup = "$ReleaseDir\EasyTools-$TargetTag-Setup.exe"
Copy-Item $SetupExe $TargetSetup

$TempPortable = Join-Path $env:TEMP "EasyTools-$TargetTag"
if (Test-Path $TempPortable) { Remove-Item -Recurse -Force $TempPortable }
Copy-Item -Recurse "deploy_dist" $TempPortable
$TargetZip = "$ReleaseDir\EasyTools-$TargetTag-win-x64-portable.zip"
Compress-Archive -Path $TempPortable -DestinationPath $TargetZip -CompressionLevel Optimal
Remove-Item -Recurse -Force $TempPortable

$Checksums = @()
Get-ChildItem -Path $ReleaseDir -File | ForEach-Object {
    $Hash = (Get-FileHash -Path $_.FullName -Algorithm SHA256).Hash.ToLower()
    $Checksums += "$Hash  $($_.Name)"
}
$ChecksumFile = "$ReleaseDir\SHA256SUMS.txt"
$Checksums | Out-File -FilePath $ChecksumFile -Encoding utf8
Write-Host "[OK] 发布资产已就绪并生成 SHA256 校验和" -ForegroundColor Green

# 10. 创建 Tag 并推送到 GitHub
Write-Host "[GIT] 创建并推送标签 $TargetTag..." -ForegroundColor Cyan
git tag -a $TargetTag -m "EasyTools $TargetTag" -f
if ($LASTEXITCODE -ne 0) { throw "创建标签失败！" }

git push origin main
git push origin "refs/tags/$TargetTag" --force
if ($CurrentBranch -ne "main") {
    git checkout $CurrentBranch
    git merge main
    git push origin $CurrentBranch
}
Write-Host "[OK] Git 分支与 Tag 已全量同步至远程 GitHub" -ForegroundColor Green

# 11. GitHub Release 官方发布
if (-not $SkipGitHubPublish) {
    Write-Host "[GITHUB] 发布官方 GitHub Release ($TargetTag)..." -ForegroundColor Cyan
    $ReleaseExists = $false
    try {
        gh release view $TargetTag 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) { $ReleaseExists = $true }
    } catch {}

    if ($ReleaseExists) {
        gh release upload $TargetTag $TargetSetup $TargetZip $ChecksumFile --clobber
    } else {
        gh release create $TargetTag $TargetSetup $TargetZip $ChecksumFile --title "EasyTools $TargetTag (Windows x64)" --notes-file $NotesFile --latest
    }
    Write-Host "[OK] GitHub Release 发布成功！" -ForegroundColor Green
}

# 12. 检出后续特性分支
if ([string]::IsNullOrWhiteSpace($NextFeatureBranch)) {
    $NextFeatureBranch = $CurrentBranch
    if ($NextFeatureBranch -eq "main" -or $NextFeatureBranch -eq "dev") {
        $NextFeatureBranch = "feature/optimize-capture-and-recording"
    }
}
Write-Host "[GIT] 切换至下一阶段特性分支: $NextFeatureBranch" -ForegroundColor Cyan
git checkout -B $NextFeatureBranch main
git push origin -u $NextFeatureBranch

Write-Host "=======================================================" -ForegroundColor Green
Write-Host " 🎉 恭喜！EasyTools $TargetTag 全自动化发版流水线执行完毕！" -ForegroundColor Green
Write-Host " - 最新安装包: Output\EasyTools-Setup.exe" -ForegroundColor Green
Write-Host " - 发布资产库: release_assets" -ForegroundColor Green
Write-Host " - GitHub 发布页: https://github.com/yuan278501381/EasyTools/releases/tag/$TargetTag" -ForegroundColor Green
Write-Host " - 当前所在分支: $NextFeatureBranch" -ForegroundColor Green
Write-Host "=======================================================" -ForegroundColor Green
