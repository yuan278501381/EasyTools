#include "ui/native/window/NativeFramelessWindow.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include <algorithm>

namespace tools3000::ui::native {

NativeFramelessWindow::NativeFramelessWindow() = default;
NativeFramelessWindow::~NativeFramelessWindow() = default;

void NativeFramelessWindow::setFramelessMode(FramelessMode mode) {
    m_mode = mode;
    if (m_mode == FramelessMode::TrayMenu || m_mode == FramelessMode::Popup) {
        setAutoCloseOnBlur(true);
        setAlwaysOnTop(true);
    } else if (m_mode == FramelessMode::SpotlightCenter) {
        setAutoCloseOnBlur(true);
        setAlwaysOnTop(true);
    }
}

void NativeFramelessWindow::setAlwaysOnTop(bool alwaysOnTop) {
    m_alwaysOnTop = alwaysOnTop;
    if (hwnd() && IsWindow(hwnd())) {
        SetWindowPos(
            hwnd(),
            alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
}

void NativeFramelessWindow::positionNearTray(int anchorX, int anchorY, int contentW, int contentH) {
    if (!hwnd()) return;

    POINT pt{ anchorX, anchorY };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    RECT workArea = tools3000::core::dpi::workArea(hMon);
    float scale = tools3000::core::dpi::scaleForMonitor(hMon);

    const int physicalW = std::max(1, static_cast<int>(contentW * scale));
    const int physicalH = std::max(1, static_cast<int>(contentH * scale));
    const int margin = static_cast<int>(12.0f * scale);

    int posX = std::clamp(anchorX - physicalW / 2, static_cast<int>(workArea.left + margin), static_cast<int>(workArea.right - physicalW - margin));
    int posY = anchorY - physicalH - margin;
    if (posY < workArea.top + margin) {
        // 如果上方空间不足（如任务栏在顶部），则向下弹出
        posY = anchorY + margin;
    }
    posY = std::clamp(posY, static_cast<int>(workArea.top + margin), static_cast<int>(workArea.bottom - physicalH - margin));

    SetWindowPos(
        hwnd(),
        m_alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        posX, posY, physicalW, physicalH,
        SWP_NOACTIVATE | SWP_FRAMECHANGED
    );

    int radius = static_cast<int>(12.0f * scale);
    RECT rcClient{};
    GetClientRect(hwnd(), &rcClient);
    int actualW = rcClient.right - rcClient.left;
    int actualH = rcClient.bottom - rcClient.top;
    tools3000::core::WinUtils::applyUniversalRoundedCorners(hwnd(), actualW > 0 ? actualW : physicalW, actualH > 0 ? actualH : physicalH, radius);
    m_lastShowTick = GetTickCount64();
    requestLayout();
}

void NativeFramelessWindow::positionCentered(int w, int h) {
    if (!hwnd()) return;

    HMONITOR hMon = MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST);
    RECT workArea = tools3000::core::dpi::workArea(hMon);
    float scale = tools3000::core::dpi::scaleForMonitor(hMon);

    const int physicalW = static_cast<int>(w * scale);
    const int physicalH = static_cast<int>(h * scale);

    int posX = workArea.left + std::max(0, static_cast<int>((workArea.right - workArea.left - physicalW) / 2));
    int posY = workArea.top + std::max(0, static_cast<int>((workArea.bottom - workArea.top - physicalH) / 2));

    SetWindowPos(
        hwnd(),
        m_alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        posX, posY, physicalW, physicalH,
        SWP_FRAMECHANGED
    );

    int radius = static_cast<int>(12.0f * scale);
    RECT rcClient{};
    GetClientRect(hwnd(), &rcClient);
    int actualW = rcClient.right - rcClient.left;
    int actualH = rcClient.bottom - rcClient.top;
    tools3000::core::WinUtils::applyUniversalRoundedCorners(hwnd(), actualW > 0 ? actualW : physicalW, actualH > 0 ? actualH : physicalH, radius);
    m_lastShowTick = GetTickCount64();
    requestLayout();
}

void NativeFramelessWindow::setLogicalSize(int widthDip, int heightDip) {
    if (!hwnd()) return;
    float scale = getDpiScale();
    int pw = static_cast<int>(widthDip * scale);
    int ph = static_cast<int>(heightDip * scale);
    SetWindowPos(hwnd(), nullptr, 0, 0, pw, ph, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    int radius = static_cast<int>(12.0f * scale);
    RECT rcClient{};
    GetClientRect(hwnd(), &rcClient);
    int actualW = rcClient.right - rcClient.left;
    int actualH = rcClient.bottom - rcClient.top;
    tools3000::core::WinUtils::applyUniversalRoundedCorners(hwnd(), actualW > 0 ? actualW : pw, actualH > 0 ? actualH : ph, radius);
    requestLayout();
}

LRESULT NativeFramelessWindow::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE && m_autoCloseOnBlur) {
            uint64_t now = GetTickCount64();
            // 避免呼出瞬间的误触失焦 (给 150ms 缓冲)
            if (now - m_lastShowTick > 150) {
                if (m_onFocusLost) {
                    m_onFocusLost();
                } else {
                    hide();
                }
            }
        }
        break;
    }
    case WM_KILLFOCUS: {
        if (m_autoCloseOnBlur) {
            uint64_t now = GetTickCount64();
            if (now - m_lastShowTick > 150) {
                if (m_onFocusLost) {
                    m_onFocusLost();
                } else {
                    hide();
                }
            }
        }
        break;
    }
    default:
        break;
    }

    return NativeWindowHost::handleMessage(uMsg, wParam, lParam);
}

} // namespace tools3000::ui::native
