#include "PsdAiExtractor.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <sstream>

namespace easy::service::content {

namespace {

std::wstring utf8ToWide(const char* data, size_t length) {
    if (length == 0) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring result(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), result.data(), wideLen);
    return result;
}

std::wstring extractXmpText(const std::string& xmp) {
    std::wstring result;
    result.reserve(xmp.size() / 2);

    bool insideTag = false;
    std::string currentText;
    currentText.reserve(xmp.size() / 2);

    for (size_t i = 0; i < xmp.size(); ++i) {
        char c = xmp[i];
        if (c == '<') {
            insideTag = true;
            currentText.push_back(' ');
        } else if (c == '>') {
            insideTag = false;
        } else if (!insideTag) {
            currentText.push_back(c);
        }
    }

    if (currentText.empty()) return {};
    return utf8ToWide(currentText.data(), currentText.size());
}

} // namespace

PsdAiExtractor::PsdAiExtractor() {
    m_supportedExts.insert(L"psd");
    m_supportedExts.insert(L"psb");
    m_supportedExts.insert(L"ai");
}

bool PsdAiExtractor::canHandle(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        lowerExt.push_back(std::towlower(c));
    }
    return m_supportedExts.find(lowerExt) != m_supportedExts.end();
}

bool PsdAiExtractor::extractPsdAiText(
    const uint8_t* pData,
    size_t dataSize,
    std::wstring& outExtractedText
) const {
    if (dataSize < 12) return false;

    // 1. 提取嵌入的 XMP Packet (<?xpacket ... </xpacket>)
    std::string_view fullView(reinterpret_cast<const char*>(pData), dataSize);
    size_t xmpStart = fullView.find("<?xpacket");
    if (xmpStart != std::string_view::npos) {
        size_t xmpEnd = fullView.find("</xpacket>", xmpStart);
        if (xmpEnd != std::string_view::npos) {
            std::string xmpContent(fullView.substr(xmpStart, xmpEnd - xmpStart + 10));
            std::wstring xmpText = extractXmpText(xmpContent);
            if (!xmpText.empty()) {
                outExtractedText += xmpText;
                outExtractedText += L"\n";
            }
        }
    }

    // 2. 扫描 PSD / AI 文字图层标记 (/Txt /Text /EngineData /TySh /Txt2)
    // 在 PSD 文本图层中，文本通常作为 UTF-16 BE 或 Pascal/C 字符串存储
    size_t i = 0;
    while (i + 4 < dataSize) {
        // 查找 8BIM 或 TySh 或 Txt2
        if (pData[i] == 'T' && pData[i+1] == 'y' && pData[i+2] == 'S' && pData[i+3] == 'h') {
            size_t scanLimit = std::min(dataSize, i + 8192);
            for (size_t k = i + 4; k + 4 < scanLimit; ++k) {
                // 查找 UTF-16BE 连续可见字符 (0x00 0xXX 或 0x4E-0x9F 0xXX 中文区)
                if (pData[k] == 0x00 && pData[k+1] >= 0x20 && pData[k+1] <= 0x7E) {
                    std::wstring layerText;
                    while (k + 1 < scanLimit) {
                        if (pData[k] == 0x00 && pData[k+1] >= 0x20 && pData[k+1] <= 0x7E) {
                            layerText.push_back(static_cast<wchar_t>(pData[k+1]));
                            k += 2;
                        } else if (pData[k] >= 0x4E && pData[k] <= 0x9F) {
                            // 中文字符 (UTF-16BE)
                            wchar_t zhChar = static_cast<wchar_t>((pData[k] << 8) | pData[k+1]);
                            layerText.push_back(zhChar);
                            k += 2;
                        } else {
                            break;
                        }
                    }
                    if (layerText.size() >= 2) {
                        outExtractedText += layerText;
                        outExtractedText += L"\n";
                    }
                }
            }
            i += 4;
        } else {
            i++;
        }
    }

    return !outExtractedText.empty();
}

bool PsdAiExtractor::searchContent(
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
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0 || fileSize.QuadPart > 150 * 1024 * 1024) {
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

    std::wstring extractedAllText;
    bool hasText = extractPsdAiText(pData, mapSize, extractedAllText);

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    if (!hasText || extractedAllText.empty()) return false;

    // ── 匹配模版 ──────────────────────────────────────────────────
    std::wstring lowerQuery;
    lowerQuery.reserve(queryPattern.size());
    for (wchar_t c : queryPattern) {
        lowerQuery.push_back(caseSensitive ? c : std::towlower(c));
    }

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

        lineNumber++;
    }

    return !outSnippets.empty();
}

} // namespace easy::service::content
