#ifndef EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H
#define EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H

#include <windows.h>
#include <atomic>

namespace easy::core {

class KeyboardHook {
public:
    static KeyboardHook& instance();

    bool install();
    void uninstall();

private:
    KeyboardHook() = default;
    ~KeyboardHook() = default;

    static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_hookHandle = nullptr;
    std::atomic<bool> m_paused{false};
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_HOTKEY_KEYBOARDHOOK_H
