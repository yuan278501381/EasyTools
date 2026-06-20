// ─────────────────────────────────────────────────────────────────────────────
// BuiltinCommands.cpp — 内置命令分发器实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/BuiltinCommands.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <windows.h>

namespace easy::gesture {

BuiltinCommandDispatcher& BuiltinCommandDispatcher::instance() {
    static BuiltinCommandDispatcher inst;
    return inst;
}

void BuiltinCommandDispatcher::registerHandler(BuiltinCommand cmd, Handler handler) {
    m_handlers[cmd] = std::move(handler);
}

namespace {

/// 合成一组按键 (复用 KeyStroke 的 SendInput 实现)。
void sendCombo(uint8_t modifiers, uint16_t vk) {
    KeyStroke ks;
    ks.modifiers = modifiers;
    ks.virtualKey = vk;
    ks.send();
}

/// 取得手势作用的目标窗口: 当前前台顶层窗口。
HWND targetWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return nullptr;
    // 上溯到顶层窗口 (避免命中子控件)。
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}

void toggleTopmost(HWND hwnd) {
    if (!hwnd) return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool isTopmost = (ex & WS_EX_TOPMOST) != 0;
    SetWindowPos(hwnd, isTopmost ? HWND_NOTOPMOST : HWND_TOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    LOG_DEBUG("窗口置顶切换: {} -> {}", isTopmost, !isTopmost);
}

}  // namespace

bool BuiltinCommandDispatcher::dispatchAppCommand(BuiltinCommand cmd) const {
    auto it = m_handlers.find(cmd);
    if (it != m_handlers.end() && it->second) {
        it->second();
        return true;
    }
    LOG_WARN("应用级内置命令未注册 Handler: cmd={}", static_cast<int>(cmd));
    return false;
}

void BuiltinCommandDispatcher::execute(BuiltinCommand cmd) const {
    easy::core::TraceId::Scope scope;

    switch (cmd) {
        // ── 窗口管理 (作用于前台窗口) ───────────────────────────────────────
        case BuiltinCommand::CloseWindow: {
            if (HWND h = targetWindow()) PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        }
        case BuiltinCommand::MaximizeWindow: {
            HWND h = targetWindow();
            if (h) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);  // 切换式
            break;
        }
        case BuiltinCommand::MinimizeWindow: {
            if (HWND h = targetWindow()) ShowWindow(h, SW_MINIMIZE);
            break;
        }
        case BuiltinCommand::RestoreWindow: {
            if (HWND h = targetWindow()) ShowWindow(h, SW_RESTORE);
            break;
        }
        case BuiltinCommand::ToggleTopmost: {
            toggleTopmost(targetWindow());
            break;
        }

        // ── 标签页 / 编辑 (合成快捷键, 由前台应用解释) ─────────────────────
        case BuiltinCommand::CloseTab:           sendCombo(MOD_CONTROL, 'W'); break;
        case BuiltinCommand::RestoreClosedTab:   sendCombo(MOD_CONTROL | MOD_SHIFT, 'T'); break;

        // ── 系统 / 桌面 ──────────────────────────────────────────────────────
        case BuiltinCommand::ShowDesktop:        sendCombo(MOD_WIN, 'D'); break;
        case BuiltinCommand::TaskView:           sendCombo(MOD_WIN, VK_TAB); break;
        case BuiltinCommand::SwitchDesktop:      sendCombo(MOD_WIN | MOD_CONTROL, VK_RIGHT); break;
        case BuiltinCommand::LockScreen:         LockWorkStation(); break;

        // ── 应用级 (经回调路由, 保持单向依赖) ─────────────────────────────────
        case BuiltinCommand::PauseGestures:
        case BuiltinCommand::TakeScreenshot:
        case BuiltinCommand::StartRecording:
            dispatchAppCommand(cmd);
            break;
    }
}

}  // namespace easy::gesture
