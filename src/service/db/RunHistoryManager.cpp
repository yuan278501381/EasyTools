#include "RunHistoryManager.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <cmath>

namespace easy::service::db {

namespace {

std::wstring getDefaultAppDataFolder() {
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = std::wstring(appData) + L"\\EasyTools";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir;
    }
    return L".";
}

std::wstring trim(const std::wstring& str) {
    size_t first = str.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\r\n");
    return str.substr(first, (last - first + 1));
}

} // namespace

RunHistoryManager& RunHistoryManager::instance() {
    static RunHistoryManager s_mgr;
    return s_mgr;
}

RunHistoryManager::RunHistoryManager() {
    init();
}

RunHistoryManager::~RunHistoryManager() {
    if (m_isDirty) {
        save();
    }
}

uint64_t RunHistoryManager::getCurrentFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

std::wstring RunHistoryManager::getNormalizedKey(const std::wstring& path) const {
    std::wstring key = path;
    for (auto& ch : key) {
        if (ch == L'/') ch = L'\\';
        ch = std::towlower(ch);
    }
    return key;
}

void RunHistoryManager::init(const std::wstring& customCsvPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!customCsvPath.empty()) {
        m_csvPath = customCsvPath;
    } else if (m_csvPath.empty()) {
        m_csvPath = getDefaultAppDataFolder() + L"\\Run History.csv";
    }
    load();
}

void RunHistoryManager::recordRun(const std::wstring& filename) {
    if (filename.empty()) return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring key = getNormalizedKey(filename);
    uint64_t nowFt = getCurrentFileTime();

    auto it = m_records.find(key);
    if (it != m_records.end()) {
        it->second.runCount++;
        it->second.lastRunDate = nowFt;
        it->second.filename = filename; // 更新保留最新大小写
    } else {
        m_records[key] = RunRecord{
            .filename = filename,
            .runCount = 1,
            .lastRunDate = nowFt
        };
    }
    m_isDirty = true;
    save();
}

uint32_t RunHistoryManager::getRunCount(const std::wstring& filename) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring key = getNormalizedKey(filename);
    auto it = m_records.find(key);
    if (it != m_records.end()) {
        return it->second.runCount;
    }
    return 0;
}

uint64_t RunHistoryManager::getLastRunDate(const std::wstring& filename) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring key = getNormalizedKey(filename);
    auto it = m_records.find(key);
    if (it != m_records.end()) {
        return it->second.lastRunDate;
    }
    return 0;
}

double RunHistoryManager::calculateFrecencyScore(const std::wstring& filename, uint64_t currentFileTime) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring key = getNormalizedKey(filename);
    auto it = m_records.find(key);
    if (it == m_records.end() || it->second.runCount == 0) {
        return 0.0;
    }

    const auto& rec = it->second;
    uint64_t nowFt = (currentFileTime != 0) ? currentFileTime : getCurrentFileTime();

    // 1 秒 = 10,000,000 个 100ns 间隔；1 小时 = 36,000,000,000
    constexpr uint64_t FT_ONE_HOUR = 3600ULL * 10000000ULL;
    constexpr uint64_t FT_ONE_DAY = 24ULL * FT_ONE_HOUR;

    uint64_t diff = (nowFt > rec.lastRunDate) ? (nowFt - rec.lastRunDate) : 0;

    double recencyBonus = 0.0;
    if (diff <= FT_ONE_HOUR * 4) {
        recencyBonus = 1000.0; // 4 小时内访问
    } else if (diff <= FT_ONE_DAY) {
        recencyBonus = 700.0;  // 24 小时内访问
    } else if (diff <= FT_ONE_DAY * 7) {
        recencyBonus = 400.0;  // 7 天内访问
    } else if (diff <= FT_ONE_DAY * 30) {
        recencyBonus = 150.0;  // 30 天内访问
    } else {
        recencyBonus = 20.0;   // 历史访问
    }

    // Frecency 综合得分公式
    return static_cast<double>(rec.runCount) * 100.0 + recencyBonus;
}

std::vector<RunRecord> RunHistoryManager::getTopRuns(size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<RunRecord> result;
    result.reserve(m_records.size());
    for (const auto& [_, rec] : m_records) {
        result.push_back(rec);
    }

    uint64_t nowFt = getCurrentFileTime();
    std::sort(result.begin(), result.end(), [this, nowFt](const RunRecord& a, const RunRecord& b) {
        double scoreA = calculateFrecencyScore(a.filename, nowFt);
        double scoreB = calculateFrecencyScore(b.filename, nowFt);
        if (scoreA != scoreB) return scoreA > scoreB;
        return a.runCount > b.runCount;
    });

    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

bool RunHistoryManager::load() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_csvPath.empty()) return false;

    HANDLE hFile = CreateFileW(
        m_csvPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size) || size.QuadPart == 0) {
        CloseHandle(hFile);
        return false;
    }

    std::string buffer(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    // UTF-8 BOM 剥离
    size_t offset = 0;
    if (buffer.size() >= 3 && static_cast<uint8_t>(buffer[0]) == 0xEF &&
        static_cast<uint8_t>(buffer[1]) == 0xBB && static_cast<uint8_t>(buffer[2]) == 0xBF) {
        offset = 3;
    }

    std::string_view content(buffer.data() + offset, buffer.size() - offset);
    std::istringstream stream((std::string(content)));
    std::string line;

    // 跳过首行表头: Filename,Run Count,Last Run Date
    if (std::getline(stream, line)) {
        // header checked
    }

    m_records.clear();

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // 解析 CSV 行: "Filename",Run Count,Last Run Date
        size_t firstQuote = line.find('"');
        size_t secondQuote = (firstQuote != std::string::npos) ? line.find('"', firstQuote + 1) : std::string::npos;

        std::string filenameUtf8;
        std::string rest;

        if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
            filenameUtf8 = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            if (secondQuote + 1 < line.size()) {
                rest = line.substr(secondQuote + 1);
            }
        } else {
            size_t comma = line.find(',');
            if (comma != std::string::npos) {
                filenameUtf8 = line.substr(0, comma);
                rest = line.substr(comma);
            } else {
                continue;
            }
        }

        uint32_t runCount = 0;
        uint64_t lastRunDate = 0;

        if (!rest.empty()) {
            if (rest.front() == ',') rest = rest.substr(1);
            size_t comma = rest.find(',');
            if (comma != std::string::npos) {
                try {
                    runCount = static_cast<uint32_t>(std::stoul(rest.substr(0, comma)));
                    lastRunDate = std::stoull(rest.substr(comma + 1));
                } catch (...) {}
            } else {
                try {
                    runCount = static_cast<uint32_t>(std::stoul(rest));
                } catch (...) {}
            }
        }

        if (!filenameUtf8.empty()) {
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, filenameUtf8.data(), static_cast<int>(filenameUtf8.size()), nullptr, 0);
            if (wideLen > 0) {
                std::wstring wFilename(wideLen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, filenameUtf8.data(), static_cast<int>(filenameUtf8.size()), wFilename.data(), wideLen);

                std::wstring key = getNormalizedKey(wFilename);
                m_records[key] = RunRecord{
                    .filename = std::move(wFilename),
                    .runCount = runCount,
                    .lastRunDate = lastRunDate
                };
            }
        }
    }

    m_isDirty = false;
    return true;
}

bool RunHistoryManager::save() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_csvPath.empty()) return false;

    std::wstring tmpPath = m_csvPath + L".tmp";
    HANDLE hFile = CreateFileW(
        tmpPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    std::string csvData = "Filename,Run Count,Last Run Date\r\n";

    // 排序后导出：运行频次高、最近运行的排在前列
    std::vector<RunRecord> list;
    list.reserve(m_records.size());
    for (const auto& [_, rec] : m_records) {
        list.push_back(rec);
    }

    std::sort(list.begin(), list.end(), [](const RunRecord& a, const RunRecord& b) {
        if (a.runCount != b.runCount) return a.runCount > b.runCount;
        return a.lastRunDate > b.lastRunDate;
    });

    for (const auto& item : list) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, item.filename.data(), static_cast<int>(item.filename.size()), nullptr, 0, nullptr, nullptr);
        std::string filenameUtf8(utf8Len, '\0');
        if (utf8Len > 0) {
            WideCharToMultiByte(CP_UTF8, 0, item.filename.data(), static_cast<int>(item.filename.size()), filenameUtf8.data(), utf8Len, nullptr, nullptr);
        }

        csvData += "\"" + filenameUtf8 + "\"," + std::to_string(item.runCount) + "," + std::to_string(item.lastRunDate) + "\r\n";
    }

    DWORD bytesWritten = 0;
    WriteFile(hFile, csvData.data(), static_cast<DWORD>(csvData.size()), &bytesWritten, nullptr);
    CloseHandle(hFile);

    // 原子替换
    MoveFileExW(tmpPath.c_str(), m_csvPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    m_isDirty = false;
    return true;
}

void RunHistoryManager::clear() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_records.clear();
    m_isDirty = true;
    save();
}

size_t RunHistoryManager::size() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_records.size();
}

std::wstring RunHistoryManager::getFilePath() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_csvPath;
}

} // namespace easy::service::db
