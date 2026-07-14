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
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/ipc/MessageBridge.h"

// WebView2 SDK 头文件
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl/event.h>

#include <filesystem>
#include <fstream>
#include <dwmapi.h>
#include <algorithm>

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
    if (m_controller) {
        m_controller->Close();
        m_controller = nullptr;
    }
    m_webView = nullptr;
    m_environment = nullptr;

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
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

    // 获取 DPI 缩放比例
    float scale = easy::core::WinUtils::getDpiScale();
    int scaledWidth = static_cast<int>(m_config.width * scale);
    int scaledHeight = static_cast<int>(m_config.height * scale);

    // 计算居中位置
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = m_config.startCentered ? (screenW - scaledWidth) / 2 : CW_USEDEFAULT;
    int y = m_config.startCentered ? (screenH - scaledHeight) / 2 : CW_USEDEFAULT;

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        SETTINGS_WINDOW_CLASS,
        L"EasyTools 设置",
        WS_OVERLAPPEDWINDOW,
        x, y, scaledWidth, scaledHeight,
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

    // 设置 DPI 感知的最小窗口尺寸
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    LOG_DEBUG("Win32 设置窗口已创建, size={}x{}", m_config.width, m_config.height);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// WebView2 初始化
// ─────────────────────────────────────────────────────────────────────────────

void SettingsWindow::initializeWebView2() {
    // 用户数据目录
    auto userDataDir = easy::core::WinUtils::getAppDataDirectory() / L"webview2_data";
    std::wstring userDataPath = userDataDir.wstring();

    LOG_DEBUG("WebView2 用户数据目录: {}", easy::core::WinUtils::wstringToUtf8(userDataPath));

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(L"--enable-features=OverlayScrollbar --allow-file-access-from-files");

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,                    // 使用 Evergreen Runtime（不指定浏览器路径）
        userDataPath.c_str(),       // 用户数据目录
        options.Get(),              // 环境选项
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    LOG_ERROR("WebView2 环境创建失败, hr=0x{:08X}", static_cast<unsigned>(result));
                    return result;
                }

                m_environment = env;
                LOG_DEBUG("WebView2 环境创建成功");

                // 创建 WebView2 控件
                env->CreateCoreWebView2Controller(
                    m_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) {
                                LOG_ERROR("WebView2 控件创建失败, hr=0x{:08X}", static_cast<unsigned>(result));
                                return result;
                            }

                            m_controller = controller;
                            controller->get_CoreWebView2(&m_webView);

                            onWebView2Ready();
                            return S_OK;
                        }
                    ).Get()
                );

                return S_OK;
            }
        ).Get()
    );

    if (FAILED(hr)) {
        LOG_ERROR("CreateCoreWebView2EnvironmentWithOptions 失败, hr=0x{:08X}", static_cast<unsigned>(hr));
    }
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

#ifdef _DEBUG
        settings->put_AreDevToolsEnabled(m_config.devToolsEnabled ? TRUE : FALSE);
#else
        settings->put_AreDevToolsEnabled(FALSE);
#endif
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
    auto webViewWeak = m_webView;  // 弱引用避免循环引用
    easy::core::MessageBridge::instance().setEventPusher(
        [webViewWeak](const std::string& eventName, const nlohmann::json& data) {
            if (!webViewWeak) return;

            nlohmann::json envelope = {
                {"type", "event"},
                {"event", eventName},
                {"data", data}
            };
            std::wstring msg = easy::core::WinUtils::utf8ToWstring(envelope.dump());
            webViewWeak->PostWebMessageAsString(msg.c_str());
        }
    );

    // ── 加载前端 UI ─────────────────────────────────────────────────────
    std::string entryUrl = getUIEntryUrl();
    std::wstring wUrl = easy::core::WinUtils::utf8ToWstring(entryUrl);
    
    // 监听导航失败事件，记录详细错误，方便诊断白屏
    EventRegistrationToken token;
    m_webView->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, entryUrl](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                BOOL success;
                args->get_IsSuccess(&success);
                if (success) {
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

    m_webViewReady = true;
    LOG_INFO("WebView2 正在加载前端 UI: {}", entryUrl);
}

std::string SettingsWindow::getUIEntryUrl() const {
    auto exeDir = easy::core::WinUtils::getExeDirectory();
    auto indexPath = exeDir / L"ui" / L"index.html";

    // 1. 优先使用本地单文件打包 (生产模式) - 绝对的 file:/// 路径加载，完美离线免疫代理
    std::error_code ec;
    if (std::filesystem::exists(indexPath, ec)) {
        std::string pathStr = easy::core::WinUtils::wstringToUtf8(indexPath.wstring());
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
        return "file:///" + pathStr;
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
    if (!m_webView || !m_webViewReady) return;

    // 通过 JS 路由导航
    std::string script = "window.location.hash = '" + path + "';";
    std::wstring wScript = easy::core::WinUtils::utf8ToWstring(script);
    m_webView->ExecuteScript(wScript.c_str(), nullptr);
}

void SettingsWindow::pushEventToFrontend(const std::string& eventName, const std::string& dataJson) {
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
            // 关闭设置窗口 = 隐藏到托盘（不退出程序）
            if (self) {
                self->hide();
            }
            return 0;  // 不调用 DestroyWindow
        }

        case WM_GETMINMAXINFO: {
            float scale = easy::core::WinUtils::getDpiScale(hwnd);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = static_cast<int>(800 * scale);
            mmi->ptMinTrackSize.y = static_cast<int>(600 * scale);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace easy::ui
