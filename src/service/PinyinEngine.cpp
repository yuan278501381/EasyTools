#include "PinyinEngine.h"
#include <windows.h>
#include <cctype>

// GB2312 Level 1 boundaries for Pinyin initials
static const int gb2312_pinyin_bounds[] = {
    0xB0A1, 0xB0C5, 0xB2C1, 0xB4EE, 0xB6EA, 0xB7A2, 0xB8C1, 0xB9FE,
    0xBBF7, 0xBFA6, 0xC0AC, 0xC2E8, 0xC4C3, 0xC5B6, 0xC5BE, 0xC6DA,
    0xC8BB, 0xC8F6, 0xCBFA, 0xCDDA, 0xCEF4, 0xD1B9, 0xD4D1, 0xD7FA
};
static const char gb2312_pinyin_letters[] = "abcdefghjklmnopqrstwxyz";

wchar_t PinyinEngine::GetFirstLetter(wchar_t ch) {
    if (ch >= 0 && ch <= 127) {
        return std::tolower(ch);
    }
    
    // Convert to GBK (codepage 936)
    char gbk[3] = {0};
    int len = WideCharToMultiByte(936, 0, &ch, 1, gbk, 2, NULL, NULL);
    if (len == 2) {
        unsigned char high = (unsigned char)gbk[0];
        unsigned char low  = (unsigned char)gbk[1];
        int code = (high << 8) | low;

        // Level 1 characters are between B0A1 and D7F9
        if (code >= 0xB0A1 && code <= 0xD7F9) {
            for (int i = 0; i < 23; ++i) {
                if (code >= gb2312_pinyin_bounds[i] && code < gb2312_pinyin_bounds[i+1]) {
                    return gb2312_pinyin_letters[i];
                }
            }
        }
    }
    return std::tolower(ch);
}

std::wstring PinyinEngine::GetInitials(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.length());
    for (wchar_t ch : text) {
        result += GetFirstLetter(ch);
    }
    return result;
}
