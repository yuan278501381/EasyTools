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
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ElevationPolicy.h"
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
#include "core/utils/ShellContextMenuService.h"
#include "EasyToolsVersion.h"
#include "tray/TrayIcon.h"
#include "ui/SettingsWindow.h"
#include "ui/SearchWindow.h"
#include "ui/QuickLookWindow.h"
#include "ui/TrayWindow.h"
#include "ui/ToastOverlay.h"
#include "ui/SpotlightOverlay.h"
#include "ui/WebViewEnvironmentManager.h"
#include "gesture/GestureInputPolicy.h"

// ── 常量 ─────────────────────────────────────────────────────────────────────
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"EasyTools_MessageWindow";
static constexpr const wchar_t* WINDOW_TITLE      = L"EasyToolsMessageWindow";
static constexpr const wchar_t* MUTEX_NAME        = L"Global\\EasyTools_SingleInstance_Mutex";
static constexpr UINT WM_EASYTOOLS_SHOW_SETTINGS  = WM_APP + 101;
static constexpr UINT_PTR TIMER_ID_QUICKLOOK_REFRESH = 0x4551;
static constexpr UINT_PTR TIMER_ID_PERFORMANCE_BASELINE = 0x4552;

// ── 前向声明 ─────────────────────────────────────────────────────────────────
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool checkSingleInstance();
bool hasCommandLineFlag(std::wstring_view flag);
std::optional<std::filesystem::path> commandLinePathValue(std::wstring_view prefix);
std::optional<std::wstring> commandLineStringValue(std::wstring_view prefix);
HWND createMessageWindow(HINSTANCE hInstance);
void initializeSubsystems(HWND hwnd, bool preloadSettings);
void shutdownSubsystems();
void showSettingsWindow();
void preloadSettingsWindow(HINSTANCE hInstance);

// ── 全局状态 ─────────────────────────────────────────────────────────────────
static HANDLE g_singleInstanceMutex = nullptr;
static UINT g_wmTaskbarCreated = 0;
static std::optional<std::filesystem::path> g_performanceScenarioOutput;

namespace {

bool writePerformanceBaselineSnapshot(const std::filesystem::path& outputPath) {
    std::error_code error;
    const auto parent = outputPath.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, error) || error) {
        LOG_ERROR("Performance baseline output directory is unavailable: {}", outputPath.string());
        return false;
    }
    nlohmann::json snapshot = {
        {"schemaVersion", 1},
        {"processId", GetCurrentProcessId()},
        {"metrics", easy::core::PerformanceMonitor::instance().getMetricsJson()}
    };
    const auto temporary = outputPath.wstring() + L".partial";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            LOG_ERROR("Cannot create performance baseline snapshot: {}", outputPath.string());
            return false;
        }
        stream << snapshot.dump(2) << '\n';
        stream.flush();
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, error);
            LOG_ERROR("Cannot write performance baseline snapshot: {}", outputPath.string());
            return false;
        }
    }
    std::filesystem::rename(temporary, outputPath, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        LOG_ERROR("Cannot finalize performance baseline snapshot: {}", outputPath.string());
        return false;
    }
    return true;
}

void releaseSingleInstanceMutex() {
    if (!g_singleInstanceMutex) return;
    CloseHandle(g_singleInstanceMutex);
    g_singleInstanceMutex = nullptr;
}

std::wstring buildRestartParameters(bool includeWindowPos) {
    std::wstring params = L"--restart-pid=" + std::to_wstring(GetCurrentProcessId());
    if (hasCommandLineFlag(L"--silent")) {
        params += L" --silent";
    }
    if (!includeWindowPos) return params;

    auto& settingsWnd = easy::ui::SettingsWindow::instance();
    if (settingsWnd.isVisible() && settingsWnd.hwnd() && IsWindow(settingsWnd.hwnd())) {
        RECT rc{};
        if (GetWindowRect(settingsWnd.hwnd(), &rc)) {
            params += L" --window-pos=" + std::to_wstring(rc.left) + L"," +
                      std::to_wstring(rc.top) + L"," +
                      std::to_wstring(rc.right - rc.left) + L"," +
                      std::to_wstring(rc.bottom - rc.top);
        }
    }
    return params;
}

bool launchElevatedSuccessor(bool includeWindowPos) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    const std::wstring params = buildRestartParameters(includeWindowPos);
    releaseSingleInstanceMutex(); // 先释放单例互斥锁，确保子进程能平滑获取单例锁

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        // 用户在 UAC 弹窗点击了“否”或提权失败，重新占有单例锁以普通权限继续运行
        g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
        return false;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

}  // namespace

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

    const bool elevateSuppressed = hasCommandLineFlag(L"--no-elevate") ||
        commandLinePathValue(L"--performance-baseline-output=").has_value();
    if (easy::core::shouldAutoElevateOnStartup(
            easy::core::ConfigManager::instance().get<bool>("/general/runAsAdmin", true),
            easy::core::WinUtils::isCurrentProcessElevated(),
            elevateSuppressed)) {
        if (launchElevatedSuccessor(false)) {
            LOG_INFO("已按设置拉起管理员实例，当前进程退出");
            easy::core::ConfigManager::instance().shutdown();
            easy::core::PerformanceMonitor::instance().stop();
            easy::core::Logger::shutdown();
            CoUninitialize();
            return 0;
        }
        LOG_WARN("自动提权被取消或失败，以普通权限继续运行");
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
    initializeSubsystems(hwndMessage, false);
    easy::core::PerformanceMonitor::instance().recordLatency(
        "startup.core",
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startupBeganAt).count());

    // Dedicated benchmark commands never synthesize input into an existing
    // user session. Cold-start exits immediately after normal initialization;
    // search-first-open owns only this freshly launched process and lets the
    // actual SearchWindow show path publish its own hostShow metric first.
    if (const auto output = commandLinePathValue(L"--performance-baseline-output=")) {
        const auto scenario = commandLineStringValue(L"--performance-baseline-scenario=");
        if (!scenario) {
            writePerformanceBaselineSnapshot(*output);
            PostQuitMessage(0);
        } else if (_wcsicmp(scenario->c_str(), L"search-first-open") == 0) {
            g_performanceScenarioOutput = *output;
            easy::ui::SearchWindow::instance().show(hInstance);
            // SearchWindow::show records search.hostShow synchronously. Keep
            // the surface visible for one short message-pump turn so its real
            // HWND/WebView creation work is not skipped, then cleanly hide and
            // exit the benchmark-owned host.
            SetTimer(hwndMessage, TIMER_ID_PERFORMANCE_BASELINE, 250, nullptr);
        } else {
            LOG_ERROR("Unknown performance baseline scenario: {}",
                      easy::core::WinUtils::wstringToUtf8(*scenario));
            PostQuitMessage(1);
        }
    }

    // 用户主动启动时直接呈现设置；开机自启动使用 --silent 静默驻留托盘。
    if (!silentStart) {
        showSettingsWindow();
        // 仅用户主动启动时反馈成功；登录自启动的 --silent 必须真正安静。
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"EasyTools 已启动"});
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

bool parseWindowPos(int& x, int& y, int& w, int& h) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (wcsncmp(argv[i], L"--window-pos=", 13) == 0) {
            std::wstring val = argv[i] + 13;
            size_t p1 = val.find(L',');
            size_t p2 = (p1 != std::wstring::npos) ? val.find(L',', p1 + 1) : std::wstring::npos;
            size_t p3 = (p2 != std::wstring::npos) ? val.find(L',', p2 + 1) : std::wstring::npos;
            if (p1 != std::wstring::npos && p2 != std::wstring::npos && p3 != std::wstring::npos) {
                try {
                    x = std::stoi(val.substr(0, p1));
                    y = std::stoi(val.substr(p1 + 1, p2 - p1 - 1));
                    w = std::stoi(val.substr(p2 + 1, p3 - p2 - 1));
                    h = std::stoi(val.substr(p3 + 1));
                    found = true;
                } catch (...) {}
            }
            break;
        }
    }
    LocalFree(argv);
    return found;
}

bool checkSingleInstance() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD restartPid = 0;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcsncmp(argv[i], L"--restart-pid=", 14) == 0) {
                restartPid = static_cast<DWORD>(_wtoi(argv[i] + 14));
            }
        }
        LocalFree(argv);
    }

    // 若是热重启拉起的新进程，等待旧进程完全退出并释放所有系统资源
    if (restartPid > 0) {
        HANDLE hOldProc = OpenProcess(SYNCHRONIZE, FALSE, restartPid);
        if (hOldProc) {
            WaitForSingleObject(hOldProc, 3000);
            CloseHandle(hOldProc);
        }
    }

    // 重试获取互斥锁（热重启重试 30 次，正常二次启动仅重试 1 次以实现零延迟秒开）
    const int maxRetries = (restartPid > 0) ? 30 : 1;
    for (int retry = 0; retry < maxRetries; ++retry) {
        g_singleInstanceMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
        if (g_singleInstanceMutex && GetLastError() != ERROR_ALREADY_EXISTS) {
            return true;
        }
        if (g_singleInstanceMutex) {
            CloseHandle(g_singleInstanceMutex);
            g_singleInstanceMutex = nullptr;
        }
        if (retry < maxRetries - 1) {
            Sleep(50);
        }
    }

    // 允许现有后台实例将窗口置顶于前台
    AllowSetForegroundWindow(ASFW_ANY);

    // 依然被占用时，通知已有实例唤醒并弹出设置窗口
    HWND existing = FindWindowW(WINDOW_CLASS_NAME, nullptr);
    if (existing) {
        DWORD targetPid = 0;
        GetWindowThreadProcessId(existing, &targetPid);
        if (targetPid > 0) {
            AllowSetForegroundWindow(targetPid);
        }
        PostMessageW(existing, WM_EASYTOOLS_SHOW_SETTINGS, 0, 0);
    } else {
        HWND settingsWnd = FindWindowW(L"EasyTools_SettingsWindow", nullptr);
        if (settingsWnd) {
            DWORD targetPid = 0;
            GetWindowThreadProcessId(settingsWnd, &targetPid);
            if (targetPid > 0) {
                AllowSetForegroundWindow(targetPid);
            }
            ShowWindow(settingsWnd, SW_SHOW);
            SetForegroundWindow(settingsWnd);
        } else {
            UINT msgShow = RegisterWindowMessageW(L"EasyTools_ShowSettings_Broadcast");
            PostMessageW(HWND_BROADCAST, msgShow, 0, 0);
        }
    }
    return false;
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

std::optional<std::filesystem::path> commandLinePathValue(std::wstring_view prefix) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return std::nullopt;
    std::optional<std::filesystem::path> value;
    for (int index = 1; index < argc; ++index) {
        std::wstring_view argument(argv[index]);
        if (argument.size() >= prefix.size() &&
            _wcsnicmp(argument.data(), prefix.data(), static_cast<int>(prefix.size())) == 0) {
            const auto rawValue = argument.substr(prefix.size());
            if (!rawValue.empty()) value = std::filesystem::path(rawValue);
            break;
        }
    }
    LocalFree(argv);
    return value;
}

std::optional<std::wstring> commandLineStringValue(std::wstring_view prefix) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return std::nullopt;
    std::optional<std::wstring> value;
    for (int index = 1; index < argc; ++index) {
        std::wstring_view argument(argv[index]);
        if (argument.size() >= prefix.size() &&
            _wcsnicmp(argument.data(), prefix.data(), static_cast<int>(prefix.size())) == 0) {
            const auto rawValue = argument.substr(prefix.size());
            if (!rawValue.empty()) value = std::wstring(rawValue);
            break;
        }
    }
    LocalFree(argv);
    return value;
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
    tray.onSearch([]() {
        auto& searchWnd = easy::ui::SearchWindow::instance();
        if (searchWnd.isVisible()) searchWnd.hide();
        else searchWnd.show(GetModuleHandleW(nullptr));
    });
    tray.onPauseGesture([]() { easy::core::EventBus::instance().publish(easy::core::ActionToggleGesturePauseEvent{}); });
    tray.onRestartElevated([hwnd]() {
        easy::core::ConfigManager::instance().set<bool>("/general/runAsAdmin", true);
        if (launchElevatedSuccessor(true)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    });

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
        auto toggleSearchSafe = []() {
            auto& searchWnd = easy::ui::SearchWindow::instance();
            if (searchWnd.isVisible()) {
                searchWnd.hide();
                return;
            }
            if (easy::core::ConfigManager::instance().get<bool>("/search/autoBypassFullscreen", true)) {
                HWND fg = GetForegroundWindow();
                if (fg && easy::core::WinUtils::isWindowFullscreen(fg)) {
                    const std::wstring classWide = easy::core::WinUtils::getWindowClassName(fg);
                    if (easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide))) {
                        LOG_INFO("前台处于全屏独占应用，自动免打扰跳过搜索窗口呼出: hwnd=0x{:X}", reinterpret_cast<uintptr_t>(fg));
                        return;
                    }
                }
            }
            searchWnd.show(GetModuleHandleW(nullptr));
        };

        easy::core::HotkeyManager::instance().registerHotkey("Toggle Search", searchHotkey, [toggleSearchSafe]() {
            toggleSearchSafe();
        });

        easy::core::MessageBridge::instance().registerHandler(
            "search.toggle", [toggleSearchSafe](const nlohmann::json&) -> nlohmann::json {
                toggleSearchSafe();
                return {{"success", true}};
            });
        easy::core::MessageBridge::instance().registerHandler(
            "search.hide", [](const nlohmann::json&) -> nlohmann::json {
                easy::ui::SearchWindow::instance().hide();
                return {{"success", true}};
            });
        easy::core::MessageBridge::instance().registerHandler(
            "search.getWindowSize", [](const nlohmann::json&) -> nlohmann::json {
                auto [w, h] = easy::ui::SearchWindow::instance().getWindowSize();
                return {{"width", w}, {"height", h}};
            });
        easy::core::MessageBridge::instance().registerHandler(
            "search.setWindowSize", [](const nlohmann::json& params) -> nlohmann::json {
                if (params.contains("width") && params.contains("height")) {
                    int w = params["width"].get<int>();
                    int h = params["height"].get<int>();
                    bool center = params.value("center", false);
                    easy::ui::SearchWindow::instance().setWindowSize(w, h, center);
                    return {{"success", true}, {"width", w}, {"height", h}};
                }
                return {{"success", false}, {"error", "missing width or height"}};
            });
    }

    // 注册 Alt+X 划词极速翻译快捷键
    const easy::core::HotkeyDef translateFallback{easy::core::ModKey::Alt, 'X'};
    const auto translateText = easy::core::ConfigManager::instance().get<std::string>(
        "/hotkeys/Translate Selection", translateFallback.toString());
    const auto translateHotkey = translateText.empty() ? easy::core::HotkeyDef{}
        : easy::core::HotkeyDef::fromString(translateText).value_or(translateFallback);
    easy::core::HotkeyManager::instance().registerHotkey("Translate Selection", translateHotkey, []() {
        std::string selected = easy::core::WinUtils::captureSelectedText();
        // 移除首尾空白
        selected.erase(0, selected.find_first_not_of(" \t\n\r"));
        selected.erase(selected.find_last_not_of(" \t\n\r") + 1);

        if (selected.empty()) {
            easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"请先划选文字或复制文本后再按 Alt+X"});
            return;
        }

        std::string encoded;
        for (unsigned char c : selected) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", c);
                encoded += buf;
            }
        }
        std::string url = "https://translate.google.com/?sl=auto&tl=zh-CN&op=translate&text=" + encoded;
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"已触发划词翻译并打开翻译页面"});
    });

    // 注册核心基础 IPC 处理器
    easy::core::MessageBridge::instance().registerBuiltinHandlers();

    // 注册 QuickLook 快速预览 IPC 处理器
    easy::core::MessageBridge::instance().registerHandler("quicklook.open", [](const nlohmann::json& params) -> nlohmann::json {
        std::string pathUtf8 = params.value("path", "");
        if (pathUtf8.empty()) pathUtf8 = easy::core::WinUtils::wstringToUtf8(easy::ui::QuickLookWindow::instance().currentFilePath());
        if (!pathUtf8.empty()) {
            ShellExecuteW(nullptr, L"open", easy::core::WinUtils::utf8ToWstring(pathUtf8).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            easy::ui::QuickLookWindow::instance().hide();
        }
        return {{"success", true}};
    });
    easy::core::MessageBridge::instance().registerHandler("quicklook.showInFolder", [](const nlohmann::json& params) -> nlohmann::json {
        std::string pathUtf8 = params.value("path", "");
        if (pathUtf8.empty()) pathUtf8 = easy::core::WinUtils::wstringToUtf8(easy::ui::QuickLookWindow::instance().currentFilePath());
        if (!pathUtf8.empty()) {
            std::wstring cmd = L"/select,\"" + easy::core::WinUtils::utf8ToWstring(pathUtf8) + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOWNORMAL);
        }
        return {{"success", true}};
    });
    easy::core::MessageBridge::instance().registerHandler("quicklook.copyPath", [](const nlohmann::json& params) -> nlohmann::json {
        std::string pathUtf8 = params.value("path", "");
        if (pathUtf8.empty()) pathUtf8 = easy::core::WinUtils::wstringToUtf8(easy::ui::QuickLookWindow::instance().currentFilePath());
        easy::core::WinUtils::copyToClipboard(pathUtf8);
        return {{"success", true}};
    });
    easy::core::MessageBridge::instance().registerHandler("quicklook.hide", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::QuickLookWindow::instance().hide();
        return {{"success", true}};
    });

    // 注册打开诊断日志目录 IPC 处理器
    easy::core::MessageBridge::instance().registerHandler("app.openLogDir", [](const nlohmann::json&) -> nlohmann::json {
        std::filesystem::path logDir = easy::core::WinUtils::getLogDirectory();
        std::error_code ec;
        if (!std::filesystem::exists(logDir, ec)) {
            std::filesystem::create_directories(logDir, ec);
        }
        ShellExecuteW(nullptr, L"open", logDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return {{"success", true}, {"path", easy::core::WinUtils::wstringToUtf8(logDir.wstring())}};
    });

    // 注册导出诊断日志 IPC 处理器
    easy::core::MessageBridge::instance().registerHandler("app.exportLogs", [](const nlohmann::json& params) -> nlohmann::json {
        std::optional<std::filesystem::path> savePath;
        const auto supplied = params.value("path", "");
        if (!supplied.empty()) {
            savePath = easy::core::WinUtils::utf8ToWstring(supplied);
        } else {
            IFileDialog* dialog = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
            if (SUCCEEDED(hr) && dialog) {
                static const COMDLG_FILTERSPEC filters[] = {
                    {L"日志文件 (*.log;*.txt)", L"*.log;*.txt"},
                    {L"所有文件 (*.*)", L"*.*"},
                };
                dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
                dialog->SetDefaultExtension(L"log");
                
                auto now = std::chrono::system_clock::now();
                auto in_time_t = std::chrono::system_clock::to_time_t(now);
                std::tm tmBuffer{};
                localtime_s(&tmBuffer, &in_time_t);
                wchar_t defaultName[64];
                swprintf_s(defaultName, L"EasyTools_Logs_%04d%02d%02d_%02d%02d%02d.log",
                           tmBuffer.tm_year + 1900, tmBuffer.tm_mon + 1, tmBuffer.tm_mday,
                           tmBuffer.tm_hour, tmBuffer.tm_min, tmBuffer.tm_sec);

                dialog->SetFileName(defaultName);
                dialog->SetTitle(L"导出 EasyTools 诊断日志");
                
                if (SUCCEEDED(dialog->Show(nullptr))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                        PWSTR rawPath = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) && rawPath) {
                            savePath = std::filesystem::path(rawPath);
                            CoTaskMemFree(rawPath);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }
        }

        if (!savePath) {
            return {{"success", false}, {"cancelled", true}};
        }

        // 刷写日志缓存
        if (easy::core::Logger::instance()) {
            easy::core::Logger::instance()->flush();
        }

        std::filesystem::path target = *savePath;
        std::ofstream out(target, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return {{"success", false}, {"error", "无法创建目标导出文件"}};
        }

        // 写入环境诊断头
        out << "=======================================================\n";
        out << " EasyTools 诊断日志导出报告\n";
        out << " 软件版本: " << easy::version::String << "\n";
        out << " 进程 PID: " << GetCurrentProcessId() << "\n";
        out << "=======================================================\n\n";

        // 读取所有当前存在的日志文件并合并
        std::filesystem::path logDir = easy::core::WinUtils::getLogDirectory();
        std::error_code ec;
        if (std::filesystem::exists(logDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(logDir, ec)) {
                if (entry.is_regular_file(ec) && entry.path().extension() == ".log") {
                    out << ">>> File: " << entry.path().filename().string() << " <<<\n";
                    std::ifstream in(entry.path(), std::ios::in | std::ios::binary);
                    if (in.is_open()) {
                        out << in.rdbuf() << "\n\n";
                    }
                }
            }
        }
        out.close();

        // 自动在资源管理器中高亮选中导出的日志
        std::wstring targetStr = target.wstring();
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + targetStr + L"\"").c_str(), nullptr, SW_SHOWNORMAL);

        return {{"success", true}, {"path", easy::core::WinUtils::wstringToUtf8(targetStr)}};
    });

    // 注册应用一键热重启 IPC 处理器
    easy::core::MessageBridge::instance().registerHandler("app.restart", [hwnd](const nlohmann::json&) -> nlohmann::json {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
            DWORD currentPid = GetCurrentProcessId();
            std::wstring cmdLine = L"\"";
            cmdLine += exePath;
            cmdLine += L"\" --restart-pid=" + std::to_wstring(currentPid);

            auto& settingsWnd = easy::ui::SettingsWindow::instance();
            if (settingsWnd.isVisible() && settingsWnd.hwnd() && IsWindow(settingsWnd.hwnd())) {
                RECT rc{};
                if (GetWindowRect(settingsWnd.hwnd(), &rc)) {
                    cmdLine += L" --window-pos=" + std::to_wstring(rc.left) + L"," +
                               std::to_wstring(rc.top) + L"," +
                               std::to_wstring(rc.right - rc.left) + L"," +
                               std::to_wstring(rc.bottom - rc.top);
                }
            }

            // 预先释放互斥锁句柄，为新实例让路
            releaseSingleInstanceMutex();

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(exePath, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return {{"success", true}};
    });

    easy::core::MessageBridge::instance().registerHandler("app.restartElevated", [hwnd](const nlohmann::json&) -> nlohmann::json {
        if (easy::core::WinUtils::isCurrentProcessElevated()) {
            return {{"success", true}, {"alreadyElevated", true}};
        }
        if (!launchElevatedSuccessor(true)) {
            const DWORD err = GetLastError();
            return {
                {"success", false},
                {"cancelled", err == ERROR_CANCELLED},
                {"error", err == ERROR_CANCELLED ? "uac cancelled" : "failed to start elevated process"}
            };
        }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return {{"success", true}};
    });

    // 注册托盘菜单 IPC 处理函数
    easy::core::MessageBridge::instance().registerHandler("tray.action", [hwnd](const nlohmann::json& params) -> nlohmann::json {
        std::string action = params.value("action", "");
        
        // 执行前先隐藏托盘菜单
        easy::ui::TrayWindow::instance().hide();

        if (action == "openSettings") {
            showSettingsWindow();
        } else if (action == "search") {
            auto& searchWnd = easy::ui::SearchWindow::instance();
            if (searchWnd.isVisible()) searchWnd.hide();
            else searchWnd.show(GetModuleHandleW(nullptr));
        } else if (action == "screenshot") {
            easy::core::EventBus::instance().publish(easy::core::ActionTriggerScreenshotEvent{});
        } else if (action == "recording") {
            easy::core::EventBus::instance().publish(easy::core::ActionToggleRecordingEvent{});
        } else if (action == "pauseGesture") {
            easy::core::EventBus::instance().publish(easy::core::ActionToggleGesturePauseEvent{});
        } else if (action == "restartElevated") {
            easy::core::ConfigManager::instance().set<bool>("/general/runAsAdmin", true);
            if (launchElevatedSuccessor(true)) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
        } else if (action == "exit") {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            return {{"success", false}, {"error", "unknown tray action"}};
        }
        return {{"success", true}};
    });

    easy::core::MessageBridge::instance().registerHandler("tray.resize", [](const nlohmann::json& params) -> nlohmann::json {
        int width = params.value("width", 0);
        int height = params.value("height", 0);
        if (width > 0 && height > 0) {
            easy::ui::TrayWindow::instance().setContentSize(width, height);
            return {{"success", true}};
        }
        return {{"success", false}};
    });

    easy::core::MessageBridge::instance().registerHandler("tray.hide", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::TrayWindow::instance().hide();
        return {{"success", true}};
    });

    // 5. 鼠标聚光灯 Overlay 初始化与 IPC 注册
    easy::ui::SpotlightOverlay::instance().initialize(GetModuleHandleW(nullptr));
    easy::core::KeyboardHook::instance().setKeyboardActivityCallback([](DWORD vk, WPARAM wp) {
        easy::ui::SpotlightOverlay::instance().onKeyboardEvent(vk, wp);
    });

    easy::core::MessageBridge::instance().registerHandler("spotlight.getSettings", [](const nlohmann::json&) -> nlohmann::json {
        auto s = easy::ui::SpotlightOverlay::instance().getSettings();
        return {
            {"enabled", s.enabled},
            {"triggerDoubleCtrl", s.triggerDoubleCtrl},
            {"triggerShakeMouse", s.triggerShakeMouse},
            {"autoBypassFullscreen", s.autoBypassFullscreen},
            {"spotlightColor", s.spotlightColor},
            {"spotlightSize", s.spotlightSize},
            {"animationDurationMs", s.animationDurationMs},
            {"holdDurationMs", s.holdDurationMs},
            {"shakeThreshold", s.shakeThreshold},
            {"clickRippleEnabled", s.clickRippleEnabled},
            {"mouseTrailEnabled", s.mouseTrailEnabled},
            {"leftClickColor", s.leftClickColor},
            {"rightClickColor", s.rightClickColor},
            {"middleClickColor", s.middleClickColor}
        };
    });

    easy::core::MessageBridge::instance().registerHandler("spotlight.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
        auto s = easy::ui::SpotlightOverlay::instance().getSettings();
        if (params.contains("enabled") && params["enabled"].is_boolean()) s.enabled = params["enabled"].get<bool>();
        if (params.contains("triggerDoubleCtrl") && params["triggerDoubleCtrl"].is_boolean()) s.triggerDoubleCtrl = params["triggerDoubleCtrl"].get<bool>();
        if (params.contains("triggerShakeMouse") && params["triggerShakeMouse"].is_boolean()) s.triggerShakeMouse = params["triggerShakeMouse"].get<bool>();
        if (params.contains("autoBypassFullscreen") && params["autoBypassFullscreen"].is_boolean()) s.autoBypassFullscreen = params["autoBypassFullscreen"].get<bool>();
        if (params.contains("spotlightColor") && params["spotlightColor"].is_string()) s.spotlightColor = params["spotlightColor"].get<std::string>();
        if (params.contains("spotlightSize") && params["spotlightSize"].is_number()) s.spotlightSize = params["spotlightSize"].get<int>();
        if (params.contains("animationDurationMs") && params["animationDurationMs"].is_number()) s.animationDurationMs = params["animationDurationMs"].get<int>();
        if (params.contains("holdDurationMs") && params["holdDurationMs"].is_number()) s.holdDurationMs = params["holdDurationMs"].get<int>();
        if (params.contains("shakeThreshold") && params["shakeThreshold"].is_number()) s.shakeThreshold = params["shakeThreshold"].get<int>();

        if (params.contains("clickRippleEnabled") && params["clickRippleEnabled"].is_boolean()) s.clickRippleEnabled = params["clickRippleEnabled"].get<bool>();
        if (params.contains("mouseTrailEnabled") && params["mouseTrailEnabled"].is_boolean()) s.mouseTrailEnabled = params["mouseTrailEnabled"].get<bool>();
        if (params.contains("leftClickColor") && params["leftClickColor"].is_string()) s.leftClickColor = params["leftClickColor"].get<std::string>();
        if (params.contains("rightClickColor") && params["rightClickColor"].is_string()) s.rightClickColor = params["rightClickColor"].get<std::string>();
        if (params.contains("middleClickColor") && params["middleClickColor"].is_string()) s.middleClickColor = params["middleClickColor"].get<std::string>();

        easy::ui::SpotlightOverlay::instance().updateSettings(s);
        return {{"success", true}};
    });

    easy::core::MessageBridge::instance().registerHandler("spotlight.trigger", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::SpotlightOverlay::instance().trigger();
        return {{"success", true}};
    });

    easy::core::MessageBridge::instance().registerHandler("spotlight.dismiss", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::SpotlightOverlay::instance().dismiss();
        return {{"success", true}};
    });

    easy::core::MessageBridge::instance().registerHandler("spotlight.resetDefaults", [](const nlohmann::json&) -> nlohmann::json {
        easy::ui::SpotlightOverlay::instance().resetDefaults();
        return {{"success", true}};
    });

    // 6. 键盘钩子（用于按键统计、按键显示与空格键 QuickLook 预览拦截）
    easy::core::KeyboardHook::instance().install();
    easy::core::KeyboardHook::instance().setKeyInterceptor([hwnd](DWORD vkCode, WPARAM wParam) -> bool {
        if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) return false;

        // 1. 空格键 (Space): 在资源管理器或桌面触发 QuickLook 文件预览
        if (vkCode == VK_SPACE) {
            bool hasMods = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                           (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            if (!hasMods) {
                if (easy::ui::QuickLookWindow::instance().isVisible()) {
                    easy::core::MainThreadDispatcher::instance().post([]() {
                        easy::ui::QuickLookWindow::instance().hide();
                    });
                    return true;
                } else {
                    auto sel = easy::core::WinUtils::getSelectedExplorerFile();
                    if (sel.has_value()) {
                        std::wstring path = *sel;
                        easy::core::MainThreadDispatcher::instance().post([path]() {
                            easy::ui::QuickLookWindow::instance().show(path);
                        });
                        return true;
                    }
                }
            }
        }

        // 2. ESC：先收 QuickLook；再通知手势/轮盘取消追踪，但不吞掉按键
        if (vkCode == VK_ESCAPE) {
            if (easy::ui::QuickLookWindow::instance().isVisible()) {
                easy::core::MainThreadDispatcher::instance().post([]() {
                    easy::ui::QuickLookWindow::instance().hide();
                });
                return true;
            }
            easy::core::EventBus::instance().publish(easy::core::CancelTransientUiEvent{});
        }

        // 3. 方向键联动：在资源管理器中切换选中项时，QuickLook 自动刷新预览内容
        if (vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_LEFT || vkCode == VK_RIGHT) {
            if (easy::ui::QuickLookWindow::instance().isVisible()) {
                // 同一 HWND/ID 的 SetTimer 会重置截止时间，形成无阻塞防抖。
                SetTimer(hwnd, TIMER_ID_QUICKLOOK_REFRESH, 35, nullptr);
            }
        }

        return false;
    });

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

    // 8. 托盘菜单是常驻入口：启动后立即在隐藏窗口中完成 WebView2、前端
    // chunk 与状态数据预热。这样第一次左/右键打开时直接显示完整菜单，
    // 不把 React Suspense 的“正在加载”暴露给用户。
    easy::ui::TrayWindow::instance().preload(GetModuleHandleW(nullptr));

    // 用户主动启动时预热设置页；开机静默驻留不额外预热完整设置页。
    if (preloadSettings) preloadSettingsWindow(GetModuleHandleW(nullptr));

    // 全局搜索窗口默认按需创建以避免常驻 Chromium 进程；需要极致首开速度的用户
    // 可显式开启预热。隐藏后 SearchWindow 会请求 WebView2 挂起，释放渲染资源。
    if (easy::core::ConfigManager::instance().get<bool>("/search/preloadWindow", false)) {
        easy::ui::SearchWindow::instance().preload(GetModuleHandleW(nullptr));
    }

    // 9. 更新检查严格在后台执行，并由内部频率限制保护启动性能。
    easy::core::UpdateChecker::instance().checkAsync(false);
}

void shutdownSubsystems() {
    easy::core::UpdateChecker::instance().shutdown();
    // 先关闭所有 WebView 入口，阻止新的 IPC 请求进入插件，再等待并卸载插件。
    easy::ui::SettingsWindow::instance().destroy();
    easy::ui::SearchWindow::instance().destroy();
    easy::ui::QuickLookWindow::instance().destroy();
    easy::ui::TrayWindow::instance().destroy();
    easy::core::PluginManager::instance().shutdownPlugins();
    easy::core::ShellContextMenuService::instance().shutdown();
    easy::ui::ToastOverlay::instance().shutdown();
    easy::ui::SpotlightOverlay::instance().shutdown();
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
    int wx = -1, wy = -1, ww = -1, wh = -1;
    if (parseWindowPos(wx, wy, ww, wh) && ww > 0 && wh > 0) {
        config.posX = wx;
        config.posY = wy;
        config.width = ww;
        config.height = wh;
        config.hasCustomPlacement = true;
        config.startCentered = false;
    }
    config.devServerUrl = "http://localhost:5173";
    settingsWnd.setConfig(config);
    settingsWnd.show(GetModuleHandleW(nullptr));
}

void preloadSettingsWindow(HINSTANCE hInstance) {
    auto& settingsWnd = easy::ui::SettingsWindow::instance();
    easy::ui::SettingsWindowConfig config;
    int wx = -1, wy = -1, ww = -1, wh = -1;
    if (parseWindowPos(wx, wy, ww, wh) && ww > 0 && wh > 0) {
        config.posX = wx;
        config.posY = wy;
        config.width = ww;
        config.height = wh;
        config.hasCustomPlacement = true;
        config.startCentered = false;
    }
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
        if (wParam == TIMER_ID_PERFORMANCE_BASELINE) {
            KillTimer(hwnd, TIMER_ID_PERFORMANCE_BASELINE);
            easy::ui::SearchWindow::instance().hide();
            const bool wrote = g_performanceScenarioOutput &&
                writePerformanceBaselineSnapshot(*g_performanceScenarioOutput);
            g_performanceScenarioOutput.reset();
            PostQuitMessage(wrote ? 0 : 1);
            return 0;
        }
        if (wParam == TIMER_ID_QUICKLOOK_REFRESH) {
            KillTimer(hwnd, TIMER_ID_QUICKLOOK_REFRESH);
            auto selected = easy::core::WinUtils::getSelectedExplorerFile();
            auto& quickLook = easy::ui::QuickLookWindow::instance();
            if (selected.has_value() && quickLook.isVisible() &&
                *selected != quickLook.currentFilePath()) {
                quickLook.previewFile(*selected);
            }
            return 0;
        }
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

    if (msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED) {
        easy::tray::TrayIcon::instance().refreshThemeIcon();
        return 0;
    }

    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
