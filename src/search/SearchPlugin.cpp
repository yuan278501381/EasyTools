#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ShellContextMenuService.h"
#include "search/ServiceLifetime.h"
#include "search/ServiceStartupPolicy.h"
#include "service/PipeProtocol.h"
#include "service/PipeEndpoint.h"
#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <string>
#include <array>
#include <bcrypt.h>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

namespace {

std::string g_searchPipe;
std::string g_searchPipeToken;
std::wstring g_searchClientSid;

// 本进程是否亲手拉起过索引服务。退出时据此决定要不要把它一并带走。
static std::atomic<bool> g_serviceSpawnedByUs{false};

std::optional<std::wstring> currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;
    struct TokenGuard { HANDLE value; ~TokenGuard() { if (value) CloseHandle(value); } } guard{token};

    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (!bytes) return std::nullopt;
    std::vector<std::byte> buffer(bytes);
    if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) return std::nullopt;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR rawSid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &rawSid) || !rawSid) return std::nullopt;
    std::wstring sid(rawSid);
    LocalFree(rawSid);
    return sid;
}

std::optional<std::string> generatePipeToken() {
    std::array<unsigned char, easy::service::pipe_endpoint::TokenHexLength / 2> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return std::nullopt;
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        result.push_back(hex[value >> 4]);
        result.push_back(hex[value & 0x0F]);
    }
    return result;
}

bool initializePipeEndpoint() {
    auto sid = currentUserSid();
    if (!sid) {
        LOG_ERROR("SearchPlugin: 无法取得当前用户 SID，拒绝创建搜索 IPC 端点");
        return false;
    }
    auto& config = easy::core::ConfigManager::instance();
    std::string token = config.get<std::string>("/search/pipeToken", "");
    if (!easy::service::pipe_endpoint::isValidToken(token)) {
        auto generated = generatePipeToken();
        if (!generated || !config.set("/search/pipeToken", *generated)) {
            LOG_ERROR("SearchPlugin: 无法安全生成或保存搜索 IPC 端点令牌");
            return false;
        }
        token = std::move(*generated);
    }
    auto pipe = easy::service::pipe_endpoint::makePipeName(token);
    if (!pipe) return false;
    g_searchClientSid = std::move(*sid);
    g_searchPipeToken = std::move(token);
    g_searchPipe = std::move(*pipe);
    return true;
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

enum class ScmEndpointResult {
    Ready,
    AllowPortableFallback,
    Unavailable,
};

easy::search::ScmServiceState toScmServiceState(DWORD state) noexcept {
    switch (state) {
        case SERVICE_STOPPED: return easy::search::ScmServiceState::Stopped;
        case SERVICE_START_PENDING: return easy::search::ScmServiceState::StartPending;
        case SERVICE_RUNNING: return easy::search::ScmServiceState::Running;
        case SERVICE_STOP_PENDING: return easy::search::ScmServiceState::StopPending;
        default: return easy::search::ScmServiceState::Failed;
    }
}

// 等待 SCM 已接受的启动请求完成。服务创建本用户受限端点之前可能需要加载快照或
// 扫描卷，不能用固定数百毫秒猜测失败并再拉起一个便携服务。
ScmEndpointResult startScmServiceAndWait(DWORD& error) {
    error = ERROR_SUCCESS;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        error = GetLastError();
        return error == ERROR_ACCESS_DENIED ? ScmEndpointResult::AllowPortableFallback
                                            : ScmEndpointResult::Unavailable;
    }
    struct ScmGuard { SC_HANDLE value; ~ScmGuard() { if (value) CloseServiceHandle(value); } } scmGuard{scm};

    SC_HANDLE service = OpenServiceW(scm, L"EasyTools_SearchService",
                                     SERVICE_START | SERVICE_QUERY_STATUS);
    bool canStart = true;
    if (!service) {
        error = GetLastError();
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            return ScmEndpointResult::AllowPortableFallback;
        }
        if (easy::search::scmOpenShouldRetryQueryOnly(error)) {
            service = OpenServiceW(scm, L"EasyTools_SearchService", SERVICE_QUERY_STATUS);
            if (!service) {
                error = GetLastError();
                LOG_WARN("SearchPlugin: 无法查询 SCM 搜索服务，回退便携进程, error={}", error);
                return ScmEndpointResult::AllowPortableFallback;
            }
            canStart = false;
            error = ERROR_SUCCESS;
        } else {
            return ScmEndpointResult::Unavailable;
        }
    }
    struct ServiceGuard { SC_HANDLE value; ~ServiceGuard() { if (value) CloseServiceHandle(value); } } serviceGuard{service};

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
        error = GetLastError();
        return canStart ? ScmEndpointResult::Unavailable : ScmEndpointResult::AllowPortableFallback;
    }

    bool startExplicitlyFailed = !canStart;
    auto action = easy::search::decideStartupAction(
        WaitNamedPipeA(g_searchPipe.c_str(), 1) != FALSE,
        toScmServiceState(status.dwCurrentState), startExplicitlyFailed);
    if (action == easy::search::StartupAction::UseEndpoint) return ScmEndpointResult::Ready;
    if (action == easy::search::StartupAction::StartScmService) {
        const std::wstring tokenArg = L"--pipe-token=" +
            easy::core::WinUtils::utf8ToWstring(g_searchPipeToken);
        const std::wstring sidArg = L"--client-sid=" + g_searchClientSid;
        const wchar_t* args[] = {tokenArg.c_str(), sidArg.c_str()};
        if (!StartServiceW(service, static_cast<DWORD>(std::size(args)), args)) {
            error = GetLastError();
            // Another EasyTools process can win the StartService race after our
            // status query. Its SCM service remains the only safe singleton;
            // never turn this specific result into a portable duplicate.
            if (error == ERROR_SERVICE_ALREADY_RUNNING) {
                if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                          reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
                    error = GetLastError();
                    return ScmEndpointResult::Unavailable;
                }
                action = easy::search::decideStartupAction(
                    false, toScmServiceState(status.dwCurrentState), false);
                if (action == easy::search::StartupAction::WaitForScmEndpoint) {
                    // Continue into the bounded endpoint wait below.
                } else {
                    return ScmEndpointResult::Unavailable;
                }
            } else {
                // A stopped service whose start request was rejected has not
                // become a singleton, so portable mode remains compatible.
                startExplicitlyFailed = true;
                action = easy::search::decideStartupAction(false,
                    easy::search::ScmServiceState::Stopped, startExplicitlyFailed);
                return action == easy::search::StartupAction::AllowPortableFallback
                    ? ScmEndpointResult::AllowPortableFallback : ScmEndpointResult::Unavailable;
            }
        }
    } else if (action == easy::search::StartupAction::AllowPortableFallback) {
        LOG_WARN("SearchPlugin: SCM 无法为本用户启动（无启动权限或服务已停止），回退便携索引进程");
        return ScmEndpointResult::AllowPortableFallback;
    } else if (action != easy::search::StartupAction::WaitForScmEndpoint) {
        error = ERROR_SERVICE_NOT_ACTIVE;
        return ScmEndpointResult::Unavailable;
    }

    const ULONGLONG hardDeadline = GetTickCount64() + 120'000; // 有界等待，避免后台桥接线程无限阻塞。
    DWORD previousCheckpoint = 0;
    ULONGLONG checkpointDeadline = hardDeadline;
    auto lastKnownState = toScmServiceState(status.dwCurrentState);
    while (GetTickCount64() < hardDeadline) {
        if (WaitNamedPipeA(g_searchPipe.c_str(), 100)) return ScmEndpointResult::Ready;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded)) {
            error = GetLastError();
            return ScmEndpointResult::Unavailable;
        }
        const auto state = toScmServiceState(status.dwCurrentState);
        lastKnownState = state;
        if (state == easy::search::ScmServiceState::Stopped ||
            state == easy::search::ScmServiceState::Failed) {
            error = status.dwWin32ExitCode ? status.dwWin32ExitCode : ERROR_SERVICE_NOT_ACTIVE;
            return ScmEndpointResult::AllowPortableFallback;
        }
        if (state == easy::search::ScmServiceState::StopPending) {
            error = ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
            return ScmEndpointResult::Unavailable;
        }
        // SCM 的 checkpoint 有推进时，按照 WaitHint 延长当前阶段等待；没有推进
        // 则不能无限延长，防止损坏的服务永久占用客户端请求。
        if (status.dwCheckPoint != previousCheckpoint) {
            previousCheckpoint = status.dwCheckPoint;
            const DWORD waitHint = (std::clamp)(status.dwWaitHint,
                                                static_cast<DWORD>(1'000),
                                                static_cast<DWORD>(30'000));
            checkpointDeadline = (std::min)(hardDeadline, GetTickCount64() + waitHint * 2ull);
        } else if (GetTickCount64() >= checkpointDeadline) {
            error = ERROR_TIMEOUT;
            return ScmEndpointResult::Unavailable;
        }
        const DWORD sleepMs = (std::clamp)(status.dwWaitHint / 10,
                                            static_cast<DWORD>(100),
                                            static_cast<DWORD>(1'000));
        Sleep(sleepMs);
    }
    // Since the service publishes the authenticated pipe before indexing, a
    // still-running SCM instance that never creates *this* tokenized endpoint
    // is not merely slow. It belongs to a different user/session or was started
    // with stale credentials; surface that boundary explicitly.
    error = easy::search::isScmEndpointIdentityConflict(lastKnownState, false, true)
        ? ERROR_NOT_SUPPORTED : ERROR_TIMEOUT;
    return ScmEndpointResult::Unavailable;
}

static bool ensureSearchServiceRunning() {
    if (g_searchPipe.empty()) return false;
    if (WaitNamedPipeA(g_searchPipe.c_str(), 100)) {
        return true;
    }

    // 查询走线程池并发执行，若不串行化，多个线程会在服务建好管道之前的空窗期里
    // 各自拉起一个索引进程，每个都会建一份完整索引。
    static std::mutex launchMutex;
    std::lock_guard<std::mutex> launchGuard(launchMutex);

    // 进程内 mutex 只能串行当前插件实例；同一用户连续启动两个 EasyTools
    // 进程时仍可能同时拉起服务。令牌已是每用户不可预测值，因此可安全地作为
    // Local 命名 mutex 的组成部分，不暴露固定全局对象名给其他会话猜测。
    const std::wstring launchMutexName = L"Local\\EasyToolsSearchLaunch-" +
        easy::core::WinUtils::utf8ToWstring(g_searchPipeToken);
    HANDLE processLaunchMutex = CreateMutexW(nullptr, FALSE, launchMutexName.c_str());
    if (!processLaunchMutex) return false;
    struct LaunchMutexGuard {
        HANDLE value;
        bool locked = false;
        ~LaunchMutexGuard() {
            if (locked) ReleaseMutex(value);
            if (value) CloseHandle(value);
        }
    } processLaunchGuard{processLaunchMutex};
    const DWORD waitResult = WaitForSingleObject(processLaunchMutex, 10'000);
    if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) {
        SetLastError(waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
        return false;
    }
    processLaunchGuard.locked = true;

    if (WaitNamedPipeA(g_searchPipe.c_str(), 100)) {
        return true;
    }

    DWORD scmError = ERROR_SUCCESS;
    const auto scmResult = startScmServiceAndWait(scmError);
    if (scmResult == ScmEndpointResult::Ready) return true;
    if (scmResult == ScmEndpointResult::Unavailable) {
        SetLastError(scmError);
        LOG_WARN("SearchPlugin: SCM 服务未提供当前用户端点，拒绝启动第二个索引进程, error={}", scmError);
        return false;
    }

    // SCM 明确不存在、无法启动或已停止后，才允许便携进程回退。
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
        std::wstring cmd = L"\"" + serviceExe.wstring() + L"\" --pipe-token=" +
            easy::core::WinUtils::utf8ToWstring(g_searchPipeToken) + L" --client-sid=" + g_searchClientSid;
        if (CreateProcessW(serviceExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, exeDir.c_str(), &si, &pi)) {
            g_serviceSpawnedByUs.store(true);
            easy::core::WinUtils::assignProcessToCurrentJob(pi.hProcess);
            if (pi.hProcess) CloseHandle(pi.hProcess);
            if (pi.hThread) CloseHandle(pi.hThread);
        } else {
            // Preserve the launch failure. WaitNamedPipe below has its own error
            // path and must not hide a malformed executable/ACL/creation error.
            const DWORD launchError = GetLastError();
            SetLastError(launchError);
            return false;
        }
    } else {
        const DWORD missingServiceError = ec ? static_cast<DWORD>(ec.value()) : ERROR_FILE_NOT_FOUND;
        SetLastError(missingServiceError);
        return false;
    }

    if (WaitNamedPipeA(g_searchPipe.c_str(), 3000)) return true;
    const DWORD pipeWaitError = GetLastError();
    SetLastError(pipeWaitError);
    return false;
}

// autoStart 为 false 时，服务没在跑就直接放弃本次调用。历史记录、数据库统计这类
// 辅助数据会在搜索窗 WebView 预热完成的瞬间被前端拉一遍，若允许它们拉起服务，
// 用户从没按过搜索热键也会在开机时凭空多出几百 MB 的索引进程。
std::optional<std::string> querySearchService(const std::string& query, DWORD& error,
                                              bool autoStart = true) {
    error = ERROR_SUCCESS;
    if (g_searchPipe.empty()) {
        error = ERROR_ACCESS_DENIED;
        return std::nullopt;
    }
    if (!WaitNamedPipeA(g_searchPipe.c_str(), autoStart ? 1000 : 1)) {
        if (!autoStart) {
            error = ERROR_SERVICE_NOT_ACTIVE;
            return std::nullopt;
        }
        if (!ensureSearchServiceRunning()) {
            error = GetLastError();
            return std::nullopt;
        }
    }

    HANDLE pipe = CreateFileA(g_searchPipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (WaitNamedPipeA(g_searchPipe.c_str(), 1500)) {
            pipe = CreateFileA(g_searchPipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
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
        if (!initializePipeEndpoint()) return false;

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
                    const bool isInit = result.value("initializing", false);
                    result["available"] = !isInit;
                    result["status"] = isInit ? "starting" : "ready";
                    return result;
                } catch (...) {
                    LOG_ERROR("SearchPlugin: 无法解析 JSON 结果");
                }
            } else {
                LOG_WARN("SearchPlugin: 管道调用超时或返回空, error={}", pipeError);
            }

            const bool isStarting = pipeError == ERROR_TIMEOUT || pipeError == ERROR_PIPE_BUSY ||
                                   pipeError == ERROR_FILE_NOT_FOUND || g_serviceSpawnedByUs.load();
            const char* statusError = pipeError == ERROR_NOT_SUPPORTED
                ? "search service is attached to another Windows user or session"
                : (isStarting ? "search service starting" : "search service unavailable");
            return {
                {"results", nlohmann::json::array()},
                {"available", false},
                {"status", isStarting ? "starting" : "unavailable"},
                {"error", statusError}
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
            const bool started = easy::core::ShellContextMenuService::instance().showAsync(widePath);
            return {{"success", started}, {"busy", !started}};
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
                {"available", !g_searchPipe.empty() && WaitNamedPipeA(g_searchPipe.c_str(), 1) != FALSE}
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
            bool autoBypassFullscreen = cfg.get<bool>("/search/autoBypassFullscreen", true);

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
                {"excludeSystem", excludeSystem},
                {"autoBypassFullscreen", autoBypassFullscreen}
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
            if (params.contains("autoBypassFullscreen") && params["autoBypassFullscreen"].is_boolean()) {
                cfg.set("/search/autoBypassFullscreen", params["autoBypassFullscreen"].get<bool>());
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
