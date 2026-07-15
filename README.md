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
- 截图与贴图：多显示器选区、窗口吸附、标注、历史、剪贴板和贴图管理。
- 长截图与录屏：滚动拼接；H.264、H.265、VP9、GIF 编码与区域录制。
- 离线 OCR：使用 Windows.Media.Ocr，支持结果窗口和剪贴板输出。
- 快速搜索：后台 NTFS/MFT 索引服务、安全命名管道、缓存和多卷查询。
- 按键回显与统计：可开关的回显层、热力图、历史与汇总。
- 通用能力：中英文、亮色/暗色/跟随系统、快捷键重绑定、配置导入导出、
  自动更新检查、日志和崩溃转储。

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
配置目录。插件关闭时先停止线程、清理外部回调，再卸载 DLL；配置写入采用临时
文件替换和回滚，文件监控可区分自身写入与外部修改。

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
