#include "ZipXmlExtractor.h"
#include <windows.h>
#include <zlib.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <sstream>

namespace easy::service::content {

namespace {

#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t signature;        // 0x04034b50
    uint16_t versionNeeded;
    uint16_t bitFlag;
    uint16_t compressionMethod; // 0 = stored, 8 = deflated
    uint16_t lastModTime;
    uint16_t lastModDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLength;
    uint16_t extraFieldLength;
};
#pragma pack(pop)

std::wstring stripXmlTags(const std::string& xml) {
    std::wstring result;
    result.reserve(xml.size() / 2);

    bool insideTag = false;
    std::string currentText;
    currentText.reserve(xml.size() / 2);

    for (size_t i = 0; i < xml.size(); ++i) {
        char c = xml[i];
        if (c == '<') {
            insideTag = true;
            currentText.push_back(' '); // 替换标签为分词空格
        } else if (c == '>') {
            insideTag = false;
        } else if (!insideTag) {
            currentText.push_back(c);
        }
    }

    if (currentText.empty()) return {};

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, currentText.data(), static_cast<int>(currentText.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    result.resize(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, currentText.data(), static_cast<int>(currentText.size()), result.data(), wideLen);

    return result;
}

bool decompressRawDeflate(const uint8_t* compressed, size_t compSize, size_t uncompSize, std::string& out) {
    if (uncompSize == 0 || compSize == 0) return false;
    
    // 限制单 XML 解压上限为 30MB
    constexpr size_t MAX_UNCOMP_SIZE = 30 * 1024 * 1024;
    size_t targetSize = std::min(uncompSize, MAX_UNCOMP_SIZE);

    out.resize(targetSize);

    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(compressed);
    strm.avail_in = static_cast<uInt>(compSize);
    strm.next_out = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(targetSize);

    // -MAX_WBITS (raw deflate without header)
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
        out.clear();
        return false;
    }

    int ret = inflate(&strm, Z_SYNC_FLUSH);
    size_t actualSize = strm.total_out;
    inflateEnd(&strm);

    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
        out.clear();
        return false;
    }

    out.resize(actualSize);
    return true;
}

} // namespace

ZipXmlExtractor::ZipXmlExtractor() {
    const wchar_t* exts[] = {
        L"docx", L"dotx", L"docm", L"wps",
        L"xlsx", L"xltx", L"xlsm", L"et",
        L"pptx", L"potx", L"pptm", L"dps",
        L"cdr", L"xmind"
    };
    for (const auto* ext : exts) {
        m_supportedExts.insert(ext);
    }
}

bool ZipXmlExtractor::canHandle(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        lowerExt.push_back(std::towlower(c));
    }
    return m_supportedExts.find(lowerExt) != m_supportedExts.end();
}

bool ZipXmlExtractor::extractZipEntriesText(
    const uint8_t* pZipData,
    size_t zipSize,
    std::wstring_view extension,
    std::wstring& outExtractedText
) const {
    size_t offset = 0;
    std::wstring lowerExt;
    for (wchar_t c : extension) lowerExt.push_back(std::towlower(c));

    while (offset + sizeof(ZipLocalHeader) <= zipSize) {
        const auto* header = reinterpret_cast<const ZipLocalHeader*>(pZipData + offset);
        if (header->signature != 0x04034b50) {
            break; // 结束或到达中央目录区
        }

        size_t nameOffset = offset + sizeof(ZipLocalHeader);
        if (nameOffset + header->filenameLength > zipSize) break;

        std::string filename(reinterpret_cast<const char*>(pZipData + nameOffset), header->filenameLength);
        size_t dataOffset = nameOffset + header->filenameLength + header->extraFieldLength;
        if (dataOffset + header->compressedSize > zipSize) break;

        bool shouldExtract = false;
        if (lowerExt == L"docx" || lowerExt == L"wps" || lowerExt == L"dotx") {
            if (filename == "word/document.xml" || filename == "word/header1.xml" || filename == "word/footer1.xml") {
                shouldExtract = true;
            }
        } else if (lowerExt == L"xlsx" || lowerExt == L"et" || lowerExt == L"xlsm") {
            if (filename == "xl/sharedStrings.xml" || filename.rfind("xl/worksheets/sheet", 0) == 0) {
                shouldExtract = true;
            }
        } else if (lowerExt == L"pptx" || lowerExt == L"dps") {
            if (filename.rfind("ppt/slides/slide", 0) == 0 || filename == "ppt/presentation.xml") {
                shouldExtract = true;
            }
        } else if (lowerExt == L"cdr") {
            if (filename == "content/root.xml" || filename == "content/riff.cdr") {
                shouldExtract = true;
            }
        } else if (lowerExt == L"xmind") {
            if (filename == "content.json" || filename == "content.xml") {
                shouldExtract = true;
            }
        }

        if (shouldExtract) {
            std::string uncompData;
            if (header->compressionMethod == 0) {
                uncompData.assign(reinterpret_cast<const char*>(pZipData + dataOffset), header->compressedSize);
            } else if (header->compressionMethod == 8) {
                decompressRawDeflate(pZipData + dataOffset, header->compressedSize, header->uncompressedSize, uncompData);
            }

            if (!uncompData.empty()) {
                std::wstring plain = stripXmlTags(uncompData);
                if (!plain.empty()) {
                    outExtractedText += plain;
                    outExtractedText += L"\n";
                }
            }
        }

        offset = dataOffset + header->compressedSize;
    }

    return !outExtractedText.empty();
}

bool ZipXmlExtractor::searchContent(
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

    // 提取扩展名
    std::wstring ext;
    size_t dotPos = filePath.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        ext = filePath.substr(dotPos + 1);
    }

    std::wstring extractedAllText;
    bool hasText = extractZipEntriesText(pData, mapSize, ext, extractedAllText);

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    if (!hasText || extractedAllText.empty()) return false;

    // ── 准备小写匹配模版 ──────────────────────────────────────────
    std::vector<std::wstring> subQueries;
    {
        size_t s = 0;
        while (s < queryPattern.size()) {
            while (s < queryPattern.size() && iswspace(queryPattern[s])) s++;
            if (s >= queryPattern.size()) break;
            size_t e = s;
            while (e < queryPattern.size() && !iswspace(queryPattern[e])) e++;
            std::wstring sub = std::wstring(queryPattern.substr(s, e - s));
            if (!caseSensitive) {
                for (auto& c : sub) c = std::towlower(c);
            }
            if (!sub.empty()) subQueries.push_back(std::move(sub));
            s = e;
        }
    }
    std::wstring lowerQuery;
    lowerQuery.reserve(queryPattern.size());
    for (wchar_t c : queryPattern) {
        lowerQuery.push_back(caseSensitive ? c : std::towlower(c));
    }

    // 逐段/逐行匹配
    std::wstringstream ss(extractedAllText);
    std::wstring line;
    uint32_t lineNumber = 1;

    constexpr size_t MAX_SNIPPET_LEN = 200;
    constexpr size_t CONTEXT_PADDING = 40;

    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            lineNumber++;
            continue;
        }

        std::wstring searchTarget = line;
        if (!caseSensitive) {
            for (auto& wc : searchTarget) wc = std::towlower(wc);
        }

        size_t pos = searchTarget.find(lowerQuery);
        size_t matchLen = lowerQuery.size();
        if (pos == std::wstring::npos && !subQueries.empty()) {
            for (const auto& sq : subQueries) {
                size_t p = searchTarget.find(sq);
                if (p != std::wstring::npos) {
                    pos = p;
                    matchLen = sq.size();
                    break;
                }
            }
        }
        if (pos != std::wstring::npos) {
            uint32_t snippetOffset = static_cast<uint32_t>(pos);
            std::wstring snippetLine = line;

            if (snippetLine.size() > MAX_SNIPPET_LEN) {
                size_t start = (pos > CONTEXT_PADDING) ? (pos - CONTEXT_PADDING) : 0;
                size_t end = std::min(snippetLine.size(), pos + matchLen + CONTEXT_PADDING);
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
                .matchLength = static_cast<uint32_t>(matchLen)
            });

            if (outSnippets.size() >= maxSnippetsPerFile) break;
        }

        lineNumber++;
    }

    return !outSnippets.empty();
}

} // namespace easy::service::content
