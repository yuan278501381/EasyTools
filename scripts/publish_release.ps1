param(
    [string]$Tag = "v1.0.4"
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "发布 EasyTools $Tag 到 GitHub Releases" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

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

- 🎯 **鼠标下方窗口精准控制**：手势操作（关闭/最大化/最小化/置顶/浏览器前进后退等）默认直接作用于鼠标指针所在的窗口，无需手动点击切换前台；
- 🎵 **全局多媒体控制支持**：新增上一曲、下一曲、播放/暂停、静音、音量调节等内置全局多媒体命令，保持全局广播不破坏窗口焦点；
- 📏 **统计大盘米制单位**：鼠标移动距离由像素换算为直观的国际标准米 (m) 展示；
- ✨ **全界面 UI 矢量化升级**：画板水印、状态标题与高频徽章全面移除原生 emoji，重构为微胶囊发光容器与高质感 Lucide SVG 矢量图标；
- ⚡ **原子性按键分发**：彻底移除中间延时，单次系统调用原子提交，根除修饰键粘滞；
- 🎨 **原作者官方署名**：统一遵循 Yy1 (@yuan278501381) 与 MIT 开源版权规范。
"@

$NotesFile = "$ReleaseDir\RELEASE_NOTES.md"
$Notes | Out-File -FilePath $NotesFile -Encoding utf8

gh release create $Tag $TargetSetup $TargetZip $ChecksumFile --title "EasyTools $Tag (Windows x64)" --notes-file $NotesFile --latest

Write-Host "=========================================" -ForegroundColor Green
Write-Host "EasyTools $Tag 发布成功！" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
