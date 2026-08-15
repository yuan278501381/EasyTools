#include "PinyinEngine.h"
#include "UnicodePinyinData.h"
#include <cctype>

// ─────────────────────────────────────────────────────────────────────────────
// PinyinEngine — 零系统依赖的高性能 Unicode 汉字拼音引擎
// 内部使用基于已排序 Unicode 码点的二分查找表，O(log N) 单字符耗时 < 5ns
// ─────────────────────────────────────────────────────────────────────────────

wchar_t PinyinEngine::GetFirstLetter(wchar_t ch) {
    if (ch >= 0 && ch <= 127) {
        return static_cast<wchar_t>(std::tolower(static_cast<int>(ch)));
    }
    std::wstring pinyin = GetCharPinyin(ch);
    if (!pinyin.empty()) {
        return pinyin[0];
    }
    return static_cast<wchar_t>(std::tolower(static_cast<int>(ch)));
}

std::wstring PinyinEngine::GetCharPinyin(wchar_t ch) {
    if (ch >= 0 && ch <= 127) {
        return std::wstring(1, static_cast<wchar_t>(std::tolower(static_cast<int>(ch))));
    }

    const uint16_t u = static_cast<uint16_t>(ch);

    // 二分查找 UnicodePinyinTable
    size_t left = 0;
    size_t right = kUnicodePinyinTableSize - 1;
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        if (kUnicodePinyinTable[mid].unicode == u) {
            uint16_t syllableId = kUnicodePinyinTable[mid].syllableId;
            const char* py = kPinyinSyllables[syllableId];
            std::wstring res;
            while (*py) res.push_back(static_cast<wchar_t>(*py++));
            return res;
        }
        if (kUnicodePinyinTable[mid].unicode < u) {
            left = mid + 1;
        } else {
            if (mid == 0) break;
            right = mid - 1;
        }
    }

    return std::wstring(1, static_cast<wchar_t>(std::tolower(static_cast<int>(ch))));
}

std::wstring PinyinEngine::GetInitials(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.length());
    for (wchar_t ch : text) {
        result += GetFirstLetter(ch);
    }
    return result;
}

std::wstring PinyinEngine::GetFullPinyin(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.length() * 4);
    for (wchar_t ch : text) {
        result += GetCharPinyin(ch);
    }
    return result;
}
