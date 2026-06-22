#pragma once
#include <string>

class PinyinEngine {
public:
    // Returns the pinyin initials for a given string (e.g. "你好" -> "nh")
    // Non-chinese characters are kept as lowercase
    static std::wstring GetInitials(const std::wstring& text);
    
private:
    static wchar_t GetFirstLetter(wchar_t ch);
};
