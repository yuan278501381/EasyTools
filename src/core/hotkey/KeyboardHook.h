#ifndef EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H
#define EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H

#include "core/utils/Export.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <string>

namespace easy::core {

class EASYCORE_API KeyboardHook {
public:
    static KeyboardHook& instance();

    bool install();
    void uninstall();

    void setKeycastCallback(std::function<void(const std::string&)> cb) { m_keycastCallback = std::move(cb); }
    const std::function<void(const std::string&)>& getKeycastCallback() const { return m_keycastCallback; }

private:
    KeyboardHook() = default;
    ~KeyboardHook() = default;

    static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_hookHandle = nullptr;
    std::atomic<bool> m_paused{false};
    std::function<void(const std::string&)> m_keycastCallback;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H

