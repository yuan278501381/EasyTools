#include "MftParser.h"
#include "PinyinEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>

#define BUF_LEN 65536

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

        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = (USN*)buffer;
        PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)((PBYTE)buffer + sizeof(USN));

        if (dwRetBytes > 0) {
            std::unique_lock lock(m_MapMutex);
            while (dwRetBytes > 0) {
                if (pRecord->Reason & USN_REASON_FILE_CREATE || pRecord->Reason & USN_REASON_RENAME_NEW_NAME) {
                    std::unique_ptr<FileRecord> record = std::make_unique<FileRecord>();
                    record->fileReferenceNumber = pRecord->FileReferenceNumber;
                    record->parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
                    record->isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    int nameLen = pRecord->FileNameLength / 2;
                    record->fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);
                    record->pinyinInitials = PinyinEngine::GetInitials(record->fileName);
                    
                    m_FileMap[record->fileReferenceNumber] = std::move(record);
                } 
                else if (pRecord->Reason & USN_REASON_FILE_DELETE) {
                    m_FileMap.erase(pRecord->FileReferenceNumber);
                }

                dwRetBytes -= pRecord->RecordLength;
                pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
            }
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
        DWORD dwRetBytes = bytesReturned - sizeof(USN);
        USN* pUsn = (USN*)buffer;
        PUSN_RECORD_V2 pRecord = (PUSN_RECORD_V2)((PBYTE)buffer + sizeof(USN));
        
        std::unique_lock lock(m_MapMutex);
        while (dwRetBytes > 0) {
            std::unique_ptr<FileRecord> record = std::make_unique<FileRecord>();
            record->fileReferenceNumber = pRecord->FileReferenceNumber;
            record->parentFileReferenceNumber = pRecord->ParentFileReferenceNumber;
            record->isDirectory = (pRecord->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            
            int nameLen = pRecord->FileNameLength / 2;
            record->fileName.assign((wchar_t*)((PBYTE)pRecord + pRecord->FileNameOffset), nameLen);
            record->pinyinInitials = PinyinEngine::GetInitials(record->fileName);

            m_FileMap[record->fileReferenceNumber] = std::move(record);
            count++;

            dwRetBytes -= pRecord->RecordLength;
            pRecord = (PUSN_RECORD_V2)((PBYTE)pRecord + pRecord->RecordLength);
        }
        med.StartFileReferenceNumber = *pUsn;
    }
    spdlog::info("MFT enumeration completed. Indexed {} files.", count);
}

std::vector<FileRecord*> MftParser::Search(const std::wstring& query, int limit) {
    std::shared_lock lock(m_MapMutex);
    std::vector<FileRecord*> results;
    
    // Naive linear search for demonstration; will upgrade to Trie/Pinyin
    // converting query to lowercase for basic matching
    std::wstring lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::towlower);

    // If query is pure ASCII letters, it might be a pinyin search
    bool isPinyinSearch = true;
    for (wchar_t ch : lowerQuery) {
        if (ch < L'a' || ch > L'z') {
            isPinyinSearch = false;
            break;
        }
    }

    for (const auto& pair : m_FileMap) {
        if (results.size() >= limit) break;
        
        std::wstring lowerName = pair.second->fileName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerName.find(lowerQuery) != std::wstring::npos) {
            results.push_back(pair.second.get());
        } else if (isPinyinSearch) {
            if (pair.second->pinyinInitials.find(lowerQuery) != std::wstring::npos) {
                results.push_back(pair.second.get());
            }
        }
    }
    return results;
}
