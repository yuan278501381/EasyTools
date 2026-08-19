#pragma once

#include <ShellScalingApi.h>
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace easy::core::dpi {

inline constexpr UINT DefaultDpi = 96;
inline constexpr float MinimumScale = 1.0f;
inline constexpr float MaximumScale = 5.0f;

inline float scaleForDpi(UINT dpi) noexcept {
    const float normalized = dpi > 0
        ? static_cast<float>(dpi) / static_cast<float>(DefaultDpi)
        : MinimumScale;
    return std::clamp(normalized, MinimumScale, MaximumScale);
}

inline int scaleMetric(int logicalPixels, float scale) noexcept {
    return std::max(1, static_cast<int>(std::lround(
        logicalPixels * std::clamp(scale, MinimumScale, MaximumScale))));
}

inline UINT effectiveDpiForMonitor(HMONITOR monitor) noexcept {
    UINT dpiX = DefaultDpi;
    UINT dpiY = DefaultDpi;
    if (!monitor || FAILED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = GetDpiForSystem();
    }
    return std::max(DefaultDpi, dpiX);
}

inline float scaleForMonitor(HMONITOR monitor) noexcept {
    return scaleForDpi(effectiveDpiForMonitor(monitor));
}

inline UINT effectiveDpiForWindow(HWND hwnd) noexcept {
    if (hwnd && IsWindow(hwnd)) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi > 0) return std::max(DefaultDpi, dpi);
        return effectiveDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
    }
    return GetDpiForSystem();
}

inline float scaleForWindow(HWND hwnd) noexcept {
    return scaleForDpi(effectiveDpiForWindow(hwnd));
}

inline HMONITOR monitorAtPoint(POINT screenPoint) noexcept {
    return MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST);
}

inline float scaleAtPoint(POINT screenPoint) noexcept {
    return scaleForMonitor(monitorAtPoint(screenPoint));
}

inline HMONITOR activeMonitor() noexcept {
    if (const HWND foreground = GetForegroundWindow()) {
        if (const HMONITOR monitor = MonitorFromWindow(
                foreground, MONITOR_DEFAULTTONEAREST)) {
            return monitor;
        }
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    return monitorAtPoint(cursor);
}

inline RECT workArea(HMONITOR monitor) noexcept {
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

/// 将窗口矩形钳制到工作区内，支持负坐标副屏；尺寸不会超出工作区。
inline RECT clampWindowToWorkArea(RECT window, RECT work) noexcept {
    int width = (std::max)(1, static_cast<int>(window.right - window.left));
    int height = (std::max)(1, static_cast<int>(window.bottom - window.top));
    const int workW = (std::max)(1, static_cast<int>(work.right - work.left));
    const int workH = (std::max)(1, static_cast<int>(work.bottom - work.top));
    width = (std::min)(width, workW);
    height = (std::min)(height, workH);

    int x = window.left;
    int y = window.top;
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    return RECT{x, y, x + width, y + height};
}

}  // namespace easy::core::dpi
