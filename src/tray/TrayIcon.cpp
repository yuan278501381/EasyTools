// ─────────────────────────────────────────────────────────────────────────────
// TrayIcon.cpp — 系统托盘图标实现
// ─────────────────────────────────────────────────────────────────────────────

#include "tray/TrayIcon.h"
#include "core/logger/Logger.h"

namespace easy::tray {

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

    wcscpy_s(m_nid.szTip, L"EasyTools — 桌面效率工具");

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
    setTooltip(paused ? L"EasyTools — 手势已暂停" : L"EasyTools — 桌面效率工具");
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
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::OpenSettings), L"⚙ 设置(&S)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Screenshot), L"📷 截图(&C)");
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Recording), L"🎬 录屏(&R)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 暂停手势 — 带勾选状态
    AppendMenuW(hMenu, MF_STRING | (m_gesturePaused ? MF_CHECKED : MF_UNCHECKED),
                static_cast<UINT>(TrayMenuId::PauseGesture), L"⏸ 暂停手势(&P)");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, static_cast<UINT>(TrayMenuId::Exit), L"退出(&X)");

    // 必须先 SetForegroundWindow 再 TrackPopupMenu，否则菜单无法正确关闭
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);

    DestroyMenu(hMenu);
    PostMessage(m_hwnd, WM_NULL, 0, 0);  // 确保菜单正确关闭

    if (cmd > 0) {
        auto menuId = static_cast<TrayMenuId>(cmd);

        fireCallback(menuId);
    }
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
