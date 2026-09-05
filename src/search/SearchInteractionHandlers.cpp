#include "search/SearchInteractionHandlers.h"

#include "core/config/ConfigManager.h"
#include "core/ipc/FileInteractionHandlers.h"
#include "core/ipc/MessageBridge.h"

#include <windows.h>

namespace easy::search {
using easy::core::json;

namespace {

HWND currentProcessSearchWindow() {
    const DWORD currentProcessId = GetCurrentProcessId();
    HWND after = nullptr;
    while ((after = FindWindowExW(
                nullptr, after, L"EasyTools_SearchWindow", nullptr)) != nullptr) {
        DWORD ownerProcessId = 0;
        GetWindowThreadProcessId(after, &ownerProcessId);
        if (ownerProcessId == currentProcessId) return after;
    }
    return nullptr;
}

}  // namespace

void registerSearchInteractionHandlers(easy::core::MessageBridge& bridge) {
    easy::core::registerFileInteractionHandlers(bridge, "search", "path is empty", true);

    bridge.registerHandler("search.startDrag", [](const json&) -> json {
        const HWND hwnd = currentProcessSearchWindow();
        if (hwnd && IsWindow(hwnd)) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return {{"success", true}};
    });
    bridge.registerHandler("search.startResize", [](const json& params) -> json {
        const std::string edge = params.value("edge", params.value("direction", "bottom_right"));
        const HWND hwnd = currentProcessSearchWindow();
        if (hwnd && IsWindow(hwnd)) {
            WPARAM hitTest = HTBOTTOMRIGHT;
            if (edge == "top" || edge == "n") hitTest = HTTOP;
            else if (edge == "bottom" || edge == "s") hitTest = HTBOTTOM;
            else if (edge == "left" || edge == "w") hitTest = HTLEFT;
            else if (edge == "right" || edge == "e") hitTest = HTRIGHT;
            else if (edge == "top_left" || edge == "nw") hitTest = HTTOPLEFT;
            else if (edge == "top_right" || edge == "ne") hitTest = HTTOPRIGHT;
            else if (edge == "bottom_left" || edge == "sw") hitTest = HTBOTTOMLEFT;
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, hitTest, 0);
        }
        return {{"success", true}};
    });
    bridge.registerHandler("search.resetPlacement", [](const json&) -> json {
        auto& config = easy::core::ConfigManager::instance();
        config.set<int>("/search/windowWidth", 760);
        config.set<int>("/search/windowHeight", 520);
        config.set<int>("/search/windowX", -99999);
        config.set<int>("/search/windowY", -99999);
        const HWND hwnd = currentProcessSearchWindow();
        if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
        return {{"success", true}};
    });
    bridge.registerHandler("search.setPinned", [](const json& params) -> json {
        const bool pinned = params.value("pinned", false);
        const HWND hwnd = currentProcessSearchWindow();
        if (hwnd && IsWindow(hwnd)) {
            if (pinned) {
                SetPropW(hwnd, L"EasyTools_SearchPinned", reinterpret_cast<HANDLE>(1));
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            } else {
                RemovePropW(hwnd, L"EasyTools_SearchPinned");
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        return {{"success", true}, {"pinned", pinned}};
    });
    bridge.registerHandler("search.isPinned", [](const json&) -> json {
        const HWND hwnd = currentProcessSearchWindow();
        return {{"pinned", hwnd && IsWindow(hwnd) &&
                           GetPropW(hwnd, L"EasyTools_SearchPinned") != nullptr}};
    });
}

}  // namespace easy::search
