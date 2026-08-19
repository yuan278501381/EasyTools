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
#include "db/RunHistoryManager.h"
#include "db/SearchHistoryManager.h"
#include "db/DatabaseManager.h"

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
    std::string searchMode = "name";
    std::vector<char> enabledDrives;
    SearchExcludeOptions excludeOpts;
    size_t requestedLimit = 100;

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
                            pRaw->StartListening();
                            std::vector<MftParser*> allParsers;
                            for (auto& p : g_MftParsers) allParsers.push_back(p.get());
                            easy::service::db::DatabaseManager::instance().saveSnapshot(allParsers);
                        }).detach();
                    }
                    return {{"success", true}, {"rebuilding", true}};
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
                    return {
                        {"success", true},
                        {"dbPath", WStringToString(stats.dbPath)},
                        {"dbSize", stats.fileSize},
                        {"timestamp", stats.timestamp},
                        {"totalRecords", stats.totalRecords},
                        {"volumeCount", stats.volumeCount},
                        {"exists", stats.exists},
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
            }

            if (reqJson.contains("query") && reqJson["query"].is_string()) {
                wQuery = StringToWString(reqJson["query"].get<std::string>());
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
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOTE &&
            driveType != DRIVE_REMOVABLE && driveType != DRIVE_RAMDISK) continue;

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
                indexThreads.emplace_back([pRaw, drive]() {
                    pRaw->EnumerateFiles();
                    pRaw->StartListening();
                    spdlog::info("Indexed drive {}: (volume was unpopulated in snapshot)", drive);
                });
            } else {
                pRaw->StartListening();
            }
        }
        if (anyMissing) {
            std::thread([rawParsers]() {
                Sleep(5000);
                easy::service::db::DatabaseManager::instance().saveSnapshot(rawParsers);
                spdlog::info("Updated complete database snapshot saved to EasyTools.db");
            }).detach();
        }
    } else {
        spdlog::info("No valid database snapshot found, performing full MFT scan and building initial index...");
        for (auto* pRaw : rawParsers) {
            char drive = pRaw->getDriveLetter();
            indexThreads.emplace_back([pRaw, drive]() {
                pRaw->EnumerateFiles();
                pRaw->StartListening();
                spdlog::info("Indexed drive {}:", drive);
            });
        }
        // 初始全量索引完成后在后台异步保存快照
        std::thread([rawParsers]() {
            Sleep(5000);
            easy::service::db::DatabaseManager::instance().saveSnapshot(rawParsers);
            spdlog::info("Initial database snapshot saved to EasyTools.db");
        }).detach();
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
