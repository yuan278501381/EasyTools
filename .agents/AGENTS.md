## C++ Backend Development
1. **DLL Export Conventions**: When marking a class with export macros (e.g., `EASYCORE_API`, `PLUGIN_EXPORTS`), DO NOT implement non-template methods or static singletons completely inline in the header. Their implementations must be moved to a `.cpp` file to ensure they are properly exported and avoid `LNK2019` (unresolved external symbol) errors across DLL boundaries.
2. **Windows Macro Collisions**: Avoid defining custom constants, enums, or variables named `MOD_CTRL`, `MOD_ALT`, `MOD_SHIFT`, `MOD_WIN` (or other common `windows.h` macros). They will collide with system macros and cause `C2059` syntax errors. Use custom prefixes like `MOUSE_MOD_` or `APP_MOD_`.

## Frontend (React/TypeScript) Development
1. **i18next Dynamic Keys**: The project's `react-i18next` `t()` function uses strict TypeScript union types for keys. When passing dynamic variables as translation keys (e.g., from an array or config), cast the key `as any` (e.g., `t(item.key as any)`) to bypass `TS2345` type errors.
