// ─────────────────────────────────────────────────────────────────────────────
// TrayIcon.cpp — 系统托盘图标实现
// ─────────────────────────────────────────────────────────────────────────────

#include "tray/TrayIcon.h"
#include "core/logger/Logger.h"
#include "core/config/ConfigManager.h"
#include "ui/TrayWindow.h"

namespace easy::tray {

static bool isEnglishLocale() {
    std::string lang = easy::core::ConfigManager::instance().get<std::string>("/general/language", "auto");
    if (lang == "en" || lang == "en-US") return true;
    if (lang == "auto") {
        LANGID langID = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(langID) == LANG_ENGLISH) return true;
    }
    return false;
}

TrayIcon& TrayIcon::instance() {
    static TrayIcon inst;
    return inst;
}

bool TrayIcon::create(HWND hwnd, HICON icon) {
    m_hwnd = hwnd;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    // m_nid.uVersion = NOTIFYICON_VERSION_4; // 移除 V4，使用默认版本以接收标准鼠标消息

    // 尝试使用传入的图标，如果没有则尝试加载资源中的图标，最后 fallback 到系统图标
    m_nid.hIcon = icon;
    if (!m_nid.hIcon) {
        // 尝试加载应用程序资源中可能包含的主图标（ID = 101 或者默认的应用程序图标）
        m_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    }
    if (!m_nid.hIcon) {
        // IDI_APPLICATION 在某些系统和无窗口程序中会透明或不可见，这里改用 IDI_INFORMATION 避免空白占位
        m_nid.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    }

    wcscpy_s(m_nid.szTip, isEnglishLocale() ? L"EasyTools - Desktop Utility" : L"EasyTools — 桌面效率工具");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        LOG_ERROR("创建托盘图标失败, error={}", GetLastError());
        return false;
    }

    // Shell_NotifyIconW(NIM_SETVERSION, &m_nid); // 移除版本设置
    LOG_INFO("系统托盘图标已创建");
    return true;
}

void TrayIcon::destroy() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    LOG_INFO("系统托盘图标已销毁");
}

void TrayIcon::showNotification(const std::wstring& title, const std::wstring& message,
                                 DWORD iconType, UINT timeoutMs) {
    m_nid.uFlags |= NIF_INFO;
    m_nid.dwInfoFlags = iconType;
    m_nid.uTimeout = timeoutMs;
    wcscpy_s(m_nid.szInfoTitle, title.c_str());
    wcscpy_s(m_nid.szInfo, message.c_str());

    Shell_NotifyIconW(NIM_MODIFY, &m_nid);

    // 恢复 uFlags
    m_nid.uFlags &= ~NIF_INFO;
}

void TrayIcon::setIcon(HICON icon) {
    m_nid.hIcon = icon;
    m_nid.uFlags |= NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::setTooltip(const std::wstring& tooltip) {
    wcscpy_s(m_nid.szTip, tooltip.c_str());
    m_nid.uFlags |= NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::setGesturePaused(bool paused) {
    m_gesturePaused = paused;
    if (isEnglishLocale()) {
        setTooltip(paused ? L"EasyTools - Gesture Paused" : L"EasyTools - Desktop Utility");
    } else {
        setTooltip(paused ? L"EasyTools — 手势已暂停" : L"EasyTools — 桌面效率工具");
    }
}

void TrayIcon::handleMessage(WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    UINT msg = LOWORD(lParam);

    switch (msg) {
        case WM_LBUTTONDBLCLK:
            // 双击打开设置
            fireCallback(TrayMenuId::OpenSettings);
            break;

        case WM_RBUTTONUP:
            // 右键弹出菜单
            showContextMenu();
            break;

        default:
            break;
    }
}

void TrayIcon::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    // 调用现代的 WebView2 托盘窗口
    easy::ui::TrayWindow::instance().show(GetModuleHandleW(nullptr), pt.x, pt.y);
}

void TrayIcon::fireCallback(TrayMenuId id) {
    auto it = m_callbacks.find(id);
    if (it != m_callbacks.end() && it->second) {
        try {
            it->second();
        } catch (const std::exception& e) {
            LOG_ERROR("托盘菜单回调异常: menuId={}, error={}", static_cast<UINT>(id), e.what());
        }
    }
}

}  // namespace easy::tray
