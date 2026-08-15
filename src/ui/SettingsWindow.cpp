// ─────────────────────────────────────────────────────────────────────────────
// SettingsWindow.cpp — WebView2 设置窗口实现
//
// 核心流程:
//   1. 创建 Win32 顶层窗口（可调整大小、有标题栏）
//   2. CreateCoreWebView2EnvironmentWithOptions → 初始化 WebView2 环境
//   3. 环境就绪后 CreateCoreWebView2Controller → 创建 WebView2 控件
//   4. 控件就绪后加载前端 UI + 注册 JS↔C++ 消息桥
//   5. 窗口大小变化时自动调整 WebView2 控件尺寸
// ─────────────────────────────────────────────────────────────────────────────

#include "ui/SettingsWindow.h"
#include "ui/WebViewWindowStyle.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/ipc/MessageBridge.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/config/ConfigManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "ui/WebViewEnvironmentManager.h"

// WebView2 SDK 头文件
#include <WebView2.h>
#include <wrl/event.h>

#include <filesystem>
#include <fstream>
#include <dwmapi.h>
#include <algorithm>
#include <utility>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#define DWMSBT_MAINWINDOW 2 // Mica
#define DWMSBT_TRANSIENTWINDOW 3 // Acrylic
#endif

using namespace Microsoft::WRL;

namespace easy::ui {

static constexpr const wchar_t* SETTINGS_WINDOW_CLASS = L"EasyTools_SettingsWindow";

SettingsWindow& SettingsWindow::instance() {
    static SettingsWindow inst;
    return inst;
}

void SettingsWindow::show(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;
    m_showRequestedAt = std::chrono::steady_clock::now();

    if (m_hwnd && IsWindow(m_hwnd)) {
        // 窗口已存在，直接显示并激活
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        m_visible = true;
        
        // 强制刷新 WebView2 尺寸和可见性（防御性编程）
        if (m_controller) {
            m_controller->put_IsVisible(TRUE);
            RECT bounds;
            GetClientRect(m_hwnd, &bounds);
            m_controller->put_Bounds(bounds);
        }
        if (m_webViewReady) {
            easy::core::PerformanceMonitor::instance().recordLatency(
                "settings.open",
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - m_showRequestedAt).count());
            m_showRequestedAt = {};
        }
        
        LOG_INFO("设置窗口已激活（复用已有窗口）");
        return;
    }

    if (!createWindow(hInstance)) {
        LOG_ERROR("创建设置窗口失败");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    m_visible = true;

    LOG_INFO("设置窗口已创建并显示");
}

void SettingsWindow::preload(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;
    if (m_hwnd && IsWindow(m_hwnd)) {
        return; // 已创建
    }

    if (createWindow(hInstance)) {
        m_initializationStartedAt = std::chrono::steady_clock::now();
        initializeWebView2(); // 初始化 WebView2，但不调用 ShowWindow，实现静默预热
        LOG_INFO("设置窗口后台静默预热完成");
    } else {
        LOG_ERROR("设置窗口后台静默预热失败");
    }
}

void SettingsWindow::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        
        if (m_controller) {
            m_controller->put_IsVisible(FALSE);
        }
        
        LOG_DEBUG("设置窗口已隐藏");
    }
}

bool SettingsWindow::isVisible() const {
    return m_visible.load() && m_hwnd && IsWindowVisible(m_hwnd);
}

void SettingsWindow::destroy() {
    ++m_generation;
    easy::core::MessageBridge::instance().setEventPusher({});
    if (m_controller) {
        m_controller->Close();
        m_controller = nullptr;
    }
    m_webView = nullptr;
    m_environment = nullptr;

    const HWND hwnd = std::exchange(m_hwnd, nullptr);
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    m_visible = false;
    m_webViewReady = false;
    LOG_INFO("设置窗口已销毁");
}

// ─────────────────────────────────────────────────────────────────────────────
// Win32 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsWindow::createWindow(HINSTANCE hInstance) {
    // 注册窗口类
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(20, 20, 30)); // 默认暗色背景，防止 Mica 失效时白屏
    wc.lpszClassName = SETTINGS_WINDOW_CLASS;
    wc.hIcon = LoadIconW(hInstance, IDI_APPLICATION);

    RegisterClassExW(&wc);  // 重复注册会返回 0，忽略即可

    // Per-Monitor DPI Awareness V2: 依据当前活动显示器工作区与 DPI 计算初始自适应布局
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT work = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    SIZE targetSize = (m_config.width == SettingsWindowStyle::BaseWidth &&
                       m_config.height == SettingsWindowStyle::BaseHeight)
        ? SettingsWindowStyle::windowSizeForDpi(dpi)
        : SIZE{easy::core::dpi::scaleMetric(m_config.width, scale),
               easy::core::dpi::scaleMetric(m_config.height, scale)};

    const int margin = easy::core::dpi::scaleMetric(SettingsWindowStyle::BaseScreenMargin, scale);
    const int maxW = (std::max)(1, static_cast<int>(work.right - work.left) - margin * 2);
    const int maxH = (std::max)(1, static_cast<int>(work.bottom - work.top) - margin * 2);
    targetSize.cx = (std::min)(targetSize.cx, static_cast<LONG>(maxW));
    targetSize.cy = (std::min)(targetSize.cy, static_cast<LONG>(maxH));

    const int x = m_config.startCentered
        ? work.left + (work.right - work.left - targetSize.cx) / 2
        : CW_USEDEFAULT;
    const int y = m_config.startCentered
        ? work.top + (work.bottom - work.top - targetSize.cy) / 2
        : CW_USEDEFAULT;

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        SETTINGS_WINDOW_CLASS,
        L"EasyTools 设置",
        WS_OVERLAPPEDWINDOW,
        x, y, targetSize.cx, targetSize.cy,
        nullptr, nullptr, hInstance, this  // 传递 this 指针
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败, error={}", GetLastError());
        return false;
    }

    // 启用 Windows 11 Fluent Mica 材质 & 沉浸式暗黑模式
    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
    DWORD backdropType = DWMSBT_MAINWINDOW; // Mica 材质
    DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    LOG_DEBUG("Win32 设置窗口已创建, size={}x{} on DPI {}", targetSize.cx, targetSize.cy, dpi);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// WebView2 初始化
// ─────────────────────────────────────────────────────────────────────────────

void SettingsWindow::initializeWebView2() {
    if (m_initializationStartedAt == std::chrono::steady_clock::time_point{}) {
        m_initializationStartedAt = std::chrono::steady_clock::now();
    }
    const uint64_t generation = ++m_generation;
    WebViewEnvironmentManager::instance().acquire(
        [this, generation](HRESULT result, ICoreWebView2Environment* environment) {
            // destroy() invalidates the generation before closing WebView2.
            // An E_ABORT callback after that is expected cancellation, not a
            // startup failure worth alarming the user or polluting telemetry.
            if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) {
                return;
            }
            if (FAILED(result) || !environment) {
                LOG_ERROR("WebView2 环境获取失败, hr=0x{:08X}", static_cast<unsigned>(result));
                return;
            }

            m_environment = environment;
            const HRESULT controllerResult = environment->CreateCoreWebView2Controller(
                m_hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, generation](HRESULT readyResult,
                                       ICoreWebView2Controller* controller) -> HRESULT {
                        if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) {
                            if (controller) controller->Close();
                            LOG_DEBUG("WebView2 控件创建已因窗口销毁而取消");
                            return S_OK;
                        }
                        if (FAILED(readyResult) || !controller) {
                            LOG_ERROR("WebView2 控件创建失败, hr=0x{:08X}",
                                      static_cast<unsigned>(readyResult));
                            return readyResult;
                        }
                        m_controller = controller;
                        controller->get_CoreWebView2(&m_webView);
                        onWebView2Ready();
                        return S_OK;
                    }).Get());
            if (FAILED(controllerResult)) {
                LOG_ERROR("WebView2 控件创建请求失败, hr=0x{:08X}",
                          static_cast<unsigned>(controllerResult));
            }
        });
}

void SettingsWindow::onWebView2Ready() {
    if (!m_webView || !m_controller) return;

    easy::core::TraceId::Scope scope;
    LOG_INFO("WebView2 控件就绪");

    // ── 调整控件尺寸以填满窗口并确保可见 ─────────────────────────────────
    if (m_visible.load()) {
        m_controller->put_IsVisible(TRUE);
    } else {
        m_controller->put_IsVisible(FALSE);
    }
    
    RECT bounds;
    GetClientRect(m_hwnd, &bounds);
    m_controller->put_Bounds(bounds);

    // ── 配置 WebView2 设置 ──────────────────────────────────────────────
    ComPtr<ICoreWebView2Settings> settings;
    m_webView->get_Settings(&settings);

    if (settings) {
        settings->put_IsScriptEnabled(TRUE);
        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
        settings->put_IsWebMessageEnabled(TRUE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);

#ifdef _DEBUG
        settings->put_AreDevToolsEnabled(m_config.devToolsEnabled ? TRUE : FALSE);
#else
        settings->put_AreDevToolsEnabled(FALSE);
#endif
    }

    // Serve packaged UI from a constrained virtual HTTPS origin instead of
    // granting broad file:// access to the browser process.
    const auto uiFolder = (easy::core::WinUtils::getExeDirectory() / L"ui").wstring();
    std::error_code mappingError;
    if (std::filesystem::exists(uiFolder, mappingError)) {
        ComPtr<ICoreWebView2_3> webView3;
        if (SUCCEEDED(m_webView.As(&webView3)) && webView3) {
            webView3->SetVirtualHostNameToFolderMapping(
                L"easytools.local", uiFolder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
    }

    // ── 设置 WebView2 透明背景，使得底层 Mica 材质透出 ──────────────────
    ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(m_controller.As(&controller2))) {
        COREWEBVIEW2_COLOR color = { 0, 0, 0, 0 }; // 完全透明
        controller2->put_DefaultBackgroundColor(color);
    }

    // ── 注册 JS → C++ 消息监听 ─────────────────────────────────────────
    m_webView->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                // 关键: C++ 异常绝不能逃逸回 WebView2 的 native 调用栈, 否则进程崩溃。
                try {
                    LPWSTR messageRaw = nullptr;
                    args->TryGetWebMessageAsString(&messageRaw);

                    if (messageRaw) {
                        std::string message = easy::core::WinUtils::wstringToUtf8(messageRaw);
                        CoTaskMemFree(messageRaw);

                        std::string response = easy::core::MessageBridge::instance().handleMessage(message);

                        std::wstring wResponse = easy::core::WinUtils::utf8ToWstring(response);
                        sender->PostWebMessageAsString(wResponse.c_str());
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("WebMessageReceived 处理异常: {}", e.what());
                } catch (...) {
                    LOG_ERROR("WebMessageReceived 处理未知异常");
                }
                return S_OK;
            }
        ).Get(),
        nullptr
    );

    // ── 设置事件推送器（C++ → JS）───────────────────────────────────────
    easy::core::MessageBridge::instance().setEventPusher(
        [this](const std::string& eventName, const nlohmann::json& data) {
            easy::core::MainThreadDispatcher::instance().post([this, eventName, data]() {
                if (!m_webView || !m_webViewReady) return;
                const nlohmann::json envelope = {
                    {"type", "event"}, {"event", eventName}, {"data", data}
                };
                const auto msg = easy::core::WinUtils::utf8ToWstring(envelope.dump());
                m_webView->PostWebMessageAsString(msg.c_str());
            });
        }
    );

    // ── 加载前端 UI ─────────────────────────────────────────────────────
    std::string entryUrl = getUIEntryUrl();
    std::wstring wUrl = easy::core::WinUtils::utf8ToWstring(entryUrl);
    
    // 监听导航失败事件，记录详细错误，方便诊断白屏
    EventRegistrationToken token;
    m_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, entryUrl](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL success;
                args->get_IsSuccess(&success);
                if (success) {
                    m_webViewReady = true;
                    const auto now = std::chrono::steady_clock::now();
                    if (m_initializationStartedAt != std::chrono::steady_clock::time_point{}) {
                        easy::core::PerformanceMonitor::instance().recordLatency(
                            "settings.navigation",
                            std::chrono::duration<double, std::milli>(
                                now - m_initializationStartedAt).count());
                        m_initializationStartedAt = {};
                    }
                    if (m_showRequestedAt != std::chrono::steady_clock::time_point{}) {
                        easy::core::PerformanceMonitor::instance().recordLatency(
                            "settings.open",
                            std::chrono::duration<double, std::milli>(
                                now - m_showRequestedAt).count());
                        m_showRequestedAt = {};
                    }
                    LOG_INFO("WebView2 导航成功: {}", entryUrl);
                } else {
                    COREWEBVIEW2_WEB_ERROR_STATUS status;
                    args->get_WebErrorStatus(&status);
                    LOG_ERROR("WebView2 导航失败, status={}", static_cast<int>(status));
                }
                return S_OK;
            }
        ).Get(), &token);

    m_webView->Navigate(wUrl.c_str());

    LOG_INFO("WebView2 正在加载前端 UI: {}", entryUrl);
}

std::string SettingsWindow::getUIEntryUrl() const {
    auto exeDir = easy::core::WinUtils::getExeDirectory();
    auto indexPath = exeDir / L"ui" / L"index.html";

    // 1. 优先使用本地单文件包，通过受限虚拟 HTTPS 源离线加载。
    std::error_code ec;
    if (std::filesystem::exists(indexPath, ec)) {
        return "https://easytools.local/index.html";
    }

    // 2. 如果本地不存在，说明是在 C++ 开发模式下运行。尝试读取 Vite 动态端口文件
    auto devUrlPath = exeDir.parent_path().parent_path().parent_path() / L"ui" / L".dev-server-url";
    if (std::filesystem::exists(devUrlPath, ec)) {
        try {
            std::ifstream file(devUrlPath);
            std::string url;
            if (std::getline(file, url) && !url.empty()) {
                LOG_INFO("成功读取到动态开发服务器地址: {}", url);
                return url;
            }
        } catch (...) {}
    }

    // 3. 配置中的备用开发地址 (如果有的话，主要防呆)
    if (!m_config.devServerUrl.empty() && m_config.devServerUrl != "http://localhost:5173") {
        return m_config.devServerUrl;
    }

    // 4. 终极降级: 使用开发服务器默认地址
    LOG_WARN("未找到本地 UI 文件及动态端口文件, 尝试连接默认开发服务器 http://localhost:5173");
    return "http://localhost:5173";
}

void SettingsWindow::navigateTo(const std::string& path) {
    if (!easy::core::MainThreadDispatcher::instance().isOwnerThread()) {
        easy::core::MainThreadDispatcher::instance().post([this, path]() { navigateTo(path); });
        return;
    }
    if (!m_webView || !m_webViewReady) return;

    // 通过 JS 路由导航
    std::string script = "window.location.hash = '" + path + "';";
    std::wstring wScript = easy::core::WinUtils::utf8ToWstring(script);
    m_webView->ExecuteScript(wScript.c_str(), nullptr);
}

void SettingsWindow::pushEventToFrontend(const std::string& eventName, const std::string& dataJson) {
    if (!easy::core::MainThreadDispatcher::instance().isOwnerThread()) {
        easy::core::MainThreadDispatcher::instance().post(
            [this, eventName, dataJson]() { pushEventToFrontend(eventName, dataJson); });
        return;
    }
    if (!m_webView || !m_webViewReady) return;

    std::string envelope = R"({"type":"event","event":")" + eventName + R"(","data":)" + dataJson + "}";
    std::wstring wMsg = easy::core::WinUtils::utf8ToWstring(envelope);
    m_webView->PostWebMessageAsString(wMsg.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_SIZE: {
            if (self && self->m_controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                self->m_controller->put_Bounds(bounds);
                
                // 确保尺寸变化时如果窗口可见，组件也可见
                if (IsWindowVisible(hwnd)) {
                    self->m_controller->put_IsVisible(TRUE);
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            if (!easy::core::ConfigManager::instance().get<bool>(
                    "/general/minimizeToTray", true)) {
                PostQuitMessage(0);
            } else if (self) self->hide();
            return 0;  // 不调用 DestroyWindow
        }

        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, nullptr,
                             suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (self && self->m_controller) {
                RECT bounds{};
                GetClientRect(hwnd, &bounds);
                self->m_controller->put_Bounds(bounds);
            }
            return 0;
        }

        case WM_GETMINMAXINFO: {
            const unsigned dpi = easy::core::dpi::effectiveDpiForWindow(hwnd);
            const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            const RECT work = easy::core::dpi::workArea(monitor);
            const int workW = (std::max)(1, static_cast<int>(work.right - work.left));
            const int workH = (std::max)(1, static_cast<int>(work.bottom - work.top));

            const SIZE minSize = SettingsWindowStyle::minWindowSizeForDpi(dpi);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = (std::min)(minSize.cx, static_cast<LONG>(workW * 0.9f));
            mmi->ptMinTrackSize.y = (std::min)(minSize.cy, static_cast<LONG>(workH * 0.9f));
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace easy::ui
