param(
    [string]$Tag = "",
    [switch]$ForceRebuild = $false
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }
Set-Location $ScriptDir

$VersionFile = Join-Path $ScriptDir "VERSION"
if (-not (Test-Path -LiteralPath $VersionFile)) {
    throw "缺少唯一版本源: $VersionFile"
}
$ProjectVersion = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if ($ProjectVersion -notmatch '^\d+\.\d+\.\d+$') {
    throw "VERSION 必须是稳定 SemVer（例如 1.2.3），当前值: $ProjectVersion"
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
    $Tag = "v$ProjectVersion"
}
if ($Tag -notmatch '^v\d+\.\d+\.\d+$') {
    throw "发布标签必须使用 vMAJOR.MINOR.PATCH 格式，当前值: $Tag"
}
$CleanVersion = $Tag.Substring(1)
if ($CleanVersion -ne $ProjectVersion) {
    throw "发布标签 $Tag 与 VERSION ($ProjectVersion) 不一致。请只修改根目录 VERSION。"
}

$DirtyFiles = @(git status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0) { throw "无法读取 Git 工作树状态" }
if ($DirtyFiles.Count -gt 0) {
    throw "发布要求干净工作树；请先审查并提交所有改动。"
}

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "发布 EasyTools $Tag (版本号: $CleanVersion) 到 GitHub Releases" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# VERSION 是唯一产品版本源。发布过程只读取和校验，绝不在构建中改写源码。
# 如果强制重构或尚无安装包，触发 deploy.ps1 重新打包。
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

$HeadCommit = (git rev-parse HEAD).Trim()
$ExistingTagCommit = git rev-list -n 1 $Tag 2>$null
if ($LASTEXITCODE -eq 0) {
    if ($ExistingTagCommit.Trim() -ne $HeadCommit) {
        throw "标签 $Tag 已指向其他提交；发行标签不可移动或强制覆盖。"
    }
    Write-Host "[OK] 标签 $Tag 已正确指向当前提交" -ForegroundColor Green
} else {
    git tag -a $Tag -m "EasyTools $Tag"
    if ($LASTEXITCODE -ne 0) { throw "创建标签 $Tag 失败" }
}
git push origin "refs/tags/$Tag"
if ($LASTEXITCODE -ne 0) { throw "推送标签 $Tag 失败" }

$NotesFile = Join-Path $ScriptDir "docs\RELEASE_NOTES_$Tag.md"
if (-not (Test-Path $NotesFile)) {
    $NotesFile = Join-Path $ScriptDir "CHANGELOG.md"
}

gh release create $Tag $TargetSetup $TargetZip $ChecksumFile --title "EasyTools $Tag (Windows x64)" --notes-file $NotesFile --latest

Write-Host "=========================================" -ForegroundColor Green
Write-Host "EasyTools $Tag 发布成功！" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
