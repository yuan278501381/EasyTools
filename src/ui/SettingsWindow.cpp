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

void SettingsWindow::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = SETTINGS_WINDOW_CLASS;
    wc.hIcon = LoadIconW(hInstance, IDI_APPLICATION);

    RegisterClassExW(&wc);  // 重复注册会返回 0，忽略即可

    // 计算居中位置
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = m_config.startCentered ? (screenW - m_config.width) / 2 : CW_USEDEFAULT;
    int y = m_config.startCentered ? (screenH - m_config.height) / 2 : CW_USEDEFAULT;

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        SETTINGS_WINDOW_CLASS,
        L"EasyTools 设置",
        WS_OVERLAPPEDWINDOW,
        x, y, m_config.width, m_config.height,
        nullptr, nullptr, hInstance, this  // 传递 this 指针
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败, error={}", GetLastError());
        return false;
    }

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

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,                    // 使用 Evergreen Runtime（不指定浏览器路径）
        userDataPath.c_str(),       // 用户数据目录
        nullptr,                    // 环境选项（使用默认）
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

    // ── 调整控件尺寸以填满窗口 ───────────────────────────────────────────
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

    // ── 注册 JS → C++ 消息监听 ─────────────────────────────────────────
    m_webView->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR messageRaw = nullptr;
                args->TryGetWebMessageAsString(&messageRaw);

                if (messageRaw) {
                    std::string message = easy::core::WinUtils::wstringToUtf8(messageRaw);
                    CoTaskMemFree(messageRaw);

                    // 路由到 MessageBridge
                    std::string response = easy::core::MessageBridge::instance().handleMessage(message);

                    // 将响应发回前端
                    std::wstring wResponse = easy::core::WinUtils::utf8ToWstring(response);
                    sender->PostWebMessageAsString(wResponse.c_str());
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
    m_webView->Navigate(wUrl.c_str());

    m_webViewReady = true;
    LOG_INFO("WebView2 已加载前端 UI: {}", entryUrl);
}

std::string SettingsWindow::getUIEntryUrl() const {
    // 优先使用开发服务器（开发模式）
    if (!m_config.devServerUrl.empty()) {
        return m_config.devServerUrl;
    }

    // 使用本地打包文件
    auto exeDir = easy::core::WinUtils::getExeDirectory();
    auto indexPath = exeDir / L"ui" / L"index.html";

    if (std::filesystem::exists(indexPath)) {
        return "file:///" + easy::core::WinUtils::wstringToUtf8(indexPath.wstring());
    }

    // 降级: 使用开发服务器默认地址
    LOG_WARN("未找到本地 UI 文件, 尝试连接开发服务器 http://localhost:5173");
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
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 800;
            mmi->ptMinTrackSize.y = 600;
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace easy::ui
