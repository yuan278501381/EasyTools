#pragma once
#include "SearchExpression.h"
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>

struct SearchResult {
    std::wstring fileName;
    std::wstring fullPath;
    bool isDirectory = false;
    uint64_t fileSize = 0;
    uint64_t creationTime = 0;
    uint64_t lastWriteTime = 0;
};

struct SearchExcludeOptions {
    std::vector<std::wstring> patterns;
    bool excludeHidden = false;
    bool excludeSystem = false;
};

class MftParser {
public:
    MftParser();
    ~MftParser();

    bool Initialize(char driveLetter);
    void EnumerateFiles();
    
    // USN Journal Monitoring
    void StartListening();
    void StopListening();
    
    // Quick search
    std::vector<SearchResult> Search(const std::wstring& query, int limit = 100,
                                     const SearchExcludeOptions& excludeOpts = {});

    // 快照导出与载入接口
    void exportSnapshot(std::vector<FileRecord>& outRecords, uint64_t& outLastUsn, uint32_t& outVolumeSerial) const;
    bool importSnapshot(std::vector<FileRecord>&& records, uint64_t lastUsn, uint32_t volumeSerial);
    bool catchUpUsnJournal(uint64_t fromUsn);

    char getDriveLetter() const { return m_DriveLetter; }
    UINT getDriveType() const { return m_DriveType; }
    uint64_t getCurrentUsn() const;
    uint32_t getVolumeSerialNumber() const;
    size_t getFileCount() const;

private:
    char m_DriveLetter = 0;
    UINT m_DriveType = DRIVE_UNKNOWN;
    HANDLE m_hVolume = INVALID_HANDLE_VALUE;
    USN_JOURNAL_DATA_V0 m_UsnJournalData{};
    
    // Listener Thread
    std::atomic<bool> m_IsListening{false};
    std::unique_ptr<std::thread> m_ListenerThread;
    void UsnListenerLoop();
    
    // Memory structures
    std::shared_mutex m_MapMutex;
    std::unordered_map<DWORDLONG, std::unique_ptr<FileRecord>> m_FileMap;
    std::atomic<uint64_t> m_IndexGeneration{0};
    std::mutex m_SearchCacheMutex;
    std::wstring m_CachedQuery;
    std::vector<DWORDLONG> m_CachedCandidates;
    uint64_t m_CachedGeneration = 0;

    bool QueryUsnJournal();
    void EnumerateFilesViaDirectoryWalk(char driveLetter);
    std::wstring buildFullPath(DWORDLONG fileReferenceNumber) const;
    void rebuildFolderPaths();

    std::unordered_map<DWORDLONG, std::wstring> m_FolderPaths;
    std::vector<const FileRecord*> m_FlatRecords;
    bool m_IsFallbackDirectoryWalk{false};
};
