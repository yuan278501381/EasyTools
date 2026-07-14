#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SettingsWindow — WebView2 设置窗口
//
// 职责:
//   1. 创建 Win32 顶层窗口承载 WebView2 控件
//   2. 初始化 WebView2 环境（Evergreen Runtime）
//   3. 加载前端 UI（本地文件或开发服务器）
//   4. 桥接 JS ↔ C++ 双向通信（通过 MessageBridge）
//   5. 窗口生命周期管理（显示/隐藏/最小化到托盘）
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_UI_SETTINGSWINDOW_H
#define EASYTOOLS_UI_SETTINGSWINDOW_H

#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <memory>

// WebView2 头文件（来自 NuGet 包）
#include <wrl/client.h>

// 前向声明 WebView2 接口（避免在头文件中引入完整 WebView2 SDK）
struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

namespace easy::ui {

/// 设置窗口配置
struct SettingsWindowConfig {
    int width  = 1100;              // 窗口宽度
    int height = 750;               // 窗口高度
    bool startCentered = true;      // 是否居中显示
    bool devToolsEnabled = true;    // 是否启用开发者工具（仅 Debug）
    std::string devServerUrl;       // 开发服务器 URL（空则使用本地文件）
};

class SettingsWindow {
public:
    static SettingsWindow& instance();

    /// 创建并显示设置窗口
    void show(HINSTANCE hInstance);

    /// 后台静默创建并预热 WebView2 环境，不显示窗口（实现极速冷启动）
    void preload(HINSTANCE hInstance);

    /// 隐藏窗口（不销毁，下次 show 时直接显示）
    void hide();

    /// 窗口是否可见
    bool isVisible() const;

    /// 销毁窗口
    void destroy();

    /// 设置配置
    void setConfig(const SettingsWindowConfig& config) { m_config = config; }

    /// 导航到指定页面（如 "#/gesture" 直接跳转到手势设置页）
    void navigateTo(const std::string& path);

    /// 向前端推送事件
    void pushEventToFrontend(const std::string& eventName, const std::string& dataJson);

private:
    SettingsWindow() = default;
    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    /// 创建 Win32 窗口
    bool createWindow(HINSTANCE hInstance);

    /// 初始化 WebView2 环境
    void initializeWebView2();

    /// WebView2 初始化完成后的回调
    void onWebView2Ready();

    /// 获取前端 UI 入口 URL
    std::string getUIEntryUrl() const;

    /// 窗口过程
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    SettingsWindowConfig m_config;
    std::atomic<bool> m_visible{false};
    bool m_webViewReady = false;

    // WebView2 组件（使用 void* 避免头文件依赖）
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_webView;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_environment;
};

}  // namespace easy::ui

#endif  // EASYTOOLS_UI_SETTINGSWINDOW_H
