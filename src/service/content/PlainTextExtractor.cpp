#include "PlainTextExtractor.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <memory>

namespace easy::service::content {

namespace {

bool isValidUtf8(const uint8_t* data, size_t length) {
    size_t i = 0;
    while (i < length) {
        if (data[i] <= 0x7F) {
            i++;
        } else if ((data[i] & 0xE0) == 0xC0) {
            if (i + 1 >= length || (data[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((data[i] & 0xF0) == 0xE0) {
            if (i + 2 >= length || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((data[i] & 0xF8) == 0xF0) {
            if (i + 3 >= length || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

std::wstring decodeLine(const char* data, size_t length, UINT codePage) {
    if (length == 0) return {};
    int wideLen = MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring result(wideLen, L'\0');
    MultiByteToWideChar(codePage, 0, data, static_cast<int>(length), result.data(), wideLen);
    return result;
}

void formatAndAddSnippet(
    uint32_t lineNumber,
    std::wstring line,
    size_t matchPos,
    size_t matchLen,
    std::vector<ContentSnippet>& outSnippets
) {
    // 裁剪超长单行（例如压缩混淆后的 JS/JSON 单行数十万字符）
    constexpr size_t MAX_SNIPPET_LINE_LEN = 240;
    constexpr size_t CONTEXT_PADDING = 50;

    uint32_t snippetOffset = static_cast<uint32_t>(matchPos);
    if (line.size() > MAX_SNIPPET_LINE_LEN) {
        size_t start = (matchPos > CONTEXT_PADDING) ? (matchPos - CONTEXT_PADDING) : 0;
        size_t end = std::min(line.size(), matchPos + matchLen + CONTEXT_PADDING);
        std::wstring truncated;
        if (start > 0) truncated += L"...";
        snippetOffset = static_cast<uint32_t>(truncated.size() + (matchPos - start));
        truncated += line.substr(start, end - start);
        if (end < line.size()) truncated += L"...";
        line = std::move(truncated);
    }

    outSnippets.push_back(ContentSnippet{
        .lineNumber = lineNumber,
        .lineContent = std::move(line),
        .matchOffset = snippetOffset,
        .matchLength = static_cast<uint32_t>(matchLen)
    });
}

} // namespace

PlainTextExtractor::PlainTextExtractor() {
    const wchar_t* exts[] = {
        // C/C++
        L"c", L"cpp", L"cc", L"cxx", L"h", L"hpp", L"hxx", L"inl", L"rc", L"def",
        // C#/Java/Kotlin/Rust/Go/Swift/Scala
        L"cs", L"java", L"kt", L"kts", L"rs", L"go", L"swift", L"scala", L"dart",
        // Web/Script
        L"js", L"jsx", L"mjs", L"cjs", L"ts", L"tsx", L"vue", L"svelte",
        L"html", L"htm", L"css", L"scss", L"sass", L"less",
        // Data & Config
        L"json", L"jsonc", L"json5", L"xml", L"xaml", L"yaml", L"yml", L"toml",
        L"ini", L"cfg", L"conf", L"config", L"properties", L"env", L"reg",
        // Python/Ruby/PHP/Lua/Shell/PS
        L"py", L"pyw", L"rb", L"php", L"pl", L"pm", L"lua", L"sh", L"bash", L"zsh",
        L"ps1", L"psm1", L"psd1", L"bat", L"cmd", L"vbs",
        // SQL & Database
        L"sql", L"prc", L"fnc", L"trg", L"pks", L"pkb", L"pls",
        // Plain text & Docs
        L"txt", L"md", L"markdown", L"log", L"csv", L"tsv", L"tex", L"bib", L"diff", L"patch",
        // Assembly & Shaders
        L"asm", L"s", L"glsl", L"hlsl", L"vert", L"frag", L"geom", L"comp", L"shader"
    };

    for (const auto* ext : exts) {
        m_supportedExts.insert(ext);
    }
}

bool PlainTextExtractor::canHandle(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        lowerExt.push_back(std::towlower(c));
    }
    return m_supportedExts.find(lowerExt) != m_supportedExts.end();
}

bool PlainTextExtractor::searchContent(
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
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        return false;
    }

    // 限制单文件最大扫描大小为 50MB，避免超大日志或 dump 卡死
    constexpr uint64_t MAX_SCAN_SIZE = 50 * 1024 * 1024;
    size_t mapSize = static_cast<size_t>(std::min<uint64_t>(fileSize.QuadPart, MAX_SCAN_SIZE));

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

    // ── 智能字符集与编码识别 ─────────────────────────────────────────
    UINT codePage = CP_UTF8;
    size_t offset = 0;
    bool isUtf16Le = false;
    bool isUtf16Be = false;

    if (mapSize >= 3 && pData[0] == 0xEF && pData[1] == 0xBB && pData[2] == 0xBF) {
        codePage = CP_UTF8;
        offset = 3;
    } else if (mapSize >= 2 && pData[0] == 0xFF && pData[1] == 0xFE) {
        isUtf16Le = true;
        offset = 2;
    } else if (mapSize >= 2 && pData[0] == 0xFE && pData[1] == 0xFF) {
        isUtf16Be = true;
        offset = 2;
    } else {
        // 采样探测前 4KB 是否为合法的 UTF-8
        size_t sampleLen = std::min<size_t>(mapSize, 4096);
        if (isValidUtf8(pData, sampleLen)) {
            codePage = CP_UTF8;
        } else {
            codePage = CP_ACP; // 自动兼容 GBK / GB18030 / ANSI
        }
    }

    // ── 扫描与匹配 ──────────────────────────────────────────────────
    uint32_t lineNumber = 1;

    if (isUtf16Le || isUtf16Be) {
        // UTF-16 文本逐行扫描
        const wchar_t* wData = reinterpret_cast<const wchar_t*>(pData + offset);
        size_t wCount = (mapSize - offset) / sizeof(wchar_t);
        size_t lineStart = 0;

        for (size_t i = 0; i < wCount; ++i) {
            wchar_t ch = wData[i];
            if (isUtf16Be) {
                ch = static_cast<wchar_t>(((ch & 0xFF) << 8) | ((ch >> 8) & 0xFF));
            }

            if (ch == L'\n' || i == wCount - 1) {
                size_t lineEnd = (ch == L'\n') ? i : (i + 1);
                std::wstring line(wData + lineStart, lineEnd - lineStart);
                if (isUtf16Be) {
                    for (auto& wc : line) {
                        wc = static_cast<wchar_t>(((wc & 0xFF) << 8) | ((wc >> 8) & 0xFF));
                    }
                }
                while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
                    line.pop_back();
                }

                std::wstring searchTarget = line;
                if (!caseSensitive) {
                    for (auto& wc : searchTarget) wc = std::towlower(wc);
                }

                size_t pos = searchTarget.find(lowerQuery);
                if (pos != std::wstring::npos) {
                    formatAndAddSnippet(lineNumber, std::move(line), pos, lowerQuery.size(), outSnippets);
                    if (outSnippets.size() >= maxSnippetsPerFile) break;
                }

                lineStart = i + 1;
                lineNumber++;
            }
        }
    } else {
        // UTF-8 / GBK 文本逐行扫描
        size_t lineStart = offset;
        for (size_t i = offset; i < mapSize; ++i) {
            if (pData[i] == '\n' || i == mapSize - 1) {
                size_t lineEnd = (pData[i] == '\n') ? i : (i + 1);
                size_t lineLen = lineEnd - lineStart;

                std::wstring line = decodeLine(reinterpret_cast<const char*>(pData + lineStart), lineLen, codePage);
                while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) {
                    line.pop_back();
                }

                std::wstring searchTarget = line;
                if (!caseSensitive) {
                    for (auto& wc : searchTarget) wc = std::towlower(wc);
                }

                size_t pos = searchTarget.find(lowerQuery);
                if (pos != std::wstring::npos) {
                    formatAndAddSnippet(lineNumber, std::move(line), pos, lowerQuery.size(), outSnippets);
                    if (outSnippets.size() >= maxSnippetsPerFile) break;
                }

                lineStart = i + 1;
                lineNumber++;
            }
        }
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return !outSnippets.empty();
}

} // namespace easy::service::content
