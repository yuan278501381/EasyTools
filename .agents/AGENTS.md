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

## Quality Assurance & Code Coverage (100% Coverage Mandate)
1. **100% Code Coverage Standard**: All core business logic, utility classes, codecs, parsers, state machines, math/transform algorithms, and plugin contracts must maintain 100% statement and branch test coverage.
2. **Zero-Dead-Code Principle**: Any code path that cannot be covered or is unexecutable must be strictly refactored or removed. Do not introduce unreachable switch cases or phantom branches.
3. **Automated Gate & Verification**: Unit tests (`EasyToolsTests.exe`) and frontend checks (`npm run lint`, `npm run i18n-check`) must execute and pass completely on every build and CI pipeline.

## Frontend (React/TypeScript) Development
1. **i18next Dynamic Keys**: The project's `react-i18next` `t()` function uses strict TypeScript union types for keys. When passing dynamic variables as translation keys (e.g., from an array or config), cast the key `as any` (e.g., `t(item.key as any)`) to bypass `TS2345` type errors.
2. **Typography & Font Rendering Standards (方案 B & C 黄金准则 & 排版单一事实源)**:
   - **Zero System Font Pollution**: 严禁在安装包中向 Windows `C:\Windows\Fonts` 写入字体或修改系统注册表，杜绝管理员权限受限、DirectWrite 进程锁定导致的卸载残留以及字体分发版权合规风险。
   - **WebFont 选型权衡架构记忆 (WebFont vs Native Font Stack Trade-offs & 方案 B 黄金准则)**：
     - *视觉评估*：思源黑体（Noto Sans SC）在中文字形饱满度、大中宫与现代几何字面表现力上明显优于系统默认微软雅黑；
     - *桌面端旧方案硬伤*：内嵌全量 `@fontsource/noto-sans-sc` 引入 100+ 个 `.woff2` 切片碎片文件，使安装包由 16MB 暴增至 32.4MB（+100% 膨胀）并在打包时产生海量磁盘 I/O 碎片；
     - *方案 B 架构终局标准*：全项目采用 **单字重极简无损思源黑体架构（500 Medium · 仅 1.10MB 单文件 · 0 碎片 I/O）** 并结合 `@font-face` 中的 `local('Source Han Sans SC')` 本地白嫖机制。既彻底消灭 100+ 切片导致的 20MB 膨胀与打包卡顿，又完美呈现中英数字/括号 100% 同源浑然一体的世界级现代几何字面质感！
   - **Single Source of Truth Font Stack**: 界面无衬线统一引用 `--font-sans`（`"Noto Sans SC", "Source Han Sans SC", -apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;`）；现代等宽/类名/代码/快捷键统一引用 `--font-mono`（`"Cascadia Code", "Cascadia Mono", "Segoe UI Mono", "Consolas", "PingFang SC", "Microsoft YaHei UI", monospace;`）。严禁在任何新组件中裸写 `ui-monospace` 或硬编码 `font-family`。
   - **Legibility & Font Size Floor**: 界面中所有文本（含次级辅助说明、状态徽章、输入框等）字号不得低于 `0.83rem` (`11.8px ~ 12px`)，次级文本字重不得低于 500（正文 550，标题 650~700），行高不得低于 `1.4`，保障 ClearType 次像素渲染字字饱满锐利。
   - **Inline Glass Code Badge Standard**: 所有窗口类名、进程名、文件路径等技术标识符必须统一使用 `<CodeBadge />` 微晶代码胶囊封装，严禁粗糙裸露细文本。
   - **Automated Typography CI Gate**: CI 流水线强制执行 `npm run typography-check`，一旦发现孤立字体声明或低于 11.8px 的微小字号直接阻断构建。
3. **Zero Emoji & Vector Iconography Standard**:
   - **Strict Red Lines**: Strictly prohibit hardcoded Unicode color emojis (e.g., `📦`, `💻`, `🟢`) in UI badges, state indicators, and descriptions.
   - **Vector SVG Consistency**: Uniformly use crisp Lucide vector SVG icons paired with semi-transparent glass capsule badges and ClearType subpixel rendering.

## Copyright & Open Source Attribution Standards
1. **Author Identity**: The official author identifier for the project is **`Yy1 (yuan278501381)`** (display format: `Yy1 (@yuan278501381)`).
2. **GitHub Repository & Profile**: Author profile is `https://github.com/yuan278501381`, project repository is `https://github.com/yuan278501381/easyTools`.
3. **License & Notice Enforcement**: The project is licensed under **MIT License**. All `LICENSE` files, UI About pages, Inno Setup `AppPublisher` metadata, and documentation must uniformly attribute `Copyright (c) 2026 Yy1 (yuan278501381) & EasyTools contributors`.

## Git Branch Management & Non-Fast-Forward Release Pipeline (`GitFlowReleasePipeline`)
1. **`dev` Branch (Daily R&D Arena)**: Holds all granular exploration, feature iterations, and tuning commits. Before releasing, commit version bumping and notes on `dev`: `chore(release): 升级项目版本至 vX.Y.Z 并同步官方发布日志与版本元数据`.
2. **`main` Branch (Production Release Line & `--no-ff` Merge Mandate)**:
   - **Strict Non-Fast-Forward Rule**: NEVER fast-forward `dev` into `main`. ALWAYS execute explicit non-fast-forward merge:
     ```bash
     git checkout main
     git merge --no-ff dev -m "merge(dev): 合并 dev 分支至 main 分支，发布 vX.Y.Z 正式版"
     ```
   - **Visual Graph Integrity**: Ensures the Git Graph visually retains the independent development branch line and a dual-parent milestone convergence node (`merge(dev)`).
3. **Tagging & GitHub Release Automation**:
   - Create and push signed/annotated tag `vX.Y.Z` directly on the `merge(dev)` commit:
     ```bash
     git tag -a vX.Y.Z -m "EasyTools vX.Y.Z"
     git push origin refs/tags/vX.Y.Z
     ```
   - Execute `pwsh scripts/publish_release.ps1 -Tag vX.Y.Z` to build setup installers, generate SHA256 checksums, and publish release notes.
4. **Post-Release Feature Branching**: Immediately branch off from the latest `main` commit to create dedicated feature branches (e.g., `git checkout -b feature/optimize-capture-recorder-gestures main`).

