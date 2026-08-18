#include "MftParser.h"
#include "PinyinEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <cwctype>

#define BUF_LEN 65536

namespace {

std::wstring normalize(std::wstring value) {
    return SearchExpression::normalize(value);
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

void MftParser::UsnListenerLoop() {
    READ_USN_JOURNAL_DATA_V0 rujd = {0};
    rujd.StartUsn = m_UsnJournalData.NextUsn;
    rujd.ReasonMask = 0xFFFFFFFF; // All reasons
    rujd.ReturnOnlyOnClose = FALSE;
    rujd.Timeout = 0;
    rujd.BytesToWaitFor = 0;
    rujd.UsnJournalID = m_UsnJournalData.UsnJournalID;

    BYTE buffer[BUF_LEN];
    DWORD bytesReturned = 0;

    while (m_IsListening) {
        if (!DeviceIoControl(m_hVolume, FSCTL_READ_USN_JOURNAL, &rujd, sizeof(rujd), buffer, BUF_LEN, &bytesReturned, NULL)) {
            Sleep(100);
            continue;
        }

        if (bytesReturned <= sizeof(USN)) {
            Sleep(50);
            continue;
        }
        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = (USN*)buffer;
        PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)((PBYTE)buffer + sizeof(USN));

        if (dwRetBytes > 0) {
            std::unique_lock lock(m_MapMutex);
            bool changed = false;
            while (dwRetBytes >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength >= sizeof(USN_RECORD_V2) &&
                   pRecord->RecordLength <= dwRetBytes) {
                if (pRecord->Reason & USN_REASON_FILE_CREATE || pRecord->Reason & USN_REASON_RENAME_NEW_NAME) {
                    std::unique_ptr<FileRecord> record = std::make_unique<FileRecord>();
                    record->fileReferenceNumber = pRecord->FileReferenceNumber;
                    record->parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
                    record->isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    int nameLen = pRecord->FileNameLength / 2;
                    record->fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);
                    record->normalizedName = normalize(record->fileName);
                    record->pinyinInitials = normalize(PinyinEngine::GetInitials(record->fileName));
                    record->pinyinFull = normalize(PinyinEngine::GetFullPinyin(record->fileName));
                    
                    m_FileMap[record->fileReferenceNumber] = std::move(record);
                    changed = true;
                } 
                else if (pRecord->Reason & USN_REASON_FILE_DELETE) {
                    changed = m_FileMap.erase(pRecord->FileReferenceNumber) > 0 || changed;
                }

                dwRetBytes -= pRecord->RecordLength;
                pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
            }
            if (changed) m_IndexGeneration.fetch_add(1, std::memory_order_release);
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
    m_DriveLetter = driveLetter;
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
    std::vector<std::pair<DWORDLONG, std::unique_ptr<FileRecord>>> batch;
    batch.reserve(2048);

    auto flushBatch = [&]() {
        if (batch.empty()) return;
        std::unique_lock lock(m_MapMutex);
        for (auto& item : batch) {
            m_FileMap[item.first] = std::move(item.second);
        }
        batch.clear();
        m_IndexGeneration.fetch_add(1, std::memory_order_release);
    };

    while (!dirsToScan.empty()) {
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

            auto record = std::make_unique<FileRecord>();
            record->fileReferenceNumber = fileId;
            record->parentFileReferenceNumber = parentId;
            record->isDirectory = isDir;
            record->fileSize = isDir ? 0 : ((static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow);
            record->creationTime = (static_cast<uint64_t>(findData.ftCreationTime.dwHighDateTime) << 32) | findData.ftCreationTime.dwLowDateTime;
            record->lastWriteTime = (static_cast<uint64_t>(findData.ftLastWriteTime.dwHighDateTime) << 32) | findData.ftLastWriteTime.dwLowDateTime;
            record->fileName = findData.cFileName;
            record->normalizedName = normalize(record->fileName);
            record->pinyinInitials = normalize(PinyinEngine::GetInitials(record->fileName));
            record->pinyinFull = normalize(PinyinEngine::GetFullPinyin(record->fileName));

            if (isDir) {
                std::wstring subDir = currentDir;
                if (subDir.back() != L'\\') subDir += L'\\';
                subDir += findData.cFileName;
                dirsToScan.push_back({std::move(subDir), fileId});
            }

            batch.push_back({fileId, std::move(record)});
            count++;

            if (batch.size() >= 2048) {
                flushBatch();
            }
        } while (FindNextFileW(hFind, &findData));

        FindClose(hFind);
    }

    flushBatch();
    {
        std::unique_lock lock(m_MapMutex);
        rebuildFolderPaths();
    }
    spdlog::info("Directory walk enumeration completed. Indexed {} files on drive {}:", count, driveLetter);
}

void MftParser::EnumerateFiles() {
    if (m_IsFallbackDirectoryWalk || m_hVolume == INVALID_HANDLE_VALUE) {
        EnumerateFilesViaDirectoryWalk(m_DriveLetter);
        return;
    }

    spdlog::info("Starting MFT enumeration...");
    MFT_ENUM_DATA_V0 med;
    med.StartFileReferenceNumber = 0;
    med.LowUsn = 0;
    med.HighUsn = m_UsnJournalData.NextUsn;

    BYTE buffer[BUF_LEN];
    DWORD bytesReturned = 0;
    int count = 0;

    while (DeviceIoControl(m_hVolume, FSCTL_ENUM_USN_DATA, &med, sizeof(med), buffer, BUF_LEN, &bytesReturned, NULL)) {
        if (bytesReturned <= sizeof(USN)) break;
        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = (USN*)buffer;
        PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)((PBYTE)buffer + sizeof(USN));
        
        std::unique_lock lock(m_MapMutex);
        while (dwRetBytes >= sizeof(USN_RECORD_V2) &&
               pRecord->RecordLength >= sizeof(USN_RECORD_V2) &&
               pRecord->RecordLength <= dwRetBytes) {
            std::unique_ptr<FileRecord> record = std::make_unique<FileRecord>();
            record->fileReferenceNumber = pRecord->FileReferenceNumber;
            record->parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
            record->isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            record->lastWriteTime = static_cast<uint64_t>(pRecord->TimeStamp.QuadPart);
            
            int nameLen = pRecord->FileNameLength / 2;
            record->fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);
            record->normalizedName = normalize(record->fileName);
            record->pinyinInitials = normalize(PinyinEngine::GetInitials(record->fileName));
            record->pinyinFull = normalize(PinyinEngine::GetFullPinyin(record->fileName));

            m_FileMap[record->fileReferenceNumber] = std::move(record);
            count++;

            dwRetBytes -= pRecord->RecordLength;
            pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
        }
        med.StartFileReferenceNumber = *pUsn;
    }
    {
        std::unique_lock lock(m_MapMutex);
        rebuildFolderPaths();
    }
    m_IndexGeneration.fetch_add(1, std::memory_order_release);
    spdlog::info("MFT enumeration completed. Indexed {} files.", count);
}

void MftParser::rebuildFolderPaths() {
    std::unordered_map<DWORDLONG, std::wstring> folderPaths;
    folderPaths.reserve(std::min<size_t>(m_FileMap.size() / 4, 65536));

    auto getFolderFullPath = [&](auto& self, DWORDLONG ref) -> const std::wstring& {
        auto it = folderPaths.find(ref);
        if (it != folderPaths.end()) return it->second;

        auto fileIt = m_FileMap.find(ref);
        if (fileIt == m_FileMap.end()) {
            static const std::wstring emptyPath;
            return emptyPath;
        }

        const auto& rec = *fileIt->second;
        if (rec.parentFileReferenceNumber == ref || rec.parentFileReferenceNumber == 0) {
            std::wstring rootPath;
            rootPath += static_cast<wchar_t>(m_DriveLetter);
            rootPath += L":";
            if (!rec.fileName.empty() && rec.fileName != L".") {
                rootPath += L"\\";
                rootPath += rec.fileName;
            }
            return folderPaths[ref] = std::move(rootPath);
        }

        const std::wstring& parentPath = self(self, rec.parentFileReferenceNumber);
        std::wstring curPath;
        if (parentPath.empty()) {
            curPath += static_cast<wchar_t>(m_DriveLetter);
            curPath += L":\\";
            curPath += rec.fileName;
        } else {
            curPath = parentPath;
            if (!curPath.empty() && curPath.back() != L'\\') curPath += L'\\';
            curPath += rec.fileName;
        }
        return folderPaths[ref] = std::move(curPath);
    };

    for (const auto& [id, record] : m_FileMap) {
        if (record->isDirectory) {
            getFolderFullPath(getFolderFullPath, id);
        }
    }

    m_FolderPaths = std::move(folderPaths);
    spdlog::info("Pre-computed {} folder paths for drive {}:", m_FolderPaths.size(), m_DriveLetter);
}

std::vector<SearchResult> MftParser::Search(const std::wstring& query, int limit) {
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    const std::wstring lowerQuery = normalize(query);
    const auto expr = SearchExpression::parse(query);

    std::shared_lock lock(m_MapMutex);
    const uint64_t generation = m_IndexGeneration.load(std::memory_order_acquire);

    auto testCandidate = [&](DWORDLONG id, const FileRecord& record) {
        return expr.matchesWithLazyPath(record, static_cast<wchar_t>(m_DriveLetter), [&]() {
            return buildFullPath(id);
        });
    };

    std::vector<DWORDLONG> candidates;
    bool canNarrow = false;
    {
        std::lock_guard<std::mutex> cacheLock(m_SearchCacheMutex);
        if (generation == m_CachedGeneration && !m_CachedQuery.empty() &&
            lowerQuery.rfind(m_CachedQuery, 0) == 0) {
            candidates = m_CachedCandidates;
            canNarrow = true;
        }
    }

    struct RankedCandidate {
        DWORDLONG id;
        const FileRecord* record;
        int rank;
    };
    std::vector<RankedCandidate> ranked;

    if (canNarrow) {
        std::vector<DWORDLONG> filtered;
        filtered.reserve(std::min<size_t>(candidates.size(), 65536));
        ranked.reserve(std::min<size_t>(candidates.size(), 65536));
        for (const auto id : candidates) {
            const auto it = m_FileMap.find(id);
            if (it == m_FileMap.end()) continue;
            const auto& record = *it->second;
            if (testCandidate(id, record)) {
                filtered.push_back(id);
                ranked.push_back({id, &record, expr.calculateRank(record)});
            }
        }
        candidates = std::move(filtered);
    } else {
        candidates.reserve(std::min<size_t>(m_FileMap.size(), 65536));
        ranked.reserve(std::min<size_t>(m_FileMap.size(), 65536));
        for (const auto& [id, record] : m_FileMap) {
            if (testCandidate(id, *record)) {
                candidates.push_back(id);
                ranked.push_back({id, record.get(), expr.calculateRank(*record)});
            }
        }
    }
    const auto compareRank = [&expr](const auto& a, const auto& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (expr.hasContentFilter()) {
            return a.record->lastWriteTime > b.record->lastWriteTime;
        }
        if (a.record->normalizedName.size() != b.record->normalizedName.size())
            return a.record->normalizedName.size() < b.record->normalizedName.size();
        return a.record->normalizedName < b.record->normalizedName;
    };
    const size_t resultCount = std::min(ranked.size(), static_cast<size_t>(limit));
    if (resultCount < ranked.size()) {
        std::partial_sort(ranked.begin(), ranked.begin() + resultCount, ranked.end(), compareRank);
    } else {
        std::sort(ranked.begin(), ranked.end(), compareRank);
    }

    m_CachedQuery = lowerQuery;
    m_CachedCandidates = std::move(candidates);
    m_CachedGeneration = generation;

    results.reserve(resultCount);
    for (size_t i = 0; i < resultCount; ++i) {
        const auto& candidate = ranked[i];
        results.push_back({
            candidate.record->fileName,
            buildFullPath(candidate.id),
            candidate.record->isDirectory,
            candidate.record->fileSize,
            candidate.record->creationTime,
            candidate.record->lastWriteTime
        });
    }
    return results;
}

std::wstring MftParser::buildFullPath(DWORDLONG fileReferenceNumber) const {
    const auto it = m_FileMap.find(fileReferenceNumber);
    if (it == m_FileMap.end()) return L"";
    const auto& record = *it->second;
    if (record.isDirectory) {
        auto folderIt = m_FolderPaths.find(fileReferenceNumber);
        if (folderIt != m_FolderPaths.end()) return folderIt->second;
    } else {
        auto folderIt = m_FolderPaths.find(record.parentFileReferenceNumber);
        if (folderIt != m_FolderPaths.end()) {
            std::wstring full = folderIt->second;
            if (!full.empty() && full.back() != L'\\') full += L'\\';
            full += record.fileName;
            return full;
        }
    }

    // 备用全链路递归构建
    std::vector<std::wstring> parts;
    DWORDLONG current = fileReferenceNumber;
    for (size_t depth = 0; depth < 512; ++depth) {
        const auto fit = m_FileMap.find(current);
        if (fit == m_FileMap.end()) break;
        const auto& rec = *fit->second;
        if (!rec.fileName.empty() && rec.fileName != L".") {
            parts.push_back(rec.fileName);
        }
        if (rec.parentFileReferenceNumber == current) break;
        current = rec.parentFileReferenceNumber;
    }

    std::wstring path;
    path += static_cast<wchar_t>(m_DriveLetter);
    path += L":\\";
    for (auto pit = parts.rbegin(); pit != parts.rend(); ++pit) {
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        path += *pit;
    }
    return path;
}
