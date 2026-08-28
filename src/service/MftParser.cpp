#include "MftParser.h"
#include "PinyinEngine.h"
#include "content/ContentSearchEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <cwctype>

#define BUF_LEN 65536

namespace {

std::wstring normalize(std::wstring_view value) {
    return SearchExpression::normalize(value);
}

uint64_t fileTimeToUint64(const FILETIME& value) noexcept {
    ULARGE_INTEGER raw{};
    raw.LowPart = value.dwLowDateTime;
    raw.HighPart = value.dwHighDateTime;
    return raw.QuadPart;
}

// FSCTL_ENUM_USN_DATA is deliberately fast, but its records do not contain a
// file's size or creation time; USN_RECORD::TimeStamp is also the journal event
// time rather than the authoritative last-write time. Hydrate only the small
// final result set so the index stays compact while every displayed property is
// accurate.
void hydrateResultProperties(SearchResult& result) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(result.fullPath.c_str(), GetFileExInfoStandard, &data)) {
        return;
    }

    result.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    result.creationTime = fileTimeToUint64(data.ftCreationTime);
    result.lastWriteTime = fileTimeToUint64(data.ftLastWriteTime);
    result.fileSize = result.isDirectory
        ? 0
        : (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

}  // namespace

MftParser::MftParser() : m_DriveLetter('C'), m_hVolume(INVALID_HANDLE_VALUE) {
}

MftParser::~MftParser() {
    StopListening();
    if (m_hVolume != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hVolume);
    }
}

size_t MftParser::getFileCount() const {
    std::shared_lock lock(m_MapMutex);
    return m_Store.size();
}

uint64_t MftParser::getApproximateIndexBytes() const {
    std::shared_lock lock(m_MapMutex);
    return m_Store.approximateBytes();
}

void MftParser::UsnListenerLoop() {
    READ_USN_JOURNAL_DATA_V0 rujd = {0};
    rujd.StartUsn = m_UsnJournalData.NextUsn;
    rujd.ReasonMask = 0xFFFFFFFF; // All reasons
    rujd.ReturnOnlyOnClose = FALSE;
    rujd.Timeout = 0;
    rujd.BytesToWaitFor = 0;
    rujd.UsnJournalID = m_UsnJournalData.UsnJournalID;

    std::vector<BYTE> buffer(BUF_LEN);
    DWORD bytesReturned = 0;

    while (m_IsListening) {
        if (!DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &rujd, sizeof(rujd), buffer.data(), BUF_LEN, &bytesReturned, NULL)) {
            Sleep(100);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            Sleep(50);
            continue;
        }
        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = reinterpret_cast<USN*>(buffer.data());
        PUSN_RECORD_V2 pRecord = reinterpret_cast<PUSN_RECORD_V2>(buffer.data() + sizeof(USN));

        if (dwRetBytes > 0) {
            std::unique_lock lock(m_MapMutex);
            bool changed = false;
            while (dwRetBytes >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength <= dwRetBytes) {
                if (pRecord->Reason & USN_REASON_FILE_CREATE || pRecord->Reason & USN_REASON_RENAME_NEW_NAME) {
                    FileRecordInit record;
                    record.fileReferenceNumber = pRecord->FileReferenceNumber;
                    record.parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
                    record.isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    int nameLen = pRecord->FileNameLength / 2;
                    record.fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);

                    m_Store.upsert(record);
                    changed = true;
                } 
                else if (pRecord->Reason & USN_REASON_FILE_DELETE) {
                    changed = m_Store.erase(pRecord->FileReferenceNumber) || changed;
                }

                dwRetBytes -= pRecord->RecordLength;
                pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
            }
            if (changed) {
                m_Store.compactIfSparse();
                m_IndexGeneration.fetch_add(1, std::memory_order_release);
            }
        }
        rujd.StartUsn = *pUsn;
        Sleep(50); // Prevent 100% CPU on fast changes
    }
}

void MftParser::StartListening() {
    if (m_IsFallbackDirectoryWalk || m_hVolume == INVALID_HANDLE_VALUE) {
        return;
    }
    m_IsListening = true;
    m_ListenerThread = std::make_unique<std::thread>(&MftParser::UsnListenerLoop, this);
}

void MftParser::StopListening() {
    m_IsListening = false;
    if (m_ListenerThread && m_ListenerThread->joinable()) {
        m_ListenerThread->join();
    }
}

bool MftParser::Initialize(char driveLetter) {
    resetStopRequest();
    m_DriveLetter = driveLetter;
    const std::wstring root{static_cast<wchar_t>(driveLetter), L':', L'\\'};
    m_DriveType = GetDriveTypeW(root.c_str());

    std::string volumePath = "\\\\.\\";
    volumePath += driveLetter;
    volumePath += ":";

    // 优先尝试以直接 NTFS 卷设备句柄打开以实现 100ms 级 MFT 全盘枚举
    m_hVolume = CreateFileA(volumePath.c_str(), 
                            GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            NULL, 
                            OPEN_EXISTING, 
                            FILE_ATTRIBUTE_NORMAL, 
                            NULL);

    if (m_hVolume == INVALID_HANDLE_VALUE) {
        m_hVolume = CreateFileA(volumePath.c_str(), 
                                GENERIC_READ, 
                                FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                NULL, 
                                OPEN_EXISTING, 
                                FILE_ATTRIBUTE_NORMAL, 
                                NULL);
    }

    if (m_hVolume == INVALID_HANDLE_VALUE || !QueryUsnJournal()) {
        if (m_hVolume != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hVolume);
            m_hVolume = INVALID_HANDLE_VALUE;
        }
        // 当以非管理员或便携模式运行时，自动启用极速目录树遍历作为优雅回退
        m_IsFallbackDirectoryWalk = true;
        spdlog::warn("Volume handle unavailable (Error: {}). Enabled DirectoryWalk fallback mode for drive {}:",
                     GetLastError(), driveLetter);
        return true;
    }

    m_IsFallbackDirectoryWalk = false;
    return true;
}

bool MftParser::QueryUsnJournal() {
    DWORD br;
    if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &m_UsnJournalData, sizeof(m_UsnJournalData), &br, NULL)) {
        // Try creating it
        CREATE_USN_JOURNAL_DATA cujd;
        cujd.MaximumSize = 0; // default
        cujd.AllocationDelta = 0; // default
        if (!DeviceIoControl(m_hVolume, FSCTL_CREATE_USN_JOURNAL, &cujd, sizeof(cujd), NULL, 0, &br, NULL)) {
            return false;
        }
        // Try query again
        if (!DeviceIoControl(m_hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &m_UsnJournalData, sizeof(m_UsnJournalData), &br, NULL)) {
            return false;
        }
    }
    return true;
}

void MftParser::EnumerateFilesViaDirectoryWalk(char driveLetter) {
    spdlog::info("Starting high-speed directory walk enumeration for drive {}: ...", driveLetter);
    std::wstring rootPath;
    rootPath += static_cast<wchar_t>(driveLetter);
    rootPath += L":\\";

    uint64_t nextId = 1;
    std::vector<std::pair<std::wstring, uint64_t>> dirsToScan;
    dirsToScan.reserve(8192);
    dirsToScan.push_back({rootPath, 0});

    int count = 0;
    std::vector<FileRecordInit> batch;
    batch.reserve(2048);

    auto flushBatch = [&]() {
        if (batch.empty()) return;
        std::unique_lock lock(m_MapMutex);
        for (const auto& item : batch) {
            m_Store.upsert(item);
        }
        batch.clear();
        m_IndexGeneration.fetch_add(1, std::memory_order_release);
    };

    while (!dirsToScan.empty() && !m_StopRequested.load(std::memory_order_acquire)) {
        auto [currentDir, parentId] = std::move(dirsToScan.back());
        dirsToScan.pop_back();

        std::wstring searchPattern = currentDir;
        if (searchPattern.back() != L'\\') searchPattern += L'\\';
        searchPattern += L'*';

        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileExW(
            searchPattern.c_str(),
            FindExInfoBasic,
            &findData,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH
        );

        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (m_StopRequested.load(std::memory_order_acquire)) break;
            if (findData.cFileName[0] == L'.' && 
                (findData.cFileName[1] == L'\0' || (findData.cFileName[1] == L'.' && findData.cFileName[2] == L'\0'))) {
                continue;
            }

            // 跳过 NTFS 重解析点/符号链接，防止目录循环与死锁
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                continue;
            }

            const bool isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const uint64_t fileId = nextId++;

            FileRecordInit record;
            record.fileReferenceNumber = fileId;
            record.parentFileReferenceNumber = parentId;
            record.isDirectory = isDir;
            record.fileAttributes = findData.dwFileAttributes;
            record.fileSize = isDir ? 0 : ((static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow);
            record.creationTime = (static_cast<uint64_t>(findData.ftCreationTime.dwHighDateTime) << 32) | findData.ftCreationTime.dwLowDateTime;
            record.lastWriteTime = (static_cast<uint64_t>(findData.ftLastWriteTime.dwHighDateTime) << 32) | findData.ftLastWriteTime.dwLowDateTime;
            record.fileName = findData.cFileName;

            if (isDir) {
                std::wstring subDir = currentDir;
                if (subDir.back() != L'\\') subDir += L'\\';
                subDir += findData.cFileName;
                dirsToScan.push_back({std::move(subDir), fileId});
            }

            batch.push_back(std::move(record));
            count++;

            if (batch.size() >= 2048) {
                flushBatch();
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    flushBatch();
    spdlog::info("Directory walk enumeration completed. Indexed {} files on drive {}:", count, driveLetter);
}

void MftParser::EnumerateFiles() {
    if (m_StopRequested.load(std::memory_order_acquire)) return;
    {
        std::unique_lock lock(m_MapMutex);
        m_Store.clear();
    }
    if (m_IsFallbackDirectoryWalk || m_hVolume == INVALID_HANDLE_VALUE) {
        EnumerateFilesViaDirectoryWalk(m_DriveLetter);
        return;
    }

    spdlog::info("Starting MFT enumeration...");
    MFT_ENUM_DATA_V0 med;
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = m_UsnJournalData.NextUsn;

    std::vector<BYTE> buffer(BUF_LEN);
    DWORD bytesReturned = 0;
    int count = 0;

    while (!m_StopRequested.load(std::memory_order_acquire) &&
           DeviceIoControl(m_hVolume, FSCTL_ENUM_USN_DATA, &med, sizeof(med), buffer.data(), BUF_LEN, &bytesReturned, NULL)) {
        if (bytesReturned <= sizeof(USN)) break;
        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = reinterpret_cast<USN*>(buffer.data());
        PUSN_RECORD_V2 pRecord = reinterpret_cast<PUSN_RECORD_V2>(buffer.data() + sizeof(USN));
        
        std::unique_lock lock(m_MapMutex);
        while (!m_StopRequested.load(std::memory_order_relaxed) &&
               dwRetBytes >= sizeof(USN_RECORD_V2) &&
               pRecord->RecordLength >= sizeof(USN_RECORD_V2) &&
               pRecord->RecordLength <= dwRetBytes) {
            FileRecordInit record;
            record.fileReferenceNumber = pRecord->FileReferenceNumber;
            record.parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
            record.isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            record.fileAttributes = pRecord->FileAttributes;
            record.lastWriteTime = static_cast<uint64_t>(pRecord->TimeStamp.QuadPart);
            
            int nameLen = pRecord->FileNameLength / 2;
            record.fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);

            m_Store.upsert(record);
            count++;

            dwRetBytes -= pRecord->RecordLength;
            pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
        }
        med.StartFileReferenceNumber = *pUsn;
    }
    {
        std::unique_lock lock(m_MapMutex);
        // Names repeat heavily across a volume, so interning pays for itself
        // during enumeration; afterwards only a trickle of USN updates arrives
        // and the lookup table is no longer worth its footprint.
        m_Store.releaseBuildScratch();
    }
    m_IndexGeneration.fetch_add(1, std::memory_order_release);
    const uint64_t storeBytes = m_Store.approximateBytes();
    spdlog::info("Drive {}: MFT enumeration completed. Indexed {} files ({} MB records+names, {} B/file)",
                 m_DriveLetter, m_Store.size(), storeBytes / (1024 * 1024),
                 m_Store.size() ? storeBytes / m_Store.size() : 0);
}

std::vector<SearchResult> MftParser::Search(const std::wstring& query, int limit,
                                           const SearchExcludeOptions& excludeOpts) {
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    const std::wstring lowerQuery = normalize(query);
    const auto expr = SearchExpression::parse(query);

    std::shared_lock lock(m_MapMutex);
    const uint64_t generation = m_IndexGeneration.load(std::memory_order_acquire);

    auto testCandidate = [&](DWORDLONG id, const FileRecord& record) {
        if (excludeOpts.excludeHidden && (record.fileAttributes & FILE_ATTRIBUTE_HIDDEN)) return false;
        if (excludeOpts.excludeSystem && (record.fileAttributes & FILE_ATTRIBUTE_SYSTEM)) return false;

        std::wstring lazyPath;
        bool hasLazyPath = false;
        auto getPath = [&]() -> const std::wstring& {
            if (!hasLazyPath) {
                lazyPath = buildFullPath(id);
                hasLazyPath = true;
            }
            return lazyPath;
        };

        // 1. 若为全文内容搜索模式，提前剪枝目录与不支持的二进制/媒体格式
        if (expr.hasContentFilter()) {
            if (record.isDirectory) return false;
            size_t dotPos = record.fileName.rfind(L'.');
            if (dotPos == std::wstring::npos) return false;
            std::wstring_view ext = std::wstring_view(record.fileName).substr(dotPos + 1);
            if (!easy::service::content::ContentSearchEngine::instance().canSearchContent(ext)) {
                return false;
            }
        }

        // 2. 优先执行毫秒级纯内存表达式比对 (绝大部分非匹配文件在此立即返回 false)
        if (!expr.matchesWithLazyPath(record, static_cast<wchar_t>(m_DriveLetter), getPath)) {
            return false;
        }

        // 2. 仅对命中的极少数候选执行排除规则过滤 (避免对 265 万未命中文件强制构建全路径)
        if (!excludeOpts.patterns.empty()) {
            const std::wstring& p = getPath();
            for (const auto& pat : excludeOpts.patterns) {
                if (pat.empty()) continue;
                if (p.find(pat) != std::wstring::npos || record.normalizedName.find(pat) != std::wstring::npos) {
                    return false;
                }
            }
        }

        return true;
    };

    std::vector<DWORDLONG> candidates;
    bool canNarrow = false;
    {
        std::lock_guard<std::mutex> cacheLock(m_SearchCacheMutex);
        if (generation == m_CachedGeneration && !m_CachedQuery.empty() &&
            !expr.requiresFullPath() &&
            lowerQuery.rfind(m_CachedQuery, 0) == 0 &&
            lowerQuery.find_first_of(L"*?|/\\: \t\r\n") == std::wstring::npos &&
            m_CachedQuery.find_first_of(L"*?|/\\: \t\r\n") == std::wstring::npos) {
            candidates = m_CachedCandidates;
            canNarrow = true;
        }
    }

    auto calculateFolderPriority = [this](DWORDLONG parentRef) -> int {
        if (parentRef == 0) return 200;
        int priority = (m_DriveLetter != 'C' && m_DriveLetter != 'c') ? 500 : 200;
        DWORDLONG current = parentRef;
        for (size_t depth = 0; depth < 32; ++depth) {
            const auto* node = m_Store.find(current);
            if (!node) break;
            const FileRecord rec = m_Store.view(*node);
            if (rec.fileName.empty()) break;
            
            std::wstring name = normalize(rec.fileName);
            if (name == L"npm-cache" || name == L"pip" || name == L"go-build" ||
                name == L".gradle" || name == L"temp" || name == L"windows" ||
                name == L"$recycle.bin" || name == L"node_modules" || name == L".git" ||
                name == L"虚拟机共享文件夹" || name == L"baidunetdiskdownload" ||
                name == L"wxwork" || name == L"xwechat_files" || name == L"wechat files" ||
                name == L"cefcache" || name == L"crashpad" || name == L"coverage_report" ||
                name == L"extensions") {
                return 1;
            }
            if (name == L"appdata" || name == L"program files" || name == L"programdata") {
                priority = (std::min)(priority, 20);
            } else if (name == L"chosen" || name == L"repo" || name == L"sap_b1" ||
                       name == L"workspace" || name == L"projects" || name == L"source" ||
                       name == L"src") {
                priority = (std::max)(priority, 2000);
            } else if (name == L"desktop" || name == L"documents" || name == L"downloads") {
                priority = (std::max)(priority, 1000);
            }
            if (rec.parentFileReferenceNumber == current || rec.parentFileReferenceNumber == 0) break;
            current = rec.parentFileReferenceNumber;
        }
        return priority;
    };

    // FileRecord is a view over the arena, so copying one is a handful of
    // pointers and never allocates.
    struct RankedCandidate {
        DWORDLONG id;
        FileRecord record;
        int rank;
        int folderPriority;
    };
    std::vector<RankedCandidate> ranked;
    const bool isContentSearch = expr.hasContentFilter();

    if (canNarrow) {
        std::vector<DWORDLONG> filtered;
        filtered.reserve(std::min<size_t>(candidates.size(), 65536));
        ranked.reserve(std::min<size_t>(candidates.size(), 65536));
        for (const auto id : candidates) {
            const auto* stored = m_Store.find(id);
            if (!stored) continue;
            const FileRecord record = m_Store.view(*stored);
            if (testCandidate(id, record)) {
                filtered.push_back(id);
                int fPriority = isContentSearch ? calculateFolderPriority(record.parentFileReferenceNumber) : 0;
                ranked.push_back({id, record, expr.calculateRank(record), fPriority});
            }
        }
        candidates = std::move(filtered);
    } else {
        const size_t totalRecords = m_Store.slotCount();
        unsigned int numThreads = std::max(1u, std::min(std::thread::hardware_concurrency(), 16u));
        if (totalRecords < 10000) numThreads = 1;

        std::vector<std::vector<DWORDLONG>> threadCandidates(numThreads);
        std::vector<std::vector<RankedCandidate>> threadRanked(numThreads);

        size_t chunkSize = (totalRecords + numThreads - 1) / numThreads;
        std::vector<std::thread> workers;
        workers.reserve(numThreads);

        for (unsigned int t = 0; t < numThreads; ++t) {
            size_t startIdx = t * chunkSize;
            size_t endIdx = std::min(startIdx + chunkSize, totalRecords);
            if (startIdx >= endIdx) break;

            workers.emplace_back([&, t, startIdx, endIdx]() {
                auto& localCand = threadCandidates[t];
                auto& localRank = threadRanked[t];
                localCand.reserve(std::min<size_t>((endIdx - startIdx) / 16 + 64, 8192));
                localRank.reserve(std::min<size_t>((endIdx - startIdx) / 16 + 64, 8192));

                for (size_t i = startIdx; i < endIdx; ++i) {
                    const auto* stored = m_Store.at(i);
                    if (!stored) continue;
                    const FileRecord record = m_Store.view(*stored);
                    DWORDLONG id = record.fileReferenceNumber;
                    if (testCandidate(id, record)) {
                        localCand.push_back(id);
                        int fPriority = isContentSearch ? calculateFolderPriority(record.parentFileReferenceNumber) : 0;
                        localRank.push_back({id, record, expr.calculateRank(record), fPriority});
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        size_t totalFound = 0;
        for (unsigned int t = 0; t < numThreads; ++t) {
            totalFound += threadCandidates[t].size();
        }

        candidates.reserve(totalFound);
        ranked.reserve(totalFound);
        for (unsigned int t = 0; t < numThreads; ++t) {
            candidates.insert(candidates.end(),
                              std::make_move_iterator(threadCandidates[t].begin()),
                              std::make_move_iterator(threadCandidates[t].end()));
            ranked.insert(ranked.end(),
                          std::make_move_iterator(threadRanked[t].begin()),
                          std::make_move_iterator(threadRanked[t].end()));
        }
    }
    const auto compareRank = [&expr](const auto& a, const auto& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (expr.hasContentFilter()) {
            if (a.folderPriority != b.folderPriority) return a.folderPriority > b.folderPriority;
            return a.record.lastWriteTime > b.record.lastWriteTime;
        }
        if (a.record.normalizedName.size() != b.record.normalizedName.size())
            return a.record.normalizedName.size() < b.record.normalizedName.size();
        return a.record.normalizedName < b.record.normalizedName;
    };
    const size_t resultCount = std::min(ranked.size(), static_cast<size_t>(limit));
    if (resultCount < ranked.size()) {
        std::partial_sort(ranked.begin(), ranked.begin() + resultCount, ranked.end(), compareRank);
    } else {
        std::sort(ranked.begin(), ranked.end(), compareRank);
    }

    {
        std::lock_guard<std::mutex> cacheLock(m_SearchCacheMutex);
        if (!expr.requiresFullPath() &&
            lowerQuery.find_first_of(L"*?|/\\: \t\r\n") == std::wstring::npos) {
            m_CachedQuery = lowerQuery;
            m_CachedCandidates = std::move(candidates);
            m_CachedGeneration = generation;
        } else {
            m_CachedQuery.clear();
            m_CachedCandidates.clear();
        }
    }

    results.reserve(resultCount);
    for (size_t i = 0; i < resultCount; ++i) {
        const auto& candidate = ranked[i];
        results.push_back({
            std::wstring(candidate.record.fileName),
            buildFullPath(candidate.id),
            candidate.record.isDirectory,
            candidate.record.fileSize,
            candidate.record.creationTime,
            candidate.record.lastWriteTime
        });
    }

    // Never hold the index lock across filesystem I/O. The result owns its
    // strings and can safely be enriched after concurrent USN updates resume.
    lock.unlock();
    for (auto& result : results) {
        hydrateResultProperties(result);
    }
    return results;
}

std::wstring MftParser::buildFullPath(DWORDLONG fileReferenceNumber) const {
    const auto* stored = m_Store.find(fileReferenceNumber);
    if (!stored) return L"";

    std::vector<std::wstring_view> parts;
    parts.reserve(16);
    DWORDLONG current = fileReferenceNumber;
    for (size_t depth = 0; depth < 512; ++depth) {
        const auto* node = m_Store.find(current);
        if (!node) break;
        const FileRecord rec = m_Store.view(*node);
        if (!rec.fileName.empty() && rec.fileName != L".") {
            parts.push_back(rec.fileName);
        }
        if (rec.parentFileReferenceNumber == current || rec.parentFileReferenceNumber == 0) break;
        current = rec.parentFileReferenceNumber;
    }

    std::wstring path;
    path.reserve(256);
    path += static_cast<wchar_t>(m_DriveLetter);
    path += L":\\";
    for (auto pit = parts.rbegin(); pit != parts.rend(); ++pit) {
        if (path.size() > 3 && path.back() != L'\\') path += L'\\';
        path += *pit;
    }
    return path;
}

uint64_t MftParser::getCurrentUsn() const {
    return m_UsnJournalData.NextUsn;
}

uint32_t MftParser::getVolumeSerialNumber() const {
    std::wstring root;
    root += static_cast<wchar_t>(m_DriveLetter);
    root += L":\\";
    DWORD serial = 0;
    GetVolumeInformationW(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    return serial;
}

void MftParser::exportSnapshot(const SnapshotVisitor& visit, uint64_t& outLastUsn, uint32_t& outVolumeSerial) const {
    std::shared_lock lock(m_MapMutex);
    if (visit) {
        m_Store.forEach([&](const easy::service::StoredFileRecord& record) {
            visit(m_Store.view(record));
        });
    }
    outLastUsn = m_UsnJournalData.NextUsn;
    outVolumeSerial = getVolumeSerialNumber();
}

bool MftParser::importSnapshot(const SnapshotProducer& next, size_t expectedRecords,
                               uint64_t lastUsn, uint32_t volumeSerial) {
    (void)volumeSerial;
    if (!next) return false;

    std::unique_lock lock(m_MapMutex);
    m_Store.clear();
    m_Store.reserve(expectedRecords);

    FileRecordInit scratch;
    while (true) {
        scratch.fileName.clear();
        scratch.fileReferenceNumber = 0;
        scratch.parentFileReferenceNumber = 0;
        scratch.isDirectory = false;
        scratch.fileAttributes = 0;
        scratch.fileSize = 0;
        scratch.creationTime = 0;
        scratch.lastWriteTime = 0;
        if (!next(scratch)) break;
        m_Store.upsert(scratch);
    }
    m_Store.releaseBuildScratch();

    m_UsnJournalData.NextUsn = lastUsn;
    m_IndexGeneration++;
    return true;
}

bool MftParser::importSnapshot(std::vector<FileRecordInit>&& records, uint64_t lastUsn, uint32_t volumeSerial) {
    size_t cursor = 0;
    const size_t total = records.size();
    return importSnapshot(
        [&](FileRecordInit& out) {
            if (cursor >= total) return false;
            out = std::move(records[cursor++]);
            return true;
        },
        total, lastUsn, volumeSerial);
}

bool MftParser::catchUpUsnJournal(uint64_t fromUsn) {
    if (m_hVolume == INVALID_HANDLE_VALUE || m_IsFallbackDirectoryWalk) {
        return false;
    }

    if (!QueryUsnJournal()) {
        return false;
    }

    const auto startUsn = static_cast<USN>(fromUsn);
    if (fromUsn == 0 || startUsn < m_UsnJournalData.LowestValidUsn || startUsn > m_UsnJournalData.NextUsn) {
        spdlog::warn("USN journal on drive {} is out of range (fromUsn: {}, lowest: {}, next: {}), triggering full rebuild",
                     m_DriveLetter, fromUsn, m_UsnJournalData.LowestValidUsn, m_UsnJournalData.NextUsn);
        EnumerateFiles();
        return true;
    }

    READ_USN_JOURNAL_DATA_V0 rujd = {0};
    rujd.StartUsn = fromUsn;
    rujd.ReasonMask = 0xFFFFFFFF;
    rujd.ReturnOnlyOnClose = FALSE;
    rujd.Timeout = 0;
    rujd.BytesToWaitFor = 0;
    rujd.UsnJournalID = m_UsnJournalData.UsnJournalID;

    std::vector<BYTE> buffer(BUF_LEN);
    DWORD bytesReturned = 0;
    bool anyChanged = false;

    while (DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &rujd, sizeof(rujd), buffer.data(), BUF_LEN, &bytesReturned, NULL)) {
        if (bytesReturned <= sizeof(USN)) break;

        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pNextUsn = reinterpret_cast<USN*>(buffer.data());
        rujd.StartUsn = *pNextUsn;

        PUSN_RECORD_V2 pRecord = reinterpret_cast<PUSN_RECORD_V2>(buffer.data() + sizeof(USN));

        if (dwRetBytes > 0) {
            std::unique_lock lock(m_MapMutex);
            while (dwRetBytes >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength <= dwRetBytes) {
                if (pRecord->Reason & USN_REASON_FILE_CREATE || pRecord->Reason & USN_REASON_RENAME_NEW_NAME) {
                    FileRecordInit record;
                    record.fileReferenceNumber = pRecord->FileReferenceNumber;
                    record.parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
                    record.isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    record.fileAttributes = pRecord->FileAttributes;
                    int nameLen = pRecord->FileNameLength / 2;
                    record.fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);

                    m_Store.upsert(record);
                    anyChanged = true;
                } else if (pRecord->Reason & USN_REASON_FILE_DELETE) {
                    anyChanged = m_Store.erase(pRecord->FileReferenceNumber) || anyChanged;
                }

                dwRetBytes -= pRecord->RecordLength;
                pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
            }
        }

        if (*pNextUsn >= m_UsnJournalData.NextUsn) break;
    }

    if (anyChanged) {
        std::unique_lock lock(m_MapMutex);
        m_Store.compactIfSparse();
        m_IndexGeneration++;
    }

    return true;
}
