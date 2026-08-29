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
#include "ui/WebViewDpi.h"
#include "ui/WebViewSecurity.h"
#include "ui/WebViewSuspend.h"
#include "ui/KeyboardPipeline.h"

// WebView2 SDK 头文件
#include <windowsx.h>
#include <dwmapi.h>
#include <WebView2.h>
#include <wrl/event.h>

#include <filesystem>
#include <commctrl.h>
#include <fstream>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <utility>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

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

namespace {

LRESULT CALLBACK WebViewResizeSubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    if (uMsg == WM_NCHITTEST) {
        HWND parentHwnd = reinterpret_cast<HWND>(dwRefData);
        if (parentHwnd && IsWindow(parentHwnd) && !IsZoomed(parentHwnd)) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetWindowRect(parentHwnd, &rc);
            const int border = 8;
            // 当鼠标位于父窗口 8px 边缘范围内时，子窗口声明透明，让宿主窗口响应拉伸
            if (pt.x < rc.left + border || pt.x >= rc.right - border ||
                pt.y < rc.top + border || pt.y >= rc.bottom - border) {
                return HTTRANSPARENT;
            }
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void hookWebViewChildWindows(HWND parentHwnd) {
    if (!parentHwnd || !IsWindow(parentHwnd)) return;
    EnumChildWindows(parentHwnd, [](HWND child, LPARAM lParam) -> BOOL {
        SetWindowSubclass(child, WebViewResizeSubclassProc, 1001, static_cast<DWORD_PTR>(lParam));
        return TRUE;
    }, reinterpret_cast<LPARAM>(parentHwnd));
}

} // namespace

static constexpr const wchar_t* SETTINGS_WINDOW_CLASS = L"EasyTools_SettingsWindow";

SettingsWindow& SettingsWindow::instance() {
    static SettingsWindow inst;
    return inst;
}

void SettingsWindow::show(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;
    m_showRequestedAt = std::chrono::steady_clock::now();
    if (hInstance) m_hInstance = hInstance;
    else if (!m_hInstance) m_hInstance = GetModuleHandleW(nullptr);

    if (m_hwnd && IsWindow(m_hwnd)) {
        // 用户重新激活设置窗口，立即取消 1 分钟闲置销毁倒计时并复用窗口
        KillTimer(m_hwnd, IDT_IDLE_DESTROY);

        const std::wstring windowTitle = easy::core::WinUtils::isCurrentProcessElevated()
            ? L"EasyTools 设置 (管理员)"
            : L"EasyTools 设置";
        SetWindowTextW(m_hwnd, windowTitle.c_str());

        if (IsIconic(m_hwnd)) {
            ShowWindow(m_hwnd, SW_RESTORE);
        } else {
            ShowWindow(m_hwnd, SW_SHOW);
        }
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetActiveWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_visible = true;
        
        // 强制刷新 WebView2 尺寸和可见性（防御性编程）
        if (m_webView) m_suspendController.resume(m_webView.Get(), "settings");
        if (m_controller) {
            m_controller->put_IsVisible(TRUE);
            syncWebViewDpi(m_controller.Get(), m_hwnd);
        }
        if (m_webViewReady) {
            easy::core::PerformanceMonitor::instance().recordLatency(
                "settings.open",
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - m_showRequestedAt).count());
            m_showRequestedAt = {};
        }
        
        LOG_INFO("设置窗口已激活（复用已有窗口，已取消闲置销毁定时器）");
        return;
    }

    if (!createWindow(m_hInstance)) {
        LOG_ERROR("创建设置窗口失败");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_visible = true;

    LOG_INFO("设置窗口已按需创建并显示");
}

void SettingsWindow::preload(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;
    if (hInstance) m_hInstance = hInstance;
    else if (!m_hInstance) m_hInstance = GetModuleHandleW(nullptr);

    if (m_hwnd && IsWindow(m_hwnd)) {
        return; // 已创建
    }

    if (createWindow(m_hInstance)) {
        m_initializationStartedAt = std::chrono::steady_clock::now();
        initializeWebView2(); // 初始化 WebView2，但不调用 ShowWindow，实现静默预热
        LOG_INFO("设置窗口后台静默预热完成");
    } else {
        LOG_ERROR("设置窗口后台静默预热失败");
    }
}

void SettingsWindow::hide() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        persistGeometry();
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        
        if (m_controller) {
            m_controller->put_IsVisible(FALSE);
        }

        // 挂起 Chromium 渲染管线以释放 GPU/DOM 显存与工作集
        if (m_webView) m_suspendController.requestSuspend(m_webView.Get(), "settings");

        const bool autoRelease = easy::core::ConfigManager::instance().get<bool>(
            "/general/autoReleaseSettingsMemory", true);
        if (autoRelease) {
            // 启动 1 分钟闲置销毁定时器：若用户 60 秒内未再次打开设置，则彻底销毁 WebView2 释放物理内存
            SetTimer(m_hwnd, IDT_IDLE_DESTROY, IDLE_DESTROY_TIMEOUT_MS, nullptr);
            LOG_INFO("设置窗口已隐藏，已启动 1 分钟闲置自动销毁倒计时");
        } else {
            KillTimer(m_hwnd, IDT_IDLE_DESTROY);
            LOG_INFO("设置窗口已隐藏（自动释放内存开关已关闭，保持后台常驻）");
        }

        // 冷路径退场：主动释放物理内存工作集
        easy::core::WinUtils::trimWorkingSet();
    }
}

bool SettingsWindow::isVisible() const {
    return m_visible.load() && m_hwnd && IsWindowVisible(m_hwnd);
}

void SettingsWindow::destroy() {
    m_suspendController.abandon();
    ++m_generation;
    easy::core::MessageBridge::instance().setEventPusher({});
    if (m_hwnd && IsWindow(m_hwnd)) {
        KillTimer(m_hwnd, IDT_IDLE_DESTROY);
    }
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
    easy::core::WinUtils::trimWorkingSet();
    LOG_INFO("设置窗口已彻底销毁并释放 WebView2 渲染器");
}

// ─────────────────────────────────────────────────────────────────────────────
// Win32 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsWindow::createWindow(HINSTANCE hInstance) {
    m_suspendController.reset();

    if (!m_config.hasCustomPlacement) {
        applyPersistedPlacementIfAny();
    }

    const POINT placementOrigin{m_config.hasCustomPlacement ? m_config.posX : 0,
                                m_config.hasCustomPlacement ? m_config.posY : 0};
    const HMONITOR monitor = m_config.hasCustomPlacement
        ? MonitorFromPoint(placementOrigin, MONITOR_DEFAULTTONEAREST)
        : easy::core::dpi::activeMonitor();
    const RECT work = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(20, 20, 30));
    wc.lpszClassName = SETTINGS_WINDOW_CLASS;
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi), LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
        GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR);
    RegisterClassExW(&wc);

    SIZE targetSize = (m_config.width == SettingsWindowStyle::BaseWidth &&
                       m_config.height == SettingsWindowStyle::BaseHeight)
        ? SettingsWindowStyle::windowSizeForDpi(dpi)
        : SIZE{easy::core::dpi::scaleMetric(m_config.width, scale),
               easy::core::dpi::scaleMetric(m_config.height, scale)};

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    const int margin = easy::core::dpi::scaleMetric(SettingsWindowStyle::BaseScreenMargin, scale);
    if (m_config.hasCustomPlacement) {
        targetSize.cx = (std::max)(400, m_config.width);
        targetSize.cy = (std::max)(300, m_config.height);
        const RECT proposed{
            m_config.posX,
            m_config.posY,
            m_config.posX + targetSize.cx,
            m_config.posY + targetSize.cy};
        const RECT clamped = easy::core::dpi::clampWindowToWorkArea(proposed, work);
        x = clamped.left;
        y = clamped.top;
        targetSize.cx = clamped.right - clamped.left;
        targetSize.cy = clamped.bottom - clamped.top;
    } else {
        targetSize = easy::core::dpi::fitSizeToWorkArea(targetSize, work, margin);
        x = m_config.startCentered
            ? work.left + (work.right - work.left - targetSize.cx) / 2
            : CW_USEDEFAULT;
        y = m_config.startCentered
            ? work.top + (work.bottom - work.top - targetSize.cy) / 2
            : CW_USEDEFAULT;
    }

    const std::wstring windowTitle = easy::core::WinUtils::isCurrentProcessElevated()
        ? L"EasyTools 设置 (管理员)"
        : L"EasyTools 设置";

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        SETTINGS_WINDOW_CLASS,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, targetSize.cx, targetSize.cy,
        nullptr, nullptr, hInstance, this  // 传递 this 指针
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败, error={}", GetLastError());
        return false;
    }

    // 启用 DWM 全客户区扩展与跨平台通用圆角裁剪，全兼容 Win11、Win10、Server 2022/2025
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // 强制通知系统 Frame 已变更，立即移除系统黑色标题栏
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    const int radius = static_cast<int>(12 * scale);
    easy::core::WinUtils::applyUniversalRoundedCorners(m_hwnd, targetSize.cx, targetSize.cy, radius);

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (wc.hIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    }
    if (wc.hIconSm) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);
    }

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
    syncWebViewDpi(m_controller.Get(), m_hwnd);

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

    web_security::applyNavigationPolicy(m_webView.Get());
    KeyboardPipeline::applyWebKeyboardPolicy(m_controller.Get(), m_config.devToolsEnabled);

    // ── 注册 JS → C++ 消息监听 ─────────────────────────────────────────
    m_webView->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (!web_security::isTrustedMessageSource(args)) return S_OK;
                // 关键: C++ 异常绝不能逃逸回 WebView2 的 native 调用栈, 否则进程崩溃。
                try {
                    LPWSTR messageRaw = nullptr;
                    args->TryGetWebMessageAsString(&messageRaw);

                    if (messageRaw) {
                        std::string message = easy::core::WinUtils::wstringToUtf8(messageRaw);
                        CoTaskMemFree(messageRaw);

                        if (!web_security::isBridgeMethodAllowed(
                                message, web_security::Surface::Settings)) return S_OK;

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
    hookWebViewChildWindows(m_hwnd);

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

#ifndef _DEBUG
    // Release 构建必须失败关闭。绝不能因为安装文件损坏而连接可能被其他本机进程
    // 占用的开发端口，同时向该页面暴露原生消息桥。
    LOG_ERROR("打包 UI 缺失: {}", easy::core::WinUtils::wstringToUtf8(indexPath.wstring()));
    return "https://easytools.local/index.html";
#else

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
#endif
}

void SettingsWindow::applyPersistedPlacementIfAny() {
    auto& cfg = easy::core::ConfigManager::instance();
    if (!cfg.has("/ui/settingsWindow/width") || !cfg.has("/ui/settingsWindow/height")) {
        return;
    }

    const int savedX = cfg.get<int>("/ui/settingsWindow/x", 0);
    const int savedY = cfg.get<int>("/ui/settingsWindow/y", 0);
    const int savedW = cfg.get<int>("/ui/settingsWindow/width", SettingsWindowStyle::BaseWidth);
    const int savedH = cfg.get<int>("/ui/settingsWindow/height", SettingsWindowStyle::BaseHeight);
    const int savedDpi = cfg.get<int>("/ui/settingsWindow/dpi", static_cast<int>(easy::core::dpi::DefaultDpi));

    const POINT origin{savedX, savedY};
    const unsigned targetDpi = easy::core::dpi::effectiveDpiForMonitor(
        MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST));
    const float ratio = easy::core::dpi::scaleForDpi(targetDpi) /
                        easy::core::dpi::scaleForDpi(static_cast<unsigned>(
                            (std::max)(1, savedDpi)));

    // 若历史记录保存的尺寸超过或接近全屏，平滑回退为标准适中尺寸
    const int scaledW = static_cast<int>(std::lround(savedW * ratio));
    const int scaledH = static_cast<int>(std::lround(savedH * ratio));
    const RECT workArea = easy::core::dpi::workArea(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST));
    const int maxAcceptableW = static_cast<int>((workArea.right - workArea.left) * 0.88f);
    const int maxAcceptableH = static_cast<int>((workArea.bottom - workArea.top) * 0.88f);

    if (scaledW > maxAcceptableW || scaledH > maxAcceptableH) {
        m_config.width = SettingsWindowStyle::BaseWidth;
        m_config.height = SettingsWindowStyle::BaseHeight;
        m_config.hasCustomPlacement = false;
        m_config.startCentered = true;
        return;
    }

    m_config.posX = savedX;
    m_config.posY = savedY;
    m_config.width = (std::max)(400, scaledW);
    m_config.height = (std::max)(300, scaledH);
    m_config.hasCustomPlacement = true;
    m_config.startCentered = false;
}

void SettingsWindow::persistGeometry() {
    if (!m_hwnd || !IsWindow(m_hwnd) || IsIconic(m_hwnd)) return;

    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc)) return;
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    easy::core::ConfigManager::instance().mergePatch({
        {"ui", {
            {"settingsWindow", {
                {"x", rc.left},
                {"y", rc.top},
                {"width", width},
                {"height", height},
                {"dpi", static_cast<int>(easy::core::dpi::effectiveDpiForWindow(m_hwnd))}
            }}
        }}
    }, "/ui/settingsWindow");
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

void SettingsWindow::minimize() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);
}

void SettingsWindow::toggleMaximize() {
    if (!m_hwnd) return;
    if (IsZoomed(m_hwnd)) {
        ShowWindow(m_hwnd, SW_RESTORE);
    } else {
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    }
}

void SettingsWindow::close() {
    if (m_hwnd) SendMessageW(m_hwnd, WM_CLOSE, 0, 0);
}

void SettingsWindow::dragMove() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    ReleaseCapture();
    SendMessageW(m_hwnd, WM_SYSCOMMAND, 0xF012, 0);
}

void SettingsWindow::showSystemMenu(int screenX, int screenY) {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    HMENU hMenu = GetSystemMenu(m_hwnd, FALSE);
    if (!hMenu) return;

    const bool zoomed = IsZoomed(m_hwnd) != FALSE;
    EnableMenuItem(hMenu, SC_RESTORE, MF_BYCOMMAND | (zoomed ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(hMenu, SC_MOVE, MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(hMenu, SC_SIZE, MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(hMenu, SC_MINIMIZE, MF_BYCOMMAND | MF_ENABLED);
    EnableMenuItem(hMenu, SC_MAXIMIZE, MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);

    POINT pt{};
    if (GetCursorPos(&pt)) {
        screenX = pt.x;
        screenY = pt.y;
    } else if (screenX == -1 || screenY == -1) {
        RECT rc{};
        GetWindowRect(m_hwnd, &rc);
        screenX = rc.left + 24;
        screenY = rc.top + 32;
    }

    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                             screenX, screenY, 0, m_hwnd, nullptr);
    if (cmd > 0) {
        PostMessageW(m_hwnd, WM_SYSCOMMAND, cmd, 0);
    }
}

void SettingsWindow::startResize(const std::string& edge) {
    if (!m_hwnd || !IsWindow(m_hwnd) || IsZoomed(m_hwnd)) return;
    ReleaseCapture();
    WPARAM scSizeParam = 0;
    if (edge == "left") scSizeParam = 0xF001;          // SC_SIZE + WMSZ_LEFT
    else if (edge == "right") scSizeParam = 0xF002;    // SC_SIZE + WMSZ_RIGHT
    else if (edge == "top") scSizeParam = 0xF003;      // SC_SIZE + WMSZ_TOP
    else if (edge == "top_left") scSizeParam = 0xF004; // SC_SIZE + WMSZ_TOPLEFT
    else if (edge == "top_right") scSizeParam = 0xF005;// SC_SIZE + WMSZ_TOPRIGHT
    else if (edge == "bottom") scSizeParam = 0xF006;   // SC_SIZE + WMSZ_BOTTOM
    else if (edge == "bottom_left") scSizeParam = 0xF007;// SC_SIZE + WMSZ_BOTTOMLEFT
    else if (edge == "bottom_right") scSizeParam = 0xF008;// SC_SIZE + WMSZ_BOTTOMRIGHT
    else return;

    SendMessageW(m_hwnd, WM_SYSCOMMAND, scSizeParam, 0);
}

bool SettingsWindow::isMaximized() const {
    return m_hwnd && (IsZoomed(m_hwnd) != FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_TIMER: {
            if (wParam == IDT_IDLE_DESTROY) {
                KillTimer(hwnd, IDT_IDLE_DESTROY);
                const bool autoRelease = easy::core::ConfigManager::instance().get<bool>(
                    "/general/autoReleaseSettingsMemory", true);
                if (autoRelease && self && !self->isVisible()) {
                    LOG_INFO("设置窗口已闲置 1 分钟，自动销毁 Win32 窗口并释放 WebView2 渲染进程物理内存");
                    self->destroy();
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_NCCALCSIZE: {
            if (wParam) {
                if (IsZoomed(hwnd)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi = { sizeof(mi) };
                    if (GetMonitorInfoW(hMon, &mi)) {
                        params->rgrc[0] = mi.rcWork;
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_NCHITTEST: {
            if (!IsZoomed(hwnd)) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT rc;
                GetWindowRect(hwnd, &rc);
                const int border = 8;

                if (pt.y < rc.top + border && pt.x < rc.left + border) return HTTOPLEFT;
                if (pt.y < rc.top + border && pt.x >= rc.right - border) return HTTOPRIGHT;
                if (pt.y >= rc.bottom - border && pt.x < rc.left + border) return HTBOTTOMLEFT;
                if (pt.y >= rc.bottom - border && pt.x >= rc.right - border) return HTBOTTOMRIGHT;
                if (pt.y < rc.top + border) return HTTOP;
                if (pt.y >= rc.bottom - border) return HTBOTTOM;
                if (pt.x < rc.left + border) return HTLEFT;
                if (pt.x >= rc.right - border) return HTRIGHT;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_SIZE: {
            if (self) {
                const bool maxState = (wParam == SIZE_MAXIMIZED || IsZoomed(hwnd));
                nlohmann::json data = {{"isMaximized", maxState}};
                self->pushEventToFrontend("window:maximizedChanged", data.dump());

                const int newW = LOWORD(lParam);
                const int newH = HIWORD(lParam);
                if (newW > 0 && newH > 0) {
                    if (maxState) {
                        SetWindowRgn(hwnd, nullptr, TRUE);
                    } else {
                        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                        const float scale = easy::core::dpi::scaleForMonitor(monitor);
                        const int radius = static_cast<int>(12 * scale);
                        easy::core::WinUtils::applyUniversalRoundedCorners(hwnd, newW, newH, radius);
                    }
                }

                if (self->m_controller) {
                    syncWebViewDpi(self->m_controller.Get(), hwnd);
                    hookWebViewChildWindows(hwnd);
                    if (IsWindowVisible(hwnd)) {
                        self->m_controller->put_IsVisible(TRUE);
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE: {
            if (self) self->persistGeometry();
            if (!easy::core::ConfigManager::instance().get<bool>(
                    "/general/minimizeToTray", true)) {
                PostQuitMessage(0);
            } else if (self) self->hide();
            return 0;  // 不调用 DestroyWindow
        }

        case WM_EXITSIZEMOVE: {
            if (self) {
                self->persistGeometry();
                if (!IsZoomed(hwnd)) {
                    RECT rc;
                    if (GetWindowRect(hwnd, &rc)) {
                        const int w = rc.right - rc.left;
                        const int h = rc.bottom - rc.top;
                        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                        const float scale = easy::core::dpi::scaleForMonitor(monitor);
                        const int radius = static_cast<int>(12 * scale);
                        easy::core::WinUtils::applyUniversalRoundedCorners(hwnd, w, h, radius);
                    }
                }
            }
            return 0;
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
                syncWebViewDpi(self->m_controller.Get(), hwnd);
            }
            if (self) self->persistGeometry();
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

        case WM_SYSCOMMAND:
        case WM_HELP: {
            if (KeyboardPipeline::filterWindowMessage(hwnd, msg, wParam, lParam)) {
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace easy::ui
