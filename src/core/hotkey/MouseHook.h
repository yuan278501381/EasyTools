#ifndef EASYTOOLS_CORE_HOTKEY_MOUSEHOOK_H
#define EASYTOOLS_CORE_HOTKEY_MOUSEHOOK_H

#include "core/utils/Export.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>

namespace easy::core {

class EASYCORE_API MouseHook {
public:
    static MouseHook& instance();

    bool install();
    void uninstall();

    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    void setMouseActivityCallback(std::function<void(int button, long x, long y)> cb);

private:
    MouseHook() = default;
    ~MouseHook() = default;

    static LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK m_hookHandle = nullptr;
    std::atomic<bool> m_paused{false};
    std::function<void(int button, long x, long y)> m_activityCallback;
    mutable std::mutex m_callbackMutex;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_HOTKEY_MOUSEHOOK_H
