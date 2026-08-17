# EasyTools

<div align="center">

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C++-20-00599C.svg?logo=c%2B%2B)
![Direct2D](https://img.shields.io/badge/DirectX-Direct2D%20%2F%20DXGI-0078D7.svg)
![React 19](https://img.shields.io/badge/React-19-61dafb.svg?logo=react)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%2F%2011%20(x64%20%2F%20ARM64)-0078D6.svg?logo=windows)
![Build](https://github.com/yuan278501381/easyTools/actions/workflows/build.yml/badge.svg)
![Coverage](https://img.shields.io/badge/Coverage-100%25-brightgreen.svg)

**一款极致轻量、亚毫秒级响应、具备世界级交互美学的 Windows 现代桌面效率工具箱**

[特性概览](#-核心特性) • [快速开始](#-快速开始-quick-start) • [手势与快捷键](#-手势与快捷键速查) • [架构设计](#-架构与性能工程) • [源码构建](#-从源码构建与开发)

</div>

---

## 📖 项目简介

**EasyTools** 是一款面向极客与高频日常操作的 Windows 效率神器。
- **底层内核**：采用现代 **C++20 + Direct2D / DXGI / WASAPI / Win32 原生 API**，提供 1000Hz 鼠标钩子零延迟捕获、亚毫秒级硬件加速渲染、GPU 零拷贝 4K 录屏与毫秒级 MFT 全盘搜索；
- **用户界面**：采用 **React 19 + TypeScript + WebView2**，带来丝滑流畅、支持多主题切换与 High-DPI 全链路自适应的现代化视觉体验；
- **设计哲学**：**高性能、极致易用、冷路径物理内存深度回缩**，杜绝任何卡顿与冗余消耗。

---

## ✨ 核心特性

| 模块 | 核心能力与技术亮点 |
| :--- | :--- |
| 🖱️ **鼠标手势** | Direct2D 硬件加速流光轨迹，300 点滑窗缓冲杜绝绘制积压；原子性按键分发；粗白圆角大气 Toast HUD；15 秒超时调侃反馈与动作智能执行。 |
| 🪟 **热角与轮盘** | 屏幕四角热区触发自定义动作，呼出式径向轮盘快捷菜单，支持多层级自定义排序与快速分发。 |
| 📸 **智能截图 & 贴图** | 多显示器智能跨屏感知与 DPI 适配；窗口元素自动吸附；像素级矢量标注；一键将剪贴板/截图钉在桌面顶层（Pin）。 |
| 📜 **滚动长截图** | 智能视觉特征分析与自动向下滚动拼接；支持快速长图生成与实时缩放预览。 |
| 🎥 **4K 屏幕录像** | DXGI Desktop Duplication GPU 零拷贝捕获（极低 CPU 占用）；WASAPI 系统声与麦克风双通道实时混音与独立增益；支持 H.264 / H.265 / VP9 / GIF 编码。 |
| 🔤 **离线 OCR** | 基于 Windows 10/11 原生引擎，毫秒级离线提取屏幕中的文字与表格，支持一键复制到剪贴板。 |
| 🔍 **极速全盘搜索** | 后台 Windows NTFS USN/MFT 索引服务，安全命名管道 IPC 通信，毫秒级瞬时检索数百万个文件。 |
| ⌨️ **按键回显 (Keycast)** | 广义修饰键与组合键全屏置顶 HUD；粗圆角纯白边框 + 深灰毛玻璃底板；内置按键敲击次数与鼠标移动热力图统计。 |
| 🧠 **冷路径物理内存回缩** | 遵循“冷路径退场修剪，热操作期间绝不修剪”原则；窗口隐藏时自动调用 `TrySuspend` 挂起 Chromium 管线并主动调用 `WinUtils::trimWorkingSet()` 归还物理内存。 |

---

## 🚀 快速开始 (Quick Start)

### 方式一：下载开箱即用（推荐用户）

1. 从 [Releases 页面](https://github.com/yuan278501381/easyTools/releases) 下载最新版本的安装包 `EasyTools-Setup.exe` 或绿色便携版压缩包；
2. 运行安装程序或直接解压运行 `deploy_dist/EasyTools.exe`；
3. 软件启动后常驻系统托盘，所有功能即刻可用。

---

## ⌨️ 手势与快捷键速查

### 1. 默认全局快捷键

| 功能 | 默认快捷键 | 说明 |
| :--- | :--- | :--- |
| **区域截图** | <kbd>Alt</kbd> + <kbd>A</kbd> | 唤起智能选区截图，支持标注与贴图 |
| **滚动长截图** | <kbd>Alt</kbd> + <kbd>S</kbd> | 滚动捕获超长页面或对话记录 |
| **屏幕录像** | <kbd>Alt</kbd> + <kbd>R</kbd> | 开启 4K / 高清区域录屏，支持声音混音 |
| **快速搜索** | <kbd>Alt</kbd> + <kbd>Space</kbd> | 呼出全盘文件与应用毫秒级极速搜索框 |
| **设置中心** | <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>S</kbd> | 打开 EasyTools 控制面板与个性化配置 |

> 💡 *所有全局快捷键均可在「设置 → 通用设置」中自由修改或禁用。*

### 2. 常用鼠标手势（按住鼠标右键划线）

| 轨迹代号 | 视觉手势方向 | 默认绑定动作 | 作用场景说明 |
| :---: | :---: | :--- | :--- |
| **`L`** | ⬅️ 向左 | **后退** | 浏览器 / 文件资源管理器后退 |
| **`R`** | ➡️ 向右 | **前进** | 浏览器 / 文件资源管理器前进 |
| **`U`** | ⬆️ 向上 | **最大化 / 还原** | 切换当前活动窗口的最大化与还原 |
| **`D`** | ⬇️ 向下 | **最小化** | 最小化当前活动窗口 |
| **`D-R`** | ⬇️ ➡️ 下再向右 | **关闭标签页 / 窗口** | 关闭当前浏览器标签页或应用程序窗口 |
| **`R-U`** | ➡️ ⬆️ 右再向上 | **恢复关闭的标签页** | 浏览器中重新打开最近关闭的标签页 |
| **`U-R`** | ⬆️ ➡️ 上再向右 | **切换到下一个标签页** | 快速切换浏览器或多标签应用中的下一个 Tab |
| **`U-L`** | ⬆️ ⬅️ 上再向左 | **切换到上一个标签页** | 快速切换浏览器或多标签应用中的上一个 Tab |
| **`D-U`** | ⬇️ ⬆️ 下再向上 | **刷新** | 刷新当前页面（F5） |
| **`U-D`** | ⬆️ ⬇️ 上再向下 | **新建标签页** | 新建空白标签页（Ctrl+T） |
| **`L-D`** | ⬅️ ⬇️ 左再向下 | **显示桌面** | 一键最小化所有窗口并显示桌面（Win+D） |
| **`R-D`** | ➡️ ⬇️ 右再向下 | **任务视图** | 呼出 Windows 虚拟桌面与任务视图（Win+Tab） |
| **`D-R-D`** | ⬇️ ➡️ ⬇️ 下-右-下 | **区域截图** | 快速唤起 EasyTools 屏幕截图工具 |
| **`•••`** | 持续画满 15 秒 | **调侃报错状态** | 持续划线超过 15 秒不松手时展示红底 3 个大圆点，松手后安全淡出 |

---

## 🛠️ 从源码构建与开发

### 环境准备

- **操作系统**：Windows 10 / 11 (x64 或 ARM64)
- **编译工具链**：Visual Studio 2022 (安装 `C++ 桌面开发` 组件，支持 C++20 标准)
- **脚本引擎**：PowerShell 7+ (pwsh)
- **前端环境**：Node.js 20+ 及 npm
- **C++ 包管理器**：[vcpkg](https://github.com/microsoft/vcpkg)
- **安装包编译器**（可选）：[Inno Setup 6](https://jrsoftware.org/isdl.php)

### 一键构建与自动化门禁

在项目根目录下运行原子发布与构建脚本：

```powershell
# 完整发布构建（包含前端编译、C++ Release 构建、全单元测试门禁与安装包生成）
pwsh -ExecutionPolicy Bypass -File .\deploy.ps1 -Configuration Release

# 快速增量构建（跳过安装包压缩，仅输出 deploy_dist 绿色运行目录）
pwsh -ExecutionPolicy Bypass -File .\deploy.ps1 -Quick -SkipInstaller
```

### 单独调试前端 UI

```powershell
Set-Location ui
npm install
npm run dev           # 启动 Vite 开发热重载服务器 (http://localhost:5173)
npm run lint          # 执行 ESLint 静态代码检查
npm run i18n-check    # 执行多语言缺失键与完整性检查
npm run check-css     # 执行 CSS 变量全声明无悬空检查
npm run build         # 构建单文件内联生产资源
```

### 单独运行 C++ 单元测试

```powershell
.\build\bin\Release\EasyToolsTests.exe
```

---

## 🏗️ 架构与性能工程

### 1. 进程与模块拓扑

```text
EasyTools.exe (主宿主进程 / WebView2 UI / 事件主循环)
├── EasyCore.dll                 配置持久化、IPC 管道、日志(Spdlog)、热键钩子、物理内存修剪
├── plugins/
│   ├── Plugin_Gesture.dll       鼠标手势引擎、热角检测、径向轮盘菜单
│   ├── Plugin_Capture.dll       截图标注、置顶贴图、滚动长截图、4K 录屏、Windows OCR
│   ├── Plugin_Search.dll        NTFS 搜索客户端、IPC 管道桥接
│   └── Plugin_Keycast.dll       按键实时回显、热力统计
├── EasyTools_Service.exe        机器级 NTFS / MFT 极速文件索引服务
└── ui/index.html                React 19 单文件高内聚前端生产资源
```

### 2. 核心架构亮点

1. **单 Environment 多窗口复用**：
   - 设置窗口、搜索浮窗与托盘菜单共享同一个 WebView2 浏览器环境，避免重复加载 Chromium 运行时，节省数百兆内存；
2. **冷路径退场修剪与深度休眠**：
   - 窗口隐藏时通过 `ICoreWebView2_3::TrySuspend()` 挂起 Chromium 渲染管线，并在截图/录屏/OCR 等重型任务结束后主动调用 `WinUtils::trimWorkingSet()` 归还物理工作集；
   - 严禁在 1000Hz 鼠标钩子或 60FPS 渲染循环等热路径中修剪，彻底杜绝软缺页卡顿；
3. **原子性按键投递模型**：
   - 快捷键执行在单次 `SendInput` 调用中原子性提交完整 Down + Up 序列，彻底杜绝多线程环境下的按键粘滞与幽灵按键叠加；
4. **全链路 Per-Monitor V2 High-DPI 适配**：
   - 屏幕缩放、字号排版、内边距、圆角与点击命中区域在多显示器混合 DPI 场景下完美自适应。

---

## 📄 开源许可证

本项目采用 [MIT 许可证](LICENSE) 开源。欢迎提交 Issue 与 Pull Request！
