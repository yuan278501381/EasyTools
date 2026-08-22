# 手势输入：关窗、轨迹与热路径

本文记录鼠标手势在「关窗不生效 / 轨迹看不见 / 全屏误放行」上的类别策略。实现按窗口类与工具包判定，不写死具体应用名。

策略纯函数在 `src/gesture/GestureInputPolicy.h`，动作投递在 `src/gesture/GestureAction.cpp`，按下热路径在 `src/gesture/GestureEngine.cpp`，轨迹层级在 `src/gesture/GestureTrailOverlay.cpp`。单测：`GestureInputPolicyTest`、`GestureActionTest`。

## 四类问题

| 类别 | 现象 | 策略 |
| --- | --- | --- |
| 关窗被吞 | 轨迹和 toast 都有，窗口还在 | 先向 `GA_ROOTOWNER` 投 `WM_SYSCOMMAND/SC_CLOSE` + `WM_CLOSE`，观察最多 40ms；窗口仍在才补 `Alt+F4` |
| 无标签宿主收到 Ctrl+W | `↓→` 显示「关闭标签页」但应用没有标签 | `Chrome_WidgetWin*` / Firefox / 资源管理器继续 `Ctrl+W`；CEF（`OrpheusBrowserHost`）、`Qt*`、EasyTools 自己的窗升格为关窗 |
| 轨迹沉底后不可见 | 最大化 Electron/CEF 上看不到笔迹和 toast | 注入前 `HWND_BOTTOM` 让路；**下一笔** `beginTrail` 无条件 `HWND_TOPMOST`。沉底期间不把覆盖层拉回来 |
| 全屏免打扰误伤 | IDE / 浏览器 F11 或无边框最大化时既无轨迹也无动作 | 仅无标题栏且铺满物理显示器的窗口才可能免打扰；生产力工具包类名一律继续手势 |

完整性（UIPI）只在**松手后的输入线程**查询并打日志，禁止出现在 `WH_MOUSE_LL` 按下回调里。

## 热路径预算

按下触发键时允许：状态机、全屏几何（`GetWindowRect` / `MonitorFromWindow`）、配置里例外规则是否为空。

按下时禁止：`OpenProcess`、令牌完整性、`getProcessNameFromWindow`、无条件 `GetClassName`。类名只在「已经判定为铺满显示器」之后读取，用来区分游戏与 CEF/Electron/Qt。

跨进程查询（进程名、完整性）发生在：

- 用户配置了例外规则时的匹配
- 松手后选窗日志（`class=` / `exe=`）
- `KeyStroke::send` 关窗或注入之前

`trimWorkingSet` 仍只允许出现在冷路径 / 生命周期终点，见 `.agents/AGENTS.md`。

## 关窗闭环

```
resolveCloseableWindow (GA_ROOTOWNER)
        │
        ▼
PostMessage SC_CLOSE + WM_CLOSE
        │
        ▼
观察 ≤ 40ms（同线程 PeekMessage，跨进程短睡眠）
        │
   已消失/隐藏 ──► 结束（不补按键，避免 Chrome 关两次）
        │
   仍可见 ──► 激活目标 + SendInput Alt+F4
```

内置命令 `CloseWindow` 走同一条 `KeyStroke` 路径，不再单独 `PostMessage`。

## 测试红线

`EasyToolsTests` 在开发机上跑，和用户桌面共用同一套输入栈。默认的 `BuiltinCommandDispatcher` 会把多媒体键、`Win+D`、`Win+Ctrl+Left/Right`、`LockWorkStation` 打进真实系统。

因此单测必须先 `registerHandler` 再 `execute`，或只测纯函数 / 向测试 HWND `PostMessage`。禁止对 `MediaPlayPause`、`VolumeMute`、虚拟桌面、锁屏、`Alt+F4` 走 `KeyStroke::send` / `SendInput`。

## 相关文件

- `src/gesture/GestureInputPolicy.h` — `keyStrokeShouldCloseWindow`、`isProductivityToolkitClassName`、`shouldAutoBypassFullscreenGestures`、`overlayPresentShouldForceTopmost`、`closeShouldSendKeyFallback`
- `src/core/utils/WinUtils.h` — `isWindowFullscreen`（先 `GA_ROOT`）、`isWindowHigherIntegrity`
- `src/gesture/GestureTrailOverlay.cpp` — `yieldZOrderForInput` / `raiseZOrderForDraw`
