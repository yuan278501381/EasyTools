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
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;

    // 优先加载适合托盘尺寸的标准小图标
    m_nid.hIcon = icon;
    if (!m_nid.hIcon) {
        m_nid.hIcon = (HICON)LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(101),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR
        );
    }
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    }
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
    }
    if (!m_nid.hIcon) {
        // GDI 动态生成保底图标句柄，确保绝不向 Shell 传递空句柄
        int cx = GetSystemMetrics(SM_CXSMICON);
        int cy = GetSystemMetrics(SM_CYSMICON);
        if (cx <= 0) cx = 16;
        if (cy <= 0) cy = 16;
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
        HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr);
        HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hbmColor);
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
        RECT rc{0, 0, cx, cy};
        FillRect(hdcMem, &rc, hBrush);
        DeleteObject(hBrush);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);

        ICONINFO ii{};
        ii.fIcon = TRUE;
        ii.hbmColor = hbmColor;
        ii.hbmMask = hbmMask;
        m_nid.hIcon = CreateIconIndirect(&ii);
        DeleteObject(hbmColor);
        DeleteObject(hbmMask);
    }

    wcsncpy_s(m_nid.szTip, isEnglishLocale() ? L"EasyTools - Desktop Utility" : L"EasyTools — 桌面效率工具", _TRUNCATE);

    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    bool added = Shell_NotifyIconW(NIM_ADD, &m_nid);
    if (!added) {
        // 当某些 Windows 系统对完整结构体大小严格校验时，降级使用广泛兼容的 V3 / V2 结构体尺寸
        m_nid.cbSize = NOTIFYICONDATAW_V3_SIZE;
        added = Shell_NotifyIconW(NIM_ADD, &m_nid);
    }
    if (!added) {
        m_nid.cbSize = NOTIFYICONDATAW_V2_SIZE;
        added = Shell_NotifyIconW(NIM_ADD, &m_nid);
    }
    if (!added) {
        // 尝试清理残留后重试
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_nid.cbSize = sizeof(NOTIFYICONDATAW);
        added = Shell_NotifyIconW(NIM_ADD, &m_nid);
        if (!added) {
            m_nid.cbSize = NOTIFYICONDATAW_V3_SIZE;
            added = Shell_NotifyIconW(NIM_ADD, &m_nid);
        }
    }

    if (!added) {
        // 如果依然失败，尝试直接 MODIFY 更新
        if (!Shell_NotifyIconW(NIM_MODIFY, &m_nid)) {
            LOG_ERROR("创建/更新托盘图标失败, error={}", GetLastError());
            return false;
        }
    }

    LOG_INFO("系统托盘图标已成功创建并显示 (cbSize={})", m_nid.cbSize);
    return true;
}

void TrayIcon::destroy() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    LOG_INFO("系统托盘图标已销毁");
}

void TrayIcon::showNotification(const std::wstring& title, const std::wstring& message,
                                 DWORD iconType, UINT timeoutMs) {
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = iconType;
    nid.uTimeout = timeoutMs;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::setIcon(HICON icon) {
    m_nid.hIcon = icon;
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::setTooltip(const std::wstring& tooltip) {
    wcsncpy_s(m_nid.szTip, tooltip.c_str(), _TRUNCATE);
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
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
        case WM_LBUTTONUP:
            // 左键单击：切换显示/收起托盘微型操作卡片
            showContextMenu();
            break;

        case WM_LBUTTONDBLCLK:
            // 双击：直接唤起主设置窗口
            if (easy::ui::TrayWindow::instance().isVisible()) {
                easy::ui::TrayWindow::instance().hide();
            }
            fireCallback(TrayMenuId::OpenSettings);
            break;

        case WM_RBUTTONUP:
            // 右键：弹出托盘菜单
            showContextMenu();
            break;

        default:
            break;
    }
}

void TrayIcon::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    // 如果托盘卡片当前正处于激活显示状态，再次点击托盘图标时执行平滑收起（Toggle）
    if (easy::ui::TrayWindow::instance().isVisible()) {
        easy::ui::TrayWindow::instance().hide();
        return;
    }

    // 如果用户按住 Shift 键右键，直接呼出零延迟 Windows 原生上下文菜单
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        showNativeContextMenu(pt);
        return;
    }

    // 优先调用现代 WebView2 磨砂质感托盘窗口
    easy::ui::TrayWindow::instance().show(GetModuleHandleW(nullptr), pt.x, pt.y);
}

void TrayIcon::showNativeContextMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool isEn = isEnglishLocale();
    InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, static_cast<UINT_PTR>(TrayMenuId::OpenSettings), isEn ? L"⚙️ Settings" : L"⚙️ 设置");
    InsertMenuW(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 2, MF_BYPOSITION | MF_STRING, static_cast<UINT_PTR>(TrayMenuId::Screenshot), isEn ? L"📷 Capture" : L"📷 截图");
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_STRING, static_cast<UINT_PTR>(TrayMenuId::Recording), isEn ? L"🎥 Recording" : L"🎥 录屏");
    InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 5, MF_BYPOSITION | MF_STRING, static_cast<UINT_PTR>(TrayMenuId::PauseGesture),
                m_gesturePaused ? (isEn ? L"▶️ Resume Gesture" : L"▶️ 恢复手势") : (isEn ? L"⏸️ Pause Gesture" : L"⏸️ 暂停手势"));
    InsertMenuW(hMenu, 6, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(hMenu, 7, MF_BYPOSITION | MF_STRING, static_cast<UINT_PTR>(TrayMenuId::Exit), isEn ? L"❌ Exit EasyTools" : L"❌ 退出 EasyTools");

    SetForegroundWindow(m_hwnd);
    UINT selected = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    if (selected != 0) {
        fireCallback(static_cast<TrayMenuId>(selected));
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
