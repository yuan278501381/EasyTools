#include "ui/SearchWindow.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "ui/WebViewEnvironmentManager.h"
#include <WebView2.h>
#include <wrl/event.h>
#include <filesystem>
#include <fstream>
#include <utility>
#include "core/utils/WinUtils.h"

using namespace Microsoft::WRL;

namespace easy::ui {

static constexpr const wchar_t* SEARCH_WINDOW_CLASS = L"EasyTools_SearchWindow";
static constexpr UINT WM_SEARCH_VERIFY_DEACTIVATED = WM_APP + 42;

namespace {

RECT activeMonitorWorkArea() {
    POINT point{};
    GetCursorPos(&point);
    RECT workArea{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    if (monitor && GetMonitorInfoW(monitor, &monitorInfo)) {
        workArea = monitorInfo.rcWork;
    }
    return workArea;
}

}  // namespace

SearchWindow& SearchWindow::instance() {
    static SearchWindow inst;
    return inst;
}

void SearchWindow::show(HINSTANCE hInstance) {
    if (m_hwnd && IsWindow(m_hwnd)) {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        m_visible = true;
        return;
    }

    if (!createWindow(hInstance)) {
        LOG_ERROR("SearchWindow: createWindow failed");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    m_visible = true;
}

void SearchWindow::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

bool SearchWindow::isVisible() const {
    return m_visible.load() && m_hwnd && IsWindowVisible(m_hwnd);
}

void SearchWindow::destroy() {
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

bool SearchWindow::createWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = SEARCH_WINDOW_CLASS;
    RegisterClassExW(&wc);

    int width = 800;
    int height = 600;
    const RECT workArea = activeMonitorWorkArea();
    int x = workArea.left + (workArea.right - workArea.left - width) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - height) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        SEARCH_WINDOW_CLASS,
        L"EasyTools Search",
        WS_POPUP, // Borderless
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // Use layered window to support transparency
    SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY);

    return true;
}

void SearchWindow::initializeWebView2() {
    const uint64_t generation = ++m_generation;
    WebViewEnvironmentManager::instance().acquire(
            [this, generation](HRESULT result, ICoreWebView2Environment* env) {
                if (FAILED(result) || !env) {
                    LOG_ERROR("SearchWindow: shared environment unavailable.");
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

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            m_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                            }
                            
                            // Enable transparency
                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_controller.As(&controller2))) {
                                COREWEBVIEW2_COLOR transparent = {0, 255, 0, 255}; // magenta used as colorkey
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            RECT bounds;
                            GetClientRect(m_hwnd, &bounds);
                            m_controller->put_Bounds(bounds);

                            // ── 本地打包模式: 设置虚拟主机映射 ──────────────────────────────────
                            auto uiFolder = (easy::core::WinUtils::getExeDirectory() / L"ui").wstring();
                            std::error_code ec;
                            if (std::filesystem::exists(uiFolder, ec)) {
                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
                                if (SUCCEEDED(m_webView->QueryInterface(IID_PPV_ARGS(&webView3))) && webView3) {
                                    webView3->SetVirtualHostNameToFolderMapping(
                                        L"easytools.local", uiFolder.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                }
                            }

                            // ── 动态获取 UI 基础地址 ──────────────────────────────────────────
                            auto exeDir = easy::core::WinUtils::getExeDirectory();
                            auto indexPath = exeDir / L"ui" / L"index.html";
                            std::wstring baseUrl;
                            if (std::filesystem::exists(indexPath, ec)) {
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

                            // Load the search URL
                            std::wstring targetUrl = baseUrl + L"#/search";
                            m_webView->Navigate(targetUrl.c_str());

                            // Setup JS bridge
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
                                            LOG_ERROR("SearchWindow bridge error: {}", e.what());
                                        } catch (...) {
                                            LOG_ERROR("SearchWindow bridge unknown error");
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr);

                            m_webViewReady = true;
                            return S_OK;
                        }
                    ).Get());
                if (FAILED(controllerResult)) {
                    LOG_ERROR("SearchWindow: Create controller request failed, hr=0x{:08X}",
                              static_cast<unsigned>(controllerResult));
                }
            });
}

LRESULT CALLBACK SearchWindow::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto& inst = SearchWindow::instance();
    switch (uMsg) {
        case WM_SIZE:
            if (inst.m_controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                inst.m_controller->put_Bounds(bounds);
            }
            break;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostMessageW(hwnd, WM_SEARCH_VERIFY_DEACTIVATED, 0, 0);
            }
            break;
        case WM_SEARCH_VERIFY_DEACTIVATED: {
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
