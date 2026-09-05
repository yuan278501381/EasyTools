## C++ Backend Development
1. **DLL Export Conventions**: When marking a class with export macros (e.g., `EASYCORE_API`, `PLUGIN_EXPORTS`), DO NOT implement non-template methods or static singletons completely inline in the header. Their implementations must be moved to a `.cpp` file to ensure they are properly exported and avoid `LNK2019` (unresolved external symbol) errors across DLL boundaries.
2. **Windows Macro Collisions**: Avoid defining custom constants, enums, or variables named `MOD_CTRL`, `MOD_ALT`, `MOD_SHIFT`, `MOD_WIN` (or other common `windows.h` macros). They will collide with system macros and cause `C2059` syntax errors. Use custom prefixes like `MOUSE_MOD_` or `APP_MOD_`.
3. **Working Set Physical Memory Trim (`trimWorkingSet`)**:
   - **Trigger Principle**: Always trigger memory trimming (`easy::core::WinUtils::trimWorkingSet()`) strictly on **Cold Paths / Lifecycle Endpoints** (e.g., screenshot finished/canceled, video recording stopped, scroll capture stitched, OCR finished, settings/tray window hidden, plugin paused/disabled).
   - **Strict Red Lines**: NEVER call `trimWorkingSet()` inside Hot Paths (e.g., low-level mouse/keyboard hook callbacks, 60fps render loops, gesture tracking loops, keycast typing streams) to prevent micro-stuttering caused by soft page faults.
   - **Lazy Re-initialization**: Must pair with lazy re-creation of heavy resources (Direct2D render targets, DIB sections, big Mat buffers) when features are reactivated.
4. **Keyboard Accelerator Pipeline (`KeyboardPipeline`)**:
   - **Win32 Host Filtering**: All top-level UI windows (`SettingsWindow`, `SearchWindow`, `TrayWindow`, etc.) must route messages through `KeyboardPipeline::filterWindowMessage` to intercept `SC_KEYMENU`, `SC_CONTEXTHELP`, and `WM_HELP`, preventing native menus from stealing focus when users press Alt, F10, or F1.
   - **WebView2 Accelerator Policy**: Must attach `KeyboardPipeline::applyWebKeyboardPolicy` to `ICoreWebView2Controller` to suppress default Chromium browser shortcuts (`Ctrl+P`, `Ctrl+F`, `Ctrl+U`, `Ctrl+J`, `Ctrl+H`, `Ctrl+W`, `Alt+Left/Right`), ensuring all key combinations are 100% forwarded to the React DOM.
   - **Silent Recording Mode**: During hotkey recording in UI, frontend must notify backend via `hotkey.setPaused(true)` to mute background global hotkey dispatching and prevent accidental tool triggering.
5. **Overlay Viewport & Focus Assist Avoidance (`FocusAssistAvoidance`)**:
   - **Local Bounding Box First**: For localized transient overlays (mouse ripples, particle trails, gesture strokes), NEVER create or resize windows to full virtual screen size. Compute the dynamic union bounding box of active elements to constrain the layered window to compact local viewports (e.g., 100~300px), reducing memory/GPU cost by 99% and preventing Windows Shell from triggering Focus Assist (`🔔z` Do Not Disturb).
   - **Safe Bounding Geometry**: For full-screen ambient overlays (e.g., Spotlight vignette), shrink physical window dimensions by 1 pixel (e.g., `vw - 1, vh - 1`) to break the exact full-screen exclusive geometric match checked by Windows `SHQueryUserNotificationState`.
6. **Universal Rounded Corners Dual Insurance Pipeline (`UniversalRoundedCornersDualInsurance`)**:
   - **Cross-Platform OS Fracture**: Windows 11 DWM hardware rounded corners (`DWMWCP_ROUND`) are ignored or unsupported on Windows 10, Windows Server (2019/2022/2025), Lite OS editions, and Remote Desktop (RDP) sessions, causing frameless popup windows (e.g., Search, Tray) to leak sharp rectangular backdrops or dirty shadow slices.
   - **Dual-Insurance Standard**: All floating/frameless UI windows (`SearchWindow`, `TrayWindow`, etc.) must route geometry updates through `easy::core::WinUtils::applyUniversalRoundedCorners(hwnd, width, height, radius)`:
     - *Primary Tier (Win11)*: Sets `DWMWA_WINDOW_CORNER_PREFERENCE` to `DWMWCP_ROUND` for GPU-accelerated subpixel smooth corners and native DWM drop shadows;
     - *Secondary Tier (Win10 / Server 2022/2025 / RDP)*: Calls Win32 kernel-level `CreateRoundRectRgn` and `SetWindowRgn` to hard-clip outer corner pixels, permanently eliminating square gray backdrops across all Windows environments;
     - *DPI & Zero-Padding Sync*: Frontend shell container must use 0 padding to seamlessly fit the window boundary, and corner radius must scale proportionally with monitor DPI (`scaleMetric(radius, scale)`).

## Quality Assurance & Code Coverage (Regression-Based Mandate)
1. **Practical Coverage Baseline**: The native source-tree line coverage gate starts at **32%**. A change must not reduce the established baseline, and new or modified core business logic, parsers, state machines, security boundaries, and bug fixes must include focused tests for the changed behavior. Universal 100% statement/branch coverage is not a project requirement.
2. **Coverage Ratchet**: Raise the repository-wide threshold only after the measured baseline has increased and remained stable. Branch coverage becomes a blocking metric only after the selected coverage tool reports real branch data; `0/0` branch data must never be represented as 100% coverage.
3. **Zero-Dead-Code Principle**: Code paths proven unreachable should be refactored or removed, but platform, error-recovery, and hardware-specific paths may be justified by targeted tests or documented manual verification.
4. **Automated Gate & Verification**: Unit tests (`EasyToolsTests.exe`) and frontend checks (`npm run lint`, `npm run i18n-check`) must execute and pass completely on release builds and CI pipelines. Local incremental helper builds may expose an explicit test-skip option, but must not silently claim that tests ran.

## Accepted Product Decisions & Audit Baseline
The following behaviors were explicitly reviewed and accepted by the project owner on 2026-09-04. Future audits must not report them as defects unless their implementation materially changes or a new, distinct regression is introduced:
1. **Elevated Auto-Start Task**: The per-user EasyTools scheduled task may run at `HighestAvailable` and grant `Everyone` full control over the task. This is an explicitly accepted security trade-off.
2. **Destructive CLI Uninstall Default**: `install.ps1 -Uninstall`, `uninstall.ps1`, and `uninstall.cmd` may delete EasyTools settings, caches, screenshots, and recordings by default unless `-KeepPersonalData` is supplied. This destructive default is intentional.
3. **QuickLook Format Placeholders**: QuickLook may advertise and classify video, audio, and PDF formats even while their inline preview implementation is incomplete. Treat this as accepted product scope, not a release-blocking defect.
4. **Search Service Lifecycle Contract**: The search service is `DEMAND_START`. EasyTools startup, hidden WebView preload, settings-page status checks, focus events, and EasyTools shutdown must never start it. Only an explicit user action that opens Search may start it. Once started, it remains resident across Search-window hiding and EasyTools process exit until Windows shuts down/restarts or an administrator explicitly stops it; the next Windows boot must not auto-start it.

## Frontend (React/TypeScript) Development
1. **i18next Dynamic Keys**: The project's `react-i18next` `t()` function uses strict TypeScript union types for keys. When passing dynamic variables as translation keys (e.g., from an array or config), cast the key `as any` (e.g., `t(item.key as any)`) to bypass `TS2345` type errors.
2. **Typography & Font Rendering Standards (方案 B & C 黄金准则 & 排版单一事实源)**:
   - **Zero System Font Pollution**: 严禁在安装包中向 Windows `C:\Windows\Fonts` 写入字体或修改系统注册表，杜绝管理员权限受限、DirectWrite 进程锁定导致的卸载残留以及字体分发版权合规风险。
   - **WebFont 选型权衡架构记忆 (WebFont vs Native Font Stack Trade-offs & 方案 B 黄金准则)**：
     - *视觉评估*：思源黑体（Noto Sans SC）在中文字形饱满度、大中宫与现代几何字面表现力上明显优于系统默认微软雅黑；
     - *桌面端旧方案硬伤*：内嵌全量 `@fontsource/noto-sans-sc` 引入 100+ 个 `.woff2` 切片碎片文件，使安装包由 16MB 暴增至 32.4MB（+100% 膨胀）并在打包时产生海量磁盘 I/O 碎片；
     - *方案 B 架构终局标准*：全项目采用 **单字重极简无损思源黑体架构（500 Medium · 仅 1.10MB 单文件 · 0 碎片 I/O）** 并结合 `@font-face` 中的 `local('Source Han Sans SC')` 本地白嫖机制。既彻底消灭 100+ 切片导致的 20MB 膨胀与打包卡顿，又完美呈现中英数字/括号 100% 同源浑然一体的世界级现代几何字面质感！
   - **Single Source of Truth Font Stack**: 界面无衬线统一引用 `--font-sans`（`"Noto Sans SC", "Source Han Sans SC", -apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;`）；现代等宽/类名/代码/快捷键统一引用 `--font-mono`（`"Cascadia Code", "Cascadia Mono", "Segoe UI Mono", "Consolas", "PingFang SC", "Microsoft YaHei UI", monospace;`）。严禁在任何新组件中裸写 `ui-monospace` 或硬编码 `font-family`。
   - **Legibility & Font Size / Weight Floor (字号与字重双重底线)**: 界面中所有文本（含次级辅助说明、状态徽章、输入框、占位符 placeholder 等）字号不得低于 `0.83rem` (`11.8px ~ 12px`)，**字重底线严禁低于 500 Medium**（正文 550，标题 650~700），行高不得低于 `1.4`，保障 ClearType 次像素渲染字字饱满锐利。严禁出现 400/450 细字重导致的笔画发虚与边缘发灰。
   - **Inline Glass Code Badge Standard**: 所有窗口类名、进程名、文件路径等技术标识符必须统一使用 `<CodeBadge />` 微晶代码胶囊封装，严禁粗糙裸露细文本。
   - **Automated Typography CI Gate**: CI 流水线强制执行 `npm run typography-check`，一旦发现孤立字体声明、低于 11.8px 的微小字号或低于 500 的细字重直接阻断构建。
3. **Zero Emoji & Vector Iconography Standard**:
   - **Strict Red Lines**: Strictly prohibit hardcoded Unicode color emojis (e.g., `📦`, `💻`, `🟢`) in UI badges, state indicators, and descriptions.
   - **Vector SVG Consistency**: Uniformly use crisp Lucide vector SVG icons paired with semi-transparent glass capsule badges and ClearType subpixel rendering.

## Copyright & Open Source Attribution Standards
1. **Author Identity**: The official author identifier for the project is **`Yy1 (yuan278501381)`** (display format: `Yy1 (@yuan278501381)`).
2. **GitHub Repository & Profile**: Author profile is `https://github.com/yuan278501381`, project repository is `https://github.com/yuan278501381/easyTools`.
3. **License & Notice Enforcement**: The project is licensed under **MIT License**. All `LICENSE` files, UI About pages, Inno Setup `AppPublisher` metadata, and documentation must uniformly attribute `Copyright (c) 2026 Yy1 (yuan278501381) & EasyTools contributors`.

## Git Branch Management & Non-Fast-Forward Release Pipeline (`GitFlowReleasePipeline`)
1. **One-Command DevOps Master Release (`scripts/release.ps1`)**:
   - The release workflow is orchestrated by `pwsh scripts/release.ps1` (or `pwsh scripts/release.ps1 -Bump Minor/Major`, or `pwsh scripts/release.ps1 -Version X.Y.Z`).
   - It executes: working-tree hygiene check -> `VERSION` single-source-of-truth bump (+1) -> release-notes generation -> dev commit -> non-fast-forward `--no-ff` merge to `main` -> clean build and package (`deploy.ps1`) -> lifecycle E2E gate -> binary `ProductVersion` assertion -> signed setup/portable assets and SHA256SUMS -> immutable annotated tag and GitHub Release creation -> safe checkout or creation of the next feature branch.
2. **Visual Graph Integrity**: Ensures the Git Graph visually retains the independent development branch line and a dual-parent milestone convergence node (`merge(dev)`).

