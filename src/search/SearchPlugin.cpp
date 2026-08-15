#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include <windows.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <string>
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

constexpr const char* SearchPipe = "\\\\.\\pipe\\EasyToolsSearchPipe";

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

std::optional<std::string> querySearchService(const std::string& query, DWORD& error) {
    error = ERROR_SUCCESS;
    if (!WaitNamedPipeA(SearchPipe, 100)) {
        error = GetLastError();
        return std::nullopt;
    }

    HANDLE pipe = CreateFileA(SearchPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
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
            !finishOverlapped(pipe, writeOverlapped, 150, written, error)) {
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
            !finishOverlapped(pipe, readOverlapped, 350, bytesRead, error)) {
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
                return {{"results", nlohmann::json::array()}};
            }

            if (query.size() > 1024) query.resize(1024);
            DWORD pipeError = ERROR_SUCCESS;
            auto response = querySearchService(query, pipeError);

            if (response) {
                try {
                    auto result = nlohmann::json::parse(*response);
                    result["available"] = true;
                    return result;
                } catch (...) {
                    LOG_ERROR("SearchPlugin: 无法解析 JSON 结果");
                }
            } else {
                LOG_ERROR("SearchPlugin: 命名管道调用失败, error={}", pipeError);
            }

            return {
                {"results", nlohmann::json::array()},
                {"available", false},
                {"error", "search service unavailable"}
            };
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

        return true;
    }

    void shutdown() override {
        LOG_INFO("SearchPlugin: 关闭");
    }
};

} // namespace easy::search

extern "C" __declspec(dllexport) easy::core::IPlugin* CreatePlugin() {
    static easy::search::SearchPlugin instance;
    return &instance;
}
