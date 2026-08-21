#pragma once

#include <cstddef>
#include <string_view>

#include <windows.h>

namespace easy::ui::web_security {

enum class Surface { Settings, Search, Tray, QuickLook };

// WebView bridge is a control plane, not a bulk-data channel.  Keep malformed
// renderer input from forcing unbounded UTF conversion / JSON allocations.
constexpr size_t MaxBridgeMessageBytes = 1024 * 1024;

inline bool isBridgeMessageSizeAcceptable(size_t byteCount) noexcept {
    return byteCount <= MaxBridgeMessageBytes;
}

inline bool startsWithInsensitive(std::wstring_view value, std::wstring_view prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    return _wcsnicmp(value.data(), prefix.data(), prefix.size()) == 0;
}

inline bool isTrustedUri(std::wstring_view uri) noexcept {
    if (uri == L"https://easytools.local" ||
        startsWithInsensitive(uri, L"https://easytools.local/")) {
        return true;
    }
#ifdef _DEBUG
    // 开发构建才允许本机 Vite；Release 永远只接受打包的虚拟 HTTPS 源。
    if (startsWithInsensitive(uri, L"http://localhost:") ||
        startsWithInsensitive(uri, L"http://127.0.0.1:")) {
        return true;
    }
#endif
    return false;
}

}  // namespace easy::ui::web_security
