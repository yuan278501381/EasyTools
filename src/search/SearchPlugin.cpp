#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "search/ServiceLifetime.h"
#include "service/PipeProtocol.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <array>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace {

constexpr const char* SearchPipe = "\\\\.\\pipe\\EasyToolsSearchPipe";

// 本进程是否亲手拉起过索引服务。退出时据此决定要不要把它一并带走。
static std::atomic<bool> g_serviceSpawnedByUs{false};

static bool isServiceProcessRunning() {
    PROCESSENTRY32W pe32{};
    pe32.dwSize = sizeof(pe32);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    bool running = false;
    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, L"EasyTools_Service.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe32));
    }
    CloseHandle(snapshot);
    return running;
}

bool finishOverlapped(HANDLE pipe, OVERLAPPED& overlapped, DWORD timeoutMs,
                      DWORD& transferred, DWORD& error) {
    const DWORD wait = WaitForSingleObject(overlapped.hEvent, timeoutMs);
    if (wait == WAIT_OBJECT_0 &&
        GetOverlappedResult(pipe, &overlapped, &transferred, FALSE)) {
        return true;
    }
    error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    CancelIoEx(pipe, &overlapped);
    // OVERLAPPED storage and event must remain alive until cancellation has
    // completed, otherwise a late kernel completion can access freed memory.
    DWORD ignored = 0;
    GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
    return false;
}

/// 分块的重叠 I/O 收发器，复用同一个事件对象完成一整帧的传输。
class PipeTransfer {
public:
    PipeTransfer() : m_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~PipeTransfer() { if (m_event) CloseHandle(m_event); }
    PipeTransfer(const PipeTransfer&) = delete;
    PipeTransfer& operator=(const PipeTransfer&) = delete;

    bool valid() const { return m_event != nullptr; }

    bool readExact(HANDLE pipe, char* buffer, size_t bytes, DWORD timeoutMs, DWORD& error) {
        return transfer(pipe, buffer, bytes, timeoutMs, error, false);
    }

    bool writeExact(HANDLE pipe, const char* data, size_t bytes, DWORD timeoutMs, DWORD& error) {
        return transfer(pipe, const_cast<char*>(data), bytes, timeoutMs, error, true);
    }

private:
    static constexpr size_t ChunkBytes = 64 * 1024;

    bool transfer(HANDLE pipe, char* buffer, size_t bytes, DWORD timeoutMs,
                  DWORD& error, bool writing) {
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        size_t done = 0;
        while (done < bytes) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                error = ERROR_TIMEOUT;
                return false;
            }
            const DWORD remainMs = static_cast<DWORD>(deadline - now);
            const DWORD want = static_cast<DWORD>((std::min)(bytes - done, ChunkBytes));

            OVERLAPPED overlapped{};
            ResetEvent(m_event);
            overlapped.hEvent = m_event;

            DWORD moved = 0;
            const BOOL ok = writing
                ? WriteFile(pipe, buffer + done, want, &moved, &overlapped)
                : ReadFile(pipe, buffer + done, want, &moved, &overlapped);
            if (!ok) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING ||
                    !finishOverlapped(pipe, overlapped, remainMs, moved, error)) {
                    return false;
                }
            }
            if (moved == 0) {
                error = ERROR_NO_DATA;  // 对端已关闭
                return false;
            }
            done += moved;
        }
        return true;
    }

    HANDLE m_event;
};

// 服务是否由 SCM 托管。这种情况下它是用户显式安装的常驻服务，主程序退出时
// 无权把它关掉。
static bool isServiceManagedByScm() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    bool managed = false;
    SC_HANDLE service = OpenServiceW(scm, L"EasyTools_SearchService", SERVICE_QUERY_STATUS);
    if (service) {
        SERVICE_STATUS_PROCESS status{};
        DWORD bytesNeeded = 0;
        if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                 reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
            managed = status.dwCurrentState == SERVICE_RUNNING ||
                      status.dwCurrentState == SERVICE_START_PENDING;
        }
        CloseServiceHandle(service);
    }
    CloseServiceHandle(scm);
    return managed;
}

static bool ensureSearchServiceRunning() {
    if (WaitNamedPipeA(SearchPipe, 100)) {
        return true;
    }

    // 查询走线程池并发执行，若不串行化，多个线程会在服务建好管道之前的空窗期里
    // 各自拉起一个索引进程，每个都会建一份完整索引。
    static std::mutex launchMutex;
    std::lock_guard<std::mutex> launchGuard(launchMutex);

    if (WaitNamedPipeA(SearchPipe, 100)) {
        return true;
    }

    if (isServiceProcessRunning()) {
        return WaitNamedPipeA(SearchPipe, 1500) != FALSE;
    }

    // 1. 尝试通过 SCM 启动 Windows 服务 (如果已注册服务)
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, L"EasyTools_SearchService", SERVICE_START | SERVICE_QUERY_STATUS);
        if (service) {
            SERVICE_STATUS_PROCESS ssp{};
            DWORD bytesNeeded = 0;
            if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded)) {
                if (ssp.dwCurrentState != SERVICE_RUNNING && ssp.dwCurrentState != SERVICE_START_PENDING) {
                    StartServiceW(service, 0, nullptr);
                }
            }
            CloseServiceHandle(service);
        }
        CloseServiceHandle(scm);
    }

    if (WaitNamedPipeA(SearchPipe, 500)) {
        return true;
    }

    // 2. 尝试寻找同目录下的 EasyTools_Service.exe 作为独立后台进程自启动
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    std::filesystem::path serviceExe = exeDir / L"EasyTools_Service.exe";

    std::error_code ec;
    if (std::filesystem::exists(serviceExe, ec)) {
        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + serviceExe.wstring() + L"\"";
        if (CreateProcessW(serviceExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, exeDir.c_str(), &si, &pi)) {
            g_serviceSpawnedByUs.store(true);
            if (pi.hProcess) CloseHandle(pi.hProcess);
            if (pi.hThread) CloseHandle(pi.hThread);
        }
    }

    return WaitNamedPipeA(SearchPipe, 3000) != FALSE;
}

// autoStart 为 false 时，服务没在跑就直接放弃本次调用。历史记录、数据库统计这类
// 辅助数据会在搜索窗 WebView 预热完成的瞬间被前端拉一遍，若允许它们拉起服务，
// 用户从没按过搜索热键也会在开机时凭空多出几百 MB 的索引进程。
std::optional<std::string> querySearchService(const std::string& query, DWORD& error,
                                              bool autoStart = true) {
    error = ERROR_SUCCESS;
    if (!WaitNamedPipeA(SearchPipe, autoStart ? 1000 : 1)) {
        if (!autoStart) {
            error = ERROR_SERVICE_NOT_ACTIVE;
            return std::nullopt;
        }
        if (!ensureSearchServiceRunning()) {
            error = GetLastError();
            return std::nullopt;
        }
    }

    HANDLE pipe = CreateFileA(SearchPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (WaitNamedPipeA(SearchPipe, 1500)) {
            pipe = CreateFileA(SearchPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        }
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return std::nullopt;
    }
    struct PipeGuard {
        HANDLE value;
        ~PipeGuard() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } pipeGuard{pipe};

    namespace frame = easy::service::pipe;

    if (!frame::fitsInFrame(query.size(), frame::MaxRequestBytes)) {
        error = ERROR_INVALID_PARAMETER;
        return std::nullopt;
    }

    PipeTransfer transfer;
    if (!transfer.valid()) {
        error = GetLastError();
        return std::nullopt;
    }

    const auto requestHeader = frame::encodeFrameHeader(static_cast<uint32_t>(query.size()));
    if (!transfer.writeExact(pipe, requestHeader.data(), requestHeader.size(), 3000, error) ||
        !transfer.writeExact(pipe, query.data(), query.size(), 3000, error)) {
        return std::nullopt;
    }

    // 帧头到达即表示服务端已完成计算，因此这一步承担查询本身的等待时间。
    char responseHeader[frame::HeaderSize] = {};
    if (!transfer.readExact(pipe, responseHeader, sizeof(responseHeader), 15000, error)) {
        return std::nullopt;
    }

    uint32_t responseBytes = 0;
    if (!frame::decodeFrameHeader(responseHeader, responseBytes)) {
        error = ERROR_INVALID_DATA;
        return std::nullopt;
    }

    std::string response(responseBytes, '\0');
    if (!transfer.readExact(pipe, response.data(), responseBytes, 15000, error)) {
        return std::nullopt;
    }
    return response;
}

// 主程序退出时收尾。索引常驻要几百 MB，用户点了"退出 EasyTools"却留着它，
// 直到重启才释放，这不符合退出的语义；想常驻的人可以在设置里开回来。
static void stopSearchServiceIfOwned() {
    const easy::search::ServiceOwnership ownership{
        g_serviceSpawnedByUs.load(),
        isServiceManagedByScm(),
        easy::core::ConfigManager::instance().get<bool>("/search/keepServiceRunning", false),
    };
    if (!easy::search::shouldStopServiceOnExit(ownership)) return;

    nlohmann::json req;
    req["action"] = "shutdown";
    DWORD error = ERROR_SUCCESS;
    if (querySearchService(req.dump(), error, /*autoStart=*/false)) {
        LOG_INFO("SearchPlugin: 已请求索引服务停机");
    } else {
        LOG_WARN("SearchPlugin: 索引服务停机请求失败, error={}", error);
    }
}

}  // namespace

namespace easy::search {

class SearchPlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Search"; }
    const char* getVersion() const override { return easy::version::String; }

    bool initialize() override {
        LOG_INFO("SearchPlugin: 初始化搜索引擎");

        auto& mb = easy::core::MessageBridge::instance();
        
        mb.registerHandler("search.query", [](const nlohmann::json& params) -> nlohmann::json {
            std::string query = params.value("query", "");
            if (query.empty()) {
                return {{"results", nlohmann::json::array()}, {"available", true}};
            }

            if (query.size() > 1024) query.resize(1024);

            std::string payload;
            if (params.is_object()) {
                payload = params.dump();
            } else {
                payload = query;
            }

            DWORD pipeError = ERROR_SUCCESS;
            auto response = querySearchService(payload, pipeError);

            if (response) {
                try {
                    auto result = nlohmann::json::parse(*response);
                    result["available"] = true;
                    return result;
                } catch (...) {
                    LOG_ERROR("SearchPlugin: 无法解析 JSON 结果");
                }
            } else {
                LOG_WARN("SearchPlugin: 管道调用超时或返回空, error={}", pipeError);
            }

            bool isAlive = (pipeError == ERROR_TIMEOUT || isServiceProcessRunning());
            return {
                {"results", nlohmann::json::array()},
                {"available", isAlive},
                {"error", isAlive ? "search service busy" : "search service unavailable"}
            };
        });

        mb.registerHandler("search.rebuildIndex", [](const nlohmann::json&) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "rebuild";
            DWORD pipeError = ERROR_SUCCESS;
            auto resp = querySearchService(req.dump(), pipeError);
            if (resp) {
                try {
                    return nlohmann::json::parse(*resp);
                } catch (...) {}
            }
            return {{"success", false}};
        });

        mb.registerHandler("search.sync", [](const nlohmann::json&) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "catchup";
            DWORD pipeError = ERROR_SUCCESS;
            auto resp = querySearchService(req.dump(), pipeError);
            if (resp) {
                try {
                    return nlohmann::json::parse(*resp);
                } catch (...) {}
            }
            return {{"success", false}};
        });

        mb.registerHandler("search.getDrives", [](const nlohmann::json&) -> nlohmann::json {
            auto drives = easy::core::WinUtils::getSystemDrives();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& d : drives) {
                arr.push_back({
                    {"letter", std::string(1, d.letter)},
                    {"path", easy::core::WinUtils::wstringToUtf8(d.path)},
                    {"volumeLabel", easy::core::WinUtils::wstringToUtf8(d.volumeLabel)},
                    {"fileSystem", easy::core::WinUtils::wstringToUtf8(d.fileSystem)},
                    {"type", easy::core::WinUtils::wstringToUtf8(d.typeStr)},
                    {"totalBytes", d.totalBytes},
                    {"freeBytes", d.freeBytes}
                });
            }
            return arr;
        });

        mb.registerHandler("search.openFile", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            bool ok = easy::core::WinUtils::openFile(widePath);
            return {{"success", ok}};
        });

        mb.registerHandler("search.openFolder", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            bool ok = easy::core::WinUtils::openFolderAndSelectItem(widePath);
            return {{"success", ok}};
        });

        mb.registerHandler("search.openFileAsAdmin", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            bool ok = easy::core::WinUtils::openFileAsAdmin(widePath);
            return {{"success", ok}};
        });

        mb.registerHandler("search.showFileProperties", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            bool ok = easy::core::WinUtils::showFileProperties(widePath);
            return {{"success", ok}};
        });

        mb.registerHandler("search.openWithNotepad", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            bool ok = easy::core::WinUtils::openWithNotepad(widePath);
            return {{"success", ok}};
        });

        mb.registerHandler("search.renamePath", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string oldPath = params.value("oldPath", params.value("path", ""));
            const std::string newName = params.value("newName", params.value("name", ""));
            if (oldPath.empty() || newName.empty()) return {{"success", false}, {"error", "invalid parameters"}};
            
            const auto wideOld = easy::core::WinUtils::utf8ToWstring(oldPath);
            std::filesystem::path oldP(wideOld);
            std::error_code ec;
            if (!std::filesystem::exists(oldP, ec)) {
                return {{"success", false}, {"error", "源文件或目录不存在"}};
            }
            std::filesystem::path newP = oldP.parent_path() / easy::core::WinUtils::utf8ToWstring(newName);
            if (std::filesystem::exists(newP, ec)) {
                return {{"success", false}, {"error", "目标同名文件或目录已存在"}};
            }
            std::filesystem::rename(oldP, newP, ec);
            if (ec) {
                LOG_ERROR("SearchPlugin: 重命名失败 {} -> {}, error={}", oldPath, newName, ec.message());
                return {{"success", false}, {"error", ec.message()}};
            }
            return {
                {"success", true},
                {"newPath", easy::core::WinUtils::wstringToUtf8(newP.wstring())},
                {"newName", newName}
            };
        });

        mb.registerHandler("search.showShellContextMenu", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", params.value("path", ""));
            if (filepath.empty()) return {{"success", false}, {"error", "path is empty"}};
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);

            std::thread([widePath = std::move(widePath)]() {
                HWND hwndSearch = FindWindowW(L"EasyTools_SearchWindow", nullptr);
                if (hwndSearch) SetPropW(hwndSearch, L"EasyTools_ShellMenuActive", (HANDLE)1);

                HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

                PIDLIST_ABSOLUTE pidl = nullptr;
                HRESULT hr = SHParseDisplayName(widePath.c_str(), nullptr, &pidl, 0, nullptr);
                if (FAILED(hr) || !pidl) {
                    if (hwndSearch) RemovePropW(hwndSearch, L"EasyTools_ShellMenuActive");
                    if (SUCCEEDED(hrCom)) CoUninitialize();
                    return;
                }

                IShellFolder* pParentFolder = nullptr;
                PCUITEMID_CHILD pidlChild = nullptr;
                hr = SHBindToParent(pidl, IID_IShellFolder, (void**)&pParentFolder, &pidlChild);
                if (FAILED(hr) || !pParentFolder) {
                    CoTaskMemFree(pidl);
                    if (hwndSearch) RemovePropW(hwndSearch, L"EasyTools_ShellMenuActive");
                    if (SUCCEEDED(hrCom)) CoUninitialize();
                    return;
                }

                IContextMenu* pContextMenu = nullptr;
                hr = pParentFolder->GetUIObjectOf(nullptr, 1, &pidlChild, IID_IContextMenu, nullptr, (void**)&pContextMenu);
                if (SUCCEEDED(hr) && pContextMenu) {
                    HMENU hMenu = CreatePopupMenu();
                    if (hMenu) {
                        pContextMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL | CMF_EXPLORE);
                        POINT pt = { 0, 0 };
                        GetCursorPos(&pt);

                        static const wchar_t* CLASS_NAME = L"EasyTools_ShellMenuWnd_Plugin";
                        static thread_local IContextMenu2* t_pcm2 = nullptr;
                        static thread_local IContextMenu3* t_pcm3 = nullptr;

                        pContextMenu->QueryInterface(IID_IContextMenu2, (void**)&t_pcm2);
                        pContextMenu->QueryInterface(IID_IContextMenu3, (void**)&t_pcm3);

                        WNDCLASSEXW wc{};
                        wc.cbSize = sizeof(wc);
                        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
                            if (t_pcm3) {
                                LRESULT lres = 0;
                                if (SUCCEEDED(t_pcm3->HandleMenuMsg2(msg, wp, lp, &lres))) {
                                    return lres;
                                }
                            }
                            if (t_pcm2) {
                                if (SUCCEEDED(t_pcm2->HandleMenuMsg(msg, wp, lp))) {
                                    return 0;
                                }
                            }
                            switch (msg) {
                                case WM_INITMENUPOPUP:
                                case WM_DRAWITEM:
                                case WM_MEASUREITEM:
                                case WM_MENUCHAR:
                                    return 0;
                                default:
                                    return DefWindowProcW(hwnd, msg, wp, lp);
                            }
                        };
                        wc.hInstance = GetModuleHandleW(nullptr);
                        wc.lpszClassName = CLASS_NAME;
                        RegisterClassExW(&wc);

                        HWND helperWnd = CreateWindowExW(
                            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            CLASS_NAME, L"",
                            WS_POPUP,
                            pt.x, pt.y, 0, 0,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
                        );

                        if (helperWnd) {
                            SetForegroundWindow(helperWnd);
                        }

                        UINT cmd = TrackPopupMenuEx(
                            hMenu,
                            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                            pt.x, pt.y,
                            helperWnd ? helperWnd : GetForegroundWindow(),
                            nullptr
                        );

                        if (cmd >= 1) {
                            CMINVOKECOMMANDINFOEX info{};
                            info.cbSize = sizeof(info);
                            info.fMask = CMIC_MASK_UNICODE;
                            info.hwnd = helperWnd;
                            info.lpVerb = (LPCSTR)MAKEINTRESOURCEA(cmd - 1);
                            info.lpVerbW = (LPCWSTR)MAKEINTRESOURCEW(cmd - 1);
                            info.nShow = SW_SHOWNORMAL;
                            pContextMenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
                        }

                        if (t_pcm3) { t_pcm3->Release(); t_pcm3 = nullptr; }
                        if (t_pcm2) { t_pcm2->Release(); t_pcm2 = nullptr; }

                        if (helperWnd && IsWindow(helperWnd)) {
                            DestroyWindow(helperWnd);
                        }
                        DestroyMenu(hMenu);
                    }
                    pContextMenu->Release();
                }

                pParentFolder->Release();
                CoTaskMemFree(pidl);

                if (hwndSearch && IsWindow(hwndSearch)) {
                    SetForegroundWindow(hwndSearch);
                    SetFocus(hwndSearch);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (hwndSearch && IsWindow(hwndSearch)) {
                    RemovePropW(hwndSearch, L"EasyTools_ShellMenuActive");
                }
                if (SUCCEEDED(hrCom)) CoUninitialize();
            }).detach();

            return {{"success", true}};
        });

        mb.registerHandler("search.startDrag", [](const nlohmann::json&) -> nlohmann::json {
            HWND hwnd = FindWindowW(L"EasyTools_SearchWindow", nullptr);
            if (hwnd && IsWindow(hwnd)) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            return {{"success", true}};
        });

        mb.registerHandler("search.startResize", [](const nlohmann::json& params) -> nlohmann::json {
            std::string dir = params.value("direction", "se");
            HWND hwnd = FindWindowW(L"EasyTools_SearchWindow", nullptr);
            if (hwnd && IsWindow(hwnd)) {
                WPARAM hitTest = HTBOTTOMRIGHT;
                if (dir == "se") hitTest = HTBOTTOMRIGHT;
                else if (dir == "e") hitTest = HTRIGHT;
                else if (dir == "s") hitTest = HTBOTTOM;
                else if (dir == "w") hitTest = HTLEFT;
                else if (dir == "sw") hitTest = HTBOTTOMLEFT;
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, hitTest, 0);
            }
            return {{"success", true}};
        });

        mb.registerHandler("search.resetPlacement", [](const nlohmann::json&) -> nlohmann::json {
            easy::core::ConfigManager::instance().set<int>("/search/windowWidth", 760);
            easy::core::ConfigManager::instance().set<int>("/search/windowHeight", 520);
            easy::core::ConfigManager::instance().set<int>("/search/windowX", -99999);
            easy::core::ConfigManager::instance().set<int>("/search/windowY", -99999);
            HWND hwnd = FindWindowW(L"EasyTools_SearchWindow", nullptr);
            if (hwnd && IsWindow(hwnd)) {
                PostMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
            }
            return {{"success", true}};
        });

        // 只报告状态，不再顺手拉起服务：设置页一打开就会查一次状态，那不足以
        // 说明用户要用搜索。需要拉起时走 search.warmup。
        mb.registerHandler("search.getServiceStatus", [](const nlohmann::json&) -> nlohmann::json {
            return {
                {"available", WaitNamedPipeA(SearchPipe, 1) != FALSE},
                {"pipeName", SearchPipe}
            };
        });

        // 搜索窗真正呼出时才把索引服务拉起来。用户此刻正在打字，几秒的启动时间
        // 被输入过程盖掉；而预热 WebView 时不触发，开机静默驻留就不必背这份内存。
        mb.registerHandler("search.warmup", [](const nlohmann::json&) -> nlohmann::json {
            return {{"available", ensureSearchServiceRunning()}};
        });

        mb.registerHandler("search.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& cfg = easy::core::ConfigManager::instance();
            std::string hotkey = cfg.get<std::string>("/hotkeys/Toggle Search", "Alt+Space");
            int maxResults = cfg.get<int>("/search/maxResults", 50);
            std::string defaultCategory = cfg.get<std::string>("/search/defaultCategory", "all");
            bool caseSensitive = cfg.get<bool>("/search/caseSensitive", false);
            bool matchPath = cfg.get<bool>("/search/matchPath", false);
            bool pinyinEnabled = cfg.get<bool>("/search/pinyinEnabled", true);
            std::string enabledDrives = cfg.get<std::string>("/search/enabledDrives", "");
            std::string excludePatterns = cfg.get<std::string>("/search/excludePatterns", "$Recycle.Bin,System Volume Information,node_modules,.git,__pycache__");
            bool excludeHidden = cfg.get<bool>("/search/excludeHidden", false);
            bool excludeSystem = cfg.get<bool>("/search/excludeSystem", false);
            bool keepServiceRunning = cfg.get<bool>("/search/keepServiceRunning", false);

            return {
                {"keepServiceRunning", keepServiceRunning},
                {"hotkey", hotkey},
                {"maxResults", maxResults},
                {"defaultCategory", defaultCategory},
                {"caseSensitive", caseSensitive},
                {"matchPath", matchPath},
                {"pinyinEnabled", pinyinEnabled},
                {"enabledDrives", enabledDrives},
                {"excludePatterns", excludePatterns},
                {"excludeHidden", excludeHidden},
                {"excludeSystem", excludeSystem}
            };
        });

        mb.registerHandler("search.saveSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& cfg = easy::core::ConfigManager::instance();
            if (params.contains("hotkey") && params["hotkey"].is_string()) {
                cfg.set("/hotkeys/Toggle Search", params["hotkey"].get<std::string>());
            }
            if (params.contains("maxResults") && params["maxResults"].is_number()) {
                cfg.set("/search/maxResults", params["maxResults"].get<int>());
            }
            if (params.contains("defaultCategory") && params["defaultCategory"].is_string()) {
                cfg.set("/search/defaultCategory", params["defaultCategory"].get<std::string>());
            }
            if (params.contains("caseSensitive") && params["caseSensitive"].is_boolean()) {
                cfg.set("/search/caseSensitive", params["caseSensitive"].get<bool>());
            }
            if (params.contains("matchPath") && params["matchPath"].is_boolean()) {
                cfg.set("/search/matchPath", params["matchPath"].get<bool>());
            }
            if (params.contains("pinyinEnabled") && params["pinyinEnabled"].is_boolean()) {
                cfg.set("/search/pinyinEnabled", params["pinyinEnabled"].get<bool>());
            }
            if (params.contains("enabledDrives") && params["enabledDrives"].is_string()) {
                cfg.set("/search/enabledDrives", params["enabledDrives"].get<std::string>());
            }
            if (params.contains("excludePatterns") && params["excludePatterns"].is_string()) {
                cfg.set("/search/excludePatterns", params["excludePatterns"].get<std::string>());
            }
            if (params.contains("excludeHidden") && params["excludeHidden"].is_boolean()) {
                cfg.set("/search/excludeHidden", params["excludeHidden"].get<bool>());
            }
            if (params.contains("excludeSystem") && params["excludeSystem"].is_boolean()) {
                cfg.set("/search/excludeSystem", params["excludeSystem"].get<bool>());
            }
            if (params.contains("keepServiceRunning") && params["keepServiceRunning"].is_boolean()) {
                cfg.set("/search/keepServiceRunning", params["keepServiceRunning"].get<bool>());
            }
            return {{"success", true}};
        });

        mb.registerHandler("search.recordRun", [](const nlohmann::json& params) -> nlohmann::json {
            std::string path = params.value("path", "");
            if (path.empty()) return {{"success", false}};
            nlohmann::json req;
            req["action"] = "recordRun";
            req["path"] = path;
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err, /*autoStart=*/false);
            return {{"success", res.has_value()}};
        });

        mb.registerHandler("search.recordSearch", [](const nlohmann::json& params) -> nlohmann::json {
            std::string query = params.value("query", "");
            if (query.empty()) return {{"success", false}};
            nlohmann::json req;
            req["action"] = "recordSearch";
            req["query"] = query;
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err, /*autoStart=*/false);
            return {{"success", res.has_value()}};
        });

        mb.registerHandler("search.getSearchHistory", [](const nlohmann::json& params) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "getSearchHistory";
            if (params.contains("limit")) req["limit"] = params["limit"];
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err, /*autoStart=*/false);
            if (res && !res->empty()) {
                try {
                    return nlohmann::json::parse(*res);
                } catch (...) {}
            }
            return {{"success", false}, {"history", nlohmann::json::array()}};
        });

        mb.registerHandler("search.removeSearchHistory", [](const nlohmann::json& params) -> nlohmann::json {
            std::string search = params.value("search", "");
            nlohmann::json req;
            req["action"] = "removeSearchHistory";
            req["search"] = search;
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err);
            return {{"success", res.has_value()}};
        });

        mb.registerHandler("search.clearSearchHistory", [](const nlohmann::json&) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "clearSearchHistory";
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err);
            return {{"success", res.has_value()}};
        });

        mb.registerHandler("search.getDbStats", [](const nlohmann::json&) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "getDbStats";
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err, /*autoStart=*/false);
            if (res && !res->empty()) {
                try {
                    return nlohmann::json::parse(*res);
                } catch (...) {}
            }
            return {{"success", false}};
        });

        mb.registerHandler("search.saveSnapshot", [](const nlohmann::json&) -> nlohmann::json {
            nlohmann::json req;
            req["action"] = "saveSnapshot";
            DWORD err = 0;
            auto res = querySearchService(req.dump(), err);
            return {{"success", res.has_value()}};
        });

        // 这些处理器全部要跨进程等待搜索服务，耗时由索引规模和磁盘决定。放在
        // WebView2 的 UI 线程上同步执行会让搜索窗口在整个等待期间无法响应键盘
        // 输入。窗口操作类处理器（startDrag/startResize/右键菜单等）有线程亲和
        // 性，必须留在同步路径上。
        for (const char* method : {
                 "search.query",
                 "search.sync",
                 "search.rebuildIndex",
                 "search.getSearchHistory",
                 "search.getDbStats",
                 "search.saveSnapshot",
                 "search.warmup",
             }) {
            mb.markMethodAsync(method);
        }

        return true;
    }

    void shutdown() override {
        LOG_INFO("SearchPlugin: 关闭");
        easy::core::MessageBridge::instance().unregisterHandlersByPrefix("search.");
        stopSearchServiceIfOwned();
    }
};

} // namespace easy::search

extern "C" __declspec(dllexport) easy::core::IPlugin* CreatePlugin() {
    static easy::search::SearchPlugin instance;
    return &instance;
}

extern "C" __declspec(dllexport) std::uint32_t GetPluginAbiVersion() {
    return easy::core::CurrentPluginAbiVersion;
}
