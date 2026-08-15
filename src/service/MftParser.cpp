#include "MftParser.h"
#include "PinyinEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <cwctype>

#define BUF_LEN 65536

namespace {

std::wstring normalize(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
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

void MftParser::StartListening() {
    if (m_IsListening) return;
    m_IsListening = true;
    m_ListenerThread = std::make_unique<std::thread>(&MftParser::UsnListenerLoop, this);
    spdlog::info("USN Journal listener started.");
}

void MftParser::StopListening() {
    if (m_IsListening) {
        m_IsListening = false;
        if (m_ListenerThread && m_ListenerThread->joinable()) {
            m_ListenerThread->join();
        }
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

bool MftParser::Initialize(char driveLetter) {
    m_DriveLetter = driveLetter;
    std::string volumePath = "\\\\.\\";
    volumePath += driveLetter;
    volumePath += ":";

    m_hVolume = CreateFileA(volumePath.c_str(), 
                            GENERIC_READ | GENERIC_WRITE, 
                            FILE_SHARE_READ | FILE_SHARE_WRITE, 
                            NULL, 
                            OPEN_EXISTING, 
                            FILE_ATTRIBUTE_NORMAL, 
                            NULL);

    if (m_hVolume == INVALID_HANDLE_VALUE) {
        spdlog::error("Failed to open volume {}:, error: {}", driveLetter, GetLastError());
        return false;
    }

    if (!QueryUsnJournal()) {
        spdlog::error("Failed to query USN journal.");
        return false;
    }

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

void MftParser::EnumerateFiles() {
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
    m_IndexGeneration.fetch_add(1, std::memory_order_release);
    spdlog::info("MFT enumeration completed. Indexed {} files.", count);
}

std::vector<SearchResult> MftParser::Search(const std::wstring& query, int limit) {
    std::shared_lock lock(m_MapMutex);
    std::vector<SearchResult> results;
    if (query.empty() || limit <= 0) return results;
    limit = std::min(limit, 200);
    const std::wstring lowerQuery = normalize(query);

    // If query is pure ASCII letters, it might be a pinyin search
    bool isPinyinSearch = true;
    for (wchar_t ch : lowerQuery) {
        if (ch < L'a' || ch > L'z') {
            isPinyinSearch = false;
            break;
        }
    }

    auto matches = [&](const FileRecord& record) {
        return record.normalizedName.find(lowerQuery) != std::wstring::npos ||
               (isPinyinSearch && (record.pinyinInitials.find(lowerQuery) != std::wstring::npos ||
                                   record.pinyinFull.find(lowerQuery) != std::wstring::npos));
    };

    std::lock_guard cacheLock(m_SearchCacheMutex);
    const uint64_t generation = m_IndexGeneration.load(std::memory_order_acquire);
    std::vector<DWORDLONG> candidates;
    const bool canRefineCache = generation == m_CachedGeneration &&
        !m_CachedQuery.empty() && lowerQuery.starts_with(m_CachedQuery);
    if (canRefineCache) {
        candidates.reserve(m_CachedCandidates.size());
        for (const auto id : m_CachedCandidates) {
            const auto it = m_FileMap.find(id);
            if (it != m_FileMap.end() && matches(*it->second)) candidates.push_back(id);
        }
    } else {
        candidates.reserve(std::min<size_t>(m_FileMap.size(), 65536));
        for (const auto& [id, record] : m_FileMap) {
            if (matches(*record)) candidates.push_back(id);
        }
    }

    struct RankedCandidate {
        DWORDLONG id;
        const FileRecord* record;
        int rank;
    };
    std::vector<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const auto id : candidates) {
        const auto it = m_FileMap.find(id);
        if (it == m_FileMap.end()) continue;
        const auto& record = *it->second;
        int rank = 6;
        if (record.normalizedName == lowerQuery) rank = 0;
        else if (record.normalizedName.starts_with(lowerQuery)) rank = 1;
        else if (record.pinyinFull.starts_with(lowerQuery)) rank = 2;
        else if (record.pinyinInitials.starts_with(lowerQuery)) rank = 3;
        else if (record.normalizedName.find(lowerQuery) != std::wstring::npos) rank = 4;
        else if (record.pinyinFull.find(lowerQuery) != std::wstring::npos) rank = 5;
        else if (record.pinyinInitials.find(lowerQuery) != std::wstring::npos) rank = 6;
        ranked.push_back({id, &record, rank});
    }
    const auto compareRank = [](const auto& a, const auto& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
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

    // Keep the exact matching set for the next keystroke. Moving avoids a
    // second potentially multi-megabyte copy for broad one-character queries.
    m_CachedQuery = lowerQuery;
    m_CachedCandidates = std::move(candidates);
    m_CachedGeneration = generation;

    results.reserve(resultCount);
    for (size_t i = 0; i < resultCount; ++i) {
        const auto& candidate = ranked[i];
        results.push_back({candidate.record->fileName, buildFullPath(candidate.id),
                           candidate.record->isDirectory});
    }
    return results;
}

std::wstring MftParser::buildFullPath(DWORDLONG fileReferenceNumber) const {
    std::vector<std::wstring> parts;
    DWORDLONG current = fileReferenceNumber;
    // 防御损坏或循环的父引用；NTFS 正常路径远低于此深度。
    for (size_t depth = 0; depth < 512; ++depth) {
        const auto it = m_FileMap.find(current);
        if (it == m_FileMap.end()) break;
        const auto& record = *it->second;
        if (!record.fileName.empty() && record.fileName != L".") {
            parts.push_back(record.fileName);
        }
        if (record.parentFileReferenceNumber == current) break;
        current = record.parentFileReferenceNumber;
    }

    std::wstring path;
    path += static_cast<wchar_t>(m_DriveLetter);
    path += L":\\";
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        path += *it;
    }
    return path;
}
