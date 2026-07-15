#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TrayWindow.h — WebView2 托盘无边框悬浮菜单
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_UI_TRAYWINDOW_H
#define EASYTOOLS_UI_TRAYWINDOW_H

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <wrl/client.h>
#include <WebView2.h>

namespace easy::ui {

class TrayWindow {
public:
    static TrayWindow& instance();

    /// 显示托盘菜单，并将其定位到指定的坐标附近 (通常是鼠标点击系统托盘的位置)
    void show(HINSTANCE hInstance, int x, int y);

    /// 隐藏
    void hide();

    /// 是否可见
    bool isVisible() const;

    /// 销毁
    void destroy();

private:
    TrayWindow() = default;
    ~TrayWindow() { destroy(); }
    TrayWindow(const TrayWindow&) = delete;
    TrayWindow& operator=(const TrayWindow&) = delete;

    bool createWindow(HINSTANCE hInstance, int x, int y);
    void initializeWebView2();
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    std::atomic<bool> m_visible{false};
    std::atomic<bool> m_webViewReady{false};
    std::atomic<uint64_t> m_generation{0};

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;
};

} // namespace easy::ui

#endif // EASYTOOLS_UI_TRAYWINDOW_H
