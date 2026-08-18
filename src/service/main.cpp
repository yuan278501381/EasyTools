#include <windows.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <future>
#include <sddl.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cwctype>
#include <nlohmann/json.hpp>
#include "MftParser.h"
#include "content/ContentSearchEngine.h"

#define SERVICE_NAME L"EasyTools_SearchService"

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::atomic<bool>     g_IsRunning{false};

std::vector<std::unique_ptr<MftParser>> g_MftParsers;

std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void InitLogger() {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::filesystem::path logDir;
        PWSTR programData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_CREATE, nullptr,
                                           &programData)) && programData) {
            logDir = std::filesystem::path(programData) / L"EasyTools" / L"logs";
            CoTaskMemFree(programData);
        } else {
            logDir = std::filesystem::temp_directory_path() / L"EasyTools" / L"logs";
        }
        std::filesystem::create_directories(logDir);
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (logDir / L"EasyTools_Service.log").string(), 5 * 1024 * 1024, 3);
        spdlog::sinks_init_list sink_list = { console_sink, file_sink };
        auto logger = std::make_shared<spdlog::logger>("service", sink_list.begin(), sink_list.end());
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
    }
}

// --------------------------------------------------------------------------------------
// Named Pipe Server
// --------------------------------------------------------------------------------------
#include <mutex>
#include <chrono>

nlohmann::json ProcessSearchQuery(const std::wstring& rawInput) {
    std::wstring wQuery;
    std::vector<char> enabledDrives;
    SearchExcludeOptions excludeOpts;

    std::string utf8Input = WStringToString(rawInput);
    if (!utf8Input.empty() && utf8Input.front() == '{') {
        try {
            auto reqJson = nlohmann::json::parse(utf8Input);
            if (reqJson.contains("action") && reqJson["action"].is_string()) {
                std::string act = reqJson["action"].get<std::string>();
                if (act == "rebuild" || act == "reindex") {
                    for (auto& parser : g_MftParsers) {
                        MftParser* pRaw = parser.get();
                        std::thread([pRaw]() {
                            pRaw->EnumerateFiles();
                        }).detach();
                    }
                    return {{"success", true}, {"rebuilding", true}};
                }
            }

            if (reqJson.contains("query") && reqJson["query"].is_string()) {
                wQuery = StringToWString(reqJson["query"].get<std::string>());
            }
            if (reqJson.contains("drives") && reqJson["drives"].is_array()) {
                for (const auto& d : reqJson["drives"]) {
                    if (d.is_string()) {
                        std::string s = d.get<std::string>();
                        if (!s.empty()) enabledDrives.push_back(static_cast<char>(std::toupper(s[0])));
                    }
                }
            }
            if (reqJson.contains("excludes") && reqJson["excludes"].is_array()) {
                for (const auto& ex : reqJson["excludes"]) {
                    if (ex.is_string()) {
                        std::string s = ex.get<std::string>();
                        if (!s.empty()) excludeOpts.patterns.push_back(StringToWString(s));
                    }
                }
            }
            if (reqJson.contains("excludeHidden") && reqJson["excludeHidden"].is_boolean()) {
                excludeOpts.excludeHidden = reqJson["excludeHidden"].get<bool>();
            }
            if (reqJson.contains("excludeSystem") && reqJson["excludeSystem"].is_boolean()) {
                excludeOpts.excludeSystem = reqJson["excludeSystem"].get<bool>();
            }
            if (reqJson.contains("contentCustomExts") || reqJson.contains("contentDisabledExts")) {
                std::vector<std::wstring> customExts;
                std::vector<std::wstring> disabledExts;
                if (reqJson.contains("contentCustomExts") && reqJson["contentCustomExts"].is_array()) {
                    for (const auto& item : reqJson["contentCustomExts"]) {
                        if (item.is_string()) customExts.push_back(StringToWString(item.get<std::string>()));
                    }
                }
                if (reqJson.contains("contentDisabledExts") && reqJson["contentDisabledExts"].is_array()) {
                    for (const auto& item : reqJson["contentDisabledExts"]) {
                        if (item.is_string()) disabledExts.push_back(StringToWString(item.get<std::string>()));
                    }
                }
                easy::service::content::ContentSearchEngine::instance().configureFormats(customExts, disabledExts);
            }
        } catch (...) {
            wQuery = rawInput;
        }
    } else {
        wQuery = rawInput;
    }

    if (wQuery.empty()) {
        return {{"results", nlohmann::json::array()}};
    }

    auto isDriveEnabled = [&](char driveLetter) {
        if (enabledDrives.empty()) return true;
        for (char d : enabledDrives) {
            if (std::toupper(d) == std::toupper(driveLetter)) return true;
        }
        return false;
    };

    SearchExpression expr = SearchExpression::parse(wQuery);
    nlohmann::json responseJson = {{"results", nlohmann::json::array()}};

    if (expr.hasContentFilter() && !expr.getContentQuery().empty()) {
        std::vector<SearchResult> candidates;
        for (auto& parser : g_MftParsers) {
            if (!isDriveEnabled(parser->getDriveLetter())) continue;
            auto volumeResults = parser->Search(wQuery, 5000, excludeOpts);
            candidates.insert(candidates.end(),
                              std::make_move_iterator(volumeResults.begin()),
                              std::make_move_iterator(volumeResults.end()));
        }

        auto& contentEngine = easy::service::content::ContentSearchEngine::instance();
        std::vector<SearchResult> textCandidates;
        textCandidates.reserve(candidates.size());

        for (auto& candidate : candidates) {
            if (candidate.isDirectory) continue;
            if (candidate.fileSize > 25 * 1024 * 1024) continue; // 忽略大于 25MB 的巨型文件
            
            size_t dotPos = candidate.fullPath.rfind(L'.');
            if (dotPos == std::wstring::npos) continue;
            std::wstring ext = candidate.fullPath.substr(dotPos + 1);
            if (!contentEngine.canSearchContent(ext)) continue;

            // 过滤深层 Windows 庞大二进制目录（除非显式指定 windows 路径）
            if (candidate.fullPath.find(L"\\Windows\\WinSxS\\") != std::wstring::npos ||
                candidate.fullPath.find(L"\\Windows\\System32\\") != std::wstring::npos ||
                candidate.fullPath.find(L"\\$Recycle.Bin\\") != std::wstring::npos) {
                if (wQuery.find(L"windows") == std::wstring::npos && wQuery.find(L"winsxs") == std::wstring::npos) {
                    continue;
                }
            }
            textCandidates.push_back(std::move(candidate));
        }

        // 智能优先级权重排序：将非系统工作盘（D:、E:）、用户桌面/文档/Chosen优先放置在前面扫描
        auto getPathPriority = [](const std::wstring& path) -> int {
            // 缓存与垃圾构建路径降权
            if (path.find(L"\\AppData\\Local\\npm-cache\\") != std::wstring::npos ||
                path.find(L"\\AppData\\Local\\pip\\") != std::wstring::npos ||
                path.find(L"\\AppData\\Local\\go-build\\") != std::wstring::npos ||
                path.find(L"\\.gradle\\") != std::wstring::npos ||
                path.find(L"\\AppData\\Local\\Temp\\") != std::wstring::npos) {
                return 10;
            }
            // AppData 内其他目录
            if (path.find(L"\\AppData\\") != std::wstring::npos) {
                return 30;
            }
            // 非系统盘（D:、E: 等用户数据与工作盘）最高优先级
            if (path.size() >= 2 && path[1] == L':' && path[0] != L'C' && path[0] != L'c') {
                return 100;
            }
            // C 盘中的工作区/桌面/文档/Chosen
            if (path.find(L"\\Desktop\\") != std::wstring::npos ||
                path.find(L"\\Documents\\") != std::wstring::npos ||
                path.find(L"\\Downloads\\") != std::wstring::npos ||
                path.find(L"\\repo\\") != std::wstring::npos ||
                path.find(L"\\workspace\\") != std::wstring::npos ||
                path.find(L"\\Projects\\") != std::wstring::npos ||
                path.find(L"\\Chosen\\") != std::wstring::npos) {
                return 90;
            }
            return 50;
        };

        std::stable_sort(textCandidates.begin(), textCandidates.end(), [&](const auto& a, const auto& b) {
            int pA = getPathPriority(a.fullPath);
            int pB = getPathPriority(b.fullPath);
            if (pA != pB) return pA > pB;
            return a.lastWriteTime > b.lastWriteTime;
        });

        if (textCandidates.size() > 5000) textCandidates.resize(5000);

        std::mutex resultsMutex;
        std::atomic<size_t> matchCount{0};
        const auto startTime = std::chrono::steady_clock::now();
        const auto deadline = startTime + std::chrono::milliseconds(2500);

        const unsigned int numThreads = (std::min)(4u, (std::max)(1u, std::thread::hardware_concurrency()));
        std::vector<std::thread> workers;
        std::atomic<size_t> nextIndex{0};

        for (unsigned int t = 0; t < numThreads; ++t) {
            workers.emplace_back([&]() {
                while (matchCount.load() < 50) {
                    if (std::chrono::steady_clock::now() > deadline) break;
                    size_t idx = nextIndex.fetch_add(1);
                    if (idx >= textCandidates.size()) break;

                    const auto& item = textCandidates[idx];
                    std::vector<easy::service::content::ContentSnippet> snippets;
                    if (contentEngine.searchFile(item.fullPath, expr.getContentQuery(), false, snippets)) {
                        nlohmann::json snippetsJson = nlohmann::json::array();
                        for (const auto& snip : snippets) {
                            snippetsJson.push_back({
                                {"lineNumber", snip.lineNumber},
                                {"lineContent", WStringToString(snip.lineContent)},
                                {"matchOffset", snip.matchOffset},
                                {"matchLength", snip.matchLength}
                            });
                        }

                        nlohmann::json itemJson = {
                            {"name", WStringToString(item.fileName)},
                            {"path", WStringToString(item.fullPath)},
                            {"isDirectory", item.isDirectory},
                            {"size", item.fileSize},
                            {"creationTime", item.creationTime},
                            {"lastWriteTime", item.lastWriteTime},
                            {"snippets", std::move(snippetsJson)}
                        };

                        {
                            std::lock_guard<std::mutex> lock(resultsMutex);
                            responseJson["results"].push_back(std::move(itemJson));
                        }
                        matchCount.fetch_add(1);
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
    } else {
        std::vector<std::future<std::vector<SearchResult>>> futures;
        futures.reserve(g_MftParsers.size());
        for (auto& parser : g_MftParsers) {
            if (!isDriveEnabled(parser->getDriveLetter())) continue;
            futures.push_back(std::async(std::launch::async, [&parser, &wQuery, &excludeOpts]() {
                return parser->Search(wQuery, 50, excludeOpts);
            }));
        }

        std::vector<SearchResult> results;
        for (auto& f : futures) {
            auto volumeResults = f.get();
            results.insert(results.end(),
                           std::make_move_iterator(volumeResults.begin()),
                           std::make_move_iterator(volumeResults.end()));
        }
        std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
            if (a.fileName.size() != b.fileName.size()) return a.fileName.size() < b.fileName.size();
            return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
        });
        if (results.size() > 50) results.resize(50);

        for (const auto& result : results) {
            responseJson["results"].push_back({
                {"name", WStringToString(result.fileName)},
                {"path", WStringToString(result.fullPath)},
                {"isDirectory", result.isDirectory},
                {"size", result.fileSize},
                {"creationTime", result.creationTime},
                {"lastWriteTime", result.lastWriteTime}
            });
        }
    }

    return responseJson;
}

void PipeWorkerThread(PSECURITY_DESCRIPTOR pipeDescriptor, SECURITY_ATTRIBUTES* pipeSecurity) {
    while (g_IsRunning.load()) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\EasyToolsSearchPipe",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            256 * 1024, 4096, 0,
            pipeDescriptor ? pipeSecurity : nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && g_IsRunning.load()) {
            std::array<char, 4096> buffer{};
            while (g_IsRunning.load()) {
                std::string request;
                DWORD bytesRead = 0;
                BOOL readOk = FALSE;
                do {
                    readOk = ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size()),
                                      &bytesRead, nullptr);
                    if (bytesRead > 0 && request.size() < 4096) {
                        request.append(buffer.data(), std::min<size_t>(bytesRead, 4096 - request.size()));
                    }
                } while (!readOk && GetLastError() == ERROR_MORE_DATA && request.size() < 4096);
                if ((!readOk && GetLastError() != ERROR_MORE_DATA) || request.empty()) break;

                std::wstring wQuery = StringToWString(request);
                nlohmann::json responseJson = ProcessSearchQuery(wQuery);
                const std::string response = responseJson.dump();

                DWORD bytesWritten = 0;
                if (!WriteFile(hPipe, response.c_str(), static_cast<DWORD>(response.size()),
                               &bytesWritten, nullptr) ||
                    bytesWritten != static_cast<DWORD>(response.size())) {
                    break;
                }
            }
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

void IPCServerThread() {
    spdlog::info("IPC Server Thread started.");
    
    // Index every local fixed NTFS volume instead of silently limiting search
    // to C:. Removable/network volumes are excluded because USN semantics and
    // availability are not stable enough for a resident service.
    const DWORD driveMask = GetLogicalDrives();
    std::vector<std::thread> indexThreads;
    for (char drive = 'A'; drive <= 'Z' && g_IsRunning.load(); ++drive) {
        if (!(driveMask & (1u << (drive - 'A')))) continue;
        const std::wstring root{static_cast<wchar_t>(drive), L':', L'\\'};
        UINT driveType = GetDriveTypeW(root.c_str());
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOTE &&
            driveType != DRIVE_REMOVABLE && driveType != DRIVE_RAMDISK) continue;

        auto parser = std::make_unique<MftParser>();
        if (parser->Initialize(drive)) {
            MftParser* pRaw = parser.get();
            g_MftParsers.push_back(std::move(parser));
            indexThreads.emplace_back([pRaw, drive]() {
                pRaw->EnumerateFiles();
                pRaw->StartListening();
                spdlog::info("Indexed drive {}:", drive);
            });
        }
    }
    spdlog::info("Search index launched for {} volume(s)", g_MftParsers.size());

    PSECURITY_DESCRIPTOR pipeDescriptor = nullptr;
    SECURITY_ATTRIBUTES pipeSecurity{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)", SDDL_REVISION_1,
            &pipeDescriptor, nullptr)) {
        pipeSecurity.lpSecurityDescriptor = pipeDescriptor;
    } else {
        spdlog::error("Failed to create named pipe security descriptor: {}", GetLastError());
    }

    constexpr int NUM_PIPE_WORKERS = 4;
    std::vector<std::thread> pipeWorkers;
    pipeWorkers.reserve(NUM_PIPE_WORKERS);
    for (int i = 0; i < NUM_PIPE_WORKERS; ++i) {
        pipeWorkers.emplace_back(PipeWorkerThread, pipeDescriptor, &pipeSecurity);
    }
    spdlog::info("Launched {} concurrent named pipe server worker(s)", NUM_PIPE_WORKERS);

    for (auto& w : pipeWorkers) {
        if (w.joinable()) w.join();
    }
    for (auto& t : indexThreads) {
        if (t.joinable()) t.join();
    }
    for (auto& parser : g_MftParsers) parser->StopListening();
    g_MftParsers.clear();
    if (pipeDescriptor) LocalFree(pipeDescriptor);
    spdlog::info("IPC Server Thread stopped.");
}

// --------------------------------------------------------------------------------------
// Service Control
// --------------------------------------------------------------------------------------
void WINAPI ServiceCtrlHandler(DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP:
            spdlog::info("SERVICE_CONTROL_STOP received.");
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
                break;
            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWin32ExitCode = 0;
            g_ServiceStatus.dwCheckPoint = 4;
            SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
            SetEvent(g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

void WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    (void)argc;
    (void)argv;
    g_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) return;

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    spdlog::info("Service started successfully.");
    g_IsRunning = true;
    
    // Start IPC Thread
    std::thread ipcThread(IPCServerThread);

    // Wait for stop event
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    g_IsRunning = false;
    
    // Break the named pipe wait
    HANDLE hPipe = CreateFileA("\\\\.\\pipe\\EasyToolsSearchPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
    
    if (ipcThread.joinable()) ipcThread.join();

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    spdlog::info("Service stopped successfully.");
}

int main(int argc, char** argv) {
    InitLogger();
    if (argc > 1 && std::string(argv[1]) == "--debug") {
        spdlog::info("Running in debug mode (Console).");
        g_IsRunning = true;
        std::thread ipcThread(IPCServerThread);
        std::cout << "Press ENTER to stop..." << std::endl;
        std::cin.get();
        g_IsRunning = false;
        HANDLE hPipe = CreateFileA("\\\\.\\pipe\\EasyToolsSearchPipe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe != INVALID_HANDLE_VALUE) CloseHandle(hPipe);
        if (ipcThread.joinable()) ipcThread.join();
        return 0;
    }

    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTIONW)ServiceMain},
        {NULL, NULL}
    };
    
    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // 当非 SCM 服务环境（如便携版、免安装模式或主程序子进程拉起）直接执行时，自动退化为独立后台管道服务进程
            spdlog::info("Running in standalone background mode (named pipe server).");
            g_IsRunning = true;
            IPCServerThread();
            return 0;
        }
        spdlog::error("StartServiceCtrlDispatcher failed: {}.", err);
        return 1;
    }
    return 0;
}
