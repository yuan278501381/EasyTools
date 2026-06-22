// ─────────────────────────────────────────────────────────────────────────────
// BuiltinCommands.cpp — 内置命令分发器实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/BuiltinCommands.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/ipc/MessageBridge.h"

#include <windows.h>
#include <shellapi.h>

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

void toggleTransparency(HWND hwnd) {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, 180, LWA_ALPHA); // 约 70% 透明度
        LOG_DEBUG("窗口透明度切换: 开启 (70%)");
    } else {
        BYTE alpha = 255;
        DWORD flags = 0;
        GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags);
        if (alpha < 255) {
            // 已透明，恢复不透明
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            LOG_DEBUG("窗口透明度切换: 关闭 (不透明)");
        } else {
            // 有 Layered 属性但全不透明，设为透明
            SetLayeredWindowAttributes(hwnd, 0, 180, LWA_ALPHA);
            LOG_DEBUG("窗口透明度切换: 开启 (70%)");
        }
    }
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
        case BuiltinCommand::ToggleWindowTransparency: {
            toggleTransparency(targetWindow());
            break;
        }

        // ── 标签页 / 编辑 (合成快捷键, 由前台应用解释) ─────────────────────
        case BuiltinCommand::CloseTab:           sendCombo(MOD_CONTROL, 'W'); break;
        case BuiltinCommand::RestoreClosedTab:   sendCombo(MOD_CONTROL | MOD_SHIFT, 'T'); break;
        case BuiltinCommand::WebSearch: {
            sendCombo(MOD_CONTROL, 'C'); // 发送 Ctrl+C 复制
            Sleep(50); // 等待剪贴板更新
            if (OpenClipboard(nullptr)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
                    if (pszText) {
                        std::wstring text(pszText);
                        GlobalUnlock(hData);
                        
                        // 简单处理: 如果包含 http:// 或 https://，直接打开，否则使用搜索引擎
                        std::wstring url;
                        if (text.find(L"http://") == 0 || text.find(L"https://") == 0) {
                            url = text;
                        } else {
                            // URL encode 过于复杂，我们采用最简单的宽字符启动，让浏览器/系统自动处理
                            // 注意：实际上 ShellExecuteW 能比较好地处理搜索参数
                            url = L"https://www.baidu.com/s?wd=" + text;
                        }
                        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                CloseClipboard();
            }
            break;
        }

        // ── 系统 / 桌面 ──────────────────────────────────────────────────────
        case BuiltinCommand::ShowDesktop:        sendCombo(MOD_WIN, 'D'); break;
        case BuiltinCommand::TaskView:           sendCombo(MOD_WIN, VK_TAB); break;
        case BuiltinCommand::SwitchDesktop:      sendCombo(MOD_WIN | MOD_CONTROL, VK_RIGHT); break;
        case BuiltinCommand::LockScreen:         LockWorkStation(); break;

        // ── 应用级 (经回调路由, 保持单向依赖) ─────────────────────────────────
        case BuiltinCommand::PauseGestures:
        case BuiltinCommand::TakeScreenshot:
        case BuiltinCommand::StartRecording:
        case BuiltinCommand::ToggleSearch:
        case BuiltinCommand::PasteAsPin:
            dispatchAppCommand(cmd);
            break;
            
        case BuiltinCommand::ShowRadialMenu: {
            POINT pt;
            GetCursorPos(&pt);
            // 依赖注入或回调比较好，这里为了简单直接调用 MessageBridge 发 IPC 给自己
            easy::core::MessageBridge::instance().handleMessage("gesture.showRadialMenu");
            break;
        }
    }
}

}  // namespace easy::gesture
