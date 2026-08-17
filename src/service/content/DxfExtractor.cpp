#include "DxfExtractor.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <vector>

namespace easy::service::content {

namespace {

std::wstring decodeDxfLine(const char* data, size_t length) {
    if (length == 0) return {};
    // 先尝试 UTF-8 解码，失败则回退到 ANSI/GBK（很多老版 AutoCAD 图纸使用 GBK/CP936 编码）
    int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0);
    UINT codePage = (wideLen > 0) ? CP_UTF8 : CP_ACP;
    if (wideLen <= 0) {
        wideLen = MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), nullptr, 0);
    }
    if (wideLen <= 0) return {};
    std::wstring result(wideLen, L'\0');
    MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), result.data(), wideLen);
    return result;
}

} // namespace

DxfExtractor::DxfExtractor() {
    m_supportedExts.insert(L"dxf");
}

bool DxfExtractor::canHandle(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        lowerExt.push_back(std::towlower(c));
    }
    return m_supportedExts.find(lowerExt) != m_supportedExts.end();
}

bool DxfExtractor::searchContent(
    const std::wstring& filePath,
    std::wstring_view queryPattern,
    bool caseSensitive,
    std::vector<ContentSnippet>& outSnippets,
    size_t maxSnippetsPerFile
) {
    if (queryPattern.empty()) return false;

    HANDLE hFile = CreateFileW(
        filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0 || fileSize.QuadPart > 100 * 1024 * 1024) {
        CloseHandle(hFile);
        return false;
    }

    size_t mapSize = static_cast<size_t>(fileSize.QuadPart);
    HANDLE hMapping = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMapping) {
        CloseHandle(hFile);
        return false;
    }

    const uint8_t* pData = static_cast<const uint8_t*>(MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, mapSize));
    if (!pData) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    // ── 准备小写匹配模版 ──────────────────────────────────────────
    std::wstring lowerQuery;
    lowerQuery.reserve(queryPattern.size());
    for (wchar_t c : queryPattern) {
        lowerQuery.push_back(caseSensitive ? c : std::towlower(c));
    }

    // 扫描 DXF Group Codes
    // Group Code 1: Primary text value (TEXT, MTEXT, ATTRIB, DIMENSION)
    // Group Code 3: Additional text value
    size_t lineStart = 0;
    uint32_t lineNumber = 1;
    bool isNextLineTextValue = false;

    constexpr size_t MAX_SNIPPET_LEN = 200;
    constexpr size_t CONTEXT_PADDING = 40;

    for (size_t i = 0; i < mapSize; ++i) {
        if (pData[i] == '\n' || i == mapSize - 1) {
            size_t lineEnd = (pData[i] == '\n') ? i : (i + 1);
            size_t lineLen = lineEnd - lineStart;

            std::wstring line = decodeDxfLine(reinterpret_cast<const char*>(pData + lineStart), lineLen);
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) {
                line.pop_back();
            }

            // 检查上一行是否为 Group Code 1 或 3 (文本实体值)
            if (isNextLineTextValue) {
                isNextLineTextValue = false;

                std::wstring searchTarget = line;
                if (!caseSensitive) {
                    for (auto& wc : searchTarget) wc = std::towlower(wc);
                }

                size_t pos = searchTarget.find(lowerQuery);
                if (pos != std::wstring::npos) {
                    uint32_t snippetOffset = static_cast<uint32_t>(pos);
                    std::wstring snippetLine = line;

                    if (snippetLine.size() > MAX_SNIPPET_LEN) {
                        size_t start = (pos > CONTEXT_PADDING) ? (pos - CONTEXT_PADDING) : 0;
                        size_t end = std::min(snippetLine.size(), pos + lowerQuery.size() + CONTEXT_PADDING);
                        std::wstring truncated;
                        if (start > 0) truncated += L"...";
                        snippetOffset = static_cast<uint32_t>(truncated.size() + (pos - start));
                        truncated += snippetLine.substr(start, end - start);
                        if (end < snippetLine.size()) truncated += L"...";
                        snippetLine = std::move(truncated);
                    }

                    outSnippets.push_back(ContentSnippet{
                        .lineNumber = lineNumber,
                        .lineContent = std::move(snippetLine),
                        .matchOffset = snippetOffset,
                        .matchLength = static_cast<uint32_t>(lowerQuery.size())
                    });

                    if (outSnippets.size() >= maxSnippetsPerFile) break;
                }
            } else {
                // 判断当前行是否为 Group Code "  1" 或 "1" 或 "  3" 或 "3"
                std::wstring trimmed = line;
                while (!trimmed.empty() && trimmed.front() == L' ') trimmed.erase(trimmed.begin());
                if (trimmed == L"1" || trimmed == L"3") {
                    isNextLineTextValue = true;
                }
            }

            lineStart = i + 1;
            lineNumber++;
        }
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return !outSnippets.empty();
}

} // namespace easy::service::content
