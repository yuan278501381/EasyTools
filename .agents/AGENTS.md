## C++ Backend Development
1. **DLL Export Conventions**: When marking a class with export macros (e.g., `EASYCORE_API`, `PLUGIN_EXPORTS`), DO NOT implement non-template methods or static singletons completely inline in the header. Their implementations must be moved to a `.cpp` file to ensure they are properly exported and avoid `LNK2019` (unresolved external symbol) errors across DLL boundaries.
2. **Windows Macro Collisions**: Avoid defining custom constants, enums, or variables named `MOD_CTRL`, `MOD_ALT`, `MOD_SHIFT`, `MOD_WIN` (or other common `windows.h` macros). They will collide with system macros and cause `C2059` syntax errors. Use custom prefixes like `MOUSE_MOD_` or `APP_MOD_`.
3. **Working Set Physical Memory Trim (`trimWorkingSet`)**:
   - **Trigger Principle**: Always trigger memory trimming (`easy::core::WinUtils::trimWorkingSet()`) strictly on **Cold Paths / Lifecycle Endpoints** (e.g., screenshot finished/canceled, video recording stopped, scroll capture stitched, OCR finished, settings/tray window hidden, plugin paused/disabled).
   - **Strict Red Lines**: NEVER call `trimWorkingSet()` inside Hot Paths (e.g., low-level mouse/keyboard hook callbacks, 60fps render loops, gesture tracking loops, keycast typing streams) to prevent micro-stuttering caused by soft page faults.
   - **Lazy Re-initialization**: Must pair with lazy re-creation of heavy resources (Direct2D render targets, DIB sections, big Mat buffers) when features are reactivated.

## Quality Assurance & Code Coverage (100% Coverage Mandate)
1. **100% Code Coverage Standard**: All core business logic, utility classes, codecs, parsers, state machines, math/transform algorithms, and plugin contracts must maintain 100% statement and branch test coverage.
2. **Zero-Dead-Code Principle**: Any code path that cannot be covered or is unexecutable must be strictly refactored or removed. Do not introduce unreachable switch cases or phantom branches.
3. **Automated Gate & Verification**: Unit tests (`EasyToolsTests.exe`) and frontend checks (`npm run lint`, `npm run i18n-check`) must execute and pass completely on every build and CI pipeline.

## Frontend (React/TypeScript) Development
1. **i18next Dynamic Keys**: The project's `react-i18next` `t()` function uses strict TypeScript union types for keys. When passing dynamic variables as translation keys (e.g., from an array or config), cast the key `as any` (e.g., `t(item.key as any)`) to bypass `TS2345` type errors.

## Copyright & Open Source Attribution Standards
1. **Author Identity**: The official author identifier for the project is **`Yy1 (yuan278501381)`** (display format: `Yy1 (@yuan278501381)`).
2. **GitHub Repository & Profile**: Author profile is `https://github.com/yuan278501381`, project repository is `https://github.com/yuan278501381/easyTools`.
3. **License & Notice Enforcement**: The project is licensed under **MIT License**. All `LICENSE` files, UI About pages, Inno Setup `AppPublisher` metadata, and documentation must uniformly attribute `Copyright (c) 2026 Yy1 (yuan278501381) & EasyTools contributors`.
