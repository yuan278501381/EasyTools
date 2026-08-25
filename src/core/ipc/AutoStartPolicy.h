#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace easy::core::autostart {

inline void replaceAll(std::wstring& value, std::wstring_view from, std::wstring_view to) {
    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::wstring::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline std::wstring decodeXml(std::wstring value) {
    replaceAll(value, L"&quot;", L"\"");
    replaceAll(value, L"&apos;", L"'");
    replaceAll(value, L"&lt;", L"<");
    replaceAll(value, L"&gt;", L">");
    replaceAll(value, L"&amp;", L"&");
    return value;
}

inline std::optional<std::wstring> xmlElement(std::wstring_view xml, std::wstring_view name) {
    const std::wstring open = L"<" + std::wstring(name) + L">";
    const std::wstring close = L"</" + std::wstring(name) + L">";
    const size_t begin = xml.find(open);
    if (begin == std::wstring_view::npos) return std::nullopt;
    const size_t valueBegin = begin + open.size();
    const size_t end = xml.find(close, valueBegin);
    if (end == std::wstring_view::npos) return std::nullopt;
    return decodeXml(std::wstring(xml.substr(valueBegin, end - valueBegin)));
}

inline std::wstring normalizeExecutablePath(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) value.erase(value.begin());
    while (!value.empty() && std::iswspace(value.back())) value.pop_back();
    if (value.size() >= 2 && value.front() == L'\"' && value.back() == L'\"') {
        value = value.substr(1, value.size() - 2);
    }
    value = std::filesystem::path(value).lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

inline bool taskTargetsExecutable(std::wstring_view taskXml,
                                  const std::filesystem::path& executable) {
    const auto command = xmlElement(taskXml, L"Command");
    return command && normalizeExecutablePath(*command) ==
                          normalizeExecutablePath(executable.wstring());
}

} // namespace easy::core::autostart
