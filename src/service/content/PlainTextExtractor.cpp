#include "PlainTextExtractor.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <vector>

namespace easy::service::content {

namespace {

enum class DetectedEncoding {
    UnknownBinary,
    Utf8,
    Utf16Le,
    Utf16Be,
    Gbk
};

inline bool isIllegalControlByte(uint8_t b) {
    if (b == 0x00) return true;
    if (b < 0x20) {
        return (b != 0x09 && b != 0x0A && b != 0x0D && b != 0x0C && b != 0x08 && b != 0x1B);
    }
    return b == 0x7F;
}

bool isStrictUtf8(const uint8_t* data, size_t length, size_t& validMultiByteCount, size_t& illegalControlCount) {
    size_t i = 0;
    validMultiByteCount = 0;
    illegalControlCount = 0;

    while (i < length) {
        uint8_t c = data[i];
        if (c <= 0x7F) {
            if (isIllegalControlByte(c)) {
                illegalControlCount++;
            }
            i++;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= length) break;
            if ((data[i + 1] & 0xC0) != 0x80) return false;
            validMultiByteCount++;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 >= length) break;
            uint8_t c1 = data[i + 1];
            uint8_t c2 = data[i + 2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
            if (c == 0xE0 && c1 < 0xA0) return false; // Overlong
            if (c == 0xED && c1 >= 0xA0) return false; // Surrogates (0xD800-0xDFFF)
            validMultiByteCount++;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 >= length) break;
            uint8_t c1 = data[i + 1];
            uint8_t c2 = data[i + 2];
            uint8_t c3 = data[i + 3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
            if (c == 0xF0 && c1 < 0x90) return false;
            if (c == 0xF4 && c1 > 0x8F) return false; // > U+10FFFF
            validMultiByteCount++;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

bool isStrictGbk(const uint8_t* data, size_t length, size_t& validGbkCount, size_t& illegalControlCount) {
    size_t i = 0;
    validGbkCount = 0;
    illegalControlCount = 0;

    while (i < length) {
        uint8_t c = data[i];
        if (c <= 0x7F) {
            if (isIllegalControlByte(c)) {
                illegalControlCount++;
            }
            i++;
        } else if (c >= 0x81 && c <= 0xFE) {
            if (i + 1 >= length) break;
            uint8_t c2 = data[i + 1];
            if ((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0x80 && c2 <= 0xFE)) {
                validGbkCount++;
                i += 2;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool isUtf16Buffer(const uint8_t* data, size_t length, bool isBigEndian) {
    if (length < 4 || (length % 2 != 0)) return false;
    size_t count = length / 2;
    const wchar_t* w = reinterpret_cast<const wchar_t*>(data);
    size_t controlCount = 0;
    size_t validCharCount = 0;

    for (size_t i = 0; i < count; ++i) {
        uint16_t code = static_cast<uint16_t>(w[i]);
        if (isBigEndian) {
            code = static_cast<uint16_t>(((code & 0xFF) << 8) | ((code >> 8) & 0xFF));
        }
        if (code >= 0xD800 && code <= 0xDFFF) return false;
        if (code == 0x0000) return false;
        if (code < 0x20) {
            if (code != 0x09 && code != 0x0A && code != 0x0D) {
                controlCount++;
            }
        } else {
            validCharCount++;
        }
    }

    if (count > 0 && (static_cast<double>(controlCount) / count > 0.03)) {
        return false;
    }
    return validCharCount > 0;
}

DetectedEncoding detectEncoding(const uint8_t* data, size_t length, size_t& outBomOffset) {
    outBomOffset = 0;
    if (length == 0) return DetectedEncoding::UnknownBinary;

    // 1. BOM 优先检测
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        outBomOffset = 3;
        return DetectedEncoding::Utf8;
    }
    if (length >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        outBomOffset = 2;
        return DetectedEncoding::Utf16Le;
    }
    if (length >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        outBomOffset = 2;
        return DetectedEncoding::Utf16Be;
    }

    // 2. 采样分析 (前 8KB)
    size_t sampleLen = std::min<size_t>(length, 8192);

    // 快速 Null 字节检测 (普通文本绝无 Null 字节)
    size_t nullCount = 0;
    for (size_t i = 0; i < sampleLen; ++i) {
        if (data[i] == 0x00) nullCount++;
    }

    if (nullCount > 0) {
        if (isUtf16Buffer(data, sampleLen, false)) {
            return DetectedEncoding::Utf16Le;
        }
        if (isUtf16Buffer(data, sampleLen, true)) {
            return DetectedEncoding::Utf16Be;
        }
        // 包含 0x00 且非合法 UTF-16 -> 判定为纯二进制数据 (如 edb.log, MeasuredBoot.log, dump 文件)
        return DetectedEncoding::UnknownBinary;
    }

    // 3. UTF-8 严格分析
    size_t utf8MultiBytes = 0;
    size_t utf8IllegalControls = 0;
    bool utf8Valid = isStrictUtf8(data, sampleLen, utf8MultiBytes, utf8IllegalControls);

    if (utf8Valid) {
        if (utf8IllegalControls > 0 && (static_cast<double>(utf8IllegalControls) / sampleLen > 0.005)) {
            return DetectedEncoding::UnknownBinary;
        }
        return DetectedEncoding::Utf8;
    }

    // 4. GBK / GB18030 / ANSI 分析
    size_t gbkMultiBytes = 0;
    size_t gbkIllegalControls = 0;
    bool gbkValid = isStrictGbk(data, sampleLen, gbkMultiBytes, gbkIllegalControls);

    if (gbkValid) {
        if (gbkIllegalControls > 0 && (static_cast<double>(gbkIllegalControls) / sampleLen > 0.005)) {
            return DetectedEncoding::UnknownBinary;
        }
        if (gbkMultiBytes == 0 && gbkIllegalControls == 0) {
            return DetectedEncoding::Utf8;
        }
        return DetectedEncoding::Gbk;
    }

    // 5. 无法识别为任何已知纯文本编码
    return DetectedEncoding::UnknownBinary;
}

static UINT getGbkCodePage() {
    static UINT s_cp = IsValidCodePage(936) ? 936 : (IsValidCodePage(54936) ? 54936 : CP_ACP);
    return s_cp;
}

std::wstring decodeLine(const char* data, size_t length, UINT codePage) {
    if (length == 0) return {};

    const UINT actualCp = (codePage == 936) ? getGbkCodePage() : codePage;

    if (actualCp != CP_UTF8) {
        int wideLen = MultiByteToWideChar(actualCp, 0, data, static_cast<int>(length), nullptr, 0);
        if (wideLen > 0) {
            std::wstring result(wideLen, L'\0');
            MultiByteToWideChar(actualCp, 0, data, static_cast<int>(length), result.data(), wideLen);
            return result;
        }
    }

    // 默认或 UTF-8: 优先尝试 UTF-8，失败时回退到 GBK/ACP
    int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), nullptr, 0);
    if (wideLen > 0) {
        std::wstring result(wideLen, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(length), result.data(), wideLen) > 0) {
            return result;
        }
    }

    // UTF-8 失败时回退到 GBK/ACP
    const UINT fallbackCp = getGbkCodePage();
    wideLen = MultiByteToWideChar(fallbackCp, 0, data, static_cast<int>(length), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring result(wideLen, L'\0');
    MultiByteToWideChar(fallbackCp, 0, data, static_cast<int>(length), result.data(), wideLen);
    return result;
}

void sanitizeLine(std::wstring& line) {
    for (auto& ch : line) {
        if (ch == L'\0' || (ch < 0x20 && ch != L'\t')) {
            ch = L' ';
        }
    }
}

bool isLineGarbled(const std::wstring& line) {
    if (line.empty()) return false;
    size_t badChars = 0;
    for (wchar_t ch : line) {
        if (ch == 0xFFFD || ch == 0x0000 || (ch < 0x20 && ch != L'\t')) {
            badChars++;
        }
    }
    return (static_cast<double>(badChars) / line.size()) > 0.20;
}

void formatAndAddSnippet(
    uint32_t lineNumber,
    std::wstring line,
    size_t matchPos,
    size_t matchLen,
    std::vector<ContentSnippet>& outSnippets
) {
    sanitizeLine(line);
    if (isLineGarbled(line)) return;

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
        // C/C++/C#
        L"c", L"cpp", L"cc", L"cxx", L"h", L"hpp", L"hxx", L"inl", L"rc", L"def", L"cs",
        // Modern Systems & Compilers
        L"rs", L"go", L"zig", L"v", L"nim", L"odin", L"d",
        // JVM & Mobile
        L"java", L"kt", L"kts", L"scala", L"groovy", L"dart", L"swift", L"m", L"mm",
        // Web / Fullstack / Modern Frameworks
        L"js", L"jsx", L"mjs", L"cjs", L"ts", L"tsx", L"vue", L"svelte", L"astro",
        L"html", L"htm", L"css", L"scss", L"sass", L"less",
        // Schema & IDL / Data Definitions
        L"proto", L"graphql", L"gql", L"thrift", L"prisma", L"schema", L"avsc", L"dbml",
        // Data & Config
        L"json", L"jsonc", L"json5", L"xml", L"xaml", L"yaml", L"yml", L"toml",
        L"ini", L"cfg", L"conf", L"config", L"properties", L"env", L"reg", L"lock", L"plist", L"prefs",
        // Python/Ruby/PHP/Lua/Shell/PowerShell/Automation
        L"py", L"pyw", L"rb", L"php", L"pl", L"pm", L"lua", L"sh", L"bash", L"zsh",
        L"ps1", L"psm1", L"psd1", L"bat", L"cmd", L"vbs", L"ahk", L"au3",
        // SQL & Enterprise
        L"sql", L"prc", L"fnc", L"trg", L"pks", L"pkb", L"pls", L"ch", L"pld",
        // Plain text & Docs & Markup
        L"txt", L"md", L"markdown", L"log", L"csv", L"tsv", L"tex", L"bib", L"rst", L"adoc", L"rtf", L"diff", L"patch", L"org",
        // Assembly & Shaders
        L"asm", L"s", L"glsl", L"hlsl", L"vert", L"frag", L"geom", L"comp", L"shader", L"wgsl"
    };

    for (const auto* ext : exts) {
        m_supportedExts.insert(ext);
    }
}

bool PlainTextExtractor::canHandle(std::wstring_view extension) const {
    std::wstring lowerExt;
    lowerExt.reserve(extension.size());
    for (wchar_t c : extension) {
        if (c != L'.') lowerExt.push_back(std::towlower(c));
    }
    if (lowerExt.empty()) return false;
    return m_supportedExts.find(lowerExt) != m_supportedExts.end();
}

// ── 纳秒级字节预过滤 (1 微秒内过滤 99.99% 无关文件，免去行循环与文本解码开销) ──
static bool fastBytePreFilter(
    const uint8_t* data,
    size_t len,
    std::wstring_view queryPattern,
    bool caseSensitive,
    DetectedEncoding enc
) {
    if (len == 0 || queryPattern.empty()) return false;

    if (enc == DetectedEncoding::Utf16Le || enc == DetectedEncoding::Utf16Be) {
        return true; 
    }

    // 检查查询词是否为纯 ASCII (如 "oitt", "class", "function", "select")
    bool isAscii = true;
    std::string asciiQuery;
    asciiQuery.reserve(queryPattern.size());
    for (wchar_t wc : queryPattern) {
        if (wc < 128) {
            asciiQuery.push_back(static_cast<char>(wc));
        } else {
            isAscii = false;
            break;
        }
    }

    if (isAscii) {
        const size_t patLen = asciiQuery.size();
        if (len < patLen) return false;

        const uint8_t firstLower = static_cast<uint8_t>(std::tolower(asciiQuery[0]));
        const uint8_t firstUpper = static_cast<uint8_t>(std::toupper(asciiQuery[0]));
        const size_t end = len - patLen + 1;

        for (size_t i = 0; i < end; ++i) {
            uint8_t b = data[i];
            if (caseSensitive ? (b == asciiQuery[0]) : (b == firstLower || b == firstUpper)) {
                bool match = true;
                for (size_t j = 1; j < patLen; ++j) {
                    uint8_t bj = data[i + j];
                    uint8_t pj = static_cast<uint8_t>(asciiQuery[j]);
                    if (caseSensitive ? (bj != pj) : (bj != pj && std::tolower(bj) != std::tolower(pj))) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
        }
        return false;
    } else {
        // 非 ASCII 查询词 (如中文 "表头" 或 "方案")
        // 1. 生成 UTF-8 字节串比对
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::vector<uint8_t> utf8Pat(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), reinterpret_cast<char*>(utf8Pat.data()), utf8Len, nullptr, nullptr);
            if (std::search(data, data + len, utf8Pat.begin(), utf8Pat.end()) != data + len) {
                return true;
            }
        }
        // 2. 生成 GBK (936) / ACP 字节串比对
        const UINT gbkCp = getGbkCodePage();
        int gbkLen = WideCharToMultiByte(gbkCp, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), nullptr, 0, nullptr, nullptr);
        if (gbkLen > 0) {
            std::vector<uint8_t> gbkPat(gbkLen);
            WideCharToMultiByte(gbkCp, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), reinterpret_cast<char*>(gbkPat.data()), gbkLen, nullptr, nullptr);
            if (std::search(data, data + len, gbkPat.begin(), gbkPat.end()) != data + len) {
                return true;
            }
        }
        // 3. 生成 CP_ACP 字节串比对 (若非 936)
        if (GetACP() != gbkCp && GetACP() != CP_UTF8) {
            int acpLen = WideCharToMultiByte(CP_ACP, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), nullptr, 0, nullptr, nullptr);
            if (acpLen > 0) {
                std::vector<uint8_t> acpPat(acpLen);
                WideCharToMultiByte(CP_ACP, 0, queryPattern.data(), static_cast<int>(queryPattern.size()), reinterpret_cast<char*>(acpPat.data()), acpLen, nullptr, nullptr);
                if (std::search(data, data + len, acpPat.begin(), acpPat.end()) != data + len) {
                    return true;
                }
            }
        }
        return false;
    }
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

    // ── 智能字符集与二进制深度嗅探 ─────────────────────────────────
    size_t bomOffset = 0;
    DetectedEncoding enc = detectEncoding(pData, mapSize, bomOffset);

    if (enc == DetectedEncoding::UnknownBinary) {
        // 安全拦截未知二进制文件（如 Windows ESE 日志 edb*.log 或 TPM MeasuredBoot*.log）
        UnmapViewOfFile(pData);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    // ── 纳秒级极速字节预过滤 (1 微秒内过滤 99.99% 无关文件) ───────────
    if (!fastBytePreFilter(pData + bomOffset, mapSize - bomOffset, queryPattern, caseSensitive, enc)) {
        UnmapViewOfFile(pData);
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

    // ── 扫描与匹配 ──────────────────────────────────────────────────
    uint32_t lineNumber = 1;

    if (enc == DetectedEncoding::Utf16Le || enc == DetectedEncoding::Utf16Be) {
        const bool isBe = (enc == DetectedEncoding::Utf16Be);
        const wchar_t* wData = reinterpret_cast<const wchar_t*>(pData + bomOffset);
        size_t wCount = (mapSize - bomOffset) / sizeof(wchar_t);
        size_t lineStart = 0;

        for (size_t i = 0; i < wCount; ++i) {
            wchar_t ch = wData[i];
            if (isBe) {
                ch = static_cast<wchar_t>(((ch & 0xFF) << 8) | ((ch >> 8) & 0xFF));
            }

            if (ch == L'\n' || i == wCount - 1) {
                size_t lineEnd = (ch == L'\n') ? i : (i + 1);
                std::wstring line(wData + lineStart, lineEnd - lineStart);
                if (isBe) {
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
        const UINT codePage = (enc == DetectedEncoding::Gbk) ? 936 : CP_UTF8;
        size_t lineStart = bomOffset;

        for (size_t i = bomOffset; i < mapSize; ++i) {
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
