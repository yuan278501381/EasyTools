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
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
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
#include "core/events/MainThreadDispatcher.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"
#include "EasyToolsVersion.h"
#include "tray/TrayIcon.h"
#include "ui/SettingsWindow.h"
#include "ui/SearchWindow.h"
#include "ui/TrayWindow.h"
#include "ui/ToastOverlay.h"
#include "ui/WebViewEnvironmentManager.h"

// ── 常量 ─────────────────────────────────────────────────────────────────────
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"EasyTools_MessageWindow";
static constexpr const wchar_t* WINDOW_TITLE      = L"EasyToolsMessageWindow";
static constexpr const wchar_t* MUTEX_NAME        = L"Global\\EasyTools_SingleInstance_Mutex";
static constexpr UINT WM_EASYTOOLS_SHOW_SETTINGS  = WM_APP + 101;

// ── 前向声明 ─────────────────────────────────────────────────────────────────
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool checkSingleInstance();
bool hasCommandLineFlag(std::wstring_view flag);
HWND createMessageWindow(HINSTANCE hInstance);
void initializeSubsystems(HWND hwnd, bool preloadSettings);
void shutdownSubsystems();
void showSettingsWindow();
void preloadSettingsWindow(HINSTANCE hInstance);

// ── 全局状态 ─────────────────────────────────────────────────────────────────
static HANDLE g_singleInstanceMutex = nullptr;
static UINT g_wmTaskbarCreated = 0;

// ─────────────────────────────────────────────────────────────────────────────
// WinMain — 程序入口
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    const auto startupBeganAt = std::chrono::steady_clock::now();
    // ── 0. 高分屏 (DPI) 感知 ─────────────────────────────────────────────
    easy::core::WinUtils::enableHighDpiSupport();

    // ── 1. 单实例检测 ────────────────────────────────────────────────────
    if (!checkSingleInstance()) {
        return 0;
    }

    // ── 2. COM 初始化 ────────────────────────────────────────────────────
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"COM 初始化失败", L"EasyTools 错误", MB_OK | MB_ICONERROR);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
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
    LOG_INFO("EasyTools v{} 启动", easy::version::String);
    LOG_INFO("========================================");

    // ── 5a. 性能监控启动 ─────────────────────────────────────────────
    easy::core::PerformanceMonitor::instance().start();

    // ── 5. 配置管理器 ───────────────────────────────────────────────────
    if (!easy::core::ConfigManager::instance().initialize(
            easy::core::WinUtils::getConfigDirectory())) {
        LOG_ERROR("配置管理器初始化失败，应用无法安全启动");
        easy::core::PerformanceMonitor::instance().stop();
        easy::core::Logger::shutdown();
        CoUninitialize();
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 1;
    }

    // ── 6. 创建隐藏消息窗口 ─────────────────────────────────────────────
    HWND hwndMessage = createMessageWindow(hInstance);
    if (!hwndMessage) {
        LOG_ERROR("无法创建消息窗口");
        easy::core::ConfigManager::instance().shutdown();
        easy::core::PerformanceMonitor::instance().stop();
        easy::core::Logger::shutdown();
        CoUninitialize();
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 1;
    }
    easy::core::MainThreadDispatcher::instance().initialize(hwndMessage);

    // ── 7. 初始化其他子系统 ──────────────────────────────────────────────
    const bool silentStart = hasCommandLineFlag(L"--silent");
    initializeSubsystems(hwndMessage, !silentStart);
    easy::core::PerformanceMonitor::instance().recordLatency(
        "startup.core",
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startupBeganAt).count());

    // 用户主动启动时直接呈现设置；开机自启动使用 --silent 静默驻留托盘。
    if (!silentStart) {
        showSettingsWindow();
    }

    LOG_INFO("程序启动完成，进入消息循环");

    // ── 8. 消息循环 ──────────────────────────────────────────────────────
    MSG msg{};
    int messageResult = 0;
    while ((messageResult = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (messageResult == -1) LOG_ERROR("消息循环失败, error={}", GetLastError());

    // ── 9. 清理与退出 ────────────────────────────────────────────────────
    LOG_INFO("收到退出消息，准备清理");
    shutdownSubsystems();

    if (g_singleInstanceMutex) {
        CloseHandle(g_singleInstanceMutex);
    }

    CoUninitialize();
    return messageResult == -1 ? 1 : static_cast<int>(msg.wParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// 内部实现
// ─────────────────────────────────────────────────────────────────────────────

bool checkSingleInstance() {
    g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (!g_singleInstanceMutex) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;

        // 优先查找已存在的 EasyTools 消息窗口，直接通知其唤醒并显示主设置页面
        HWND existing = FindWindowW(WINDOW_CLASS_NAME, nullptr);
        if (!existing) {
            existing = FindWindowW(nullptr, L"EasyTools - 设置");
        }
        if (existing) {
            PostMessageW(existing, WM_EASYTOOLS_SHOW_SETTINGS, 0, 0);
            SetForegroundWindow(existing);
        } else {
            // 通过广播消息通知已有实例唤醒
            UINT msgShow = RegisterWindowMessageW(L"EasyTools_ShowSettings_Broadcast");
            PostMessageW(HWND_BROADCAST, msgShow, 0, 0);
        }
        return false;
    }
    return true;
}

bool hasCommandLineFlag(std::wstring_view flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool found = false;
    const std::wstring target(flag);
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], target.c_str()) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
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
        WINDOW_TITLE,
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );
}

void initializeSubsystems(HWND hwnd, bool preloadSettings) {
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

    // 3. 发现插件。这里只读取元数据与启停配置；实际初始化要等核心服务就绪。
    auto& pm = easy::core::PluginManager::instance();
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path pluginDir =
        std::filesystem::path(exePath).parent_path() / L"plugins";
    pm.loadPlugins(pluginDir.string());

    // 4. 全局快捷键注册
    easy::core::HotkeyManager::instance().initialize(hwnd);
    if (pm.isEnabledAtLaunch("search")) {
        const easy::core::HotkeyDef searchFallback{easy::core::ModKey::Alt, VK_SPACE};
        const auto searchText = easy::core::ConfigManager::instance().get<std::string>(
            "/hotkeys/Toggle Search", searchFallback.toString());
        const auto searchHotkey = searchText.empty() ? easy::core::HotkeyDef{}
            : easy::core::HotkeyDef::fromString(searchText).value_or(searchFallback);
        easy::core::HotkeyManager::instance().registerHotkey("Toggle Search", searchHotkey, []() {
            auto& searchWnd = easy::ui::SearchWindow::instance();
            if (searchWnd.isVisible()) searchWnd.hide();
            else searchWnd.show(GetModuleHandleW(nullptr));
        });

        easy::core::MessageBridge::instance().registerHandler(
            "search.toggle", [](const nlohmann::json&) -> nlohmann::json {
                if (easy::ui::SearchWindow::instance().isVisible()) {
                    easy::ui::SearchWindow::instance().hide();
                } else {
                    easy::ui::SearchWindow::instance().show(GetModuleHandleW(nullptr));
                }
                return {{"success", true}};
            });
        easy::core::MessageBridge::instance().registerHandler(
            "search.hide", [](const nlohmann::json&) -> nlohmann::json {
                easy::ui::SearchWindow::instance().hide();
                return {{"success", true}};
            });
    }

    // 注册核心基础 IPC 处理器
    easy::core::MessageBridge::instance().registerBuiltinHandlers();

    // 注册托盘菜单 IPC 处理函数
    easy::core::MessageBridge::instance().registerHandler("tray.action", [hwnd](const nlohmann::json& params) -> nlohmann::json {
        std::string action = params.value("action", "");
        
        // 执行前先隐藏托盘菜单
        easy::ui::TrayWindow::instance().hide();

        if (action == "openSettings") {
            showSettingsWindow();
        } else if (action == "screenshot") {
            easy::core::EventBus::instance().publish(easy::core::ActionTriggerScreenshotEvent{});
        } else if (action == "recording") {
            easy::core::EventBus::instance().publish(easy::core::ActionToggleRecordingEvent{});
        } else if (action == "pauseGesture") {
            easy::core::EventBus::instance().publish(easy::core::ActionToggleGesturePauseEvent{});
        } else if (action == "exit") {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            return {{"success", false}, {"error", "unknown tray action"}};
        }
        return {{"success", true}};
    });

    // 5. 键盘钩子（用于按键统计与按键显示）
    easy::core::KeyboardHook::instance().install();

    // 6. 初始化本次启动获准加载的插件
    pm.initializePlugins();

    // 7. UI Overlay 初始化
    easy::ui::ToastOverlay::instance().initialize(GetModuleHandleW(nullptr));

    easy::core::EventBus::instance().subscribe<easy::core::ShowToastEvent>(
        [](const easy::core::ShowToastEvent& ev) {
            const auto message = ev.message;
            easy::core::MainThreadDispatcher::instance().post([message]() {
                easy::ui::ToastOverlay::instance().showToast(
                    easy::core::WinUtils::wstringToUtf8(message));
            });
        }
    );

    // 8. 用户主动启动时预热设置页；开机静默驻留保持 WebView2 完全按需，
    // 避免后台常驻 Chromium 进程和数十 MB 内存。
    if (preloadSettings) preloadSettingsWindow(GetModuleHandleW(nullptr));

    // 9. 更新检查严格在后台执行，并由内部频率限制保护启动性能。
    easy::core::UpdateChecker::instance().checkAsync(false);
}

void shutdownSubsystems() {
    easy::core::UpdateChecker::instance().shutdown();
    // 先关闭所有 WebView 入口，阻止新的 IPC 请求进入插件，再等待并卸载插件。
    easy::ui::SettingsWindow::instance().destroy();
    easy::ui::SearchWindow::instance().destroy();
    easy::ui::TrayWindow::instance().destroy();
    easy::core::PluginManager::instance().shutdownPlugins();
    easy::ui::ToastOverlay::instance().shutdown();
    easy::ui::WebViewEnvironmentManager::instance().shutdown();
    easy::core::KeyboardHook::instance().uninstall();
    easy::core::StatsManager::instance().shutdown();
    easy::core::PerformanceMonitor::instance().stop();
    easy::tray::TrayIcon::instance().destroy();
    easy::core::MainThreadDispatcher::instance().shutdown();
    easy::core::ConfigManager::instance().shutdown();
    easy::core::Logger::shutdown();
}

void showSettingsWindow() {
    easy::tray::TrayIcon::instance().ensureCreated();
    auto& settingsWnd = easy::ui::SettingsWindow::instance();
    easy::ui::SettingsWindowConfig config;
    config.width = 900;
    config.height = 700;
    config.devServerUrl = "http://localhost:5173";
    settingsWnd.setConfig(config);
    settingsWnd.show(GetModuleHandleW(nullptr));
}

void preloadSettingsWindow(HINSTANCE hInstance) {
    auto& settingsWnd = easy::ui::SettingsWindow::instance();
    easy::ui::SettingsWindowConfig config;
    config.width = 900;
    config.height = 700;
    config.devServerUrl = "http://localhost:5173";
    settingsWnd.setConfig(config);
    settingsWnd.preload(hInstance);
}

LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == easy::core::MainThreadDispatcher::MessageId) {
        easy::core::MainThreadDispatcher::instance().drain();
        return 0;
    }
    if (msg == (WM_USER + 100)) {
        easy::tray::TrayIcon::instance().handleMessage(wParam, lParam);
        return 0;
    }

    if (msg == WM_TIMER) {
        if (wParam == easy::tray::TrayIcon::TIMER_ID_TRAY_RETRY) {
            easy::tray::TrayIcon::instance().ensureCreated(hwnd);
            return 0;
        }
    }

    if (msg == WM_HOTKEY) {
        easy::core::HotkeyManager::instance().handleHotkeyMessage(wParam);
        return 0;
    }

    static UINT msgShowBroadcast = RegisterWindowMessageW(L"EasyTools_ShowSettings_Broadcast");
    if (msg == msgShowBroadcast || msg == WM_EASYTOOLS_SHOW_SETTINGS) {
        easy::tray::TrayIcon::instance().ensureCreated(hwnd);
        showSettingsWindow();
        return 0;
    }

    if (g_wmTaskbarCreated != 0 && msg == g_wmTaskbarCreated) {
        LOG_INFO("检测到系统任务栏重建 (TaskbarCreated)，重新注册托盘图标");
        easy::tray::TrayIcon::instance().recreate();
        return 0;
    }

    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
