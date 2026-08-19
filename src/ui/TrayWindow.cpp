#include "ui/TrayWindow.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/DpiUtils.h"
#include "ui/WebViewEnvironmentManager.h"
#include "ui/WebViewWindowStyle.h"
#include <wrl/event.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <utility>
#include "core/utils/WinUtils.h"

using namespace Microsoft::WRL;

namespace easy::ui {

static constexpr const wchar_t* TRAY_WINDOW_CLASS = L"EasyTools_TrayWindow";
static constexpr UINT WM_TRAY_VERIFY_DEACTIVATED = WM_APP + 41;

namespace {

SIZE getTrayWindowSize(POINT anchor) {
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    return TrayWindowStyle::windowSizeForDpi(
        easy::core::dpi::effectiveDpiForMonitor(monitor));
}

POINT trayWindowOrigin(int x, int y, int width, int height) {
    const POINT anchor{x, y};
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    const RECT workArea = easy::core::dpi::workArea(monitor);
    const float scale = easy::core::dpi::scaleForMonitor(monitor);
    const LONG margin = easy::core::dpi::scaleMetric(
        TrayWindowStyle::BaseScreenMargin, scale);
    const LONG maxX = std::max(
        workArea.left + margin, workArea.right - static_cast<LONG>(width) - margin);
    const LONG maxY = std::max(
        workArea.top + margin, workArea.bottom - static_cast<LONG>(height) - margin);
    return {
        std::clamp<LONG>(static_cast<LONG>(x - width / 2), workArea.left + margin, maxX),
        std::clamp<LONG>(static_cast<LONG>(y - height - margin), workArea.top + margin, maxY)
    };
}

}  // namespace

TrayWindow& TrayWindow::instance() {
    static TrayWindow inst;
    return inst;
}

void TrayWindow::preload(HINSTANCE hInstance) {
    if (m_hwnd && IsWindow(m_hwnd)) return;
    POINT defaultAnchor{};
    GetCursorPos(&defaultAnchor);
    const RECT work = easy::core::dpi::workArea(
        MonitorFromPoint(defaultAnchor, MONITOR_DEFAULTTONEAREST));
    if (defaultAnchor.x < work.left || defaultAnchor.x > work.right ||
        defaultAnchor.y < work.top || defaultAnchor.y > work.bottom) {
        defaultAnchor.x = work.right - 100;
        defaultAnchor.y = work.bottom - 100;
    }
    m_anchor = defaultAnchor;
    if (!createWindow(hInstance, defaultAnchor.x, defaultAnchor.y)) {
        LOG_ERROR("TrayWindow: preload createWindow failed");
        return;
    }
    initializeWebView2();
    ShowWindow(m_hwnd, SW_HIDE);
}

void TrayWindow::show(HINSTANCE hInstance, int x, int y) {
    m_anchor = {x, y};
    m_showTimeTick = GetTickCount64();
    if (m_hwnd && IsWindow(m_hwnd)) {
        updatePlacement();
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        m_visible = true;

        if (m_controller) {
            m_controller->put_IsVisible(TRUE);
            RECT bounds;
            GetClientRect(m_hwnd, &bounds);
            m_controller->put_Bounds(bounds);
        }
        return;
    }

    if (!createWindow(hInstance, x, y)) {
        LOG_ERROR("TrayWindow: createWindow failed");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    UpdateWindow(m_hwnd);
    m_visible = true;
}

void TrayWindow::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        if (m_controller) {
            m_controller->put_IsVisible(FALSE);
        }
    }
}

bool TrayWindow::isVisible() const {
    return m_visible.load() && m_hwnd && IsWindowVisible(m_hwnd);
}

void TrayWindow::destroy() {
    ++m_generation;
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
}

bool TrayWindow::createWindow(HINSTANCE hInstance, int x, int y) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = TRAY_WINDOW_CLASS;
    RegisterClassExW(&wc);

    const SIZE sz = getTrayWindowSize({x, y});
    const POINT origin = trayWindowOrigin(x, y, sz.cx, sz.cy);

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        TRAY_WINDOW_CLASS,
        L"EasyTools TrayMenu",
        WS_POPUP, // 无边框
        origin.x, origin.y, sz.cx, sz.cy,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // 启用分层透明，使得毛玻璃边界可以渲染，并避免底色黑块
    SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY);

    return true;
}

void TrayWindow::updatePlacement() {
    if (!m_hwnd || m_updatingPlacement) return;
    m_updatingPlacement = true;
    const SIZE size = getTrayWindowSize(m_anchor);
    const POINT origin = trayWindowOrigin(
        m_anchor.x, m_anchor.y, size.cx, size.cy);
    SetWindowPos(m_hwnd, HWND_TOPMOST, origin.x, origin.y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    m_updatingPlacement = false;
}

void TrayWindow::setContentSize(int width, int height) {
    if (!m_hwnd || !IsWindow(m_hwnd) || width <= 0 || height <= 0) return;
    const HMONITOR monitor = MonitorFromPoint(m_anchor, MONITOR_DEFAULTTONEAREST);
    const float scale = easy::core::dpi::scaleForMonitor(monitor);
    const int scaledW = std::max(120, static_cast<int>(width * scale));
    const int scaledH = std::max(80, static_cast<int>(height * scale));

    m_updatingPlacement = true;
    const POINT origin = trayWindowOrigin(m_anchor.x, m_anchor.y, scaledW, scaledH);
    SetWindowPos(m_hwnd, HWND_TOPMOST, origin.x, origin.y, scaledW, scaledH,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (m_controller) {
        RECT bounds{0, 0, scaledW, scaledH};
        m_controller->put_Bounds(bounds);
    }
    m_updatingPlacement = false;
}

void TrayWindow::initializeWebView2() {
    const uint64_t generation = ++m_generation;
    WebViewEnvironmentManager::instance().acquire(
            [this, generation](HRESULT result, ICoreWebView2Environment* env) {
                if (FAILED(result) || !env) {
                    LOG_ERROR("TrayWindow: shared environment unavailable.");
                    return;
                }
                if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) {
                    return;
                }
                m_environment = env;
                const HRESULT controllerResult = m_environment->CreateCoreWebView2Controller(
                    m_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, generation](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(res) || !controller) return E_FAIL;
                            if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) {
                                controller->Close();
                                return E_ABORT;
                            }
                            m_controller = controller;
                            m_controller->get_CoreWebView2(&m_webView);
                            
                            if (m_visible.load()) {
                                m_controller->put_IsVisible(TRUE);
                            } else {
                                m_controller->put_IsVisible(FALSE);
                            }
                            
                            // 开启真正完全透明背景 (ARGB = 0x00000000)
                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_controller.As(&controller2))) {
                                COREWEBVIEW2_COLOR transparent = {0, 0, 0, 0};
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            RECT bounds;
                            GetClientRect(m_hwnd, &bounds);
                            m_controller->put_Bounds(bounds);

                            // 取消右键菜单和状态栏
                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            m_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                            }

                            // 加载 Tray URL
                            auto exeDir = easy::core::WinUtils::getExeDirectory();
                            auto indexPath = exeDir / L"ui" / L"index.html";
                            std::wstring baseUrl;
                            std::error_code ec;
                            if (std::filesystem::exists(indexPath, ec)) {
                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
                                if (SUCCEEDED(m_webView->QueryInterface(IID_PPV_ARGS(&webView3))) && webView3) {
                                    webView3->SetVirtualHostNameToFolderMapping(
                                        L"easytools.local", (exeDir / L"ui").c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                }
                                baseUrl = L"https://easytools.local/index.html";
                            } else {
                                auto devUrlPath = exeDir.parent_path().parent_path().parent_path() / L"ui" / L".dev-server-url";
                                if (std::filesystem::exists(devUrlPath, ec)) {
                                    std::ifstream file(devUrlPath);
                                    std::string url;
                                    if (std::getline(file, url) && !url.empty()) {
                                        baseUrl = easy::core::WinUtils::utf8ToWstring(url);
                                    }
                                }
                                if (baseUrl.empty()) baseUrl = L"http://localhost:5173";
                            }

                            std::wstring targetUrl = baseUrl;
                            if (baseUrl.find(L"index.html") != std::wstring::npos) {
                                targetUrl += L"?tray=1";
                            } else {
                                targetUrl += L"/?tray=1";
                            }
                            
                            EventRegistrationToken token;
                            m_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success;
                                        args->get_IsSuccess(&success);
                                        if (!success) {
                                            COREWEBVIEW2_WEB_ERROR_STATUS status;
                                            args->get_WebErrorStatus(&status);
                                            LOG_ERROR("TrayWindow: WebView2 导航失败, status={}", static_cast<int>(status));
                                        }
                                        return S_OK;
                                    }
                                ).Get(), &token);

                            m_webView->Navigate(targetUrl.c_str());

                            // 注册 JS bridge
                            m_webView->AddScriptToExecuteOnDocumentCreated(
                                L"window.chrome.webview.addEventListener('message', event => {"
                                L"  const msg = event.data;"
                                L"  if (window.easyToolsBridge && window.easyToolsBridge.onMessage) {"
                                L"      window.easyToolsBridge.onMessage(msg);"
                                L"  }"
                                L"});", nullptr);

                            m_webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this, generation](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        try {
                                            if (generation != m_generation.load()) return S_OK;
                                            PWSTR messageRaw = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&messageRaw)) && messageRaw) {
                                                const std::string request = easy::core::WinUtils::wstringToUtf8(messageRaw);
                                                CoTaskMemFree(messageRaw);
                                                const std::string response =
                                                    easy::core::MessageBridge::instance().handleMessage(request);
                                                const std::wstring wideResponse =
                                                    easy::core::WinUtils::utf8ToWstring(response);
                                                sender->PostWebMessageAsString(wideResponse.c_str());
                                            }
                                        } catch (const std::exception& e) {
                                            LOG_ERROR("TrayWindow bridge error: {}", e.what());
                                        } catch (...) {
                                            LOG_ERROR("TrayWindow bridge unknown error");
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr);

                            m_webViewReady = true;
                            return S_OK;
                        }
                    ).Get());
                if (FAILED(controllerResult)) {
                    LOG_ERROR("TrayWindow: Create controller request failed, hr=0x{:08X}",
                              static_cast<unsigned>(controllerResult));
                }
            });
}

LRESULT CALLBACK TrayWindow::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto& inst = TrayWindow::instance();
    switch (uMsg) {
        case WM_SIZE:
            if (inst.m_controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                inst.m_controller->put_Bounds(bounds);
                if (IsWindowVisible(hwnd)) {
                    inst.m_controller->put_IsVisible(TRUE);
                }
            }
            break;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            if (!inst.m_updatingPlacement && IsWindowVisible(hwnd)) {
                inst.updatePlacement();
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostMessageW(hwnd, WM_TRAY_VERIFY_DEACTIVATED, 0, 0);
            }
            break;
        case WM_TRAY_VERIFY_DEACTIVATED: {
            if (!inst.m_visible.load()) break;
            const uint64_t elapsed = GetTickCount64() - inst.m_showTimeTick;
            if (elapsed < 350) {
                // 窗口刚打开 350ms 内不因初始焦点抖动或创建子窗口而意外关闭
                break;
            }
            const HWND foreground = GetForegroundWindow();
            if (foreground != hwnd && (!foreground || !IsChild(hwnd, foreground))) {
                inst.hide();
            }
            break;
        }
        case WM_DESTROY:
            if (inst.m_hwnd == hwnd) inst.m_hwnd = nullptr;
            inst.destroy();
            break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace easy::ui
