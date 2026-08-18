#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <string>
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

constexpr const char* SearchPipe = "\\\\.\\pipe\\EasyToolsSearchPipe";

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

static bool ensureSearchServiceRunning() {
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
                           CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
            if (pi.hProcess) CloseHandle(pi.hProcess);
            if (pi.hThread) CloseHandle(pi.hThread);
        }
    }

    return WaitNamedPipeA(SearchPipe, 3000) != FALSE;
}

std::optional<std::string> querySearchService(const std::string& query, DWORD& error) {
    error = ERROR_SUCCESS;
    if (!WaitNamedPipeA(SearchPipe, 1000)) {
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

    DWORD readMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe, &readMode, nullptr, nullptr)) {
        error = GetLastError();
        return std::nullopt;
    }

    OVERLAPPED writeOverlapped{};
    writeOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!writeOverlapped.hEvent) {
        error = GetLastError();
        return std::nullopt;
    }
    struct EventGuard {
        HANDLE value;
        ~EventGuard() { if (value) CloseHandle(value); }
    } writeEvent{writeOverlapped.hEvent};

    DWORD written = 0;
    if (!WriteFile(pipe, query.data(), static_cast<DWORD>(query.size()), &written,
                   &writeOverlapped)) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING ||
            !finishOverlapped(pipe, writeOverlapped, 1500, written, error)) {
            return std::nullopt;
        }
    }
    if (written != query.size()) {
        error = ERROR_WRITE_FAULT;
        return std::nullopt;
    }

    std::vector<char> response(256 * 1024);
    OVERLAPPED readOverlapped{};
    readOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!readOverlapped.hEvent) {
        error = GetLastError();
        return std::nullopt;
    }
    EventGuard readEvent{readOverlapped.hEvent};
    DWORD bytesRead = 0;
    if (!ReadFile(pipe, response.data(), static_cast<DWORD>(response.size() - 1),
                  &bytesRead, &readOverlapped)) {
        error = GetLastError();
        if (error != ERROR_IO_PENDING ||
            !finishOverlapped(pipe, readOverlapped, 10000, bytesRead, error)) {
            return std::nullopt;
        }
    }
    if (bytesRead == 0) {
        error = ERROR_NO_DATA;
        return std::nullopt;
    }
    response[bytesRead] = '\0';
    return std::string(response.data(), bytesRead);
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
            if ((params.contains("drives") && params["drives"].is_array() && !params["drives"].empty()) ||
                (params.contains("excludes") && params["excludes"].is_array()) ||
                params.contains("excludeHidden") || params.contains("excludeSystem") ||
                params.contains("contentCustomExts") || params.contains("contentDisabledExts")) {
                nlohmann::json req;
                req["query"] = query;
                if (params.contains("drives")) req["drives"] = params["drives"];
                if (params.contains("excludes")) req["excludes"] = params["excludes"];
                if (params.contains("excludeHidden")) req["excludeHidden"] = params["excludeHidden"];
                if (params.contains("excludeSystem")) req["excludeSystem"] = params["excludeSystem"];
                if (params.contains("contentCustomExts")) req["contentCustomExts"] = params["contentCustomExts"];
                if (params.contains("contentDisabledExts")) req["contentDisabledExts"] = params["contentDisabledExts"];
                payload = req.dump();
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
            const std::string filepath = params.value("filepath", "");
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            std::error_code ec;
            if (!filepath.empty() && std::filesystem::exists(std::filesystem::path(widePath), ec)) {
                HINSTANCE result = ShellExecuteW(nullptr, L"open", widePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if ((INT_PTR)result <= 32) {
                    LOG_ERROR("SearchPlugin: 无法打开文件 {}, error={}", filepath, (INT_PTR)result);
                    return {{"success", false}};
                }
                return {{"success", true}};
            }
            return {{"success", false}};
        });

        mb.registerHandler("search.openFolder", [](const nlohmann::json& params) -> nlohmann::json {
            const std::string filepath = params.value("filepath", "");
            const auto widePath = easy::core::WinUtils::utf8ToWstring(filepath);
            std::error_code ec;
            if (!filepath.empty() && std::filesystem::exists(std::filesystem::path(widePath), ec)) {
                const std::wstring args = L"/select,\"" + widePath + L"\"";
                HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                if ((INT_PTR)result <= 32) {
                    LOG_ERROR("SearchPlugin: 无法在资源管理器中定位文件 {}, error={}", filepath, (INT_PTR)result);
                    return {{"success", false}};
                }
                return {{"success", true}};
            }
            return {{"success", false}};
        });

        mb.registerHandler("search.getServiceStatus", [](const nlohmann::json&) -> nlohmann::json {
            bool available = (WaitNamedPipeA(SearchPipe, 0) != FALSE);
            if (!available) {
                available = ensureSearchServiceRunning();
            }
            return {
                {"available", available},
                {"pipeName", SearchPipe}
            };
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

            return {
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
            return {{"success", true}};
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("SearchPlugin: 关闭");
        easy::core::MessageBridge::instance().unregisterHandlersByPrefix("search.");
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
