#include "ui/TrayWindow.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include <WebView2EnvironmentOptions.h>
#include <wrl/event.h>
#include <filesystem>
#include <fstream>
#include "core/utils/WinUtils.h"

using namespace Microsoft::WRL;

namespace easy::ui {

static constexpr const wchar_t* TRAY_WINDOW_CLASS = L"EasyTools_TrayWindow";

TrayWindow& TrayWindow::instance() {
    static TrayWindow inst;
    return inst;
}

void TrayWindow::show(HINSTANCE hInstance, int x, int y) {
    if (m_hwnd && IsWindow(m_hwnd)) {
        // 更新位置
        int width = 240;
        int height = 320;
        // 修正坐标使其靠在右下角（如果 y 在任务栏，通常向上弹）
        int adjustedX = x - width / 2;
        int adjustedY = y - height;
        if (adjustedX < 0) adjustedX = 0;
        if (adjustedY < 0) adjustedY = 0;
        
        SetWindowPos(m_hwnd, HWND_TOPMOST, adjustedX, adjustedY, width, height, SWP_SHOWWINDOW);
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

    int width = 240;  // 托盘菜单宽度
    int height = 320; // 托盘菜单高度

    // 调整坐标，确保弹出方向合理（通常向左上）
    int adjustedX = x - width / 2;
    int adjustedY = y - height;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    if (adjustedX + width > screenW) adjustedX = screenW - width - 10;
    if (adjustedY < 0) adjustedY = 10;

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        TRAY_WINDOW_CLASS,
        L"EasyTools TrayMenu",
        WS_POPUP, // 无边框
        adjustedX, adjustedY, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // 启用分层透明，使得毛玻璃边界可以渲染，并避免底色黑块
    SetLayeredWindowAttributes(m_hwnd, RGB(255, 0, 255), 255, LWA_COLORKEY);

    return true;
}

void TrayWindow::initializeWebView2() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path userDataFolder = std::filesystem::path(exePath).parent_path() / L"webview2_data_tray";

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(L"--force-dark-mode --allow-file-access-from-files");

    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    LOG_ERROR("TrayWindow: Create Env failed.");
                    return E_FAIL;
                }
                m_environment = env;
                m_environment->CreateCoreWebView2Controller(
                    m_hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(res) || !controller) return E_FAIL;
                            m_controller = controller;
                            m_controller->get_CoreWebView2(&m_webView);
                            
                            if (m_visible.load()) {
                                m_controller->put_IsVisible(TRUE);
                            } else {
                                m_controller->put_IsVisible(FALSE);
                            }
                            
                            // 开启透明背景
                            Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                            if (SUCCEEDED(m_controller.As(&controller2))) {
                                COREWEBVIEW2_COLOR transparent = {0, 255, 0, 255}; // magenta as colorkey
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
                            }

                            // 加载 Tray URL
                            auto exeDir = easy::core::WinUtils::getExeDirectory();
                            auto indexPath = exeDir / L"ui" / L"index.html";
                            std::wstring baseUrl;
                            std::error_code ec;
                            if (std::filesystem::exists(indexPath, ec)) {
                                std::string pathStr = easy::core::WinUtils::wstringToUtf8(indexPath.wstring());
                                std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
                                baseUrl = easy::core::WinUtils::utf8ToWstring("file:///" + pathStr);
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
                                    [targetUrl](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
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
                                    [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        PWSTR messageRaw;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&messageRaw))) {
                                            std::wstring wmsg(messageRaw);
                                            CoTaskMemFree(messageRaw);
                                            
                                            int size = WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, nullptr, 0, nullptr, nullptr);
                                            std::string jsonStr(size, 0);
                                            WideCharToMultiByte(CP_UTF8, 0, wmsg.c_str(), -1, &jsonStr[0], size, nullptr, nullptr);
                                            
                                            std::string response = easy::core::MessageBridge::instance().handleMessage(jsonStr.c_str());
                                            std::wstring wResponse = easy::core::WinUtils::utf8ToWstring(response);
                                            sender->PostWebMessageAsString(wResponse.c_str());
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr);

                            m_webViewReady = true;
                            return S_OK;
                        }
                    ).Get());
                return S_OK;
            }
        ).Get());
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
        case WM_KILLFOCUS:
            inst.hide();
            break;
        case WM_DESTROY:
            inst.destroy();
            break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace easy::ui
