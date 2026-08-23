---
trigger: always_on
---

1、高性能
2、易用：每一个功能都要考虑人类的易用度，世界级的用户体验，方便操作，因为这本身就是效率软件
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
6、字体排版与清晰度标准（方案 B & C 黄金准则）：
   - 零系统字体污染（Zero System Font Pollution）：严禁在安装包中向 Windows `C:\Windows\Fonts` 写入字体或修改系统注册表，杜绝管理员权限受限、DirectWrite 进程锁定导致的卸载残留以及字体分发版权合规风险。
   - 应用级内嵌 WebFont + DirectWrite 硬件加速梯队：强制采用“应用级内嵌 WebFont + 系统 DirectWrite 梯队回退”体系。全局字体栈统一为：`-apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei UI", "Microsoft YaHei", "Source Han Sans SC", "Noto Sans CJK SC", sans-serif;` 并标配 `-webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility;`。
   - 中文字号与清晰度红线：界面所有文本（含次级辅助说明、状态徽章、输入框等）字号不得低于 0.83rem（11.8px~12px），行高不得低于 1.4，保障 ClearType 次像素渲染字字清晰锐利。
7、开源版权与原作者署名基准：
   - 原作者官方署名：`Yy1 (yuan278501381)`（展示格式 `Yy1 (@yuan278501381)`）。
   - GitHub 官方主页：`https://github.com/yuan278501381`。
   - 开源协议：严格遵循 MIT License，所有关于页、发布元数据与 LICENSE 文件保持版权一致性。