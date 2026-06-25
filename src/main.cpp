// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — EasyTools 程序入口点
//
// 启动流程:
//   1. 单实例检测（避免重复启动）
//   2. COM 初始化
//   3. 崩溃处理器安装
//   4. 日志系统初始化
//   5. 配置管理器初始化
//   6. 创建隐藏消息窗口（消息泵）
//   7. 系统托盘图标创建
//   8. 全局快捷键注册
//   9. 手势引擎启动
//  10. WebView2 设置窗口（按需创建）
//  11. 消息循环
// ─────────────────────────────────────────────────────────────────────────────

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <filesystem>
#include <thread>

#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/stats/StatsManager.h"
#include "core/crash/CrashHandler.h"
#include "core/lua/LuaEngine.h"
#include "core/plugin/PluginManager.h"
#include "core/events/EventBus.h"
#include "core/stats/PerformanceMonitor.h"
#include "tray/TrayIcon.h"
#include "ui/SettingsWindow.h"
#include "ui/SearchWindow.h"
#include "ui/KeycastOverlay.h"

// ── 常量 ─────────────────────────────────────────────────────────────────────
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"EasyTools_MessageWindow";
static constexpr const wchar_t* MUTEX_NAME        = L"Global\\EasyTools_SingleInstance_Mutex";

// ── 前向声明 ─────────────────────────────────────────────────────────────────
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool checkSingleInstance();
HWND createMessageWindow(HINSTANCE hInstance);
void initializeSubsystems(HWND hwnd);
void shutdownSubsystems();
void showSettingsWindow();

// ── 全局状态 ─────────────────────────────────────────────────────────────────
static HANDLE g_singleInstanceMutex = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// WinMain — 程序入口
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // ── 0. 高分屏 (DPI) 感知 ─────────────────────────────────────────────
    easy::core::WinUtils::enableHighDpiSupport();

    // ── 1. 单实例检测 ────────────────────────────────────────────────────
    if (!checkSingleInstance()) {
        MessageBoxW(nullptr, L"EasyTools 已在运行中。", L"EasyTools", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // ── 2. COM 初始化 ────────────────────────────────────────────────────
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"COM 初始化失败", L"EasyTools 错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ── 3. 崩溃处理器 ───────────────────────────────────────────────────
    auto dumpDir = easy::core::WinUtils::getAppDataDirectory() / L"crashdumps";
    easy::core::CrashHandler::install(dumpDir);

    // ── 4. 日志系统 ──────────────────────────────────────────────────────
    easy::core::LoggerConfig logConfig;
    logConfig.logDir = easy::core::WinUtils::wstringToUtf8(
        easy::core::WinUtils::getLogDirectory().wstring()
    );
    easy::core::Logger::initialize(logConfig);

    easy::core::TraceId::Scope mainScope;
    LOG_INFO("========================================");
    LOG_INFO("EasyTools v1.0.0 启动");
    LOG_INFO("========================================");

    // ── 5a. 性能监控启动 ─────────────────────────────────────────────
    easy::core::PerformanceMonitor::instance().start();

    // ── 5. 配置管理器 ───────────────────────────────────────────────────
    easy::core::ConfigManager::instance().initialize(
        easy::core::WinUtils::getConfigDirectory()
    );

    // ── 6. 创建隐藏消息窗口 ─────────────────────────────────────────────
    HWND hwndMessage = createMessageWindow(hInstance);
    if (!hwndMessage) {
        LOG_ERROR("无法创建消息窗口");
        return 1;
    }

    // ── 7. 初始化其他子系统 ──────────────────────────────────────────────
    initializeSubsystems(hwndMessage);

    LOG_INFO("程序启动完成，进入消息循环");

    // ── 8. 消息循环 ──────────────────────────────────────────────────────
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // ── 9. 清理与退出 ────────────────────────────────────────────────────
    LOG_INFO("收到退出消息，准备清理");
    shutdownSubsystems();

    if (g_singleInstanceMutex) {
        CloseHandle(g_singleInstanceMutex);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// 内部实现
// ─────────────────────────────────────────────────────────────────────────────

bool checkSingleInstance() {
    g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

HWND createMessageWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.lpfnWndProc = MessageWindowProc;
    wcex.hInstance   = hInstance;
    wcex.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassExW(&wcex);

    return CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        WINDOW_CLASS_NAME,
        L"EasyToolsMessageWindow",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );
}

void initializeSubsystems(HWND hwnd) {
    // 1. 托盘图标
    auto& tray = easy::tray::TrayIcon::instance();
    tray.create(hwnd);

    tray.onOpenSettings([]() { showSettingsWindow(); });
    tray.onExit([hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); });

    tray.onScreenshot([]() { easy::core::EventBus::instance().publish(easy::core::ActionTriggerScreenshotEvent{}); });
    tray.onRecording([]() { easy::core::EventBus::instance().publish(easy::core::ActionToggleRecordingEvent{}); });
    tray.onPauseGesture([]() { easy::core::EventBus::instance().publish(easy::core::ActionToggleGesturePauseEvent{}); });

    // 2. 统计模块
    easy::core::StatsManager::instance().initialize();

    // 3. 全局快捷键注册
    easy::core::HotkeyManager::instance().initialize(hwnd);
    easy::core::HotkeyManager::instance().registerHotkey("Toggle Search", {easy::core::ModKey::Alt, VK_SPACE}, []() {
        auto& searchWnd = easy::ui::SearchWindow::instance();
        if (searchWnd.isVisible()) {
            searchWnd.hide();
        } else {
            searchWnd.show(GetModuleHandleW(nullptr));
        }
    });

    // 注册 search.toggle IPC 处理函数
    easy::core::MessageBridge::instance().registerHandler("search.toggle", [](const nlohmann::json&) -> nlohmann::json {
        if (easy::ui::SearchWindow::instance().isVisible()) {
            easy::ui::SearchWindow::instance().hide();
        } else {
            easy::ui::SearchWindow::instance().show(GetModuleHandle(NULL));
        }
        return {{"success", true}};
    });

    // 注册 search.hide IPC 处理函数
    easy::core::MessageBridge::instance().registerHandler("search.hide", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::SearchWindow::instance().hide();
        return {{"success", true}};
    });

    // 注册核心基础 IPC 处理器
    easy::core::MessageBridge::instance().registerBuiltinHandlers();

    // 4. 键盘钩子（用于按键显示等）
    easy::core::KeyboardHook::instance().install();

    // 5. 加载并初始化插件
    auto& pm = easy::core::PluginManager::instance();
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path pluginDir = std::filesystem::path(exePath).parent_path() / L"plugins";
    pm.loadPlugins(pluginDir.string());
    pm.initializePlugins();

    // 6. UI Overlay 初始化
    easy::ui::KeycastOverlay::instance().initialize(GetModuleHandleW(nullptr));
}

void shutdownSubsystems() {
    easy::core::PluginManager::instance().shutdownPlugins();
    easy::ui::KeycastOverlay::instance().shutdown();
    easy::ui::SearchWindow::instance().destroy();
    easy::core::KeyboardHook::instance().uninstall();
    easy::core::HotkeyManager::instance().shutdown();
    easy::core::StatsManager::instance().shutdown();
    easy::core::PerformanceMonitor::instance().stop();
    easy::core::EventBus::instance().clearAll();
    easy::tray::TrayIcon::instance().destroy();
    easy::core::Logger::shutdown();
}

void showSettingsWindow() {
    auto& settingsWnd = easy::ui::SettingsWindow::instance();
    easy::ui::SettingsWindowConfig config;
    config.width = 900;
    config.height = 700;
    config.devServerUrl = "http://localhost:5173";
    settingsWnd.setConfig(config);
    settingsWnd.show(GetModuleHandleW(nullptr));
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == (WM_USER + 100)) {
        easy::tray::TrayIcon::instance().handleMessage(wParam, lParam);
        return 0;
    }

    if (msg == WM_HOTKEY) {
        easy::core::HotkeyManager::instance().handleHotkeyMessage(wParam);
        return 0;
    }

    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
