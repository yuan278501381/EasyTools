#include <windows.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
#include <memory>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <future>
#include <mutex>
#include <string_view>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cwctype>
#include <nlohmann/json.hpp>
#include "MftParser.h"
#include "PipeProtocol.h"
#include "PipeEndpoint.h"
#include "SearchCancellation.h"
#include "content/ContentSearchEngine.h"
#include "db/RunHistoryManager.h"
#include "db/SearchHistoryManager.h"
#include "db/DatabaseManager.h"

#define SERVICE_NAME L"EasyTools_SearchService"

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::atomic<bool>     g_IsRunning{false};

std::vector<std::unique_ptr<MftParser>> g_MftParsers;

// 所有会触碰 MftParser 的后台工作都由服务拥有。服务退出时先请求停止并 join，
// 再销毁解析器，禁止任何 detached worker 越过对象生命周期。
std::mutex g_BackgroundJobMutex;
std::jthread g_RebuildJob;
std::jthread g_SnapshotJob;
std::atomic<bool> g_RebuildInProgress{false};
std::atomic<bool> g_SnapshotInProgress{false};
std::atomic<int> g_InitialIndexWorkers{0};
std::atomic<bool> g_AcceptBackgroundJobs{true};
// The per-user pipe is published before expensive volume work. Until this flag
// flips, workers return an explicit initializing response and never touch the
// parser vector while it is being assembled.
std::atomic<bool> g_SearchIndexReady{false};
std::jthread g_PipePokeJob;

std::string g_SearchPipeName;
std::wstring g_SearchClientSid;
constexpr int NumPipeWorkers = 4;

std::string WStringToString(const std::wstring& wstr);

bool configurePipeEndpoint(DWORD argc, wchar_t** argv) {
    std::string token;
    std::wstring sid;
    for (DWORD index = 0; index < argc; ++index) {
        const std::wstring_view arg = argv[index] ? argv[index] : L"";
        constexpr std::wstring_view tokenPrefix = L"--pipe-token=";
        constexpr std::wstring_view sidPrefix = L"--client-sid=";
        if (arg.starts_with(tokenPrefix)) {
            token = WStringToString(std::wstring(arg.substr(tokenPrefix.size())));
        } else if (arg.starts_with(sidPrefix)) {
            sid = std::wstring(arg.substr(sidPrefix.size()));
        }
    }
    PSID parsedSid = nullptr;
    const bool sidValid = !sid.empty() && ConvertStringSidToSidW(sid.c_str(), &parsedSid);
    if (parsedSid) LocalFree(parsedSid);
    const auto pipe = easy::service::pipe_endpoint::makePipeName(token);
    if (!sidValid || !pipe) {
        spdlog::error("Search service missing or invalid per-user IPC endpoint arguments");
        return false;
    }
    g_SearchPipeName = *pipe;
    g_SearchClientSid = std::move(sid);
    return true;
}

void stopBackgroundJobs() {
    g_AcceptBackgroundJobs.store(false, std::memory_order_release);
    for (auto& parser : g_MftParsers) parser->requestStop();

    std::jthread rebuild;
    std::jthread snapshot;
    {
        std::lock_guard lock(g_BackgroundJobMutex);
        if (g_RebuildJob.joinable()) g_RebuildJob.request_stop();
        if (g_SnapshotJob.joinable()) g_SnapshotJob.request_stop();
        rebuild = std::move(g_RebuildJob);
        snapshot = std::move(g_SnapshotJob);
    }
    // jthread 的析构在当前作用域末尾完成 join。此时不持有任务锁，避免工作线程
    // 在收尾路径更新状态时形成锁反转。
}

void scheduleSnapshot(std::vector<MftParser*> parsers, std::string_view reason) {
    if (!g_AcceptBackgroundJobs.load(std::memory_order_acquire) || parsers.empty()) return;
    std::lock_guard lock(g_BackgroundJobMutex);
    if (g_SnapshotInProgress.exchange(true, std::memory_order_acq_rel)) return;
    if (g_SnapshotJob.joinable()) g_SnapshotJob.join();

    g_SnapshotJob = std::jthread(
        [parsers = std::move(parsers), reason = std::string(reason)](std::stop_token stop) {
            // 等待初始索引真正完成，而不是猜测固定 5 秒；慢盘和网络盘不会再保存半份快照。
            while (!stop.stop_requested() && g_InitialIndexWorkers.load(std::memory_order_acquire) > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            try {
                if (!stop.stop_requested()) {
                    const bool ok = easy::service::db::DatabaseManager::instance().saveSnapshot(parsers);
                    spdlog::info("{} database snapshot {}", reason, ok ? "saved" : "failed");
                }
            } catch (const std::exception& e) {
                spdlog::error("{} database snapshot failed: {}", reason, e.what());
            } catch (...) {
                spdlog::error("{} database snapshot failed with unknown exception", reason);
            }
            g_SnapshotInProgress.store(false, std::memory_order_release);
        });
}

bool scheduleRebuild() {
    if (!g_IsRunning.load(std::memory_order_acquire) ||
        !g_AcceptBackgroundJobs.load(std::memory_order_acquire) ||
        g_InitialIndexWorkers.load(std::memory_order_acquire) > 0) return false;

    std::lock_guard lock(g_BackgroundJobMutex);
    if (g_RebuildInProgress.exchange(true, std::memory_order_acq_rel)) return false;
    if (g_RebuildJob.joinable()) g_RebuildJob.join();

    std::vector<MftParser*> parsers;
    parsers.reserve(g_MftParsers.size());
    for (auto& parser : g_MftParsers) {
        parser->resetStopRequest();
        parsers.push_back(parser.get());
    }
    g_RebuildJob = std::jthread([parsers = std::move(parsers)](std::stop_token stop) {
        std::vector<std::thread> workers;
        workers.reserve(parsers.size());
        for (auto* parser : parsers) {
            workers.emplace_back([parser, stop]() {
                if (stop.stop_requested()) return;
                try {
                    parser->EnumerateFiles();
                    if (!stop.stop_requested()) parser->StartListening();
                } catch (const std::exception& e) {
                    spdlog::error("Rebuild failed on drive {}: {}", parser->getDriveLetter(), e.what());
                } catch (...) {
                    spdlog::error("Rebuild failed on drive {} with unknown exception", parser->getDriveLetter());
                }
            });
        }
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        try {
            if (!stop.stop_requested()) {
                easy::service::db::DatabaseManager::instance().saveSnapshot(parsers);
            }
        } catch (const std::exception& e) {
            spdlog::error("Rebuild snapshot failed: {}", e.what());
        } catch (...) {
            spdlog::error("Rebuild snapshot failed with unknown exception");
        }
        g_RebuildInProgress.store(false, std::memory_order_release);
    });
    return true;
}

// 主程序退出时会通过管道请求停机。工作线程此刻大多阻塞在 ConnectNamedPipe 上，
// 单靠清掉运行标志叫不醒它们，还得真的连上来几次。这件事必须交给另一个线程，
// 否则正在处理停机请求的那个线程会连到自己身上。
void RequestServiceShutdown() {
    if (!g_IsRunning.exchange(false)) return;
    if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) SetEvent(g_ServiceStopEvent);

    g_PipePokeJob = std::jthread([](std::stop_token stop) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            HANDLE poke = CreateFileA(g_SearchPipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
            if (poke == INVALID_HANDLE_VALUE) {
                // 管道实例全部消失，说明工作线程已经退干净了。
                if (GetLastError() == ERROR_FILE_NOT_FOUND) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            CloseHandle(poke);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
}

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
#include <chrono>

nlohmann::json ProcessSearchQuery(const std::wstring& rawInput) {
    std::wstring wQuery;
    std::string searchMode = "name";
    std::vector<char> enabledDrives;
    SearchExcludeOptions excludeOpts;
    size_t requestedLimit = 100;
    uint64_t queryId = 0;

    std::string utf8Input = WStringToString(rawInput);
    if (!g_SearchIndexReady.load(std::memory_order_acquire) ||
        g_InitialIndexWorkers.load(std::memory_order_acquire) > 0) {
        bool isShutdownRequest = false;
        if (!utf8Input.empty() && utf8Input.front() == '{') {
            try {
                const auto request = nlohmann::json::parse(utf8Input);
                isShutdownRequest = request.value("action", "") == "shutdown";
            } catch (...) {
                // Invalid requests are also deferred while startup owns the
                // parser collection, rather than racing initialization.
            }
        }
        if (!isShutdownRequest) {
            return {{"results", nlohmann::json::array()}, {"available", false},
                    {"initializing", true}, {"error", "search service initializing"}};
        }
    }
    if (!utf8Input.empty() && utf8Input.front() == '{') {
        try {
            auto reqJson = nlohmann::json::parse(utf8Input);
            if (reqJson.contains("action") && reqJson["action"].is_string()) {
                std::string act = reqJson["action"].get<std::string>();
                if (act == "rebuild" || act == "reindex") {
                    const bool started = scheduleRebuild();
                    return {{"success", started}, {"rebuilding", started},
                            {"alreadyRunning", !started && g_RebuildInProgress.load()}};
                }
                if (act == "catchup" || act == "sync") {
                    bool anyUpdated = false;
                    for (auto& parser : g_MftParsers) {
                        uint64_t lastUsn = parser->getCurrentUsn();
                        if (parser->catchUpUsnJournal(lastUsn)) {
                            anyUpdated = true;
                        }
                    }
                    return {{"success", true}, {"synced", true}, {"updated", anyUpdated}};
                }
                if (act == "recordRun") {
                    if (reqJson.contains("path") && reqJson["path"].is_string()) {
                        std::wstring path = StringToWString(reqJson["path"].get<std::string>());
                        easy::service::db::RunHistoryManager::instance().recordRun(path);
                        return {{"success", true}};
                    }
                    return {{"success", false}};
                }
                if (act == "recordSearch") {
                    if (reqJson.contains("query") && reqJson["query"].is_string()) {
                        std::wstring q = StringToWString(reqJson["query"].get<std::string>());
                        easy::service::db::SearchHistoryManager::instance().recordSearch(q);
                        return {{"success", true}};
                    }
                    return {{"success", false}};
                }
                if (act == "getSearchHistory") {
                    size_t limit = reqJson.value("limit", 30);
                    auto list = easy::service::db::SearchHistoryManager::instance().getRecentSearches(limit);
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& item : list) {
                        arr.push_back({
                            {"search", WStringToString(item.search)},
                            {"searchCount", item.searchCount},
                            {"lastSearchDate", item.lastSearchDate}
                        });
                    }
                    return {{"success", true}, {"history", arr}};
                }
                if (act == "removeSearchHistory") {
                    if (reqJson.contains("search") && reqJson["search"].is_string()) {
                        std::wstring q = StringToWString(reqJson["search"].get<std::string>());
                        bool ok = easy::service::db::SearchHistoryManager::instance().removeSearch(q);
                        return {{"success", ok}};
                    }
                    return {{"success", false}};
                }
                if (act == "clearSearchHistory") {
                    easy::service::db::SearchHistoryManager::instance().clear();
                    return {{"success", true}};
                }
                if (act == "getDbStats") {
                    auto stats = easy::service::db::DatabaseManager::instance().getStats();
                    const bool isIndexing = g_InitialIndexWorkers.load(std::memory_order_acquire) > 0 ||
                                            g_RebuildInProgress.load(std::memory_order_acquire) ||
                                            g_SnapshotInProgress.load(std::memory_order_acquire) ||
                                            !g_SearchIndexReady.load(std::memory_order_acquire);
                    return {
                        {"success", true},
                        {"dbPath", WStringToString(stats.dbPath)},
                        {"dbSize", stats.fileSize},
                        {"timestamp", stats.timestamp},
                        {"totalRecords", stats.totalRecords},
                        {"volumeCount", stats.volumeCount},
                        {"exists", stats.exists},
                        {"indexing", isIndexing},
                        {"initialWorkers", g_InitialIndexWorkers.load(std::memory_order_acquire)},
                        {"rebuildInProgress", g_RebuildInProgress.load(std::memory_order_acquire)},
                        {"snapshotInProgress", g_SnapshotInProgress.load(std::memory_order_acquire)},
                        {"runHistoryCount", easy::service::db::RunHistoryManager::instance().size()},
                        {"searchHistoryCount", easy::service::db::SearchHistoryManager::instance().size()}
                    };
                }
                if (act == "saveSnapshot") {
                    std::vector<MftParser*> allParsers;
                    for (auto& p : g_MftParsers) allParsers.push_back(p.get());
                    bool ok = easy::service::db::DatabaseManager::instance().saveSnapshot(allParsers);
                    return {{"success", ok}};
                }
                if (act == "shutdown") {
                    // 快照不在这里落盘：写一份要几百 MB，而下次启动导入快照时本就会用
                    // USN 日志补齐停机期间的变动，多花的磁盘代价换不来任何准确性。
                    spdlog::info("Received shutdown request from client, stopping service.");
                    RequestServiceShutdown();
                    return {{"success", true}, {"stopping", true}};
                }
            }

            if (reqJson.contains("query") && reqJson["query"].is_string()) {
                wQuery = StringToWString(reqJson["query"].get<std::string>());
            }
            if (reqJson.contains("queryId") && reqJson["queryId"].is_number_unsigned()) {
                queryId = reqJson["queryId"].get<uint64_t>();
            }
            if (reqJson.contains("searchMode") && reqJson["searchMode"].is_string()) {
                searchMode = reqJson["searchMode"].get<std::string>();
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
            if (reqJson.contains("limit") && reqJson["limit"].is_number_integer()) {
                int l = reqJson["limit"].get<int>();
                if (l <= 0) requestedLimit = 10000; // 全部返回 (安全上限 10000)
                else requestedLimit = static_cast<size_t>(l);
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
    const auto overallStartTime = std::chrono::steady_clock::now();

    auto& epochTracker = easy::service::query::sharedEpochTracker();
    if (!epochTracker.observe(queryId)) {
        // 更新的查询已经先行到达，本次结果不会再被采用。
        return {{"results", nlohmann::json::array()}, {"cancelled", true}, {"queryId", queryId}};
    }
    const auto isCancelled = [&epochTracker, queryId]() { return epochTracker.isStale(queryId); };

    auto isDriveEnabled = [&](char driveLetter) {
        if (enabledDrives.empty()) return true;
        for (char d : enabledDrives) {
            if (std::toupper(d) == std::toupper(driveLetter)) return true;
        }
        return false;
    };

    size_t totalIndexedFiles = 0;
    for (const auto& parser : g_MftParsers) {
        if (isDriveEnabled(parser->getDriveLetter())) {
            totalIndexedFiles += parser->getFileCount();
        }
    }

    if (wQuery.empty()) {
        return {
            {"results", nlohmann::json::array()},
            {"totalIndexedFiles", totalIndexedFiles},
            {"elapsedMs", 0}
        };
    }

    SearchExpression expr = SearchExpression::parse(wQuery);
    nlohmann::json responseJson = {{"results", nlohmann::json::array()}};

    auto runNameSearch = [&](const std::wstring& queryStr, size_t maxCount) -> std::vector<SearchResult> {
        std::vector<std::future<std::vector<SearchResult>>> futures;
        futures.reserve(g_MftParsers.size());
        for (auto& parser : g_MftParsers) {
            if (!isDriveEnabled(parser->getDriveLetter())) continue;
            futures.push_back(std::async(std::launch::async, [&parser, &queryStr, &excludeOpts, maxCount]() {
                return parser->Search(queryStr, static_cast<int>(maxCount), excludeOpts);
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
        if (results.size() > maxCount) results.resize(maxCount);
        return results;
    };

    auto runContentSearch = [&](const std::wstring& queryStr, const std::wstring& contentPattern, size_t maxCount) -> std::vector<nlohmann::json> {
        std::vector<SearchResult> candidates;
        for (auto& parser : g_MftParsers) {
            if (!isDriveEnabled(parser->getDriveLetter())) continue;
            if (isCancelled()) return std::vector<nlohmann::json>{};
            auto volumeResults = parser->Search(queryStr, 10000, excludeOpts);
            candidates.insert(candidates.end(),
                              std::make_move_iterator(volumeResults.begin()),
                              std::make_move_iterator(volumeResults.end()));
        }

        auto& contentEngine = easy::service::content::ContentSearchEngine::instance();
        std::vector<SearchResult> textCandidates;
        textCandidates.reserve(candidates.size());

        for (auto& candidate : candidates) {
            if (candidate.isDirectory) continue;
            if (candidate.fileSize > 50 * 1024 * 1024) continue;
            
            size_t dotPos = candidate.fullPath.rfind(L'.');
            if (dotPos == std::wstring::npos) continue;
            std::wstring ext = candidate.fullPath.substr(dotPos + 1);
            if (!contentEngine.canSearchContent(ext)) continue;

            if (candidate.fullPath.find(L"\\Windows\\WinSxS\\") != std::wstring::npos ||
                candidate.fullPath.find(L"\\Windows\\System32\\") != std::wstring::npos ||
                candidate.fullPath.find(L"\\$Recycle.Bin\\") != std::wstring::npos) {
                if (queryStr.find(L"windows") == std::wstring::npos && queryStr.find(L"winsxs") == std::wstring::npos) {
                    continue;
                }
            }
            textCandidates.push_back(std::move(candidate));
        }

        auto getPathPriority = [](const std::wstring& path) -> int {
            std::wstring p;
            p.reserve(path.size());
            for (wchar_t c : path) p.push_back(std::towlower(c));

            if (p.find(L"\\appdata\\local\\npm-cache") != std::wstring::npos ||
                p.find(L"\\appdata\\local\\pip") != std::wstring::npos ||
                p.find(L"\\appdata\\local\\go-build") != std::wstring::npos ||
                p.find(L"\\.gradle") != std::wstring::npos ||
                p.find(L"\\appdata\\local\\temp") != std::wstring::npos ||
                p.find(L"\\node_modules") != std::wstring::npos ||
                p.find(L"\\.git") != std::wstring::npos ||
                p.find(L"\\$recycle.bin") != std::wstring::npos ||
                p.find(L"\\windows") != std::wstring::npos ||
                p.find(L"\\虚拟机共享文件夹") != std::wstring::npos ||
                p.find(L"\\baidunetdiskdownload") != std::wstring::npos ||
                p.find(L"\\wxwork") != std::wstring::npos ||
                p.find(L"\\xwechat_files") != std::wstring::npos ||
                p.find(L"\\wechat files") != std::wstring::npos ||
                p.find(L"\\cefcache") != std::wstring::npos ||
                p.find(L"\\crashpad") != std::wstring::npos ||
                p.find(L"\\coverage_report") != std::wstring::npos ||
                p.find(L"\\extensions") != std::wstring::npos) {
                return 1;
            }
            if (p.find(L"\\appdata") != std::wstring::npos ||
                p.find(L"\\program files") != std::wstring::npos ||
                p.find(L"\\programdata") != std::wstring::npos) {
                return 20;
            }
            if (p.find(L"\\chosen") != std::wstring::npos ||
                p.find(L"\\repo") != std::wstring::npos ||
                p.find(L"\\sap_b1") != std::wstring::npos ||
                p.find(L"\\workspace") != std::wstring::npos ||
                p.find(L"\\projects") != std::wstring::npos ||
                p.find(L"\\source") != std::wstring::npos ||
                p.find(L"\\src") != std::wstring::npos) {
                return 2000;
            }
            if (p.find(L"\\desktop") != std::wstring::npos ||
                p.find(L"\\documents") != std::wstring::npos ||
                p.find(L"\\downloads") != std::wstring::npos) {
                return 1000;
            }
            if (p.size() >= 2 && p[1] == L':' && p[0] != L'c') {
                return 500;
            }
            return 200;
        };

        std::stable_sort(textCandidates.begin(), textCandidates.end(), [&](const auto& a, const auto& b) {
            int pA = getPathPriority(a.fullPath);
            int pB = getPathPriority(b.fullPath);
            if (pA != pB) return pA > pB;
            return a.lastWriteTime > b.lastWriteTime;
        });

        std::mutex resultsMutex;
        std::vector<nlohmann::json> contentResults;
        std::atomic<size_t> matchCount{0};
        const auto startTime = std::chrono::steady_clock::now();
        const auto deadline = startTime + std::chrono::milliseconds(10000);

        const unsigned int numThreads = (std::max)(2u, (std::min)(16u, std::thread::hardware_concurrency()));
        std::vector<std::thread> workers;
        std::atomic<size_t> nextIndex{0};

        for (unsigned int t = 0; t < numThreads; ++t) {
            workers.emplace_back([&]() {
                while (matchCount.load() < maxCount) {
                    if (std::chrono::steady_clock::now() > deadline) break;
                    // 用户已经继续打字，本次内容扫描的结果不会再被采用。
                    if (isCancelled()) break;
                    size_t idx = nextIndex.fetch_add(1);
                    if (idx >= textCandidates.size()) break;

                    const auto& item = textCandidates[idx];
                    std::vector<easy::service::content::ContentSnippet> snippets;
                    if (contentEngine.searchFile(item.fullPath, contentPattern, false, snippets)) {
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

                        std::lock_guard<std::mutex> lock(resultsMutex);
                        if (contentResults.size() < maxCount) {
                            contentResults.push_back(std::move(itemJson));
                            matchCount.fetch_add(1);
                        }
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }
        return contentResults;
    };

    bool isExplicitContent = expr.hasContentFilter() && !expr.getContentQuery().empty();
    std::wstring contentPattern = isExplicitContent ? expr.getContentQuery() : wQuery;

    if (isExplicitContent || searchMode == "content") {
        auto contentMatches = runContentSearch(isExplicitContent ? wQuery : (L"content:" + wQuery), contentPattern, requestedLimit);
        for (auto& item : contentMatches) {
            responseJson["results"].push_back(std::move(item));
        }
    } else if (searchMode == "both") {
        // 双搜模式：同时搜索文件名与文件内容
        auto nameMatches = runNameSearch(wQuery, requestedLimit);
        auto contentMatches = runContentSearch(L"content:" + wQuery, wQuery, requestedLimit);

        std::unordered_set<std::string> seenPaths;
        for (const auto& result : nameMatches) {
            std::string p = WStringToString(result.fullPath);
            seenPaths.insert(p);
            uint32_t rc = easy::service::db::RunHistoryManager::instance().getRunCount(result.fullPath);
            double fs = easy::service::db::RunHistoryManager::instance().calculateFrecencyScore(result.fullPath);
            responseJson["results"].push_back({
                {"name", WStringToString(result.fileName)},
                {"path", std::move(p)},
                {"isDirectory", result.isDirectory},
                {"size", result.fileSize},
                {"creationTime", result.creationTime},
                {"lastWriteTime", result.lastWriteTime},
                {"runCount", rc},
                {"frecencyScore", fs}
            });
        }

        for (auto& item : contentMatches) {
            std::string p = item["path"].get<std::string>();
            if (seenPaths.find(p) == seenPaths.end()) {
                seenPaths.insert(p);
                std::wstring wp = StringToWString(p);
                item["runCount"] = easy::service::db::RunHistoryManager::instance().getRunCount(wp);
                item["frecencyScore"] = easy::service::db::RunHistoryManager::instance().calculateFrecencyScore(wp);
                responseJson["results"].push_back(std::move(item));
            }
        }
    } else {
        // 仅搜文件名 (name 极速模式)
        auto nameMatches = runNameSearch(wQuery, requestedLimit);
        for (const auto& result : nameMatches) {
            uint32_t rc = easy::service::db::RunHistoryManager::instance().getRunCount(result.fullPath);
            double fs = easy::service::db::RunHistoryManager::instance().calculateFrecencyScore(result.fullPath);
            responseJson["results"].push_back({
                {"name", WStringToString(result.fileName)},
                {"path", WStringToString(result.fullPath)},
                {"isDirectory", result.isDirectory},
                {"size", result.fileSize},
                {"creationTime", result.creationTime},
                {"lastWriteTime", result.lastWriteTime},
                {"runCount", rc},
                {"frecencyScore", fs}
            });
        }
    }

    const auto overallEndTime = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(overallEndTime - overallStartTime).count();

    responseJson["totalIndexedFiles"] = totalIndexedFiles;
    responseJson["elapsedMs"] = elapsedMs;
    responseJson["queryId"] = queryId;
    responseJson["cancelled"] = isCancelled();

    return responseJson;
}

namespace {

}  // namespace

void PipeWorkerThread(PSECURITY_DESCRIPTOR pipeDescriptor, SECURITY_ATTRIBUTES* pipeSecurity) {
    namespace frame = easy::service::pipe;

    while (g_IsRunning.load()) {
        HANDLE hPipe = CreateNamedPipeA(
            g_SearchPipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            frame::IoChunkBytes, frame::IoChunkBytes, 0,
            pipeDescriptor ? pipeSecurity : nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && g_IsRunning.load()) {
            while (g_IsRunning.load()) {
                char header[frame::HeaderSize] = {};
                if (!frame::readExact(hPipe, header, sizeof(header))) break;

                uint32_t requestBytes = 0;
                if (!frame::decodeFrameHeader(header, requestBytes, frame::MaxRequestBytes)) {
                    spdlog::warn("Pipe: 请求帧头非法, 断开该连接");
                    break;
                }

                std::string request(requestBytes, '\0');
                if (!frame::readExact(hPipe, request.data(), requestBytes)) break;

                nlohmann::json responseJson = ProcessSearchQuery(StringToWString(request));
                const std::string response = responseJson.dump();
                if (!frame::fitsInFrame(response.size())) {
                    spdlog::error("Pipe: 响应超出单帧上限 ({} 字节), 断开该连接", response.size());
                    break;
                }

                const auto responseHeader =
                    frame::encodeFrameHeader(static_cast<uint32_t>(response.size()));
                if (!frame::writeExact(hPipe, responseHeader.data(), responseHeader.size())) break;
                if (!frame::writeExact(hPipe, response.data(), response.size())) break;
            }
        }
        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

void IPCServerThread() {
    spdlog::info("IPC Server Thread started.");

    // Build the DACL before creating any pipe instance. Publishing the endpoint
    // early avoids SCM/portable duplicate starts, but it must remain private to
    // the requesting SID even if later disk initialization is slow.
    PSECURITY_DESCRIPTOR pipeDescriptor = nullptr;
    SECURITY_ATTRIBUTES pipeSecurity{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    const auto securitySddl = easy::service::pipe_endpoint::makeSecurityDescriptor(g_SearchClientSid);
    if (!securitySddl || !ConvertStringSecurityDescriptorToSecurityDescriptorW(
            securitySddl->c_str(), SDDL_REVISION_1, &pipeDescriptor, nullptr)) {
        const DWORD securityError = GetLastError();
        spdlog::error("Failed to create restricted named pipe security descriptor: {}", securityError);
        g_IsRunning.store(false, std::memory_order_release);
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) SetEvent(g_ServiceStopEvent);
        return;
    }
    pipeSecurity.lpSecurityDescriptor = pipeDescriptor;

    std::vector<std::thread> pipeWorkers;
    pipeWorkers.reserve(NumPipeWorkers);
    for (int i = 0; i < NumPipeWorkers; ++i) {
        pipeWorkers.emplace_back(PipeWorkerThread, pipeDescriptor, &pipeSecurity);
    }
    spdlog::info("Launched {} restricted named pipe worker(s) before index initialization", NumPipeWorkers);

    // 初始化历史与快照数据库管理器
    easy::service::db::RunHistoryManager::instance().init();
    easy::service::db::SearchHistoryManager::instance().init();
    easy::service::db::DatabaseManager::instance().init();

    // 扫描所有本地驱动器
    const DWORD driveMask = GetLogicalDrives();
    for (char drive = 'A'; drive <= 'Z' && g_IsRunning.load(); ++drive) {
        if (!(driveMask & (1u << (drive - 'A')))) continue;
        const std::wstring root{static_cast<wchar_t>(drive), L':', L'\\'};
        UINT driveType = GetDriveTypeW(root.c_str());
        // 后台服务只自动索引本地固定卷。远程盘/可移动盘可能离线、休眠或在目录枚举
        // 中无限期阻塞，不能拖慢登录和服务关停；后续应由显式的按需索引入口接入。
        if (driveType != DRIVE_FIXED) {
            if (driveType == DRIVE_REMOTE || driveType == DRIVE_REMOVABLE) {
                spdlog::info("Skipping non-fixed drive {}: during automatic indexing", drive);
            }
            continue;
        }

        auto parser = std::make_unique<MftParser>();
        if (parser->Initialize(drive)) {
            g_MftParsers.push_back(std::move(parser));
        }
    }

    // 尝试从 EasyTools.db 二进制快照秒级冷启动 (< 30ms)
    std::vector<MftParser*> rawParsers;
    for (auto& p : g_MftParsers) rawParsers.push_back(p.get());

    bool snapshotLoaded = easy::service::db::DatabaseManager::instance().loadSnapshot(rawParsers);
    std::vector<std::thread> indexThreads;

    if (snapshotLoaded) {
        spdlog::info("Successfully loaded database snapshot from EasyTools.db, checking volume readiness...");
        bool anyMissing = false;
        for (auto* pRaw : rawParsers) {
            if (pRaw->getFileCount() == 0) {
                anyMissing = true;
                char drive = pRaw->getDriveLetter();
                g_InitialIndexWorkers.fetch_add(1, std::memory_order_relaxed);
                indexThreads.emplace_back([pRaw, drive]() {
                    try {
                        pRaw->EnumerateFiles();
                        if (g_IsRunning.load(std::memory_order_acquire)) pRaw->StartListening();
                        spdlog::info("Indexed drive {}: (volume was unpopulated in snapshot)", drive);
                    } catch (const std::exception& e) {
                        spdlog::error("Initial indexing failed on drive {}: {}", drive, e.what());
                    } catch (...) {
                        spdlog::error("Initial indexing failed on drive {} with unknown exception", drive);
                    }
                    g_InitialIndexWorkers.fetch_sub(1, std::memory_order_release);
                });
            } else {
                pRaw->StartListening();
            }
        }
        if (anyMissing) scheduleSnapshot(rawParsers, "Updated complete");
    } else {
        spdlog::info("No valid database snapshot found, performing full MFT scan and building initial index...");
        for (auto* pRaw : rawParsers) {
            char drive = pRaw->getDriveLetter();
            g_InitialIndexWorkers.fetch_add(1, std::memory_order_relaxed);
            indexThreads.emplace_back([pRaw, drive]() {
                try {
                    pRaw->EnumerateFiles();
                    if (g_IsRunning.load(std::memory_order_acquire)) pRaw->StartListening();
                    spdlog::info("Indexed drive {}:", drive);
                } catch (const std::exception& e) {
                    spdlog::error("Initial indexing failed on drive {}: {}", drive, e.what());
                } catch (...) {
                    spdlog::error("Initial indexing failed on drive {} with unknown exception", drive);
                }
                g_InitialIndexWorkers.fetch_sub(1, std::memory_order_release);
            });
        }
        // 初始全量索引完成后在后台异步保存快照
        scheduleSnapshot(rawParsers, "Initial");
    }
    spdlog::info("Search index launched for {} volume(s)", g_MftParsers.size());
    g_SearchIndexReady.store(true, std::memory_order_release);

    for (auto& w : pipeWorkers) {
        if (w.joinable()) w.join();
    }
    g_SearchIndexReady.store(false, std::memory_order_release);
    if (g_PipePokeJob.joinable()) {
        g_PipePokeJob.request_stop();
        g_PipePokeJob.join();
    }
    stopBackgroundJobs();
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

    if (!configurePipeEndpoint(argc, argv)) {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = ERROR_INVALID_PARAMETER;
        g_ServiceStatus.dwCheckPoint = 1;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

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

    RequestServiceShutdown();
    if (ipcThread.joinable()) ipcThread.join();

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    spdlog::info("Service stopped successfully.");
}

// 索引持有 USN 日志游标与内存索引，多开会导致重复扫描和不一致。互斥量在进程
// 退出时由内核释放，因此崩溃不会留下永久阻塞；创建失败则必须安全失败，不能放行
// 第二个服务实例。
enum class SingleInstanceLockResult {
    Acquired,
    AlreadyRunning,
    Failed,
};

static SingleInstanceLockResult AcquireSingleInstanceLock() {
    static HANDLE lock = nullptr;
    lock = CreateMutexW(nullptr, TRUE, L"Local\\EasyTools_SearchService_Singleton");
    if (!lock) {
        spdlog::error("Unable to acquire search-service singleton mutex: {}", GetLastError());
        return SingleInstanceLockResult::Failed;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(lock);
        lock = nullptr;
        return SingleInstanceLockResult::AlreadyRunning;
    }
    return SingleInstanceLockResult::Acquired;
}

int main(int argc, char** argv) {
    InitLogger();
    const auto lockResult = AcquireSingleInstanceLock();
    if (lockResult == SingleInstanceLockResult::AlreadyRunning) {
        spdlog::info("Another EasyTools_Service instance is already running, exiting.");
        return 0;
    }
    if (lockResult == SingleInstanceLockResult::Failed) return 1;

    if (argc > 1 && std::string(argv[1]) == "--debug") {
        int wideArgc = 0;
        LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
        const bool configured = wideArgv && configurePipeEndpoint(static_cast<DWORD>(wideArgc), wideArgv);
        if (wideArgv) LocalFree(wideArgv);
        if (!configured) return 1;
        spdlog::info("Running in debug mode (Console).");
        g_IsRunning = true;
        std::thread ipcThread(IPCServerThread);
        std::cout << "Press ENTER to stop..." << std::endl;
        std::cin.get();
        RequestServiceShutdown();
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
            int wideArgc = 0;
            LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
            const bool configured = wideArgv && configurePipeEndpoint(static_cast<DWORD>(wideArgc), wideArgv);
            if (wideArgv) LocalFree(wideArgv);
            if (!configured) return 1;
            spdlog::info("Running in standalone background mode (per-user named pipe server).");
            g_IsRunning = true;
            IPCServerThread();
            return 0;
        }
        spdlog::error("StartServiceCtrlDispatcher failed: {}.", err);
        return 1;
    }
    return 0;
}
