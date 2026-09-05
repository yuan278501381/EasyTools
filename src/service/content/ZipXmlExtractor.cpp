#include "ZipXmlExtractor.h"
#include <windows.h>
#include <zlib.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <sstream>
#include <string>
#include <string_view>

namespace easy::service::content {

namespace {

#pragma pack(push, 1)
struct ZipEocd {
    uint32_t signature;           // 0x06054b50
    uint16_t diskNumber;
    uint16_t cdDiskNumber;
    uint16_t numEntriesThisDisk;
    uint16_t totalEntries;
    uint32_t cdSize;
    uint32_t cdOffset;
    uint16_t commentLength;
};

struct ZipCdHeader {
    uint32_t signature;           // 0x02014b50
    uint16_t versionMadeBy;
    uint16_t versionNeeded;
    uint16_t bitFlag;
    uint16_t compressionMethod;   // 0 = stored, 8 = deflated
    uint16_t lastModTime;
    uint16_t lastModDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLength;
    uint16_t extraFieldLength;
    uint16_t commentLength;
    uint16_t diskNumberStart;
    uint16_t internalFileAttr;
    uint32_t externalFileAttr;
    uint32_t localHeaderOffset;
};

struct ZipLocalHeader {
    uint32_t signature;           // 0x04034b50
    uint16_t versionNeeded;
    uint16_t bitFlag;
    uint16_t compressionMethod;   // 0 = stored, 8 = deflated
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
    std::string currentText;
    currentText.reserve(xml.size() / 2);

    bool insideTag = false;
    std::string tagBuffer;

    for (size_t i = 0; i < xml.size(); ++i) {
        char c = xml[i];
        if (c == '<') {
            insideTag = true;
            tagBuffer.clear();
        } else if (c == '>') {
            insideTag = false;
            // 识别段落、换行或表格单元格闭合标签，转换为自然换行符
            if (tagBuffer == "/w:p" || tagBuffer == "/a:p" || tagBuffer == "/p:sp" ||
                tagBuffer == "w:br" || tagBuffer == "w:br/" || tagBuffer == "a:br" || tagBuffer == "a:br/" ||
                tagBuffer == "w:cr" || tagBuffer == "w:cr/" || tagBuffer == "/w:tr" || tagBuffer == "/row" ||
                tagBuffer == "/table:table-row" || tagBuffer == "/w:tc" || tagBuffer == "/c") {
                if (currentText.empty() || currentText.back() != '\n') {
                    currentText.push_back('\n');
                }
            }
            // Inline/格式/文字容器标签 (如 <w:t>, </w:t>, <w:r>, </w:r>, <a:t>, <t>, <v>) 无缝拼接文本，绝不插入破坏性空格！
        } else if (insideTag) {
            if (tagBuffer.size() < 32 && !isspace(static_cast<unsigned char>(c))) {
                tagBuffer.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            }
        } else {
            // 处理常见的 XML 实体
            if (c == '&' && i + 3 < xml.size()) {
                if (xml.compare(i, 4, "&lt;") == 0) {
                    currentText.push_back('<');
                    i += 3;
                    continue;
                } else if (xml.compare(i, 4, "&gt;") == 0) {
                    currentText.push_back('>');
                    i += 3;
                    continue;
                } else if (i + 4 < xml.size() && xml.compare(i, 5, "&amp;") == 0) {
                    currentText.push_back('&');
                    i += 4;
                    continue;
                } else if (i + 5 < xml.size() && xml.compare(i, 6, "&quot;") == 0) {
                    currentText.push_back('"');
                    i += 5;
                    continue;
                } else if (i + 5 < xml.size() && xml.compare(i, 6, "&apos;") == 0) {
                    currentText.push_back('\'');
                    i += 5;
                    continue;
                }
            }
            currentText.push_back(c);
        }
    }

    if (currentText.empty()) return {};

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, currentText.data(), static_cast<int>(currentText.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring result(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, currentText.data(), static_cast<int>(currentText.size()), result.data(), wideLen);

    return result;
}

constexpr size_t MAX_UNCOMP_SIZE = 30 * 1024 * 1024;
constexpr size_t MAX_DOC_UNCOMP_TOTAL_BYTES = 30 * 1024 * 1024;
constexpr size_t MAX_DOC_EXTRACTED_CHARS = 15 * 1024 * 1024;
constexpr size_t MAX_ZIP_MATCHING_ENTRIES = 256;

bool decompressRawDeflate(const uint8_t* compressed, size_t compSize, size_t uncompSize, std::string& out, size_t maxAllowed = MAX_UNCOMP_SIZE) {
    if (compSize == 0 || maxAllowed == 0) return false;
    
    // 限制单 XML 解压上限为 maxAllowed
    size_t targetCap = (std::min)(MAX_UNCOMP_SIZE, maxAllowed);
    size_t targetSize = uncompSize > 0 ? (std::min)(uncompSize, targetCap) : (std::min)(compSize * 10, targetCap);

    try {
        out.resize(targetSize);
    } catch (...) {
        out.clear();
        return false;
    }

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

    try {
        out.resize(actualSize);
    } catch (...) {
        out.clear();
        return false;
    }
    return true;
}

bool isMatchingZipEntry(std::string_view entryName, std::wstring_view lowerExt) {
    std::string lowerName;
    lowerName.reserve(entryName.size());
    for (char c : entryName) {
        lowerName.push_back(c == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c))));
    }

    if (lowerExt == L"docx" || lowerExt == L"wps" || lowerExt == L"dotx" || lowerExt == L"docm") {
        if (lowerName.find("word/") != std::string::npos || lowerName.find("wps/") != std::string::npos) {
            if (lowerName.ends_with(".xml")) return true;
        }
    } else if (lowerExt == L"xlsx" || lowerExt == L"et" || lowerExt == L"xlsm" || lowerExt == L"xltx") {
        if (lowerName.find("xl/") != std::string::npos || lowerName.find("et/") != std::string::npos) {
            if (lowerName.ends_with(".xml")) return true;
        }
    } else if (lowerExt == L"pptx" || lowerExt == L"dps" || lowerExt == L"pptm" || lowerExt == L"potx") {
        if (lowerName.find("ppt/") != std::string::npos || lowerName.find("dps/") != std::string::npos) {
            if (lowerName.ends_with(".xml")) return true;
        }
    } else if (lowerExt == L"cdr") {
        if (lowerName.find("content/") != std::string::npos || lowerName == "metadata.xml") {
            return true;
        }
    } else if (lowerExt == L"xmind") {
        if (lowerName == "content.json" || lowerName == "content.xml" || lowerName == "comments.xml") {
            return true;
        }
    } else if (lowerExt == L"odt" || lowerExt == L"ods" || lowerExt == L"odp") {
        if (lowerName == "content.xml") return true;
    }

    return false;
}

} // namespace

ZipXmlExtractor::ZipXmlExtractor() {
    const wchar_t* exts[] = {
        L"docx", L"dotx", L"docm", L"wps",
        L"xlsx", L"xltx", L"xlsm", L"et",
        L"pptx", L"potx", L"pptm", L"dps",
        L"cdr", L"xmind", L"odt", L"ods", L"odp"
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
    if (zipSize < sizeof(ZipEocd)) return false;

    std::wstring lowerExt;
    for (wchar_t c : extension) lowerExt.push_back(std::towlower(c));

    size_t totalUncompressedBytes = 0;
    size_t matchingEntriesCount = 0;

    // ── 1. 优先通过 Central Directory (中央目录) 解析 (工业级防 Data Descriptor 零长度硬伤) ──
    const ZipEocd* pEocd = nullptr;
    const size_t maxSearch = (std::min)(zipSize, static_cast<size_t>(65535 + sizeof(ZipEocd)));
    const size_t searchStart = zipSize - maxSearch;

    for (size_t i = zipSize - sizeof(ZipEocd); i >= searchStart; --i) {
        if (*reinterpret_cast<const uint32_t*>(pZipData + i) == 0x06054b50) {
            pEocd = reinterpret_cast<const ZipEocd*>(pZipData + i);
            break;
        }
        if (i == 0) break;
    }

    if (pEocd && pEocd->cdOffset < zipSize && (pEocd->cdOffset + pEocd->cdSize <= zipSize)) {
        size_t cdOffset = pEocd->cdOffset;
        for (uint16_t entryIdx = 0; entryIdx < pEocd->totalEntries && cdOffset + sizeof(ZipCdHeader) <= zipSize; ++entryIdx) {
            const auto* cdHeader = reinterpret_cast<const ZipCdHeader*>(pZipData + cdOffset);
            if (cdHeader->signature != 0x02014b50) break;

            size_t nameOffset = cdOffset + sizeof(ZipCdHeader);
            if (nameOffset + cdHeader->filenameLength > zipSize) break;

            std::string filename(reinterpret_cast<const char*>(pZipData + nameOffset), cdHeader->filenameLength);
            cdOffset += sizeof(ZipCdHeader) + cdHeader->filenameLength + cdHeader->extraFieldLength + cdHeader->commentLength;

            if (!isMatchingZipEntry(filename, lowerExt)) continue;

            if (totalUncompressedBytes >= MAX_DOC_UNCOMP_TOTAL_BYTES ||
                outExtractedText.size() >= MAX_DOC_EXTRACTED_CHARS ||
                matchingEntriesCount >= MAX_ZIP_MATCHING_ENTRIES) {
                break;
            }
            matchingEntriesCount++;
            const size_t remainingBudget = MAX_DOC_UNCOMP_TOTAL_BYTES - totalUncompressedBytes;

            // 定位到该 Entry 对应的 Local Header 读取压缩数据
            if (cdHeader->localHeaderOffset + sizeof(ZipLocalHeader) > zipSize) continue;
            const auto* localHeader = reinterpret_cast<const ZipLocalHeader*>(pZipData + cdHeader->localHeaderOffset);
            if (localHeader->signature != 0x04034b50) continue;

            size_t dataOffset = cdHeader->localHeaderOffset + sizeof(ZipLocalHeader) + localHeader->filenameLength + localHeader->extraFieldLength;
            if (dataOffset + cdHeader->compressedSize > zipSize) continue;

            std::string uncompData;
            if (cdHeader->compressionMethod == 0) {
                size_t takeSize = (std::min)(static_cast<size_t>(cdHeader->compressedSize), remainingBudget);
                uncompData.assign(reinterpret_cast<const char*>(pZipData + dataOffset), takeSize);
            } else if (cdHeader->compressionMethod == 8) {
                decompressRawDeflate(pZipData + dataOffset, cdHeader->compressedSize, cdHeader->uncompressedSize, uncompData, remainingBudget);
            }

            if (!uncompData.empty()) {
                totalUncompressedBytes += uncompData.size();
                std::wstring plain = stripXmlTags(uncompData);
                if (!plain.empty()) {
                    if (outExtractedText.size() + plain.size() + 1 > MAX_DOC_EXTRACTED_CHARS) {
                        const size_t allowedChars = MAX_DOC_EXTRACTED_CHARS - outExtractedText.size();
                        if (allowedChars > 0) {
                            outExtractedText.append(plain.data(), (std::min)(plain.size(), allowedChars));
                        }
                        break;
                    } else {
                        outExtractedText += plain;
                        outExtractedText += L"\n";
                    }
                }
            }
        }
        if (!outExtractedText.empty()) return true;
    }

    // ── 2. Fallback: 遍历 Local Header 兜底 (针对截断或非标准 ZIP 文件) ──
    size_t offset = 0;
    while (offset + sizeof(ZipLocalHeader) <= zipSize) {
        const auto* header = reinterpret_cast<const ZipLocalHeader*>(pZipData + offset);
        if (header->signature != 0x04034b50) break;

        size_t nameOffset = offset + sizeof(ZipLocalHeader);
        if (nameOffset + header->filenameLength > zipSize) break;

        std::string filename(reinterpret_cast<const char*>(pZipData + nameOffset), header->filenameLength);
        size_t dataOffset = nameOffset + header->filenameLength + header->extraFieldLength;
        if (dataOffset + header->compressedSize > zipSize) break;

        if (isMatchingZipEntry(filename, lowerExt)) {
            if (totalUncompressedBytes >= MAX_DOC_UNCOMP_TOTAL_BYTES ||
                outExtractedText.size() >= MAX_DOC_EXTRACTED_CHARS ||
                matchingEntriesCount >= MAX_ZIP_MATCHING_ENTRIES) {
                break;
            }
            matchingEntriesCount++;
            const size_t remainingBudget = MAX_DOC_UNCOMP_TOTAL_BYTES - totalUncompressedBytes;

            std::string uncompData;
            if (header->compressionMethod == 0 && header->compressedSize > 0) {
                size_t takeSize = (std::min)(static_cast<size_t>(header->compressedSize), remainingBudget);
                uncompData.assign(reinterpret_cast<const char*>(pZipData + dataOffset), takeSize);
            } else if (header->compressionMethod == 8 && header->compressedSize > 0) {
                decompressRawDeflate(pZipData + dataOffset, header->compressedSize, header->uncompressedSize, uncompData, remainingBudget);
            }

            if (!uncompData.empty()) {
                totalUncompressedBytes += uncompData.size();
                std::wstring plain = stripXmlTags(uncompData);
                if (!plain.empty()) {
                    if (outExtractedText.size() + plain.size() + 1 > MAX_DOC_EXTRACTED_CHARS) {
                        const size_t allowedChars = MAX_DOC_EXTRACTED_CHARS - outExtractedText.size();
                        if (allowedChars > 0) {
                            outExtractedText.append(plain.data(), (std::min)(plain.size(), allowedChars));
                        }
                        break;
                    } else {
                        outExtractedText += plain;
                        outExtractedText += L"\n";
                    }
                }
            }
        }

        if (header->compressedSize == 0) break; // 遇到流式 Data Descriptor，无法通过本地 Header 单向步进
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
    bool hasText = false;
    try {
        hasText = extractZipEntriesText(pData, mapSize, ext, extractedAllText);
    } catch (...) {
        hasText = false;
        extractedAllText.clear();
    }

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
                size_t end = (std::min)(snippetLine.size(), pos + matchLen + CONTEXT_PADDING);
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
