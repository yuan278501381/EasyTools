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

#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/crash/CrashHandler.h"
#include "tray/TrayIcon.h"
#include "gesture/GestureEngine.h"
#include "capture/ScreenCapture.h"
#include "capture/ScreenRecorder.h"
#include "ui/SettingsWindow.h"

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
    LOG_INFO("EasyTools v0.1.0 启动");
    LOG_INFO("========================================");

    // ── 5. 配置管理器 ───────────────────────────────────────────────────
    easy::core::ConfigManager::instance().initialize(
        easy::core::WinUtils::getConfigDirectory()
    );

    // ── 6. 创建隐藏消息窗口 ─────────────────────────────────────────────
    HWND hwndMessage = createMessageWindow(hInstance);
    if (!hwndMessage) {
        LOG_CRITICAL("创建消息窗口失败, 程序退出");
        return 1;
    }

    // ── 7-9. 初始化子系统 ───────────────────────────────────────────────
    initializeSubsystems(hwndMessage);

    // ── 10. 消息循环 ─────────────────────────────────────────────────────
    LOG_INFO("进入主消息循环");
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ── 11. 清理 ─────────────────────────────────────────────────────────
    shutdownSubsystems();
    CoUninitialize();

    if (g_singleInstanceMutex) {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
    }

    LOG_INFO("EasyTools 已退出");
    easy::core::Logger::shutdown();

    return static_cast<int>(msg.wParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// 单实例检测
// ─────────────────────────────────────────────────────────────────────────────
bool checkSingleInstance() {
    g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 创建隐藏消息窗口（用于 WM_HOTKEY、WM_TRAYICON 等消息接收）
// ─────────────────────────────────────────────────────────────────────────────
HWND createMessageWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MessageWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (!RegisterClassExW(&wc)) {
        return nullptr;
    }

    return CreateWindowExW(
        0, WINDOW_CLASS_NAME, L"EasyTools Message Window",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        nullptr, hInstance, nullptr
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// 消息窗口过程
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            easy::core::HotkeyManager::instance().handleHotkeyMessage(wParam);
            return 0;

        case WM_USER + 100:  // WM_TRAYICON
            easy::tray::TrayIcon::instance().handleMessage(wParam, lParam);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化子系统
// ─────────────────────────────────────────────────────────────────────────────
void initializeSubsystems(HWND hwnd) {
    // 快捷键管理器
    easy::core::HotkeyManager::instance().initialize(hwnd);

    // IPC 桥接
    easy::core::MessageBridge::instance().registerBuiltinHandlers();

    // 系统托盘
    auto& tray = easy::tray::TrayIcon::instance();
    tray.create(hwnd);
    tray.onOpenSettings([]() {
        LOG_INFO("打开设置窗口");
        showSettingsWindow();
    });
    tray.onPauseGesture([]() {
        auto& engine = easy::gesture::GestureEngine::instance();
        engine.setPaused(!engine.isPaused());
    });
    tray.onExit([hwnd]() {
        LOG_INFO("用户选择退出");
        DestroyWindow(hwnd);
    });
    tray.onScreenshot([]() {
        LOG_INFO("截图功能触发");
        easy::capture::CaptureOptions opts;
        opts.copyToClipboard = true;
        easy::capture::ScreenCapture::instance().startCapture(opts);
    });
    tray.onRecording([]() {
        auto& recorder = easy::capture::ScreenRecorder::instance();
        if (recorder.state() == easy::capture::RecordState::Idle) {
            LOG_INFO("开始录屏");
            easy::capture::RecordOptions opts;
            recorder.startRecording(opts);
        } else {
            LOG_INFO("停止录屏");
            auto path = recorder.stopRecording();
            LOG_INFO("录屏已保存: {}", path);
        }
    });

    // 注册全局快捷键
    auto& hotkeys = easy::core::HotkeyManager::instance();
    hotkeys.registerHotkey("截图", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'A'}, []() {
        LOG_INFO("截图快捷键触发");
        easy::capture::CaptureOptions opts;
        opts.copyToClipboard = true;
        easy::capture::ScreenCapture::instance().startCapture(opts);
    });
    hotkeys.registerHotkey("暂停手势", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'W'}, []() {
        auto& engine = easy::gesture::GestureEngine::instance();
        engine.setPaused(!engine.isPaused());
    });

    // 截图/录屏引擎
    easy::capture::ScreenCapture::instance().initialize(GetModuleHandleW(nullptr));
    easy::capture::ScreenRecorder::instance().initialize();

    // 手势引擎
    auto& gestureEngine = easy::gesture::GestureEngine::instance();
    gestureEngine.loadFromConfig();
    gestureEngine.start();

    LOG_INFO("所有子系统初始化完成");
}

// ─────────────────────────────────────────────────────────────────────────────
// 关闭子系统
// ─────────────────────────────────────────────────────────────────────────────
void shutdownSubsystems() {
    easy::capture::ScreenRecorder::instance().shutdown();
    easy::capture::ScreenCapture::instance().shutdown();
    easy::gesture::GestureEngine::instance().saveToConfig();
    easy::gesture::GestureEngine::instance().stop();
    easy::core::HotkeyManager::instance().shutdown();
    easy::tray::TrayIcon::instance().destroy();
    easy::core::ConfigManager::instance().shutdown();
    easy::core::CrashHandler::uninstall();

    LOG_INFO("所有子系统已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// WebView2 设置窗口（按需创建）
// ─────────────────────────────────────────────────────────────────────────────
void showSettingsWindow() {
    auto& settingsWnd = easy::ui::SettingsWindow::instance();

    // 开发模式: 连接 Vite dev server
#ifdef _DEBUG
    easy::ui::SettingsWindowConfig config;
    config.devServerUrl = "http://localhost:5173";
    config.devToolsEnabled = true;
    settingsWnd.setConfig(config);
#endif

    settingsWnd.show(GetModuleHandleW(nullptr));
}
