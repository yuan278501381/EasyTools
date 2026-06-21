// ─────────────────────────────────────────────────────────────────────────────
// MouseHook.cpp — 低级鼠标钩子实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/MouseHook.h"
#include "core/logger/Logger.h"
#include "core/stats/StatsManager.h"
#include <cmath>

namespace easy::gesture {

MouseHook& MouseHook::instance() {
    static MouseHook inst;
    return inst;
}

bool MouseHook::install() {
    if (m_hookHandle) {
        LOG_WARN("鼠标钩子已安装, 跳过重复安装");
        return true;
    }

    m_hookHandle = SetWindowsHookExW(
        WH_MOUSE_LL,
        lowLevelMouseProc,
        GetModuleHandleW(nullptr),
        0  // 全局钩子
    );

    if (!m_hookHandle) {
        LOG_ERROR("安装鼠标钩子失败, error={}", GetLastError());
        return false;
    }

    LOG_INFO("低级鼠标钩子已安装");
    return true;
}

void MouseHook::uninstall() {
    if (m_hookHandle) {
        UnhookWindowsHookEx(m_hookHandle);
        m_hookHandle = nullptr;
        LOG_INFO("低级鼠标钩子已卸载");
    }
}

void MouseHook::setPaused(bool paused) {
    m_paused.store(paused);
    LOG_INFO("鼠标钩子暂停状态: paused={}", paused);
}

void MouseHook::setEventCallback(MouseEventCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_callback = std::move(callback);
}

std::vector<MouseEvent> MouseHook::drainEvents(size_t maxCount) {
    std::lock_guard lock(m_queueMutex);
    std::vector<MouseEvent> events;
    size_t count = std::min(maxCount, m_eventQueue.size());
    events.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        events.push_back(std::move(m_eventQueue.front()));
        m_eventQueue.pop();
    }

    return events;
}

LRESULT CALLBACK MouseHook::lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    auto& self = MouseHook::instance();

    // 同线程重入防护: 手势引擎在回调里 SendInput 合成点击(补发右键等)会同步重入本钩子。
    // 若不挡掉, 内层会再次锁 m_callbackMutex / 手势引擎 m_mutex, 递归锁非递归 std::mutex
    // → "resource deadlock would occur"。注入事件虽已在下方按 LLMHF_INJECTED 过滤, 但此处
    // 多一道线程级防护更稳妥(覆盖任何同步重入路径)。
    static thread_local bool s_reentry = false;

    if (nCode >= 0 && !self.m_paused.load(std::memory_order_relaxed) && !s_reentry) {
        s_reentry = true;
        struct ReentryGuard { ~ReentryGuard() { s_reentry = false; } } reentryGuard;
        try {
            auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

            // 忽略注入事件 (手势动作的 SendInput、补发的右键点击、Lua 的 mouse.* 等),
            // 否则会形成反馈循环 / 误触发新手势。
            if (data->flags & LLMHF_INJECTED) {
                return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
            }

            MouseEvent event{};
            event.position = data->pt;
            event.timestamp = std::chrono::steady_clock::now();
            event.foregroundWindow = GetForegroundWindow();

            bool shouldCapture = false;

            switch (wParam) {
                case WM_MOUSEMOVE: {
                    event.type = MouseEventType::Move;
                    shouldCapture = true;  // Move 事件总是采集（由上层过滤）
                    
                    // 计算欧几里得距离并累加
                    static POINT lastPt = { -1, -1 };
                    if (lastPt.x != -1 && lastPt.y != -1) {
                        double dx = data->pt.x - lastPt.x;
                        double dy = data->pt.y - lastPt.y;
                        double dist = std::sqrt(dx*dx + dy*dy);
                        if (dist > 0) {
                            easy::core::StatsManager::instance().recordMouseDistance(dist);
                        }
                    }
                    lastPt = data->pt;
                    break;
                }
                case WM_RBUTTONDOWN:
                    event.type = MouseEventType::RightDown;
                    shouldCapture = true;
                    easy::core::StatsManager::instance().recordRightClick();
                    break;
                case WM_RBUTTONUP:
                    event.type = MouseEventType::RightUp;
                    shouldCapture = true;
                    break;
                case WM_MBUTTONDOWN:
                    event.type = MouseEventType::MiddleDown;
                    shouldCapture = true;
                    break;
                case WM_MBUTTONUP:
                    event.type = MouseEventType::MiddleUp;
                    shouldCapture = true;
                    break;
                case WM_LBUTTONDOWN:
                    event.type = MouseEventType::LeftDown;
                    shouldCapture = true;
                    easy::core::StatsManager::instance().recordLeftClick();
                    break;
                case WM_LBUTTONUP:
                    event.type = MouseEventType::LeftUp;
                    shouldCapture = true;
                    break;
                case WM_MOUSEWHEEL: {
                    short delta = HIWORD(data->mouseData);
                    event.type = delta > 0 ? MouseEventType::WheelUp : MouseEventType::WheelDown;
                    shouldCapture = true;
                    easy::core::StatsManager::instance().recordScroll();
                    break;
                }
                default:
                    break;
            }

            if (shouldCapture) {
                if (self.processEvent(event)) {
                    return 1; // 拦截事件，不传递给系统和其他应用
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("MouseHook 发生未捕获异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("MouseHook 发生未知异常");
        }
    }

    // 重要: 必须调用 CallNextHookEx 传递给下一个钩子
    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
}

bool MouseHook::processEvent(const MouseEvent& event) {
    // 复制回调后立即释放锁, 绝不在持有 m_callbackMutex 时执行回调:
    // 回调可能合成输入(SendInput)并同步重入本钩子, 持锁执行会递归锁 → 死锁异常。
    MouseEventCallback cb;
    {
        std::lock_guard lock(m_callbackMutex);
        cb = m_callback;
    }
    if (cb) {
        return cb(event);
    }

    // 入队模式（异步处理不支持拦截）
    std::lock_guard lock(m_queueMutex);
    if (m_eventQueue.size() < MAX_QUEUE_SIZE) {
        m_eventQueue.push(event);
    }
    return false;
}

}  // namespace easy::gesture
