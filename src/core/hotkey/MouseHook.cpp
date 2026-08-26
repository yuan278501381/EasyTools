#include "core/hotkey/MouseHook.h"
#include "core/events/EventBus.h"
#include "core/logger/Logger.h"

namespace easy::core {

MouseHook& MouseHook::instance() {
    static MouseHook inst;
    return inst;
}

bool MouseHook::install() {
    if (m_hookHandle) return true;

    m_hookHandle = SetWindowsHookExW(
        WH_MOUSE_LL,
        lowLevelMouseProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (!m_hookHandle) {
        LOG_ERROR("安装全局核心鼠标钩子失败, error={}", GetLastError());
        return false;
    }

    LOG_INFO("全局核心鼠标钩子安装成功");
    return true;
}

void MouseHook::uninstall() {
    if (m_hookHandle) {
        UnhookWindowsHookEx(m_hookHandle);
        m_hookHandle = nullptr;
        LOG_INFO("全局核心鼠标钩子已卸载");
    }
}

void MouseHook::setPaused(bool paused) {
    m_paused.store(paused);
}

void MouseHook::setMouseActivityCallback(std::function<void(int, long, long)> cb) {
    std::lock_guard lock(m_callbackMutex);
    m_activityCallback = std::move(cb);
}

LRESULT CALLBACK MouseHook::lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    auto& self = MouseHook::instance();

    if (nCode == HC_ACTION && !self.m_paused.load(std::memory_order_relaxed)) {
        auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (data) {
            int button = -1;
            bool isActivity = false;

            switch (wParam) {
                case WM_MOUSEMOVE:
                    button = -1;
                    isActivity = true;
                    break;
                case WM_LBUTTONDOWN:
                    button = 0;
                    isActivity = true;
                    break;
                case WM_RBUTTONDOWN:
                    button = 1;
                    isActivity = true;
                    break;
                case WM_MBUTTONDOWN:
                    button = 2;
                    isActivity = true;
                    break;
                default:
                    break;
            }

            if (isActivity) {
                try {
                    // 1. 同步回调
                    {
                        std::lock_guard lock(self.m_callbackMutex);
                        if (self.m_activityCallback) {
                            self.m_activityCallback(button, data->pt.x, data->pt.y);
                        }
                    }
                    // 2. 发布 EventBus 事件
                    EventBus::instance().publish(MouseActivityEvent{button, data->pt.x, data->pt.y});
                } catch (const std::exception& e) {
                    LOG_ERROR("MouseHook 活动事件分发异常: {}", e.what());
                } catch (...) {
                    LOG_ERROR("MouseHook 活动事件分发未知异常");
                }
            }
        }
    }

    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
}

} // namespace easy::core
