---
trigger: always_on
---

1、世界级架构、世界级性能、世界级鲁棒性 世界级UI，世界级UX
2、易用：每一个功能都要考虑人类的易用度，世界级的用户体验，方便操作，因为这本身就是效率软件
2.1全面支持高分屏以及Windows缩放
2.2支持EasyTools设置页面的深浅主题，以及主题色
3、极限轻量与物理内存收缩：
   - 遵循“冷路径退场修剪，热操作期间绝不修剪”原则。
   - 在重型任务结束（截图/录屏/长截图/OCR）或窗口隐藏/组件停用时，释放大对象并主动调用 `WinUtils::trimWorkingSet()` 归还物理内存。
   - 严禁在 1000Hz 鼠标钩子、键盘连击流或 60FPS 渲染循环等热路径中调用，杜绝软缺页卡顿。
4、代码覆盖率与质量红线：
   - 核心业务逻辑、工具算法、数据协议与状态机必须保持 100% 代码覆盖率。
   - 任何新增功能与重构必须同步配套全覆盖单元测试，杜绝死代码与不可达分支。
   - CI/CD 自动化部署流水线强制接入覆盖率分析与测试门禁，未达标严禁发布。
5、全链路键盘加速器管线标准（KeyboardPipeline）：
   - Win32 宿主拦截：所有顶层宿主窗口（SettingsWindow, SearchWindow, TrayWindow 等）必须通过 `KeyboardPipeline::filterWindowMessage` 拦截 `SC_KEYMENU`、`SC_CONTEXTHELP`、`WM_HELP`，杜绝 Alt、F10、F1 激活系统菜单抢夺焦点。
   - WebView2 加速器策略：必须在控制器上挂载 `KeyboardPipeline::applyWebKeyboardPolicy` 屏蔽 Chromium 默认浏览器快捷键（`Ctrl+P/F/U/J/H/W` 等），保障组合键 100% 透传至前端 DOM。
   - 录制态全局热键防误触：进入快捷键录制时底层热键自动静默（`hotkey.setPaused(true)`），防止录入时在后台误触发已有功能。
6、字体排版与清晰度标准（方案 B & C 黄金准则 & 排版单一事实源）：
   - 零系统字体污染（Zero System Font Pollution）：严禁在安装包中向 Windows `C:\Windows\Fonts` 写入字体或修改系统注册表，杜绝管理员权限受限、DirectWrite 进程锁定导致的卸载残留以及字体分发版权合规风险。
   - 应用级单一事实源字体栈体系：界面无衬线统一引用 `--font-sans`（`-apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei UI", "Microsoft YaHei", "Source Han Sans SC", "Noto Sans CJK SC", sans-serif;`）；现代等宽/类名/代码/快捷键统一引用 `--font-mono`（`"Cascadia Code", "Cascadia Mono", "Segoe UI Mono", "Consolas", "PingFang SC", "Microsoft YaHei UI", monospace;`）。严禁在任何新组件中裸写 `ui-monospace` 或硬编码 `font-family`。
   - 字号底线与字重加权红线：界面中所有文本（含次级辅助说明、状态徽章、输入控件、提示语等）字号不得低于 0.83rem（11.8px~12px），次级文本字重不得低于 500（正文 550，标题 650~700），行高不得低于 1.4，保障 ClearType 次像素物理渲染字字饱满锐利。
   - 技术标识符微晶胶囊标准：所有窗口类名、进程名、文件路径等元数据严禁以粗糙细文本直接裸露，必须统一使用 `<CodeBadge />` 微晶等宽代码胶囊封装。
   - 自动化排版 CI 门禁：CI 流水线强制执行 `npm run typography-check`，一旦发现孤立字体声明或低于 11.8px 的微小字号直接阻断构建。
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