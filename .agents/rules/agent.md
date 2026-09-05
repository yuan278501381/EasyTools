---
trigger: always_on
---

审计基准：遵循 `.agents/AGENTS.md` 的 Accepted Product Decisions & Audit Baseline。2026-09-04 项目负责人已接受计划任务 Everyone 完全控制与最高权限、CLI 卸载默认删除个人数据、QuickLook 音视频/PDF 预览占位，不得在行为未实质变化时重复报为缺陷。搜索服务只在首次显式打开搜索时启动，之后在本次 Windows 会话中跨 EasyTools 退出常驻。

1、世界级架构、世界级性能、世界级鲁棒性 世界级UI，世界级UX
2、易用：每一个功能都要考虑人类的易用度，世界级的用户体验，方便操作，因为这本身就是效率软件
2.1全面支持高分屏以及Windows缩放
2.2支持EasyTools设置页面的深浅主题，以及主题色
3、极限轻量与物理内存收缩：
   - 遵循“冷路径退场修剪，热操作期间绝不修剪”原则。
   - 在重型任务结束（截图/录屏/长截图/OCR）或窗口隐藏/组件停用时，释放大对象并主动调用 `WinUtils::trimWorkingSet()` 归还物理内存。
   - 严禁在 1000Hz 鼠标钩子、键盘连击流或 60FPS 渲染循环等热路径中调用，杜绝软缺页卡顿。
4、代码覆盖率与质量红线：
   - 原生源码行覆盖率门禁从已测基线 **32%** 起步，后续只升不降；不要求不可实现的全库 100% 语句/分支覆盖。
   - 新增或修改的核心业务逻辑、工具算法、数据协议、状态机、安全边界与缺陷修复必须配套针对性测试。
   - CI/CD 强制执行单元测试和覆盖率防回退门禁；只有在工具真实产生分支数据后才能启用分支门禁，`0/0` 不得冒充 100%。
5、全链路键盘加速器管线标准（KeyboardPipeline）：
   - Win32 宿主拦截：所有顶层宿主窗口（SettingsWindow, SearchWindow, TrayWindow 等）必须通过 `KeyboardPipeline::filterWindowMessage` 拦截 `SC_KEYMENU`、`SC_CONTEXTHELP`、`WM_HELP`，杜绝 Alt、F10、F1 激活系统菜单抢夺焦点。
   - WebView2 加速器策略：必须在控制器上挂载 `KeyboardPipeline::applyWebKeyboardPolicy` 屏蔽 Chromium 默认浏览器快捷键（`Ctrl+P/F/U/J/H/W` 等），保障组合键 100% 透传至前端 DOM。
   - 录制态全局热键防误触：进入快捷键录制时底层热键自动静默（`hotkey.setPaused(true)`），防止录入时在后台误触发已有功能。
6、字体排版与清晰度标准（方案 B & C 黄金准则 & 排版单一事实源）：
   - 零系统字体污染（Zero System Font Pollution）：严禁在安装包中向 Windows `C:\Windows\Fonts` 写入字体或修改系统注册表，杜绝管理员权限受限、DirectWrite 进程锁定导致的卸载残留以及字体分发版权合规风险。
   - WebFont 选型权衡架构记忆 (WebFont vs Native Font Stack Trade-offs & 方案 B 黄金准则)：
     * 视觉评估：思源黑体（Noto Sans SC）在中文字形饱满度、大中宫与现代几何字面表现力上明显优于系统默认微软雅黑；
     * 桌面端旧方案硬伤：内嵌全量 `@fontsource/noto-sans-sc` 引入 100+ 个 `.woff2` 碎片切片，使安装包从 16MB 暴增至 32.4MB（+100% 膨胀）并在打包时产生海量磁盘 I/O 碎片；
     * 方案 B 架构终局标准：全项目采用 **单字重极简无损思源黑体架构（500 Medium · 仅 1.10MB 单文件 · 0 碎片 I/O）** 并结合 `@font-face` 中的 `local('Source Han Sans SC')` 本地白嫖机制。既彻底消灭 100+ 切片导致的 20MB 膨胀与打包卡顿，又完美呈现中英数字/括号 100% 同源浑然一体的世界级现代几何字面质感！
   - 应用级单一事实源字体栈体系：界面无衬线统一引用 `--font-sans`（`"Noto Sans SC", "Source Han Sans SC", -apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;`）；现代等宽/类名/代码/快捷键统一引用 `--font-mono`（`"Cascadia Code", "Cascadia Mono", "Segoe UI Mono", "Consolas", "PingFang SC", "Microsoft YaHei UI", monospace;`）。严禁在任何新组件中裸写 `ui-monospace` 或硬编码 `font-family`。
   - 字号底线与字重加权红线（字号与字重双重底线）：界面中所有文本（含次级辅助说明、状态徽章、输入控件、占位符 placeholder 等）字号不得低于 0.83rem（11.8px~12px），**字重底线严禁低于 500 Medium**（正文 550，标题 650~700），行高不得低于 1.4，保障 ClearType 次像素物理渲染字字饱满锐利。严禁出现 400/450 细字重导致笔画发虚发灰。
   - 技术标识符微晶胶囊标准：所有窗口类名、进程名、文件路径等元数据严禁以粗糙细文本直接裸露，必须统一使用 `<CodeBadge />` 微晶等宽代码胶囊封装。
   - 自动化排版 CI 门禁：CI 流水线强制执行 `npm run typography-check`，一旦发现孤立字体声明、低于 11.8px 的微小字号或低于 500 的细字重直接阻断构建。
7、开源版权与原作者署名基准：
   - 原作者官方署名：`Yy1 (yuan278501381)`（展示格式 `Yy1 (@yuan278501381)`）。
   - GitHub 官方主页：`https://github.com/yuan278501381`。
   - 开源协议：严格遵循 MIT License，所有关于页、发布元数据与 LICENSE 文件保持版权一致性。
8、浮层视口与专注助手全屏避让标准（FocusAssistAvoidance）：
   - 局部动态包围盒优先：鼠标点击水波纹、移动流光轨迹、手势划线等局部特效，严禁创建或调整为全屏窗口，必须按活跃元素并集计算局部最小包围盒（100~300px 微型视口），彻底消除全屏合成延迟并从底层阻断 Windows Shell 误判全屏触发 🔔z 专注助手（免打扰）铃铛。
   - 聚光灯全屏暗角几何避让：必须覆盖全屏的暗角渐变窗口，物理尺寸避让 1 像素（vw-1, vh-1），打破 Windows `SHQueryUserNotificationState` 全屏独占检测逻辑。
9、零 Emoji 矢量化与统一设计语言红线：
   - 严禁在 UI 状态徽章、提示标签及正文中内嵌 Unicode 彩色 Emoji 字符。
   - 全面采用 Lucide 高精度矢量 SVG 图标与玻璃拟态胶囊微徽章，保障世界级桌面软件原生设计质感。
10、Git 分支管理与非快进显式合并发版标准（GitFlowReleasePipeline）：
   - 一键 DevOps 发版总控（`pwsh scripts/release.ps1`）：工作区纯净检查 -> VERSION 事实源自增 (+1) -> Release Notes 生成 -> 开发分支提交 -> main 显式非快进合并 (`--no-ff`) -> 全新构建与生命周期端到端门禁 -> 二进制 ProductVersion 校验 -> 生成安装包/便携包/SHA256SUMS -> 创建不可覆盖的 Tag 与 GitHub Release -> 安全检出下一阶段特性分支。
   - 双圆环拓扑美学：确保在 Git Graph 中呈现出独立的开发支线气泡与顶部的双圆环汇聚节点（`merge(dev)`）。
11、跨系统无缝通用圆角双保险架构标准（UniversalRoundedCornersDualInsurance）：
   - 跨系统断层机理：Windows 11 的 DWM 硬件圆角（`DWMWCP_ROUND`）在 Windows 10、Windows Server 全系列（如 Server 2019/2022/2025）、精简版系统及远程桌面 (RDP) 下会被 DWM 忽略或关闭，导致无边框弹窗（如搜索中心、托盘菜单）暴露 Win32 直角底衬与粗糙阴影裁切块。
   - 双保险终局标准：所有无边框弹出窗口（`SearchWindow`, `TrayWindow` 等）几何尺寸变更时必须统一接入 `WinUtils::applyUniversalRoundedCorners(hwnd, width, height, radius)`：
     * 第一道防线（Win11）：设置 `DWMWA_WINDOW_CORNER_PREFERENCE` 为 `DWMWCP_ROUND` 享受 GPU 硬件加速超平滑圆角与系统原生高斯深度投影；
     * 第二道防线（Win10 / Server 2022/2025 / RDP）：调用 Win32 内核级 `CreateRoundRectRgn` 与 `SetWindowRgn` 直接在物理像素层强行裁剪剔除四个直角尖角，彻底抹平全版本跨系统视觉断层；
     * 前端与 DPI 对齐：前端外壳容器必须 0 padding 贴合窗口，圆角半径 `radius` 严格跟随显示器物理 DPI 缩放毫秒级重算。
