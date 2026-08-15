# EasyTools

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C++-20-blue.svg)
![React](https://img.shields.io/badge/React-19-61dafb.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%2B-lightgrey.svg)
![Build](https://github.com/yuan278501381/easyTools/actions/workflows/build.yml/badge.svg)

EasyTools 是一款面向高频日常使用的 Windows 桌面效率工具。原生 C++20
负责全局输入、截图录屏、索引与窗口生命周期；React 19 + WebView2 提供
设置、搜索和托盘界面。项目当前版本为 1.0.0。

## 功能

- 鼠标手势：可编辑映射、应用作用域、轨迹、暂停快捷键和执行失败反馈。
- 热角与轮盘菜单：四角动作、自定义轮盘项、排序与持久化。
- 截图与贴图：多显示器选区、窗口吸附、标注、历史、剪贴板和贴图管理；截图、
  标注、长截图与录屏提供可关闭的左下角上下文快捷键提示，跨屏时按每台显示器
  的 DPI 在 100%–500% 范围动态重排，且不会进入截图或录屏结果。
- 长截图与录屏：滚动拼接；H.264、H.265、VP9、GIF 编码与区域录制；系统声音
  与麦克风可独立开启并实时混音。
- 离线 OCR：使用 Windows.Media.Ocr，支持结果窗口和剪贴板输出。
- 快速搜索：后台 NTFS/MFT 索引服务、安全命名管道、缓存和多卷查询。
- 按键回显与统计：可开关的回显层、热力图、历史与汇总。
- 原生浮层统一采用 Per-Monitor V2 DPI：系统通知、按键回显、手势轨迹、轮盘、
  截图工具栏、长截图预览与录屏状态条会在 100%/125%/150%/200% 及混合缩放屏幕
  间保持一致的物理尺寸、排版和命中区域，并自动跟随当前操作所在显示器。
- 通用能力：中英文、亮色/暗色/跟随系统、快捷键重绑定、配置导入导出、
  自动更新检查、日志和崩溃转储。
- 按需模块：手势、截图录屏、文件搜索和按键回显均可独立停用；重启后禁用
  模块不会映射 DLL，也不会注册线程、钩子、快捷键或 IPC 处理器。
- 插件在映射 DLL 前验证旁路清单，加载时再次完成二进制 ABI 握手；开发约定见
  [插件开发文档](docs/plugin-development.md)。

默认快捷键可在“通用设置”中查看和修改。首次安装会注册机器级文件搜索
服务，因此安装程序需要管理员权限；用户配置和开机自启动由应用按当前用户
管理。

## 架构

```text
EasyTools.exe
├── EasyCore.dll                 配置、IPC、事件、日志、热键、统计、更新
├── plugins/
│   ├── Plugin_Gesture.dll       手势、热角、轮盘菜单
│   ├── Plugin_Capture.dll       截图、标注、贴图、录屏、OCR
│   ├── Plugin_Search.dll        搜索客户端与 UI 接口
│   └── Plugin_Keycast.dll       按键回显
├── EasyTools_Service.exe        NTFS/MFT 文件索引服务
└── ui/index.html                单文件 React 生产资源
```

设置、搜索和托盘窗口复用同一个 WebView2 Environment，避免重复浏览器进程和
配置目录。模块开关采用持久化配置和安全的重启边界：界面会同时展示目标状态、
本次运行状态与加载故障，避免把仍有外部回调的 DLL 强制热卸载。应用退出时则按
逆序停止插件线程、清理外部回调，再卸载 DLL。配置写入采用临时文件替换和回滚，
文件监控可区分自身写入与外部修改。

IPC 与进程内事件订阅使用静默期屏障：插件关闭时先拒绝新调用，再等待在途回调
归零后销毁可调用对象，避免 DLL 卸载后的悬空函数。录屏使用单线程有界管道和
单调时钟调度；设备过载时主动跳过时间槽并保留 PTS 间隔，不会堆积帧或加速成片。
录屏对单显示器区域优先使用 DXGI Desktop Duplication GPU 捕获，并把映射后的
BGRA 帧直接交给 FFmpeg；跨显示器、旋转屏、远程会话或运行时设备丢失会自动
回退到 GDI。诊断接口会报告实际捕获后端和编码器；硬件编码优先尝试，初始化
失败时自动回退到软件编码器。
系统光标通过小区域原位 Alpha 合成写入视频，转换后立即恢复原像素，不产生额外
整帧副本；用户可以分别关闭光标或启用轻量点击扩散反馈。
音频使用事件驱动 WASAPI 共享模式采集，统一重采样为 48 kHz 立体声并通过固定
10 ms、有界队列混合；MP4 写入 AAC、WebM 写入 Opus。设备断开、格式不支持或
编码器不可用时仅关闭对应音轨，视频管道继续运行。
设置页可分别选择系统输出与麦克风设备，也可跟随 Windows 默认设备；已选择设备
离线时会自动回退默认设备。录制悬浮条显示双通道实时电平，诊断接口同时暴露
设备活动状态、峰值、缓冲中断与音频丢帧。
系统声和麦克风可设置 0–200% 独立增益；录制中调整会无锁生效。悬浮条 S/M
按钮以及可重绑定的全局快捷键可以即时静音单个音轨，不重建设备或编码器。
录制可配置立即开始或 3/5/10 秒倒计时；倒计时可随时取消且不产生空文件。媒体
先写入同目录 `.partial` 文件，至少生成一帧并完成 FFmpeg 封尾后才原子替换正式
文件，应用异常或机器掉电不会破坏已有同名成片。

## 构建、测试与发布

环境要求：Windows 10/11 x64、MSVC C++ 工具链、PowerShell、Node.js 24，
以及可用的 vcpkg。Inno Setup 6 为安装包构建依赖；未安装时仍可生成便携版。

在仓库根目录执行：

```powershell
.\deploy.ps1 -Configuration Release
```

发布脚本会依次执行：

1. `npm ci`、ESLint、i18n 键校验、TypeScript 与 Vite 生产构建；
2. 固定版本的 WebView2 SDK 检查、CMake 配置和 MSVC Release 构建；
3. CTest；任何测试失败都会中止发布；
4. 产物完整性校验和 `deploy_dist` 原子替换；
5. 若存在 Inno Setup，生成 `Output/EasyTools-Setup.exe`。

前端单独检查：

```powershell
Set-Location ui
npm ci
npm run lint
npm run i18n-check
npm run build
```

原生测试位于 `tests/unit/test_main.cpp`，由 CTest 注册为 `EasyTools.Unit`。
GitHub Actions 使用相同发布脚本，上传便携版和安装程序，并为便携版生成
SLSA 构建来源证明。

## 目录

```text
src/core/       核心基础设施、更新与主线程调度
src/gesture/    手势、作用域、热角与轮盘菜单
src/capture/    截图、标注、贴图、长截图和录屏
src/ocr/        Windows OCR 与结果窗口
src/search/     搜索插件客户端
src/service/    NTFS/MFT 索引服务
src/keycast/    按键回显插件
src/ui/         WebView2 原生窗口宿主
ui/             React/TypeScript 前端
tests/          自动化测试
```

## 许可证

[MIT](LICENSE)
