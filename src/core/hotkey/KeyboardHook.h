#ifndef EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H
#define EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H

#include "core/utils/Export.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace easy::core {

class EASYCORE_API KeyboardHook {
public:
    static KeyboardHook& instance();

    bool install();
    void uninstall();

    void setKeycastCallback(std::function<void(const std::string&)> cb);
    void setKeyInterceptor(std::function<bool(DWORD vkCode, WPARAM wParam)> interceptor);
    void setKeyboardActivityCallback(std::function<void(DWORD vkCode, WPARAM wParam)> cb);

private:
    KeyboardHook() = default;
    ~KeyboardHook() = default;

    static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_hookHandle = nullptr;
    std::atomic<bool> m_paused{false};
    std::function<void(const std::string&)> m_keycastCallback;
    std::function<bool(DWORD, WPARAM)> m_keyInterceptor;
    std::function<void(DWORD, WPARAM)> m_activityCallback;
    mutable std::mutex m_callbackMutex;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H
