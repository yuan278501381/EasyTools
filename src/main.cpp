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
#include "tray/TrayIcon.h"
#include "ui/SettingsWindow.h"
#include "ui/KeycastOverlay.h"
#include "ocr/OcrEngine.h"
#include "gesture/GestureEngine.h"
#include "gesture/BuiltinCommands.h"
#include "capture/ScreenCapture.h"
#include "capture/ScreenRecorder.h"
#include "capture/RecordingIndicator.h"
#include "capture/PinWindow.h"
#include "capture/ScrollCapture.h"
#include "capture/CaptureOverlay.h"
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
static void triggerScreenshot();
static void toggleRecording();
static void triggerOcrCapture();

// ── 全局状态 ─────────────────────────────────────────────────────────────────
static HANDLE g_singleInstanceMutex = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// 截图 / 录屏 入口 (托盘、快捷键、手势内置命令共用，避免逻辑重复)
// ─────────────────────────────────────────────────────────────────────────────
static void triggerScreenshot() {
    LOG_INFO("截图触发");
    easy::capture::CaptureOptions opts;
    opts.copyToClipboard = true;
    easy::capture::ScreenCapture::instance().startCapture(opts);
}

// 标记下一次截图完成后需要进行 OCR (而非普通截图)。由持久回调消费 (一次性)。
static std::atomic<bool> g_ocrPending{false};

// 在截图完成回调中处理 OCR 结果。持久安装一次，靠 g_ocrPending 区分普通截图。
// OCR 推理放到后台线程, 避免阻塞主消息循环 (WinRT .get() 会同步等待)。
static void onCaptureCompletedForOcr(const easy::capture::CaptureResult& result) {
    if (!g_ocrPending.exchange(false)) return;  // 非 OCR 截图，忽略
    if (!result.success || result.filePath.empty()) return;

    std::string path = result.filePath;  // 拷贝, 供后台线程使用
    std::thread([path]() {
        // 兜底: 分离线程中未捕获的异常会 std::terminate 整个进程
        try {
            auto& tray = easy::tray::TrayIcon::instance();
            std::string text = easy::ocr::OcrEngine::instance().recognizeImageFile(path);
            std::error_code ec;
            std::filesystem::remove(path, ec);  // 清理临时文件

            if (text.empty()) {
                tray.showNotification(L"EasyTools OCR", L"未识别到文字。", NIIF_INFO);
                return;
            }
            easy::core::WinUtils::copyToClipboard(text);
            tray.showNotification(L"EasyTools OCR", L"文字已识别并复制到剪贴板。", NIIF_INFO);
        } catch (const std::exception& e) {
            LOG_ERROR("OCR 后台线程异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("OCR 后台线程未知异常");
        }
    }).detach();
}

// 截图区域 → OCR → 复制文本 → 托盘通知 (复用截图管线，模块间保持解耦)
static void triggerOcrCapture() {
    auto& ocr = easy::ocr::OcrEngine::instance();
    if (!ocr.isAvailable()) {
        easy::tray::TrayIcon::instance().showNotification(
            L"EasyTools OCR", L"未检测到 OCR 语言包，请在系统“设置 → 应用 → 可选功能”中添加。",
            NIIF_WARNING);
        return;
    }

    LOG_INFO("OCR 截图触发");
    auto tempPath = easy::core::WinUtils::getAppDataDirectory() / L"temp";
    std::filesystem::create_directories(tempPath);
    auto file = tempPath / (L"ocr_" + std::to_wstring(GetTickCount64()) + L".png");

    easy::capture::CaptureOptions opts;
    opts.copyToClipboard = false;
    opts.saveToFile      = true;
    opts.savePath        = easy::core::WinUtils::wstringToUtf8(file.wstring());

    g_ocrPending.store(true);
    easy::capture::ScreenCapture::instance().startCapture(opts);
}

static void toggleRecording() {
    auto& recorder  = easy::capture::ScreenRecorder::instance();
    auto& indicator = easy::capture::RecordingIndicator::instance();

    if (recorder.state() == easy::capture::RecordState::Idle) {
        LOG_INFO("开始选择录屏区域");
        easy::capture::CaptureOptions opts;
        auto& overlay = easy::capture::CaptureOverlay::instance();
        overlay.setRecordCallback([&recorder, &indicator](const easy::capture::CaptureRegion& region) {
            easy::capture::RecordOptions recOpts;
            recOpts.regionX    = region.x;
            recOpts.regionY    = region.y;
            recOpts.width      = region.width;
            recOpts.height     = region.height;
            recOpts.fullScreen = false;
            recorder.setStateCallback([&indicator](easy::capture::RecordState state, const easy::capture::RecordStats& stats) {
                indicator.update(stats.durationSec, stats.frameCount);
                indicator.setPaused(state == easy::capture::RecordState::Paused);
            });
            recorder.startRecording(recOpts);
            indicator.show();
        });
        overlay.startSelection(opts, easy::capture::OverlayMode::RecordRegion);
    } else {
        LOG_INFO("停止录屏");
        indicator.hide();
        auto path = recorder.stopRecording();
        LOG_INFO("录屏已保存: {}", path);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WinMain — 程序入口
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // ── 0. 高分屏 (DPI) 感知 ─────────────────────────────────────────────
    // 启用 Per-Monitor V2 DPI 感知，确保在高分屏下物理像素和逻辑像素一比一映射
    // 避免鼠标钩子得到的物理坐标与系统自动缩放的窗口逻辑坐标产生分离和偏移
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

    auto& gestureEngine = easy::gesture::GestureEngine::instance();
    gestureEngine.setPauseChangedCallback([](bool paused) {
        auto& config = easy::core::ConfigManager::instance();
        config.set("/gesture/paused", paused);
        config.set("/gesture/enabled", !paused);

        easy::tray::TrayIcon::instance().setGesturePaused(paused);
        easy::core::MessageBridge::instance().pushEvent("gesture.stateChanged", {
            {"paused", paused},
            {"enabled", !paused}
        });
    });

    // 4. 统计系统初始化
    easy::core::StatsManager::instance().initialize();
    
    // 5. 初始化手势引擎与按键拦截
    easy::gesture::MouseHook::instance().install();
    easy::core::KeyboardHook::instance().install();
    easy::gesture::GestureEngine::instance().start();
    
    // 6. 初始化 OCR
    easy::ocr::OcrEngine::instance().initialize();

    // Lua 引擎
    easy::core::LuaEngine::instance().initialize();

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
    tray.onScreenshot([]() { triggerScreenshot(); });
    tray.onRecording([]() { toggleRecording(); });

    // 注册全局快捷键
    auto& hotkeys = easy::core::HotkeyManager::instance();
    hotkeys.registerHotkey("截图", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'A'}, []() {
        triggerScreenshot();
    });
    hotkeys.registerHotkey("录屏", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'R'}, []() {
        toggleRecording();
    });
    hotkeys.registerHotkey("OCR文字识别", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'O'}, []() {
        triggerOcrCapture();
    });
    hotkeys.registerHotkey("暂停手势", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'W'}, []() {
        auto& engine = easy::gesture::GestureEngine::instance();
        engine.setPaused(!engine.isPaused());
    });

    // 注册手势内置命令的应用级回调 (保持 gesture → capture/tray 单向依赖)
    {
        using easy::gesture::BuiltinCommand;
        auto& dispatcher = easy::gesture::BuiltinCommandDispatcher::instance();
        dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, []() { triggerScreenshot(); });
        dispatcher.registerHandler(BuiltinCommand::StartRecording, []() { toggleRecording(); });
        dispatcher.registerHandler(BuiltinCommand::PauseGestures, []() {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });
    }

    // 截图/录屏引擎
    easy::capture::ScreenCapture::instance().initialize(GetModuleHandleW(nullptr));
    easy::capture::ScreenCapture::instance().setCallback(onCaptureCompletedForOcr);
    easy::capture::ScreenRecorder::instance().initialize();

    // 录制指示器
    auto& indicator = easy::capture::RecordingIndicator::instance();
    indicator.initialize(GetModuleHandleW(nullptr));
    indicator.onPause([]() {
        auto& recorder = easy::capture::ScreenRecorder::instance();
        if (recorder.state() == easy::capture::RecordState::Recording) {
            recorder.pauseRecording();
        } else if (recorder.state() == easy::capture::RecordState::Paused) {
            recorder.resumeRecording();
        }
    });
    indicator.onStop([]() {
        easy::capture::RecordingIndicator::instance().hide();
        auto path = easy::capture::ScreenRecorder::instance().stopRecording();
        LOG_INFO("录屏已保存: {}", path);
    });

    // 手势引擎
    gestureEngine.loadFromConfig();
    // Keycast
    easy::ui::KeycastOverlay::instance().initialize(GetModuleHandleW(nullptr));
    bool keycastEnabled = easy::core::ConfigManager::instance().get<bool>("/general/keycastEnabled", false);
    easy::ui::KeycastOverlay::instance().setEnabled(keycastEnabled);

    LOG_INFO("所有子系统初始化完成");
}

// ─────────────────────────────────────────────────────────────────────────────
// 关闭子系统
// ─────────────────────────────────────────────────────────────────────────────
void shutdownSubsystems() {
    easy::capture::PinWindow::closeAll();
    easy::capture::RecordingIndicator::instance().shutdown();
    easy::capture::ScreenRecorder::instance().shutdown();
    easy::capture::ScreenCapture::instance().shutdown();
    easy::gesture::GestureEngine::instance().saveToConfig();
    easy::gesture::GestureEngine::instance().stop();
    easy::gesture::MouseHook::instance().uninstall();
    easy::core::KeyboardHook::instance().uninstall();
    easy::core::StatsManager::instance().shutdown();
    easy::core::HotkeyManager::instance().shutdown();
    easy::tray::TrayIcon::instance().destroy();
    easy::ui::KeycastOverlay::instance().shutdown();
    easy::ocr::OcrEngine::instance().shutdown();
    easy::core::LuaEngine::instance().shutdown();
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
