#include "ui/QuickLookWindow.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include "ui/WebViewEnvironmentManager.h"
#include "ui/WebViewWindowStyle.h"
#include <WebView2.h>
#include <wrl/event.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

using namespace Microsoft::WRL;
using json = nlohmann::json;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#define DWMSBT_MAINWINDOW 2
#define DWMSBT_TRANSIENTWINDOW 3
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

namespace easy::ui {

static constexpr const wchar_t* QUICKLOOK_WINDOW_CLASS = L"EasyTools_QuickLookWindow";

namespace {

std::string formatFileSize(uintmax_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double kb = bytes / 1024.0;
    if (kb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << kb << " KB";
        return ss.str();
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << mb << " MB";
        return ss.str();
    }
    double gb = mb / 1024.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << gb << " GB";
    return ss.str();
}

std::string formatTimePoint(std::filesystem::file_time_type ftime) {
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t cftime = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
        localtime_s(&tm, &cftime);
        char buf[64] = {0};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    } catch (...) {
        return "-";
    }
}

std::string detectFileType(const std::filesystem::path& p) {
    if (std::filesystem::is_directory(p)) return "folder";
    std::string ext = easy::core::WinUtils::toLower(p.extension().string());
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

    if (ext == "md" || ext == "markdown" || ext == "mdown" || ext == "mkd") return "markdown";
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "gif" || ext == "bmp" || ext == "svg" || ext == "ico") return "image";
    if (ext == "mp4" || ext == "webm" || ext == "mkv" || ext == "mov" || ext == "avi") return "video";
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac" || ext == "aac" || ext == "m4a") return "audio";
    if (ext == "pdf") return "pdf";
    if (ext == "json" || ext == "js" || ext == "ts" || ext == "tsx" || ext == "jsx" ||
        ext == "cpp" || ext == "c" || ext == "h" || ext == "hpp" || ext == "cs" ||
        ext == "py" || ext == "rs" || ext == "go" || ext == "java" || ext == "html" ||
        ext == "css" || ext == "xml" || ext == "yaml" || ext == "yml" || ext == "sql" ||
        ext == "sh" || ext == "ps1" || ext == "bat" || ext == "cmd" || ext == "toml" ||
        ext == "ini" || ext == "env" || ext == "log" || ext == "txt" || ext == "gitignore") {
        return "code";
    }
    return "binary";
}

json generateFilePreviewPayload(const std::wstring& filePath) {
    std::error_code ec;
    std::filesystem::path p(filePath);
    if (!std::filesystem::exists(p, ec)) {
        return {
            {"exists", false},
            {"path", easy::core::WinUtils::wstringToUtf8(filePath)},
            {"error", "file not found"}
        };
    }

    bool isDir = std::filesystem::is_directory(p, ec);
    std::string ext = isDir ? "" : easy::core::WinUtils::toLower(p.extension().string());
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::string type = detectFileType(p);

    uintmax_t sizeBytes = 0;
    if (!isDir) {
        sizeBytes = std::filesystem::file_size(p, ec);
    }

    std::string modifiedTime = formatTimePoint(std::filesystem::last_write_time(p, ec));

    json payload = {
        {"exists", true},
        {"path", easy::core::WinUtils::wstringToUtf8(filePath)},
        {"name", easy::core::WinUtils::wstringToUtf8(p.filename().wstring())},
        {"extension", ext},
        {"type", type},
        {"isDirectory", isDir},
        {"size", sizeBytes},
        {"formattedSize", isDir ? "文件夹" : formatFileSize(sizeBytes)},
        {"modified", modifiedTime}
    };

    if (isDir) {
        json children = json::array();
        size_t count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(p, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (count >= 50) break;
            bool childIsDir = entry.is_directory(ec);
            uintmax_t childSize = childIsDir ? 0 : entry.file_size(ec);
            children.push_back({
                {"name", easy::core::WinUtils::wstringToUtf8(entry.path().filename().wstring())},
                {"isDirectory", childIsDir},
                {"size", childSize},
                {"formattedSize", childIsDir ? "-" : formatFileSize(childSize)}
            });
            count++;
        }
        payload["folderChildren"] = children;
    } else if (type == "markdown" || type == "code" || type == "binary") {
        if (sizeBytes < 5 * 1024 * 1024) { // 5MB limit for text preview
            std::ifstream file(filePath, std::ios::binary);
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                if (type == "binary") {
                    // Check if UTF-8 readable text
                    bool isText = true;
                    for (size_t i = 0; i < (std::min)(content.size(), size_t(512)); ++i) {
                        if (content[i] == 0) { isText = false; break; }
                    }
                    if (isText) {
                        payload["type"] = "code";
                        payload["content"] = content;
                    } else {
                        // Provide hex sample
                        std::ostringstream hexStream;
                        for (size_t i = 0; i < (std::min)(content.size(), size_t(512)); ++i) {
                            hexStream << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(content[i]) & 0xFF) << " ";
                            if ((i + 1) % 16 == 0) hexStream << "\n";
                        }
                        payload["hexDump"] = hexStream.str();
                    }
                } else {
                    payload["content"] = content;
                }
            }
        }
    } else if (type == "image") {
        if (sizeBytes < 30 * 1024 * 1024) {
            std::ifstream file(filePath, std::ios::binary);
            if (file.is_open()) {
                std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                std::string mime = "image/" + (ext == "svg" ? "svg+xml" : (ext == "jpg" ? "jpeg" : ext));
                payload["dataUri"] = "data:" + mime + ";base64," + easy::core::WinUtils::base64Encode(buffer);
            }
        }
    }

    return payload;
}

} // namespace

QuickLookWindow& QuickLookWindow::instance() {
    static QuickLookWindow inst;
    return inst;
}

void QuickLookWindow::show(const std::wstring& filePath, HINSTANCE hInstance) {
    m_currentFilePath = filePath;
    m_pendingFilePath = filePath;

    if (m_hwnd && IsWindow(m_hwnd)) {
        updatePlacement();
        ShowWindow(m_hwnd, SW_SHOW);
        SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        m_visible = true;

        if (m_controller) {
            m_controller->put_IsVisible(TRUE);
            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        previewFile(filePath);
        return;
    }

    if (!createWindow(hInstance ? hInstance : GetModuleHandleW(nullptr))) {
        LOG_ERROR("QuickLookWindow: createWindow failed");
        return;
    }

    initializeWebView2();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(m_hwnd);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    m_visible = true;
}

void QuickLookWindow::previewFile(const std::wstring& filePath) {
    m_currentFilePath = filePath;
    m_pendingFilePath = filePath;

    if (m_webView && m_webViewReady) {
        json payload = generateFilePreviewPayload(filePath);
        std::string envelope = R"({"type":"event","event":"quicklook.fileChanged","data":)" + payload.dump() + "}";
        std::wstring wMsg = easy::core::WinUtils::utf8ToWstring(envelope);
        m_webView->PostWebMessageAsString(wMsg.c_str());
    }
}

void QuickLookWindow::hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
        if (m_controller) m_controller->put_IsVisible(FALSE);
    }
}

bool QuickLookWindow::isVisible() const {
    return m_visible.load() && m_hwnd && IsWindowVisible(m_hwnd);
}

void QuickLookWindow::destroy() {
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

void QuickLookWindow::updatePlacement() {
    if (!m_hwnd) return;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT work = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    int targetW = easy::core::dpi::scaleMetric(1060, scale);
    int targetH = easy::core::dpi::scaleMetric(740, scale);

    const int maxW = static_cast<int>(work.right - work.left) - 40;
    const int maxH = static_cast<int>(work.bottom - work.top) - 40;
    targetW = (std::min)(targetW, maxW);
    targetH = (std::min)(targetH, maxH);

    int x = work.left + (work.right - work.left - targetW) / 2;
    int y = work.top + (work.bottom - work.top - targetH) / 2;

    SetWindowPos(m_hwnd, HWND_TOP, x, y, targetW, targetH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

bool QuickLookWindow::createWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 24, 28));
    wc.lpszClassName = QUICKLOOK_WINDOW_CLASS;
    RegisterClassExW(&wc);

    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT work = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);

    int targetW = easy::core::dpi::scaleMetric(1060, scale);
    int targetH = easy::core::dpi::scaleMetric(740, scale);
    int x = work.left + (work.right - work.left - targetW) / 2;
    int y = work.top + (work.bottom - work.top - targetH) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        QUICKLOOK_WINDOW_CLASS,
        L"EasyTools QuickLook",
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x, y, targetW, targetH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("QuickLookWindow: CreateWindowExW failed, err={}", GetLastError());
        return false;
    }

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
    DWORD backdropType = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

void QuickLookWindow::initializeWebView2() {
    const uint64_t generation = ++m_generation;
    WebViewEnvironmentManager::instance().acquire(
        [this, generation](HRESULT result, ICoreWebView2Environment* environment) {
            if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) return;
            if (FAILED(result) || !environment) {
                LOG_ERROR("QuickLookWindow: acquire environment failed");
                return;
            }

            m_environment = environment;
            environment->CreateCoreWebView2Controller(
                m_hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this, generation](HRESULT readyResult, ICoreWebView2Controller* controller) -> HRESULT {
                        if (generation != m_generation.load() || !m_hwnd || !IsWindow(m_hwnd)) {
                            if (controller) controller->Close();
                            return S_OK;
                        }
                        if (FAILED(readyResult) || !controller) {
                            LOG_ERROR("QuickLookWindow: CreateCoreWebView2Controller failed");
                            return readyResult;
                        }
                        m_controller = controller;
                        controller->get_CoreWebView2(&m_webView);

                        if (m_visible.load()) {
                            m_controller->put_IsVisible(TRUE);
                        } else {
                            m_controller->put_IsVisible(FALSE);
                        }

                        RECT bounds;
                        GetClientRect(m_hwnd, &bounds);
                        m_controller->put_Bounds(bounds);

                        ComPtr<ICoreWebView2Controller2> controller2;
                        if (SUCCEEDED(m_controller.As(&controller2)) && controller2) {
                            COREWEBVIEW2_COLOR transparentColor = { 0, 0, 0, 0 };
                            controller2->put_DefaultBackgroundColor(transparentColor);
                        }

                        ComPtr<ICoreWebView2Settings> settings;
                        m_webView->get_Settings(&settings);
                        if (settings) {
                            settings->put_IsScriptEnabled(TRUE);
                            settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                            settings->put_IsWebMessageEnabled(TRUE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(FALSE);
                        }

                        const auto uiFolder = (easy::core::WinUtils::getExeDirectory() / L"ui").wstring();
                        ComPtr<ICoreWebView2_3> webView3;
                        if (SUCCEEDED(m_webView.As(&webView3)) && webView3) {
                            webView3->SetVirtualHostNameToFolderMapping(
                                L"easytools.local", uiFolder.c_str(),
                                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                        }

                        // JS -> C++ IPC Message Bridge
                        m_webView->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    try {
                                        LPWSTR msgRaw = nullptr;
                                        args->TryGetWebMessageAsString(&msgRaw);
                                        if (msgRaw) {
                                            std::string message = easy::core::WinUtils::wstringToUtf8(msgRaw);
                                            CoTaskMemFree(msgRaw);
                                            std::string response = easy::core::MessageBridge::instance().handleMessage(message);
                                            std::wstring wResponse = easy::core::WinUtils::utf8ToWstring(response);
                                            sender->PostWebMessageAsString(wResponse.c_str());
                                        }
                                    } catch (...) {}
                                    return S_OK;
                                }
                            ).Get(), nullptr
                        );

                        EventRegistrationToken token;
                        m_webView->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success;
                                    args->get_IsSuccess(&success);
                                    if (success) {
                                        m_webViewReady = true;
                                        if (!m_pendingFilePath.empty()) {
                                            previewFile(m_pendingFilePath);
                                        }
                                    }
                                    return S_OK;
                                }
                            ).Get(), &token);

                        std::string targetUrl = "https://easytools.local/index.html?quicklook=1";
                        auto devUrlPath = easy::core::WinUtils::getExeDirectory().parent_path().parent_path().parent_path() / L"ui" / L".dev-server-url";
                        std::error_code ec;
                        if (std::filesystem::exists(devUrlPath, ec)) {
                            std::ifstream f(devUrlPath);
                            std::string u;
                            if (std::getline(f, u) && !u.empty()) {
                                targetUrl = u + "?quicklook=1";
                            }
                        }
                        std::wstring wUrl = easy::core::WinUtils::utf8ToWstring(targetUrl);
                        m_webView->Navigate(wUrl.c_str());
                        return S_OK;
                    }
                ).Get()
            );
        }
    );
}

LRESULT CALLBACK QuickLookWindow::windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<QuickLookWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
        case WM_SIZE: {
            if (self && self->m_controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                self->m_controller->put_Bounds(bounds);
                if (IsWindowVisible(hwnd)) {
                    self->m_controller->put_IsVisible(TRUE);
                }
            }
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                // 点击外部窗口时自动隐藏 QuickLook，体验与 macOS 一致
                if (self && self->isVisible()) {
                    self->hide();
                }
            }
            break;
        }
        case WM_CLOSE: {
            if (self) self->hide();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace easy::ui
