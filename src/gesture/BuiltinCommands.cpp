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
    std::unique_lock lock(m_mutex);
    m_handlers[cmd] = std::move(handler);
}

void BuiltinCommandDispatcher::clearHandlers() {
    decltype(m_handlers) handlers;
    {
        std::unique_lock lock(m_mutex);
        handlers.swap(m_handlers);
    }
    handlers.clear();
}

namespace {

/// 合成一组按键 (复用 KeyStroke 的 SendInput 实现，可指定目标窗口)。
void sendCombo(uint8_t modifiers, uint16_t vk, void* targetWindow = nullptr) {
    KeyStroke ks;
    ks.modifiers = modifiers;
    ks.virtualKey = vk;
    ks.send(targetWindow);
}

/// 取得手势作用的目标窗口: 优先使用传入的 targetWindow，否则解析前台或光标下顶层窗口。
HWND resolveTargetWindow(void* targetWindowPtr) {
    HWND hwnd = static_cast<HWND>(resolveGestureKeyTarget(
        targetWindowPtr, GetForegroundWindow(), nullptr));
    if (hwnd) return hwnd;
    POINT pt;
    GetCursorPos(&pt);
    hwnd = static_cast<HWND>(windowFromPointSkippingGestureOverlay(pt.x, pt.y));
    hwnd = static_cast<HWND>(resolveGestureKeyTarget(hwnd, nullptr, nullptr));
    return hwnd;
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

bool BuiltinCommandDispatcher::hasHandler(BuiltinCommand cmd) const {
    std::shared_lock lock(m_mutex);
    return m_handlers.find(cmd) != m_handlers.end();
}

bool BuiltinCommandDispatcher::dispatchAppCommand(BuiltinCommand cmd) const {
    Handler handler;
    {
        std::shared_lock lock(m_mutex);
        if (const auto it = m_handlers.find(cmd); it != m_handlers.end()) {
            handler = it->second;
        }
    }
    if (handler) {
        handler();
        return true;
    }
    LOG_WARN("应用级内置命令未注册 Handler: cmd={}", static_cast<int>(cmd));
    return false;
}

void BuiltinCommandDispatcher::execute(BuiltinCommand cmd, void* targetWindowPtr) const {
    easy::core::TraceId::Scope scope;

    // 若注册了自定义 Handler (如应用级命令路由或测试 Mock 拦截)，优先通过 Handler 分发
    if (hasHandler(cmd)) {
        dispatchAppCommand(cmd);
        return;
    }

    switch (cmd) {
        // ── 窗口管理 (精准作用于鼠标下方目标窗口) ─────────────────────────
        case BuiltinCommand::CloseWindow: {
            sendCombo(MOD_ALT, VK_F4, targetWindowPtr);
            break;
        }
        case BuiltinCommand::MaximizeWindow: {
            HWND h = resolveTargetWindow(targetWindowPtr);
            if (h) ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);  // 切换式
            break;
        }
        case BuiltinCommand::MinimizeWindow: {
            if (HWND h = resolveTargetWindow(targetWindowPtr)) ShowWindow(h, SW_MINIMIZE);
            break;
        }
        case BuiltinCommand::RestoreWindow: {
            if (HWND h = resolveTargetWindow(targetWindowPtr)) ShowWindow(h, SW_RESTORE);
            break;
        }
        case BuiltinCommand::ToggleTopmost: {
            toggleTopmost(resolveTargetWindow(targetWindowPtr));
            break;
        }
        case BuiltinCommand::ToggleWindowTransparency: {
            toggleTransparency(resolveTargetWindow(targetWindowPtr));
            break;
        }

        // ── 标签页 / 编辑 (优先作用于鼠标下方目标窗口) ─────────────────────
        case BuiltinCommand::CloseTab:           sendCombo(MOD_CONTROL, 'W', targetWindowPtr); break;
        case BuiltinCommand::RestoreClosedTab:   sendCombo(MOD_CONTROL | MOD_SHIFT, 'T', targetWindowPtr); break;
        case BuiltinCommand::WebSearch: {
            sendCombo(MOD_CONTROL, 'C', targetWindowPtr); // 发送 Ctrl+C 复制
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
                            url = L"https://www.baidu.com/s?wd=" + text;
                        }
                        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                CloseClipboard();
            }
            break;
        }

        // ── 全局多媒体控制 (全局广播，不切换窗口焦点) ─────────────────────
        case BuiltinCommand::MediaNext:          sendCombo(0, VK_MEDIA_NEXT_TRACK); break;
        case BuiltinCommand::MediaPrev:          sendCombo(0, VK_MEDIA_PREV_TRACK); break;
        case BuiltinCommand::MediaPlayPause:     sendCombo(0, VK_MEDIA_PLAY_PAUSE); break;
        case BuiltinCommand::VolumeUp:           sendCombo(0, VK_VOLUME_UP); break;
        case BuiltinCommand::VolumeDown:         sendCombo(0, VK_VOLUME_DOWN); break;
        case BuiltinCommand::VolumeMute:         sendCombo(0, VK_VOLUME_MUTE); break;

        // ── 系统 / 桌面 ──────────────────────────────────────────────────────
        case BuiltinCommand::ShowDesktop:        sendCombo(MOD_WIN, 'D'); break;
        case BuiltinCommand::TaskView:           sendCombo(MOD_WIN, VK_TAB); break;
        case BuiltinCommand::SwitchDesktop:      sendCombo(MOD_WIN | MOD_CONTROL, VK_RIGHT); break;
        case BuiltinCommand::PrevVirtualDesktop: sendCombo(MOD_WIN | MOD_CONTROL, VK_LEFT); break;
        case BuiltinCommand::NextVirtualDesktop: sendCombo(MOD_WIN | MOD_CONTROL, VK_RIGHT); break;
        case BuiltinCommand::LockScreen:         LockWorkStation(); break;

        // ── 应用级 (经回调路由, 保持单向依赖) ─────────────────────────────────
        case BuiltinCommand::PauseGestures:
        case BuiltinCommand::TakeScreenshot:
        case BuiltinCommand::StartRecording:
        case BuiltinCommand::ToggleSearch:
        case BuiltinCommand::ShowRadialMenu:
        case BuiltinCommand::PasteAsPin:
            dispatchAppCommand(cmd);
            break;
    }
}

}  // namespace easy::gesture
