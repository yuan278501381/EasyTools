# EasyTools 🚀

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C++-20-blue.svg)
![React](https://img.shields.io/badge/React-18-61dafb.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%2010%2B-lightgrey.svg)
![Build](https://github.com/yuan278501381/easyTools/actions/workflows/build.yml/badge.svg)

**EasyTools** 是一款基于 C++20 与现代 Web 技术（React + Vite）打造的**世界级 Windows 桌面增强效率工具**。它将底层高性能操作与极致的 UI 体验完美融合，致力于在极简的操作流中为用户提供极其强大的生产力辅助。

---

## ✨ 核心特性 (Features)

### 🖱️ 智能鼠标手势 (Smart Mouse Gestures)
- **底层拦截机制**：采用 `WH_MOUSE_LL` 钩子并在关键节点实行精准拦截，真正做到防误触、防干扰。
- **动态应用黑白名单**：您可以为特定游戏或软件（如 Photoshop 等）配置黑名单，在这些软件内将自动熔断手势触发，原生消息零延迟透传。
- **Lua 脚本引擎驱动**：每一条手势动作（如启动程序、执行快捷键）均由极速的 Lua 引擎动态解析执行，具备无限扩展潜力。

### ✂️ 高性能桌面贴图 (PinWindow)
- **内存级零延迟渲染**：采用 `Direct2D` 搭配 `WS_EX_LAYERED` 透明无边框窗口实现。
- **任意停靠与悬浮**：截取屏幕任意区域，即可将其永久钉在桌面最顶层。支持透明度调节与穿透模式，非常适合写代码时对比参考图。

### 📜 智能滚屏长截图 (Scroll Capture)
- **OpenCV 视觉融合缝合**：抛弃了传统的像素强行拼接，底层引入 `cv::matchTemplate` 算法，在滚动时自动计算特征点和重叠区域。
- **动态到底识别**：即使遇到浮动导航栏或弹窗，也能智能规避并计算出完美的超长网页/文档截图。

### 🔤 极速 OCR 文本提取 (Screen OCR)
- 框选屏幕上的任意区域，系统即可在后台静默完成 OCR 识别，并将文本秒送至剪贴板，彻底告别对着图片手动打字的痛苦。

---

## 🏗️ 架构与技术栈 (Architecture)

本工程采用了前沿的 **B/S 混合架构**（即底层 C++ 服务端 + WebView2 前端容器）：

- **底层核心 (Backend)**: 纯正 `C++20` 编写。利用 Win32 API 控制系统级窗口，使用 Direct2D 处理高性能绘图。
- **渲染容器 (Bridge)**: 微软原生的 `Edge WebView2`，实现了极低的内存占用和现代浏览器特性支持。
- **用户界面 (Frontend)**: `React` + `TypeScript` + `Vite` 构建的丝滑前端设置页面，数据经由自研的 IPC (进程间通信) 桥与 C++ 实时双向绑定。
- **脚本与算法层**: `Lua` (借助 sol2) 处理灵活的业务逻辑配置，`OpenCV` 处理图像拼接，`FFmpeg` 预留视频录制接口。

---

## 🛠️ 构建与部署 (Build & Deploy)

本项目采用**工业级幂等构建流水线**，已彻底摆脱了复杂的本地环境依赖痛点。无论是在本地还是云端，仅需一键即可出包。

### 依赖管理器
- 前端：`npm` (Node.js 24)
- 后端：`Vcpkg` (Manifest 模式，无缝拉取 `opencv4`, `ffmpeg`, `spdlog`, `fmt`, `sol2`, `lua`, `nlohmann-json`)

### 自动云端构建 (GitHub Actions CI/CD)
本项目已接入 GitHub Actions。只要您向 `main` 分支推送代码，云端将自动：
1. 启动 `windows-latest` 机器。
2. 利用 Action Cache 极速命中 Vcpkg 预编译缓存（OpenCV等重型库秒级恢复）。
3. 执行自动化打包并发布为 `.zip` 产物。
4. **SLSA Provenance**：为生成的产物签发加密数字溯源防伪证明。

### 本地一键编译
如果您希望在本地编译，只需打开 PowerShell 7+，进入项目根目录：
```powershell
.\deploy.ps1 -Configuration Release
```
脚本将自动完成 npm 前端打包、下载 WebView2 SDK、CMake 配置、Vcpkg 依赖拉取以及最终的 MSVC 编译。您甚至不需要在本地全局安装 CMake 或下载任何 SDK！

---

## 📂 目录结构 (Repository Structure)

```text
easyTools/
├── .github/workflows/    # 世界级云端自动化 CI/CD 配置
├── src/                  # C++ 核心源码
│   ├── capture/          # 截图、长截图与贴图核心逻辑
│   ├── core/             # IPC 通信、日志 (spdlog)、全局配置 (ConfigManager)
│   ├── gesture/          # 鼠标底层钩子与引擎 (GestureEngine)
│   ├── ocr/              # 屏幕文本提取模块
│   └── tray/             # 系统托盘与主进程生命周期管理
├── ui/                   # 现代 React 前端页面源码
├── deploy.ps1            # 核心工业级幂等打包部署脚本
├── vcpkg.json            # Vcpkg 依赖清单
└── CMakeLists.txt        # 现代 CMake 工程配置
```

---

## 📝 许可证 (License)

本项目基于 [MIT License](LICENSE) 开源。

*打造属于未来的极简桌面效率工作流。*
