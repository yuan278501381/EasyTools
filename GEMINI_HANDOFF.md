# EasyTools → Gemini 开发交接文档

> 交接日期：2026-08-15  
> 工作区：`C:\repo\easyTools`  
> 当前分支：`main`  
> 当前 HEAD：`51ba667`  
> 重要：当前有大量**未提交工作**，工作树才是最新、最权威的状态。

## 1. 产品最终目标（不可缩减）

EasyTools 要成为一个开源、模块化、Windows 原生高性能的效率工具平台，对标并吸收以下产品的优秀体验：

- PixPin：截图、标注、长截图、贴图、操作提示；
- WGestures v2：鼠标手势、按应用作用域、轨迹和轮盘菜单；
- Snipaste Pro：快速截图、像素级标注、贴图交互；
- FocusSee：高质量录屏、鼠标强调和流畅反馈。

技术方向固定为：

- C++20 核心与性能敏感原生界面；
- React + TypeScript + WebView2 设置界面；
- 微内核/插件化架构；
- 用户可独立启用或停用截图、手势、按键展示、搜索等插件；
- 世界级性能、架构、鲁棒性、可访问性和用户体验；
- Windows Per-Monitor DPI Awareness V2，必须正确支持 100%、125%、150%、200% 以及混合缩放多屏。

原始固定需求见 `不变的需求.md`。**不要修改、覆盖或还原该文件**；它当前包含用户自己的工作树改动。

## 2. 接手前必须知道的工作树状态

当前不是干净仓库：

- 已跟踪但有改动：约 75 个文件（不计 `不变的需求.md`）；
- 未跟踪：约 30 个文件；
- 本轮累计差异约 5,500 行新增、1,300 行删除；
- 没有执行暂存、提交、reset 或 checkout；
- 不要使用 `git reset --hard`、`git checkout -- .` 或清理未跟踪文件；
- `task.md` 全部打勾只是旧的进度表，**不能作为功能完成的证据**；
- `implementation_plan.md` 同时包含愿景和历史设计，也不能替代源码与运行验证。

建议 Gemini 接手后的第一步：

1. 阅读本文件、`不变的需求.md`、`README.md`；
2. 执行 `git status --short`、`git diff --stat`；
3. 保留所有现有改动，先构建和测试；
4. 经人工确认后，将当前成果拆成有意义的提交；不要把 `不变的需求.md` 混入代码提交。

## 3. 当前架构

| 层 | 当前实现 |
|---|---|
| Host | `EasyTools.exe`，托盘、设置窗、搜索窗、共享 WebView2 环境、主线程调度 |
| Core | `EasyCore.dll`，配置、日志、事件总线、IPC、快捷键、插件管理、Lua、性能统计、更新检查 |
| 插件 | `Plugin_Gesture.dll`、`Plugin_Capture.dll`、`Plugin_Keycast.dll`、`Plugin_Search.dll` |
| 插件元数据 | `plugins/*.plugin.json.in`，Host 在加载 DLL 前读取并校验 ABI/版本/依赖 |
| 原生绘制 | Direct2D/DirectWrite/GDI/DXGI，用于截图层、手势轨迹、Toast、OCR、录制状态等 |
| 设置 UI | `ui/` 下 React + TypeScript + Vite，经 WebView2 承载 |
| 搜索服务 | `EasyTools_Service.exe`，包含 MFT/拼音相关实现 |

插件开发说明见 `docs/plugin-development.md`。

## 4. 本阶段已经落地的主要成果

### 4.1 插件与基础架构

- 插件启停状态持久化；禁用插件在启动时不会先加载重型 DLL；
- 插件 manifest、ABI/宿主版本/依赖校验；
- 插件卸载前 quiescence、事件订阅和 IPC handler 清理；
- EventBus、MessageBridge、HotkeyManager 的生命周期和并发保护；
- 主线程调度器用于将后台 OCR 等结果安全送回 UI 线程；
- 插件设置页、状态/错误反馈、可访问的控件语义；
- 性能监测数据与关键打开/录制延迟记录。

关键文件：

- `src/core/plugin/PluginManager.*`
- `src/core/plugin/PluginManifest.*`
- `src/core/events/EventBus.*`
- `src/core/ipc/MessageBridge.*`
- `ui/src/pages/PluginsPage.*`

### 4.2 截图、录屏和长截图性能

- 录屏新增自动捕获后端：优先 DXGI Desktop Duplication，失败回退 GDI BitBlt；
- FFmpeg 编码管线补强，支持 H.264/H.265/VP9/GIF 的后端枚举和硬件编码探测；
- 系统声与麦克风 WASAPI 捕获、混音、静音控制；
- 录制鼠标、点击强调、磁盘空间保护、临时文件与失败清理；
- 截图冻结帧和 DIB 数据路径减少重复复制；
- 标注合成只在内容变化时刷新，不再每帧 clone/重绘整图；
- 长截图加入有界预览和更严格的停止/资源释放；
- 录屏状态条降低静止刷新频率；
- 截图工具栏布局在高 DPI 和窄屏时可自动换行。

关键文件：

- `src/capture/CaptureBackend.*`
- `src/capture/ScreenRecorder.*`
- `src/capture/AudioCapture.*`
- `src/capture/CursorOverlay.*`
- `src/capture/CaptureRenderer.*`
- `src/capture/CaptureToolbarLayout.*`
- `src/capture/ScrollCapture.*`

### 4.3 快捷键提示 UX

新增了生产级原生快捷键提示层，在当前操作屏幕左下角克制展示，不抢焦点、不进入截图/录屏内容，并能随上下文更新：

- 截图框选；
- 选区完成/标注；
- 录屏区域选择；
- 长截图；
- 录制中；
- 录制暂停。

用户可在截图设置中关闭。提示支持中英文、自动换行和 PMv2 DPI。

关键文件：

- `src/capture/ShortcutHintOverlay.*`
- `src/capture/ShortcutHintStyle.h`
- `tests/visual/shortcut_hint_preview.cpp`
- `ui/src/pages/CapturePage.tsx`

### 4.4 高分屏与混合 DPI

已新增统一 DPI 工具 `src/core/utils/DpiUtils.h`，并迁移/适配：

- 截图覆盖层与原生标注工具栏；
- 快捷键提示；
- Toast；
- Keycast 按键展示；
- 录制状态条；
- 长截图预览；
- 手势轨迹；
- 手势轮盘菜单；
- OCR 结果窗；
- 搜索窗（最新改动，已编译，尚缺视觉 E2E）；
- 托盘 WebView 菜单（最新改动，已编译，尚缺混合屏 E2E）。

150%（144 DPI）已验证的物理尺寸：

- Toast：900 × 120；
- Keycast：1200 × 240；
- OCR 结果窗：900 × 600；
- 截图快捷键提示高度：78 px；
- 手势轮盘：600 × 600。

纯布局指标测试覆盖 96/120/144/192 DPI，缩放上限为 5 倍。

### 4.5 OCR 结果窗（最后一批已完成改动）

旧实现有三个严重问题，现已修复：

1. 点击复制时持有 `std::mutex` 后再次调用 `render()`，同线程重入会死锁；
2. 窗口显示后永久保留 16ms 定时器，哪怕没有动画也持续唤醒；
3. 每次渲染都创建/销毁 DC 与 DIB，造成 GDI 抖动。

现在：

- DIB/DC 在可见期复用，隐藏时释放；
- 淡入完成即停止动画定时器；
- “已复制/复制失败”使用单次 2 秒状态定时器；
- 支持 Ctrl+C、Esc、滚轮、PgUp/PgDn、Home/End；
- 底部内嵌克制快捷键提示，不额外打断心流；
- 跟随当前显示器 DPI，并响应 DPI/显示变化；
- 默认不被截图/录屏捕获；
- 背景改为不透出后方内容，避免阅读干扰和隐私泄漏。

关键文件：

- `src/ocr/OcrResultWindow.*`
- `src/ocr/OcrResultStyle.h`
- `tests/visual/native_overlay_preview.cpp`

### 4.6 其他体验与鲁棒性

- Toast 默认反馈曾被永久关闭的问题已修复；
- Toast/Keycast/手势浮层均复用绘制资源并修复多处退出/句柄问题；
- Keycast 修复文字布局到窗口外、静止仍 60 FPS 刷新的问题；
- 手势轮盘/轨迹修复中文 UTF-8 标签、DPI、HBITMAP 双 DC 选择等问题；
- 手势巨型虚拟桌面位图改为按需创建，暂停/关闭时释放；
- 截图 Ctrl+C/Ctrl+S/Enter/Esc 语义与提示对齐；
- 前端补充焦点、键盘操作、错误反馈、国际化和控件可访问性；
- 新增 LICENSE、插件开发文档和视觉回归 helper。

## 5. 当前验证状态

最近一次已通过：

- Release `EasyTools` 主程序完整构建：通过（包括最新 Search/Tray DPI 改动）；
- Release `Plugin_Capture`：通过；
- 单元测试：交接前最后一次完整执行为 `709 断言，0 失败`（已包含 Search/Tray DPI 尺寸断言）；
- CTest：此前 1/1 通过；
- 原生视觉 helper：Toast、Keycast、OCR 在当前 144 DPI 机器上通过；
- 视觉 helper 的重复资源检查：通过；
- 快捷键提示 helper：六种上下文在 144 DPI 下通过；
- `npm run build`：此前通过；
- `npm run lint`：此前通过；
- `npm run i18n-check`：此前通过，仅有已知“中英文值相同”的提示；
- `git diff --check -- . ':(exclude)不变的需求.md'`：此前通过。

视觉样张位于 `build/`，例如：

- `native_overlay_ocr_150_v2_toast.bmp`
- `native_overlay_ocr_150_v2_keycast.bmp`
- `native_overlay_ocr_150_v2_ocr.bmp`

## 6. 尚未完成：建议优先级

### P0：先把当前大改动稳定成可维护基线

#### P0.1 完成剩余原生窗口的 PMv2 DPI

`SettingsWindow` 仍使用系统 DPI 和主屏尺寸创建：

- `src/ui/SettingsWindow.cpp` 仍调用 `WinUtils::getDpiScale()`（无窗口）；
- 仍用 `SM_CXSCREEN/SM_CYSCREEN` 居中在主屏；
- 没有显式处理 `WM_DPICHANGED`；
- 默认 1100×750 在 150% 下成为 1650×1125，可能超过 1080p 工作区；
- 应使用活动显示器 DPI、工作区约束、`WM_DPICHANGED` 推荐矩形，并保留用户手动调整后的尺寸/位置。

`PinWindow` 的图片本身应保持像素语义，但悬浮工具栏、描边、右键菜单附近交互仍需按 DPI 审计。`PinnedWindow.*` 是旧的重复实现，当前实际调用走 `PinWindow.*`；应确认无外部引用后删除旧实现，减少二义性和二进制体积。

最新新增的 `WebViewWindowStyle.h` 已被 Search/Tray 使用，并已加入以下单元断言；仍需真机混合屏验证：

- Search：96 → 800×600，144 → 1200×900，192 → 1600×1200；
- Tray：96 → 190×215，144 → 285×323（高度按四舍五入），192 → 380×430；
- 在 100% 主屏 + 150% 副屏之间反复打开，验证没有先小后跳、裁切或位置漂移。

#### P0.2 重新审计 `trimWorkingSet()`

项目多处调用 `SetProcessWorkingSetSize(-1,-1)`。这会强制回收**整个进程**的工作集，可能让下一次打开设置、截图或手势产生缺页抖动，并抵消 WebView2 预热。

当前调用点可用以下命令查看：

```powershell
rg -n 'trimWorkingSet\(' src
```

建议原则：

- 高频打开/关闭的 UI 不做进程级 trim；
- 释放真正持有的大位图、编码器、音频缓冲和 D2D 资源；
- 仅在显式“低内存模式”或系统内存压力事件下考虑 trim；
- 用首次/二次打开延迟和 hard page fault 数据验证，而不是只看任务管理器瞬时内存。

TrayWindow 的高频 trim 已移除；Settings、OCR、CaptureOverlay、Gesture、PinWindow、Recorder、ScrollCapture 等仍需逐项判断。

#### P0.3 工作树整理和回归

- 将新增文件全部纳入版本控制；
- 分离插件架构、录屏、DPI、快捷键提示、前端 UX 等提交；
- 不提交构建产物；
- 不修改 `不变的需求.md`；
- 确认 LICENSE 与 FFmpeg/OpenCV/WebView2 等第三方许可和发布方式兼容；
- 跑完本文第 8 节全部门禁后再合并。

### P1：功能名已存在，但与对标产品仍有实质差距

#### P1.1 截图捕获后端

普通截图的真实实现目前仍主要是 `ScreenCapture::captureScreenBitBlt()`。`ScreenCapture.h` 提到 WGC，但源码没有 Windows Graphics Capture 实现。DXGI Desktop Duplication 后端目前主要服务录屏。

需要：

- 为普通截图接入 WGC 或复用 DXGI 后端；
- BitBlt 仅作为兼容降级；
- 正确处理 HDR、旋转屏、混合 DPI、RDP、安全桌面、受保护内容；
- 窗口捕获要处理阴影、圆角、遮挡和最小化窗口策略；
- 首帧延迟和失败回退必须有指标。

#### P1.2 长截图可靠性

当前基于自动滚轮与 OpenCV 重叠匹配，尚需对 Chrome、Edge、VS Code、Office、PDF、虚拟列表、粘性标题、动画内容分别验证和增强：

- 更稳健的停止/到底检测；
- 动态内容和固定区域屏蔽；
- 手动滚动模式；
- 拼接失败可回退和局部修正；
- 超长图内存/磁盘分块策略；
- WGC 捕获路径。

#### P1.3 贴图能力

当前 `PinWindow` 已有拖动、缩放、透明度、穿透、复制、保存、整理和全部隐藏，但与目标仍差：

- 旋转/翻转；
- 重新进入标注编辑；
- 更完整的键盘提示与首次引导；
- 多贴图分组、吸附和跨屏 DPI E2E；
- UI Automation 可访问性；
- 清理旧 `PinnedWindow` 重复实现。

#### P1.4 录屏产品能力

后端已有 H.264/H.265/VP9/GIF，但设置 UI 目前主要暴露 H.264/H.265。还需核对并补齐：

- VP9/GIF 的正式 UI、错误提示和质量参数；
- WebP 动图；
- 录制中实时标注；
- 视频水印叠加（截图标注中的 Watermark 不等于视频水印）；
- 摄像头画中画；
- 窗口跟随录制；
- 音频设备热插拔、默认设备切换、蓝牙设备；
- 硬件编码器耗尽/驱动重置/磁盘满时的无损失败体验；
- 长时间录制、暂停恢复和音画同步压力测试。

#### P1.5 手势与 Lua 安全

- HWND 不能作为跨重启稳定规则，需要明确仅会话规则或改为可重建选择器；
- 进程/类名/句柄优先级、通配符、批量操作要做真实应用 E2E；
- 游戏、绘图软件、管理员窗口、UAC 边界和 Hook 被系统移除后的恢复；
- Lua 暴露 `shell/fs/http/keyboard/mouse`，需要权限模型、脚本来源标识、超时/取消、资源限制和安全审计；
- 插件停用时正在运行的脚本/手势必须可安全取消。

#### P1.6 搜索模块

- 验证服务安装、权限、升级/卸载、索引恢复；
- MFT/USN 变化、网络盘、ReFS、不可访问目录、符号链接；
- 拼音排序、模糊匹配质量和大索引延迟；
- SearchWindow 最新 PMv2 改动需要真机混合屏测试；
- WebView2 初始化失败时需要原生可恢复反馈，而不是空窗。

### P2：世界级体验与发布质量

- 原生自绘窗口的 UI Automation/屏幕阅读器语义；
- 键盘全流程、焦点可见、色彩对比、减少动画设置；
- 原生文字目前多处按“系统语言”判断，应统一使用应用内语言配置；
- 触控/笔输入、压感和高采样率鼠标；
- HDR/10-bit、竖屏、负坐标显示器、显示器热插拔、睡眠唤醒；
- 自动更新的签名校验、回滚、代理和断点续传；
- 安装包代码签名、SmartScreen、崩溃隐私和可选遥测；
- 完整用户手册、插件 SDK 示例、贡献指南、威胁模型；
- CI 构建矩阵、静态分析、模糊测试和可重复性能基线。

## 7. 已知具体风险/技术债

1. `task.md` 过度乐观，后续必须用源码、运行结果和 E2E 证明完成度。
2. `ScreenCapture.h` 的 WGC 注释与真实 BitBlt 实现不一致。
3. `PinnedWindow.*` 与 `PinWindow.*` 重复；CMake 使用 GLOB，会把旧实现也编译进插件。
4. `WinUtils::copyToClipboard()` 缺少剪贴板忙重试、`GlobalLock`/`SetClipboardData` 失败处理；高频 OCR/截图场景可能偶发失败。
5. OCR 结果窗当前调用点都已切主线程，但 `showResult()` 的公共 API 没有强制线程契约；未来调用者可能误用。
6. 多个原生窗口使用 `WDA_EXCLUDEFROMCAPTURE`；需要在旧版 Windows、RDP 和不同捕获后端上验证失败行为。
7. Search/Tray 的 PMv2 最新改动只完成编译验证，未完成 WebView2 真机视觉回归。
8. 全局 `trimWorkingSet()` 可能造成二次打开抖动。
9. 大量改动未提交，任何清理/重置操作都有数据丢失风险。
10. `.gitattributes`/换行策略未统一，Git 会提示 LF→CRLF；不要用无关格式化制造巨大 diff。

## 8. 构建和验证命令

PowerShell，工作目录 `C:\repo\easyTools`：

```powershell
$cmake = 'C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = 'C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'

& $cmake --build build --config Release --target EasyTools EasyToolsTests --parallel 6
& .\build\bin\Release\EasyToolsTests.exe
& $ctest --test-dir build -C Release --output-on-failure

& $cmake --build build --config Release --target EasyToolsShortcutHintPreview EasyToolsNativeOverlayPreview --parallel 6
& .\build\bin\Release\EasyToolsShortcutHintPreview.exe (Join-Path (Resolve-Path .\build) 'shortcut_hints_check')
& .\build\bin\Release\EasyToolsNativeOverlayPreview.exe (Join-Path (Resolve-Path .\build) 'native_overlays_check')

Push-Location ui
npm run build
npm run lint
npm run i18n-check
Pop-Location

git diff --check -- . ':(exclude)不变的需求.md'
git status --short
```

建议补充人工矩阵：

| 场景 | 必测动作 |
|---|---|
| 100% 单屏 | 截图、标注、复制、保存、贴图、OCR、录屏、手势 |
| 150% 单屏 | 同上，重点检查字体、点击区域、快捷键提示 |
| 100% + 150% 双屏 | 在两屏分别触发，并在可拖窗口跨屏后操作 |
| 150% + 200% 双屏 | DPI 切换、尺寸跳变、WebView 清晰度、光标坐标 |
| 显示器热插拔 | 录制/截图中断、浮层重新定位、资源恢复 |
| RDP/锁屏/睡眠 | DXGI 丢失、音频重连、Hook 恢复 |
| 磁盘空间不足 | 录屏安全停止、临时文件清理、可理解错误提示 |
| 剪贴板被占用 | 截图/OCR 复制重试和错误反馈 |
| 插件运行中停用 | Hook、线程、IPC、窗口和 DLL 安全卸载 |

## 9. 推荐给 Gemini 的第一阶段任务

可以直接把下面这段作为下一位代理的任务说明：

> 阅读 `GEMINI_HANDOFF.md` 和 `不变的需求.md`，保留当前全部未提交工作，不得 reset/checkout/清理。先运行 Release 构建、709 断言单测、CTest、前端 build/lint/i18n 和 diff-check。然后完成 P0：修复 SettingsWindow 的 Per-Monitor V2 初始定位、工作区约束和 WM_DPICHANGED；在 100%+150% 混合屏验证 Search/Tray/Settings；审计并移除高频 UI 的进程级 trimWorkingSet，改为局部资源释放并比较二次打开延迟。之后再按 P1 顺序实现普通截图 WGC/DXGI 后端、长截图可靠性、贴图补全和录屏高级能力。所有完成声明必须有源码、构建、测试或真机证据。

## 10. 交接结论

当前项目已经从功能原型向可扩展产品迈进了一大步，插件生命周期、录屏主链路、原生浮层性能、高 DPI 和快捷键提示均有实质提升；但距离 PixPin/WGestures/Snipaste Pro/FocusSee 级别仍有明确差距。最重要的是不要被全勾选的 `task.md` 误导，下一阶段应先稳定当前大工作树并完成 PMv2/资源策略/E2E，再推进 WGC、长截图、贴图和录屏高级能力。
