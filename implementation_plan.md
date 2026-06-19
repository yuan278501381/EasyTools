# EasyTools — 需求理解与架构设计方案

> 一款基于 **C++ 核心 + WebView2 设置界面** 的高性能桌面效率工具，包含 **鼠标手势** 和 **截图/录屏** 两大功能模块。

---

## 一、我对需求的理解

### 1.1 整体定位

这不是一个简单的小工具，而是一个 **融合了 WGestures 级别的鼠标手势能力 + PixPin/ShareX 级别的截图录屏能力** 的桌面效率平台。技术架构上采用 C++ 做核心引擎（性能敏感的输入拦截、屏幕捕获、图像处理），WebView2 做设置界面（现代化 UI 体验，支持未来扩展 Web 生态）。

### 1.2 技术架构选型理解

| 层级 | 技术 | 职责 |
|------|------|------|
| **核心引擎** | C++ (Win32/COM) | 鼠标 Hook、手势识别、屏幕捕获、图像处理、Lua 脚本宿主 |
| **设置界面** | WebView2 (Evergreen) + Vite/React | 配置管理、手势编辑器、快捷键设置、主题切换 |
| **IPC 桥接** | WebView2 PostMessage / Host Objects | C++ ↔ JS 双向通信 |
| **截图标注 UI** | C++ GDI+/Direct2D 原生绘制 | 截图覆盖层、标注工具栏、实时绘制（性能敏感，不走 WebView2） |
| **脚本引擎** | Lua (sol2 binding) | 用户自定义手势动作的运行时脚本 |

> [!IMPORTANT]
> **补充视角**：截图标注界面不应该用 WebView2，而应该用 **C++ 原生绘制（Direct2D/GDI+）**。原因：截图覆盖层需要全屏半透明叠加 + 实时像素级操作（马赛克、放大镜），WebView2 的进程启动开销和渲染延迟无法满足"按快捷键→瞬间弹出"的用户预期。设置界面用 WebView2 是正确的。

---

## 二、模块一：鼠标手势 — 详细需求拆解

### 2.1 手势触发与识别

#### 触发方式
| 触发器 | 说明 |
|--------|------|
| **鼠标右键按住拖动** | 默认触发方式（经典 WGestures 模式） |
| **鼠标中键按住拖动** | 可选触发方式 |
| **修饰键组合** | 同一手势 + Ctrl/Shift/Alt 映射不同动作 |

#### 识别算法
- **方向编码法**：将鼠标轨迹实时编码为方向字符串（U/D/L/R/UL/UR/DL/DR）
- **防抖处理**：移动距离 < 阈值时忽略（消除手抖）
- **角度容差**：±22.5° 范围内归类为同一方向
- **最小移动距离**：可配置（默认 30px），避免误触

#### 底层实现
- 使用 `SetWindowsHookEx(WH_MOUSE_LL, ...)` 全局低级鼠标钩子
- Hook 回调仅做坐标采集和入队，**严禁在回调中做任何重计算**（Windows 超时 ~1000ms 会静默移除钩子）
- 独立工作线程做手势识别和动作分发

### 2.2 默认手势配置（参考 WGestures）

| 手势 | 方向编码 | 默认动作 | 对应快捷键 |
|------|----------|----------|------------|
| ← | `L` | 后退 | `Alt+←` |
| → | `R` | 前进 | `Alt+→` |
| ↑ | `U` | 关闭窗口/标签页 | `Alt+F4` / `Ctrl+W` |
| ↓ | `D` | 新建标签页 | `Ctrl+T` |
| ↖ | `UL` | 复制 | `Ctrl+C` |
| ↘ | `DR` | **关闭标签页** | `Ctrl+W` |
| ↗ | `UR` | 最大化窗口 | `Win+↑` |
| ↙ | `DL` | 最小化窗口 | `Win+↓` |
| ←↑ | `LU` | 剪切 | `Ctrl+X` |
| ↑→ | `UR_combo` | 下一个标签页 | `Ctrl+Tab` |
| ↑← | `UL_combo` | 上一个标签页 | `Ctrl+Shift+Tab` |
| ↓↑ | `DU` | 刷新 | `F5` |
| ↑↓ | `UD` | 撤销 | `Ctrl+Z` |
| →← | `RL` | 全选 | `Ctrl+A` |

#### 浏览器专用 Profile（Chrome / Edge / Firefox）

| 手势 | 方向编码 | 动作 | 对应快捷键 |
|------|----------|------|------------|
| →↓ | `RD` | 恢复最近关闭的标签页 | `Ctrl+Shift+T` |
| ↓→ | `DR_browser` | 粘贴 | `Ctrl+V` |

### 2.3 手势作用域控制

```
┌─────────────────────────────────────────┐
│               手势规则引擎               │
│                                         │
│  ┌─────────┐  ┌──────────┐  ┌────────┐  │
│  │ 进程名  │  │  窗口类名 │  │ 窗口句柄│  │
│  │ 白/黑名单│  │  白/黑名单 │  │ 精确匹配│  │
│  └────┬────┘  └─────┬────┘  └───┬────┘  │
│       └─────────────┼───────────┘       │
│                     ▼                    │
│           ┌─────────────────┐           │
│           │  优先级裁决引擎  │           │
│           │ 句柄 > 类名 > 进程│          │
│           └─────────────────┘           │
└─────────────────────────────────────────┘
```

- **按进程名**：如 `chrome.exe`、`explorer.exe`，支持通配符 `*.exe`
- **按窗口类名**：如 `Chrome_WidgetWin_1`，支持正则匹配
- **按窗口句柄**：运行时动态捕获（提供"十字准星"拾取工具，拖动到目标窗口自动获取 HWND）
- **优先级**：窗口句柄 > 窗口类名 > 进程名 > 全局默认
- **批量操作**：支持多选批量添加/删除/启用/禁用

> [!TIP]
> **补充建议**：每个作用域应支持**独立的手势配置集**（Profile），而非仅控制"启用/禁用"。例如：在 VS Code 中 `↓` 手势可以映射为"打开终端"而非"新建标签页"。

### 2.4 Lua 脚本引擎

**适用场景**：
- 手势动作绑定到 Lua 脚本（而非固定快捷键）
- 实现复杂的条件逻辑（如"如果剪贴板有URL，用浏览器打开；否则用搜索引擎搜索"）
- 用户自定义自动化工作流

**暴露给 Lua 的 API 清单（建议）**：

```lua
-- 示例：一个手势绑定的 Lua 脚本
local clipboard = easy.clipboard.getText()
if clipboard:match("^https?://") then
    easy.shell.open(clipboard)        -- 打开 URL
else
    easy.shell.open("https://www.google.com/search?q=" .. easy.url.encode(clipboard))
end

-- 可用 API 命名空间
easy.keyboard       -- sendKeys, keyDown, keyUp
easy.mouse          -- click, moveTo, scroll
easy.clipboard      -- getText, setText, getImage, setImage
easy.shell          -- open, run, runAsync
easy.window         -- getForeground, minimize, maximize, close, setTopmost, getTitle, getClass, getProcess
easy.screen         -- capture, getPixelColor
easy.fs             -- readFile, writeFile, exists
easy.http           -- get, post (简单 HTTP 请求)
easy.ui             -- toast, notify, inputBox, confirm
easy.log            -- info, warn, error (统一日志)
easy.url            -- encode, decode
```

---

## 三、模块二：截图 / 贴图 / 长截图 / 录屏 — 详细需求拆解

### 3.1 截图核心流程

```
用户按快捷键 (Ctrl+Shift+A)
       │
       ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ DXGI 全屏捕获 │───▶│ 半透明遮罩覆盖 │───▶│ 框选区域      │
│ 冻结当前帧    │    │ 高亮选区      │    │ 支持窗口吸附   │
└──────────────┘    └──────────────┘    └──────────────┘
                                              │
                                              ▼
                                   ┌──────────────────┐
                                   │   标注工具栏弹出   │
                                   │  (底部悬浮/侧边)  │
                                   └──────────────────┘
                                              │
                              ┌───────┬───────┼───────┬───────┐
                              ▼       ▼       ▼       ▼       ▼
                           保存    复制到    贴出    取消    长截图
                          到文件   剪贴板   到桌面
```

### 3.2 标注工具详细规格

| 工具 | 图标建议 | 功能细节 |
|------|----------|----------|
| **矩形** | □ | 实心/空心可切换，边框颜色/粗细可调，支持圆角 |
| **椭圆** | ○ | 同矩形，支持正圆（按住 Shift） |
| **箭头** | → | 单向/双向，箭头大小可调，支持曲线箭头 |
| **画笔** | ✎ | 自由绘制，颜色/粗细可调，支持压感（如有数位板） |
| **高亮** | █ | 半透明矩形覆盖，模拟荧光笔效果 |
| **马赛克** | ▦ | 像素化/高斯模糊两种模式，可调模糊强度和笔刷大小 |
| **文本** | A | 富文本输入，支持字体/大小/颜色/描边/阴影 |
| **放大镜** | 🔍 | 圆形放大区域，可调放大倍率（2x~8x） |
| **序列号** | ① | 自动递增编号气泡（1, 2, 3...），颜色可自定义 |
| **撤销/重做** | ↩↪ | 标注操作的 Undo/Redo 栈 |
| **橡皮擦** | ⌫ | 点击擦除单个标注元素 |

> [!NOTE]
> **序列号标注** 是你需求中的亮点。建议实现为：点击一次自动放置一个带圆形背景的递增编号，支持拖动调整位置，可在设置中自定义起始编号和样式（颜色、大小、字体）。

### 3.3 贴图（Pin to Screen）

- **置顶显示**：截图后可"贴出"到桌面，始终在其他窗口之上
- **交互能力**：
  - 自由拖动位置
  - 鼠标滚轮缩放（支持平滑动画）
  - 调整透明度（`Ctrl+滚轮`）
  - 鼠标穿透模式（点击穿过贴图到下方窗口）
  - 右键菜单：关闭、另存为、复制、编辑标注、旋转
- **多贴图管理**：支持同时存在多张贴图，提供快捷键全部隐藏/显示

### 3.4 长截图（Scrolling Capture）

**技术方案**：

1. 用户框选滚动区域
2. 使用 **Windows Graphics Capture API（WGC）** 捕获目标窗口帧
3. 自动发送 `WM_MOUSEWHEEL` 滚动目标窗口
4. 使用 **图像拼接算法**（基于特征点匹配 / 像素行相似度对比）去除重叠区域
5. 实时预览拼接进度
6. 用户手动停止或自动检测到达底部

> [!WARNING]
> **已知难点**：硬件加速窗口（Chrome、VS Code 等）使用 GDI `BitBlt` 捕获会黑屏。必须使用 WGC 或 DXGI Desktop Duplication。建议以 WGC 为首选方案，DXGI 作为降级后备。

### 3.5 屏幕录制

| 特性 | 规格 |
|------|------|
| **输出格式** | MP4（H.264/H.265）、GIF、WebP（动图） |
| **录制区域** | 全屏 / 选区 / 跟随窗口 |
| **帧率** | 可选 15/24/30/60 FPS |
| **音频** | 可选录制系统音频（WASAPI Loopback）+ 麦克风 |
| **实时标注** | 录制过程中支持标注工具（箭头、高亮、文本） |
| **鼠标高亮** | 可选显示鼠标点击特效和光圈跟随 |
| **倒计时** | 开始录制前 3/5/10 秒倒计时 |
| **录制指示器** | 屏幕边缘红色闪烁边框或小型浮动计时器 |

**技术方案**：
- **捕获**：DXGI Desktop Duplication（性能最优）或 WGC
- **编码**：Media Foundation（硬件加速 H.264/H.265） / FFmpeg（更灵活的格式支持）
- **音频**：WASAPI Loopback Capture + 麦克风混音

---

## 四、你遗漏的需求 — 关键补充

### 4.1 基础设施层（被低估但至关重要）

| 补充项 | 说明 | 优先级 |
|--------|------|--------|
| **统一日志系统** | 基于 spdlog，支持 TraceID、日志级别（TRACE/DEBUG/INFO/WARN/ERROR/FATAL）、按天滚动文件、控制台彩色输出 | 🔴 P0 |
| **全局快捷键管理** | 统一注册/冲突检测/用户自定义，避免与系统/其他软件冲突 | 🔴 P0 |
| **配置持久化** | JSON/TOML 配置文件 + 导入/导出功能，支持配置迁移 | 🔴 P0 |
| **系统托盘** | 最小化到托盘、右键菜单（暂停手势/截图/录屏/设置/退出） | 🔴 P0 |
| **开机自启** | 注册表/任务计划程序实现，可在设置中开关 | 🟡 P1 |
| **自动更新** | 检查/下载/静默更新机制 | 🟢 P2 |
| **多语言支持** | i18n 框架，至少支持中/英文 | 🟢 P2 |
| **崩溃收集** | MiniDump + 上报机制（Breakpad/Crashpad） | 🟡 P1 |

### 4.2 鼠标手势模块补充

| 补充项 | 说明 |
|--------|------|
| **触发角（Hot Corners）** | 鼠标移到屏幕四角触发动作（锁屏、显示桌面、任务视图等） |
| **摩擦边（Screen Edge Friction）** | 鼠标在屏幕边缘快速摩擦触发动作（音量调节、亮度调节等） |
| **手势轨迹可视化** | 手势执行时在屏幕上显示实时轨迹线 + 手势名称提示 |
| **手势学习/训练模式** | 新用户可以在引导模式下练习手势 |
| **手势导入/导出** | 支持将手势配置导出为文件，方便分享和备份 |
| **手势冲突检测** | 添加新手势时自动检测与已有手势的冲突（前缀冲突等） |
| **暂停/恢复快捷键** | 全局快捷键一键暂停所有手势（游戏/绘图场景） |
| **每应用独立 Profile** | 不同应用绑定不同的手势配置集 |

### 4.3 截图模块补充

| 补充项 | 说明 |
|--------|------|
| **OCR 文字识别** | 截图后可一键提取图中文字（离线引擎，如 PaddleOCR/Tesseract） |
| **截图历史记录** | 保留最近 N 张截图的缩略图，可快速回溯和重新编辑 |
| **智能窗口检测** | 截图时自动识别窗口/控件边界，支持滚轮切换父子窗口 |
| **延时截图** | 支持 3/5/10 秒倒计时后自动截图（捕获右键菜单等瞬态UI） |
| **固定截图区域** | 可保存常用截图区域，下次一键截取同一位置 |
| **颜色取色器** | 截图模式下可吸取像素颜色，显示 HEX/RGB/HSL 值 |
| **标尺/参考线** | 截图模式下可量测像素距离 |
| **自动保存路径模板** | 支持日期/时间/序号变量，如 `Screenshots/{yyyy-MM-dd}/cap_{HHmmss}_{N}.png` |
| **剪贴板图片直接贴出** | 不截图，直接从剪贴板贴出图片到桌面 |
| **图片翻译** | 截图后对图中文字进行翻译覆盖显示 |

### 4.4 录屏模块补充

| 补充项 | 说明 |
|--------|------|
| **录制水印** | 可自定义水印文字/图片叠加到录制视频上 |
| **录制暂停/恢复** | 录制过程中可暂停再恢复 |
| **录制区域锁定** | 选区后锁定，下次快速复用 |
| **摄像头画中画** | 录屏时可在角落叠加摄像头画面（可调大小和位置） |

---

## 五、项目文件夹结构（世界级标准）

```
easyTools/
├── CMakeLists.txt                    # 顶层 CMake 构建
├── README.md
├── LICENSE
├── .clang-format                     # C++ 代码格式化规则
├── .clang-tidy                       # C++ 静态分析规则
├── vcpkg.json                        # C++ 依赖管理 (vcpkg manifest)
│
├── docs/                             # 文档
│   ├── architecture.md               # 架构设计文档
│   ├── api/                          # Lua API 文档
│   └── user-guide/                   # 用户手册
│
├── src/                              # C++ 源码
│   ├── CMakeLists.txt
│   ├── main.cpp                      # 入口点
│   │
│   ├── core/                         # 核心基础设施
│   │   ├── logger/                   # 统一日志（spdlog封装，TraceID, 日志级别）
│   │   │   ├── Logger.h
│   │   │   └── Logger.cpp
│   │   ├── config/                   # 配置管理（JSON/TOML 读写、热加载）
│   │   │   ├── ConfigManager.h
│   │   │   └── ConfigManager.cpp
│   │   ├── hotkey/                   # 全局快捷键管理
│   │   │   ├── HotkeyManager.h
│   │   │   └── HotkeyManager.cpp
│   │   ├── ipc/                      # WebView2 ↔ C++ 通信桥
│   │   │   ├── MessageBridge.h
│   │   │   └── MessageBridge.cpp
│   │   ├── plugin/                   # 插件系统框架（未来扩展）
│   │   │   ├── IPlugin.h
│   │   │   └── PluginManager.h
│   │   ├── crash/                    # 崩溃收集（MiniDump）
│   │   │   └── CrashHandler.h
│   │   └── utils/                    # 通用工具类
│   │       ├── StringUtils.h
│   │       ├── WinUtils.h            # Windows API 封装
│   │       └── TraceId.h             # TraceID 生成器
│   │
│   ├── gesture/                      # 鼠标手势模块
│   │   ├── GestureEngine.h           # 手势引擎主入口
│   │   ├── GestureEngine.cpp
│   │   ├── MouseHook.h               # 低级鼠标钩子
│   │   ├── MouseHook.cpp
│   │   ├── GestureRecognizer.h       # 手势识别算法
│   │   ├── GestureRecognizer.cpp
│   │   ├── GestureAction.h           # 手势动作抽象
│   │   ├── GestureAction.cpp
│   │   ├── GestureProfile.h          # 手势配置集
│   │   ├── GestureProfile.cpp
│   │   ├── ScopeRule.h               # 作用域规则（进程/类名/句柄）
│   │   ├── ScopeRule.cpp
│   │   ├── HotCorner.h               # 触发角
│   │   ├── HotCorner.cpp
│   │   ├── ScreenEdge.h              # 摩擦边
│   │   └── ScreenEdge.cpp
│   │
│   ├── capture/                      # 截图 & 录屏模块
│   │   ├── ScreenCapture.h           # 屏幕捕获抽象层
│   │   ├── ScreenCapture.cpp
│   │   ├── backends/                 # 捕获后端
│   │   │   ├── DxgiCapture.h         # DXGI Desktop Duplication
│   │   │   ├── DxgiCapture.cpp
│   │   │   ├── WgcCapture.h          # Windows Graphics Capture
│   │   │   └── WgcCapture.cpp
│   │   ├── selector/                 # 区域选择器
│   │   │   ├── RegionSelector.h      # 全屏遮罩 + 框选
│   │   │   └── RegionSelector.cpp
│   │   ├── annotation/               # 标注工具
│   │   │   ├── AnnotationLayer.h     # 标注图层管理
│   │   │   ├── AnnotationLayer.cpp
│   │   │   ├── tools/                # 各标注工具实现
│   │   │   │   ├── ITool.h           # 工具接口
│   │   │   │   ├── RectTool.cpp
│   │   │   │   ├── EllipseTool.cpp
│   │   │   │   ├── ArrowTool.cpp
│   │   │   │   ├── BrushTool.cpp
│   │   │   │   ├── HighlightTool.cpp
│   │   │   │   ├── MosaicTool.cpp
│   │   │   │   ├── TextTool.cpp
│   │   │   │   ├── MagnifierTool.cpp
│   │   │   │   └── NumberTool.cpp    # 序列号标注
│   │   │   └── render/
│   │   │       ├── D2DRenderer.h     # Direct2D 渲染器
│   │   │       └── D2DRenderer.cpp
│   │   ├── pinboard/                 # 贴图管理
│   │   │   ├── PinWindow.h           # 贴图窗口
│   │   │   ├── PinWindow.cpp
│   │   │   └── PinManager.h          # 多贴图管理器
│   │   ├── scroller/                 # 长截图
│   │   │   ├── ScrollCapture.h       # 滚动捕获控制器
│   │   │   ├── ScrollCapture.cpp
│   │   │   └── ImageStitcher.h       # 图像拼接算法
│   │   ├── recorder/                 # 录屏
│   │   │   ├── ScreenRecorder.h
│   │   │   ├── ScreenRecorder.cpp
│   │   │   ├── VideoEncoder.h        # 视频编码（Media Foundation / FFmpeg）
│   │   │   ├── AudioCapture.h        # WASAPI 音频捕获
│   │   │   └── GifEncoder.h          # GIF/WebP 动图编码
│   │   └── history/                  # 截图历史
│   │       ├── CaptureHistory.h
│   │       └── CaptureHistory.cpp
│   │
│   ├── scripting/                    # 脚本引擎
│   │   ├── LuaEngine.h              # Lua 引擎封装
│   │   ├── LuaEngine.cpp
│   │   ├── bindings/                 # C++ → Lua 绑定
│   │   │   ├── ClipboardBinding.cpp
│   │   │   ├── KeyboardBinding.cpp
│   │   │   ├── ShellBinding.cpp
│   │   │   ├── WindowBinding.cpp
│   │   │   └── UIBinding.cpp
│   │   └── sandbox/                  # 脚本沙箱（安全限制）
│   │       └── LuaSandbox.h
│   │
│   └── tray/                         # 系统托盘
│       ├── TrayIcon.h
│       └── TrayIcon.cpp
│
├── ui/                               # WebView2 设置界面（前端项目）
│   ├── package.json
│   ├── vite.config.ts
│   ├── tsconfig.json
│   ├── index.html
│   ├── public/
│   │   └── favicon.ico
│   └── src/
│       ├── main.tsx
│       ├── App.tsx
│       ├── styles/
│       │   └── index.css             # 全局样式 / 设计系统
│       ├── components/               # 通用 UI 组件
│       │   ├── Sidebar.tsx
│       │   ├── ToggleSwitch.tsx
│       │   └── HotkeyInput.tsx
│       ├── pages/
│       │   ├── GestureSettings.tsx    # 手势设置页
│       │   ├── CaptureSettings.tsx    # 截图设置页
│       │   ├── RecorderSettings.tsx   # 录屏设置页
│       │   ├── ScriptEditor.tsx       # Lua 脚本编辑器页
│       │   ├── GeneralSettings.tsx    # 通用设置页
│       │   └── About.tsx             # 关于页
│       ├── hooks/
│       │   └── useBridge.ts          # WebView2 IPC 通信 Hook
│       └── bridge/
│           └── native.ts            # C++ 通信接口定义
│
├── resources/                        # 资源文件
│   ├── icons/                        # 应用图标 & 工具栏图标
│   ├── scripts/                      # 内置 Lua 脚本示例
│   │   ├── search_selected.lua
│   │   └── translate_clipboard.lua
│   └── i18n/                         # 多语言翻译文件
│       ├── zh-CN.json
│       └── en-US.json
│
├── third_party/                      # 第三方库（不由 vcpkg 管理的）
│   └── README.md
│
├── tests/                            # 测试
│   ├── unit/                         # 单元测试
│   │   ├── gesture_recognizer_test.cpp
│   │   └── image_stitcher_test.cpp
│   └── integration/                  # 集成测试
│
├── installer/                        # 安装程序（NSIS / WiX）
│   └── setup.nsi
│
└── scripts/                          # 构建 & CI 脚本
    ├── build.ps1
    └── ci.yml
```

---

## 六、关键技术决策（需要你确认）

### Open Questions

> [!IMPORTANT]
> **Q1：Lua 还是 LuaJIT？**
> Lua 5.4 更稳定、更易嵌入；LuaJIT 性能更强但仅支持 Lua 5.1 语法且在 Windows ARM64 上有兼容性问题。
> 建议：**先用 Lua 5.4 + sol2 binding**，未来如有性能瓶颈再切换。

> **Q2：视频编码 → ✅ 已确认选用 FFmpeg**
> 理由：格式最全面，可支持 MP4(H.264/H.265)、GIF、WebP、APNG 等多种输出格式。通过 vcpkg 集成，使用 LGPL 许可的共享库模式。

> **Q3：图像处理 → ✅ 已确认引入 OpenCV**
> 用途：长截图图像拼接（特征点匹配/行相似度对比）、马赛克/高斯模糊处理、OCR 预处理（二值化/倾斜校正）。通过 vcpkg 管理依赖。

> **Q4：前端框架 → ✅ 已确认选用 React**
> UI 风格参考 Aitiy（MouseInc 作者新作，同为 C++ + WebView2 架构）的设置界面设计：
> - 简约暗色主题 + 紫色(violet)主色调
> - 左侧图标导航栏 + 右侧内容区
> - 卡片式功能分组 + 开关/滑块交互
> - 支持亮色/暗色主题切换

> **Q5：OCR → ✅ 已确认纳入 V1**
> 方案：集成 PaddleOCR Lite（离线推理，约 30MB 模型文件），支持中英文识别。
> 技术路线：OpenCV 图像预处理 → PaddleOCR 推理 → 文字结果输出/复制。

> [!IMPORTANT]
> **Q6：最低支持的 Windows 版本？**
> - Windows 10 1903+：支持 WGC API
> - Windows 10 1809+：支持 WebView2 Evergreen
> 建议：**最低 Windows 10 1903 (19H1)**，放弃 Windows 7/8 支持。

---

## 七、开发路线图建议

### Phase 1 — 基础设施 + 鼠标手势 MVP（4-6 周）
- [ ] 项目骨架搭建（CMake + vcpkg + WebView2）
- [ ] 核心基础设施（Logger + Config + Hotkey + Tray）
- [ ] 鼠标钩子 + 方向编码识别
- [ ] 默认手势集 + 全局/进程级作用域
- [ ] WebView2 设置界面骨架
- [ ] 手势配置的 CRUD UI

### Phase 2 — 截图 MVP（4-6 周）
- [ ] DXGI/WGC 屏幕捕获
- [ ] 全屏遮罩 + 区域选择器
- [ ] 基础标注工具（矩形、箭头、文本、序列号、马赛克）
- [ ] 保存 / 复制到剪贴板
- [ ] 贴图功能

### Phase 3 — 高级功能（4-6 周）
- [ ] 长截图（滚动捕获 + 图像拼接）
- [ ] 完整标注工具（椭圆、画笔、高亮、放大镜）
- [ ] 屏幕录制（MP4 + GIF）
- [ ] Lua 脚本引擎集成

### Phase 4 — 打磨 & 发布（2-4 周）
- [ ] 触发角 + 摩擦边
- [ ] 截图历史
- [ ] 安装程序
- [ ] 自动更新
- [ ] 用户文档

---

## 八、Verification Plan

### 自动化测试
- 手势识别算法的单元测试（方向编码 + 防抖 + 冲突检测）
- 图像拼接算法的单元测试（已知重叠图像的拼接正确性）
- 配置序列化/反序列化测试
- Lua API 绑定测试

### 手动验证
- 在多显示器、高 DPI（150%/200%/250%）环境下测试截图和手势
- 在主流软件（Chrome、VS Code、微信、钉钉）中测试手势兼容性
- 长截图在不同类型窗口（网页、文档、聊天记录）中的拼接效果
- 录屏 60FPS + 系统音频的同步性
- WebView2 设置界面在不同主题（深色/浅色）下的渲染效果
