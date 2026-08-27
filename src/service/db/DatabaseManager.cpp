#include "DatabaseManager.h"
#include "../MftParser.h"
#include <shlobj.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <limits>

namespace easy::service::db {

namespace {

std::wstring getDefaultDbPath() {
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\EasyTools";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\EasyTools.db";
    }
    return L".\\EasyTools.db";
}

bool writeAll(HANDLE file, const void* data, std::size_t bytes) {
    if (file == INVALID_HANDLE_VALUE || (bytes > 0 && data == nullptr)) return false;
    const auto* cursor = static_cast<const std::byte*>(data);
    constexpr std::size_t WriteChunkBytes = 16u * 1024u * 1024u;
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const DWORD requested = static_cast<DWORD>((std::min)(remaining, WriteChunkBytes));
        DWORD written = 0;
        if (!WriteFile(file, cursor, requested, &written, nullptr) || written == 0) return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

} // namespace

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager s_mgr;
    return s_mgr;
}

DatabaseManager::DatabaseManager() {
    init();
}

DatabaseManager::~DatabaseManager() {
}

void DatabaseManager::init(const std::wstring& customDbPath) {
    if (!customDbPath.empty()) {
        m_dbPath = customDbPath;
    } else if (m_dbPath.empty()) {
        m_dbPath = getDefaultDbPath();
    }
}

std::wstring DatabaseManager::getDbPath() const {
    return m_dbPath;
}

DbStats DatabaseManager::getStats() const {
    DbStats stats;
    stats.dbPath = m_dbPath;

    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if (GetFileAttributesExW(m_dbPath.c_str(), GetFileExInfoStandard, &attr)) {
        stats.exists = true;
        stats.fileSize = (static_cast<uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;

        ULARGE_INTEGER uli;
        uli.LowPart = attr.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = attr.ftLastWriteTime.dwHighDateTime;
        stats.timestamp = uli.QuadPart;

        // 快速读取头部的 totalRecords
        HANDLE hFile = CreateFileW(m_dbPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DbHeader header{};
            DWORD bytesRead = 0;
            if (ReadFile(hFile, &header, sizeof(header), &bytesRead, nullptr) && bytesRead == sizeof(header)) {
                const bool validV2 = header.magic == 0x3230424459534145ULL && header.version == 2;
                const bool validV1 = header.magic == 0x3130424459534145ULL && header.version == 1;
                if (validV2 || validV1) {
                    stats.totalRecords = header.totalRecords;
                    stats.volumeCount = header.volumeCount;
                }
            }
            CloseHandle(hFile);
        }
    }
    return stats;
}

bool DatabaseManager::saveSnapshot(const std::vector<MftParser*>& parsers) {
    if (m_dbPath.empty() || parsers.empty()) return false;

    const std::wstring tmpPath = m_dbPath + L"." + std::to_wstring(GetCurrentProcessId()) +
                                 L"_" + std::to_wstring(GetTickCount64()) + L".tmp";
    HANDLE hFile = CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DbHeader header{};
    header.magic = 0x3230424459534145ULL; // 'EASYDB02'
    header.version = 2;
    header.volumeCount = 0;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    header.timestamp = uli.QuadPart;
    header.totalRecords = 0;

    // 先写入占位的 Header
    const auto failSave = [&]() {
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
        DeleteFileW(tmpPath.c_str());
        return false;
    };
    if (!writeAll(hFile, &header, sizeof(header))) return failSave();

    uint64_t totalRecordsAll = 0;

    for (auto* parser : parsers) {
        if (!parser) continue;

        // 直接在遍历回调里构造字符串池与紧凑 Pod 数组：全盘索引有数百万条记录，
        // 先物化成 std::vector<FileRecord> 会让保存快照的瞬间内存翻倍。
        std::vector<wchar_t> stringPool;
        std::vector<DbRecordPod> pods;
        bool serializationFailed = false;

        uint64_t lastUsn = 0;
        uint32_t volumeSerial = 0;
        parser->exportSnapshot([&](const FileRecord& r) {
            if (serializationFailed) return;
            if (r.fileName.size() > (std::numeric_limits<uint16_t>::max)() ||
                stringPool.size() > (std::numeric_limits<uint32_t>::max)() ||
                r.fileName.size() > (std::numeric_limits<uint32_t>::max)() - stringPool.size() ||
                pods.size() >= (std::numeric_limits<uint32_t>::max)()) {
                serializationFailed = true;
                return;
            }
            DbRecordPod pod{};
            pod.frn = r.fileReferenceNumber;
            pod.parentFrn = r.parentFileReferenceNumber;
            pod.attributes = r.fileAttributes;
            pod.isDirectory = r.isDirectory ? 1 : 0;
            pod.fileSize = r.fileSize;
            pod.creationTime = r.creationTime;
            pod.lastWriteTime = r.lastWriteTime;

            pod.nameOffset = static_cast<uint32_t>(stringPool.size());
            pod.nameLen = static_cast<uint16_t>(r.fileName.size());
            stringPool.insert(stringPool.end(), r.fileName.begin(), r.fileName.end());

            pods.push_back(pod);
        }, lastUsn, volumeSerial);

        if (serializationFailed ||
            stringPool.size() > (std::numeric_limits<uint32_t>::max)() / sizeof(wchar_t)) {
            return failSave();
        }

        totalRecordsAll += pods.size();

        DbVolumeHeader volHeader{};
        volHeader.driveLetter = static_cast<wchar_t>(std::toupper(parser->getDriveLetter()));
        volHeader.volumeSerial = volumeSerial;
        volHeader.lastUsn = lastUsn;
        volHeader.recordCount = static_cast<uint32_t>(pods.size());
        volHeader.stringPoolBytes = static_cast<uint32_t>(stringPool.size() * sizeof(wchar_t));

        // 写入 VolHeader
        if (!writeAll(hFile, &volHeader, sizeof(volHeader))) return failSave();

        // 写入 String Pool
        if (!stringPool.empty()) {
            if (!writeAll(hFile, stringPool.data(), volHeader.stringPoolBytes)) return failSave();
        }

        // 写入 Pod Array
        if (!pods.empty()) {
            const size_t podsBytes = pods.size() * sizeof(DbRecordPod);
            if (!writeAll(hFile, pods.data(), podsBytes)) return failSave();
        }
        ++header.volumeCount;
    }

    // 回填更新 Header.totalRecords
    header.totalRecords = totalRecordsAll;
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(hFile, start, nullptr, FILE_BEGIN) ||
        !writeAll(hFile, &header, sizeof(header)) ||
        !FlushFileBuffers(hFile)) {
        return failSave();
    }
    if (!CloseHandle(hFile)) {
        hFile = INVALID_HANDLE_VALUE;
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    hFile = INVALID_HANDLE_VALUE;

    // 原子替换
    if (!MoveFileExW(tmpPath.c_str(), m_dbPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

bool DatabaseManager::loadSnapshot(std::vector<MftParser*>& parsers) {
    if (m_dbPath.empty() || parsers.empty()) return false;

    HANDLE hFile = CreateFileW(
        m_dbPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < sizeof(DbHeader)) {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMapping) {
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pData = static_cast<const uint8_t*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, static_cast<size_t>(fileSize.QuadPart)));
    if (!pData) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    const size_t totalBytes = static_cast<size_t>(fileSize.QuadPart);
    DbHeader header{};
    std::memcpy(&header, pData, sizeof(header));

    // 校验魔数与版本
    const bool validV2 = header.magic == 0x3230424459534145ULL && header.version == 2;
    const bool validV1 = header.magic == 0x3130424459534145ULL && header.version == 1;
    if (!validV2 && !validV1) {
        UnmapViewOfFile(pData);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    size_t cursor = sizeof(DbHeader);
    bool anyVolumeLoaded = false;

    for (uint32_t v = 0; v < header.volumeCount; ++v) {
        if (cursor > totalBytes || sizeof(DbVolumeHeader) > totalBytes - cursor) break;
        DbVolumeHeader volume{};
        std::memcpy(&volume, pData + cursor, sizeof(volume));
        cursor += sizeof(DbVolumeHeader);

        const size_t stringPoolBytes = volume.stringPoolBytes;
        if (stringPoolBytes % sizeof(wchar_t) != 0 ||
            cursor > totalBytes || stringPoolBytes > totalBytes - cursor) {
            break;
        }
        const size_t afterStrings = cursor + stringPoolBytes;
        const size_t availableForPods = totalBytes - afterStrings;
        if (volume.recordCount > availableForPods / sizeof(DbRecordPod)) break;
        const size_t podsBytes = static_cast<size_t>(volume.recordCount) * sizeof(DbRecordPod);

        const wchar_t* pStrings = reinterpret_cast<const wchar_t*>(pData + cursor);
        cursor += stringPoolBytes;

        const auto* pPodBytes = pData + cursor;
        cursor += podsBytes;

        const size_t totalWChars = stringPoolBytes / sizeof(wchar_t);
        bool volumeValid = true;
        for (uint32_t index = 0; index < volume.recordCount; ++index) {
            DbRecordPod pod{};
            std::memcpy(&pod, pPodBytes + static_cast<size_t>(index) * sizeof(DbRecordPod), sizeof(pod));
            if (pod.nameOffset > totalWChars || pod.nameLen > totalWChars - pod.nameOffset) {
                volumeValid = false;
                break;
            }
        }
        if (!volumeValid) continue;

        // 查找对应盘符的 MftParser
        char drive = static_cast<char>(std::toupper(volume.driveLetter));
        MftParser* targetParser = nullptr;
        for (auto* parser : parsers) {
            if (parser && std::toupper(parser->getDriveLetter()) == drive) {
                targetParser = parser;
                break;
            }
        }

        if (!targetParser) continue;

        // 直接把内存映射里的 Pod 逐条喂给索引：一次性解码成 vector<FileRecordInit>
        // 会在恢复瞬间额外占用与整份索引同量级的临时字符串内存。
        uint32_t podCursor = 0;

        auto produce = [&](FileRecordInit& r) {
            if (podCursor >= volume.recordCount) return false;
            DbRecordPod pod{};
            std::memcpy(&pod, pPodBytes + static_cast<size_t>(podCursor++) * sizeof(DbRecordPod), sizeof(pod));
            r.fileReferenceNumber = pod.frn;
            r.parentFileReferenceNumber = pod.parentFrn;
            r.fileAttributes = pod.attributes;
            r.isDirectory = (pod.isDirectory != 0);
            r.fileSize = pod.fileSize;
            r.creationTime = pod.creationTime;
            r.lastWriteTime = pod.lastWriteTime;

            r.fileName.assign(pStrings + pod.nameOffset, pod.nameLen);
            return true;
        };

        // 导入并触发 USN 增量追赶
        if (targetParser->importSnapshot(produce, volume.recordCount, volume.lastUsn, volume.volumeSerial)) {
            targetParser->catchUpUsnJournal(volume.lastUsn);
            anyVolumeLoaded = true;
        }
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return anyVolumeLoaded;
}

} // namespace easy::service::db
