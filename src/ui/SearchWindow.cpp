#include "ui/SearchWindow.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/utils/DpiUtils.h"
#include "core/stats/PerformanceMonitor.h"
#include "ui/WebViewEnvironmentManager.h"
#include "ui/WebViewWindowStyle.h"
#include "ui/WebViewSecurity.h"
#include "ui/WebViewSuspend.h"
#include <WebView2.h>
#include <wrl/event.h>
#include <filesystem>
#include <fstream>
#include <utility>
#include <algorithm>
#include <chrono>
#include "core/utils/WinUtils.h"

using namespace Microsoft::WRL;

namespace easy::ui {

static constexpr const wchar_t* SEARCH_WINDOW_CLASS = L"EasyTools_SearchWindow";
static constexpr UINT WM_SEARCH_VERIFY_DEACTIVATED = WM_APP + 42;

namespace {

// 索引服务按需启动，预热 WebView 时并不拉起它。窗口真正呼出才是"用户要搜索"
// 的第一个可靠信号，此时把服务叫醒，启动耗时正好被用户的输入过程盖掉。
void warmUpSearchService() {
    easy::core::MessageBridge::instance().handleMessageAsync(
        R"({"id":0,"method":"search.warmup","params":{}})", [](std::string) {});
}

}  // namespace

SearchWindow& SearchWindow::instance() {
    static SearchWindow inst;
    return inst;
}

void SearchWindow::setWindowSize(int baseWidth, int baseHeight, bool forceCenter) {
    baseWidth = (std::clamp)(baseWidth, 500, 2400);
    baseHeight = (std::clamp)(baseHeight, 400, 1600);
    easy::core::ConfigManager::instance().set("/search/windowWidth", baseWidth);
    easy::core::ConfigManager::instance().set("/search/windowHeight", baseHeight);

    if (!m_hwnd || !IsWindow(m_hwnd)) return;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT workArea = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    int scaledWidth = easy::core::dpi::scaleMetric(baseWidth, scale);
    int scaledHeight = easy::core::dpi::scaleMetric(baseHeight, scale);
    const int margin = easy::core::dpi::scaleMetric(SearchWindowStyle::BaseScreenMargin, scale);
    scaledWidth = (std::min)(scaledWidth, static_cast<int>(workArea.right - workArea.left - margin * 2));
    scaledHeight = (std::min)(scaledHeight, static_cast<int>(workArea.bottom - workArea.top - margin * 2));

    int x = workArea.left + (workArea.right - workArea.left - scaledWidth) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - scaledHeight) / 2;

    if (!forceCenter) {
        RECT curRect{};
        if (GetWindowRect(m_hwnd, &curRect)) {
            // 保持当前窗口左上角 (x, y) 锚定固定，仅向右下方扩展
            int curX = curRect.left;
            int curY = curRect.top;

            // 检查当前坐标是否有效且在屏幕可视范围内
            if (curX >= workArea.left - 100 && curX < workArea.right - 100 &&
                curY >= workArea.top - 50 && curY < workArea.bottom - 100) {
                x = curX;
                y = curY;

                // 边界智能防溢出：如果向右下拉伸超出屏幕右侧或底部，自动向内微调
                if (x + scaledWidth > workArea.right - margin) {
                    x = (std::max)(static_cast<int>(workArea.left + margin), static_cast<int>(workArea.right - margin - scaledWidth));
                }
                if (y + scaledHeight > workArea.bottom - margin) {
                    y = (std::max)(static_cast<int>(workArea.top + margin), static_cast<int>(workArea.bottom - margin - scaledHeight));
                }
            }
        }
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, scaledWidth, scaledHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (m_controller) {
        RECT bounds{0, 0, scaledWidth, scaledHeight};
        m_controller->put_Bounds(bounds);
    }
}

std::pair<int, int> SearchWindow::getWindowSize() const {
    const int w = easy::core::ConfigManager::instance().get<int>("/search/windowWidth", SearchWindowStyle::BaseWidth);
    const int h = easy::core::ConfigManager::instance().get<int>("/search/windowHeight", SearchWindowStyle::BaseHeight);
    return {w, h};
}

void SearchWindow::preload(HINSTANCE hInstance) {
    if (m_hwnd && IsWindow(m_hwnd)) return;
    if (!createWindow(hInstance)) {
        LOG_ERROR("SearchWindow: preload createWindow failed");
        return;
    }
    initializeWebView2();
}

void SearchWindow::show(HINSTANCE hInstance) {
    const auto showStarted = std::chrono::steady_clock::now();
    const auto recordShown = [&showStarted]() {
        easy::core::PerformanceMonitor::instance().recordLatency(
            "search.hostShow",
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - showStarted).count());
    };
    m_showTimeTick = GetTickCount64();
    warmUpSearchService();
    if (m_hwnd && IsWindow(m_hwnd)) {
        if (m_visible) {
            updatePlacement();
            SetForegroundWindow(m_hwnd);
            SetFocus(m_hwnd);
            if (m_controller) {
                m_controller->put_IsVisible(TRUE);
                m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            }
            if (m_webView) {
                m_webView->ExecuteScript(L"window.dispatchEvent(new CustomEvent('easytools:focusSearch'))", nullptr);
            }
            recordShown();
            return;
        }
        updatePlacement();
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_visible = true;
        if (m_webView) m_suspendController.resume(m_webView.Get(), "search");
        if (m_controller) {
            m_controller->put_IsVisible(TRUE);
            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        if (m_webView) {
            m_webView->ExecuteScript(L"window.dispatchEvent(new CustomEvent('easytools:focusSearch'))", nullptr);
        }
        recordShown();
        return;
    }

    if (!createWindow(hInstance)) {
        LOG_ERROR("SearchWindow: createWindow failed");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_visible = true;
    recordShown();
}

void SearchWindow::hide() {
    if (!m_hwnd || !m_visible) return;
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
    if (m_controller) m_controller->put_IsVisible(FALSE);
    if (m_webView) m_suspendController.requestSuspend(m_webView.Get(), "search");
}

bool SearchWindow::isVisible() const {
    return m_visible;
}

void SearchWindow::destroy() {
    m_suspendController.abandon();
    m_webView = nullptr;
    m_controller = nullptr;
    m_environment = nullptr;

    const HWND hwnd = std::exchange(m_hwnd, nullptr);
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
    m_visible = false;
    m_webViewReady = false;
}

bool SearchWindow::createWindow(HINSTANCE hInstance) {
    m_suspendController.reset();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = SEARCH_WINDOW_CLASS;
    RegisterClassExW(&wc);

    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT workArea = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    const int baseW = easy::core::ConfigManager::instance().get<int>("/search/windowWidth", SearchWindowStyle::BaseWidth);
    const int baseH = easy::core::ConfigManager::instance().get<int>("/search/windowHeight", SearchWindowStyle::BaseHeight);
    const int customX = easy::core::ConfigManager::instance().get<int>("/search/windowX", -99999);
    const int customY = easy::core::ConfigManager::instance().get<int>("/search/windowY", -99999);
    SIZE size{easy::core::dpi::scaleMetric(baseW, scale), easy::core::dpi::scaleMetric(baseH, scale)};

    const int margin = easy::core::dpi::scaleMetric(
        SearchWindowStyle::BaseScreenMargin, scale);
    size.cx = (std::min)(size.cx, workArea.right - workArea.left - margin * 2);
    size.cy = (std::min)(size.cy, workArea.bottom - workArea.top - margin * 2);
    int x = workArea.left + (workArea.right - workArea.left - size.cx) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - size.cy) / 2;

    if (customX != -99999 && customY != -99999) {
        if (customX >= workArea.left - 100 && customX + size.cx <= workArea.right + 100 &&
            customY >= workArea.top - 20 && customY + size.cy <= workArea.bottom + 100) {
            x = customX;
            y = customY;
        }
    }

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        SEARCH_WINDOW_CLASS,
        L"EasyTools Search",
        WS_POPUP, // Borderless
        x, y, size.cx, size.cy,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // Use layered window to support transparency
    SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY);

    return true;
}

void SearchWindow::updatePlacement() {
    if (!m_hwnd || m_updatingPlacement) return;
    m_updatingPlacement = true;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT workArea = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    const int baseW = easy::core::ConfigManager::instance().get<int>("/search/windowWidth", SearchWindowStyle::BaseWidth);
    const int baseH = easy::core::ConfigManager::instance().get<int>("/search/windowHeight", SearchWindowStyle::BaseHeight);
    const int customX = easy::core::ConfigManager::instance().get<int>("/search/windowX", -99999);
    const int customY = easy::core::ConfigManager::instance().get<int>("/search/windowY", -99999);
    SIZE size{easy::core::dpi::scaleMetric(baseW, scale), easy::core::dpi::scaleMetric(baseH, scale)};

    const int margin = easy::core::dpi::scaleMetric(
        SearchWindowStyle::BaseScreenMargin, scale);
    size.cx = (std::min)(size.cx, workArea.right - workArea.left - margin * 2);
    size.cy = (std::min)(size.cy, workArea.bottom - workArea.top - margin * 2);
    int x = workArea.left + (workArea.right - workArea.left - size.cx) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - size.cy) / 2;

    if (customX != -99999 && customY != -99999) {
        if (customX >= workArea.left - 100 && customX + size.cx <= workArea.right + 100 &&
            customY >= workArea.top - 20 && customY + size.cy <= workArea.bottom + 100) {
            x = customX;
            y = customY;
        }
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (m_controller) {
        RECT bounds{0, 0, size.cx, size.cy};
        m_controller->put_Bounds(bounds);
    }
    m_updatingPlacement = false;
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
                            m_controller->put_IsVisible(m_visible.load() ? TRUE : FALSE);

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            m_webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                            }
                            
                            // Enable true alpha transparency
                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_controller.As(&controller2))) {
                                COREWEBVIEW2_COLOR transparent = {0, 0, 0, 0};
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            RECT bounds;
                            GetClientRect(m_hwnd, &bounds);
                            m_controller->put_Bounds(bounds);
                            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

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

                            web_security::applyNavigationPolicy(m_webView.Get());

                            // ── 动态获取 UI 基础地址 ──────────────────────────────────────────
                            auto exeDir = easy::core::WinUtils::getExeDirectory();
                            auto indexPath = exeDir / L"ui" / L"index.html";
                            std::wstring baseUrl;
                            if (std::filesystem::exists(indexPath, ec)) {
                                baseUrl = L"https://easytools.local/index.html";
                            } else {
#ifdef _DEBUG
                                auto devUrlPath = exeDir.parent_path().parent_path().parent_path() / L"ui" / L".dev-server-url";
                                if (std::filesystem::exists(devUrlPath, ec)) {
                                    std::ifstream file(devUrlPath);
                                    std::string url;
                                    if (std::getline(file, url) && !url.empty()) {
                                        baseUrl = easy::core::WinUtils::utf8ToWstring(url);
                                    }
                                }
                                if (baseUrl.empty()) baseUrl = L"http://localhost:5173";
#else
                                LOG_ERROR("SearchWindow: 打包 UI 缺失，已拒绝连接开发服务器");
                                baseUrl = L"https://easytools.local/index.html";
#endif
                            }

                            // Load the search URL
                            std::wstring targetUrl = baseUrl + L"#/search";
                            m_webView->Navigate(targetUrl.c_str());

                            // 导航完成后自动聚焦输入框
                            m_webView->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        if (m_controller) {
                                            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                                        }
                                        if (m_webView) {
                                            m_webView->ExecuteScript(L"window.dispatchEvent(new CustomEvent('easytools:focusSearch'));", nullptr);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

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
                                    [this, generation](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        try {
                                            if (!web_security::isTrustedMessageSource(args)) return S_OK;
                                            if (generation != m_generation.load()) return S_OK;
                                            PWSTR messageRaw = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&messageRaw)) && messageRaw) {
                                                 const std::string request = easy::core::WinUtils::wstringToUtf8(messageRaw);
                                                 CoTaskMemFree(messageRaw);
                                                 if (!web_security::isBridgeMethodAllowed(
                                                         request, web_security::Surface::Search)) return S_OK;
                                                // 搜索类请求要跨进程等待索引服务。同步执行会冻结 WebView2 的
                                                // UI 线程，让搜索框在整个等待期间无法接收键盘输入，因此改为
                                                // 在线程池中处理，完成后再回到 UI 线程投递响应。
                                                easy::core::MessageBridge::instance().handleMessageAsync(
                                                    request,
                                                    [this, generation](std::string response) {
                                                        easy::core::MainThreadDispatcher::instance().post(
                                                            [this, generation, response = std::move(response)]() {
                                                                if (generation != m_generation.load() || !m_webView) return;
                                                                const std::wstring wideResponse =
                                                                    easy::core::WinUtils::utf8ToWstring(response);
                                                                m_webView->PostWebMessageAsString(wideResponse.c_str());
                                                            });
                                                    });
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
                if (IsWindowVisible(hwnd)) {
                    inst.m_controller->put_IsVisible(TRUE);
                }
            }
            break;
        case WM_EXITSIZEMOVE: {
            RECT rc;
            if (GetWindowRect(hwnd, &rc)) {
                const HMONITOR monitor = easy::core::dpi::activeMonitor();
                const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
                const float scale = easy::core::dpi::scaleForDpi(dpi);
                int baseW = static_cast<int>((rc.right - rc.left) / scale);
                int baseH = static_cast<int>((rc.bottom - rc.top) / scale);
                if (baseW >= 400 && baseH >= 250) {
                    easy::core::ConfigManager::instance().set<int>("/search/windowWidth", baseW);
                    easy::core::ConfigManager::instance().set<int>("/search/windowHeight", baseH);
                    easy::core::ConfigManager::instance().set<int>("/search/windowX", rc.left);
                    easy::core::ConfigManager::instance().set<int>("/search/windowY", rc.top);
                }
            }
            break;
        }
        case WM_DPICHANGED: {
            if (!inst.m_updatingPlacement && lParam) {
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_DISPLAYCHANGE:
            if (IsWindowVisible(hwnd)) inst.updatePlacement();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostMessageW(hwnd, WM_SEARCH_VERIFY_DEACTIVATED, 0, 0);
            } else {
                if (inst.m_controller) {
                    inst.m_controller->put_IsVisible(TRUE);
                    inst.m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                }
                if (inst.m_webView) {
                    inst.m_webView->ExecuteScript(L"window.dispatchEvent(new CustomEvent('easytools:focusSearch'));", nullptr);
                }
            }
            break;
        case WM_SETFOCUS:
            if (inst.m_controller) {
                inst.m_controller->put_IsVisible(TRUE);
                inst.m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            }
            if (inst.m_webView) {
                inst.m_webView->ExecuteScript(L"window.dispatchEvent(new CustomEvent('easytools:focusSearch'));", nullptr);
            }
            return 0;
        case WM_CLOSE:
            inst.hide();
            return 0;
        case WM_SEARCH_VERIFY_DEACTIVATED: {
            if (!inst.m_visible.load()) break;
            if (inst.isMenuActive()) break;
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
