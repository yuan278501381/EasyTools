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

void MouseHook::setFaultCallback(MouseHookFaultCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_faultCallback = std::move(callback);
}

void MouseHook::setTriggerMode(TriggerMode mode) {
    m_configuredTriggerMode.store(mode, std::memory_order_release);
    LOG_INFO("鼠标手势触发模式已更新: mode={}", static_cast<int>(mode));
}

void MouseHook::setTriggerButton(MouseEventType downEvent) {
    if (downEvent == MouseEventType::MiddleDown) {
        setTriggerMode(TriggerMode::MiddleOnly);
    } else {
        setTriggerMode(TriggerMode::RightOnly);
    }
    m_configuredTriggerDown.store(downEvent, std::memory_order_release);
}

void MouseHook::resetTriggerState() noexcept {
    m_triggerButtonDown.store(false, std::memory_order_release);
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

static ScreenEdgeZone detectScreenEdgeZone(POINT pt, int tolerance = 4) {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return ScreenEdgeZone::None;
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfoW(hMon, &mi)) return ScreenEdgeZone::None;
    const RECT& rc = mi.rcMonitor;
    if (pt.y <= rc.top + tolerance) return ScreenEdgeZone::Top;
    if (pt.y >= rc.bottom - 1 - tolerance) return ScreenEdgeZone::Bottom;
    if (pt.x <= rc.left + tolerance) return ScreenEdgeZone::Left;
    if (pt.x >= rc.right - 1 - tolerance) return ScreenEdgeZone::Right;
    return ScreenEdgeZone::None;
}

LRESULT CALLBACK MouseHook::lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    auto& self = MouseHook::instance();

    // 同线程重入防护: 手势引擎在回调里 SendInput 合成点击(补发右键等)会同步重入本钩子。
    // 若不挡掉, 内层会再次锁 m_callbackMutex / 手势引擎 m_mutex, 递归锁非递归 std::mutex
    // → "resource deadlock would occur"。注入事件虽已在下方按 LLMHF_INJECTED 过滤, 但此处
    // 多一道线程级防护更稳妥(覆盖任何同步重入路径)。
    static thread_local bool s_reentry = false;

    if (nCode >= 0 && !s_reentry) {
            // ── 看门狗：检查是否处于熔断冷却期 ──
            if (self.m_circuitBreakerTripped.load(std::memory_order_relaxed)) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - self.m_circuitBreakerTime).count();
                if (elapsed > CIRCUIT_BREAKER_COOLDOWN_MS) {
                    LOG_INFO("鼠标钩子熔断冷却期满，自动尝试恢复工作状态");
                    self.m_circuitBreakerTripped.store(false, std::memory_order_relaxed);
                } else {
                    // 冷却期内不进引擎，但按键闩锁必须跟手：漏掉的抬起会让下一笔
                    // 手势的按下被 `!m_triggerButtonDown` 挡掉，表现为"画不出来，
                    // 要点一下左键才恢复"。
                    auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
                    if (data && (data->flags & LLMHF_INJECTED) == 0) {
                        if (wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP ||
                            wParam == WM_XBUTTONUP || wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP) {
                            self.m_triggerButtonDown.store(false, std::memory_order_release);
                        }
                    }
                    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
                }
            }

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

            bool shouldCapture = false;
            const bool gestureEnabled = !self.m_paused.load(std::memory_order_relaxed);

            switch (wParam) {
                case WM_MOUSEMOVE: {
                    event.type = MouseEventType::Move;

                    // Idle mouse moves are the dominant system-wide hot path.
                    // Only enter the gesture engine while a possible trigger
                    // button is held; statistics remain lock-free below.
                    shouldCapture = gestureEnabled &&
                        self.m_triggerButtonDown.load(std::memory_order_relaxed);
                    
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
                case WM_RBUTTONDOWN: {
                    event.type = MouseEventType::RightDown;
                    event.edgeZone = detectScreenEdgeZone(data->pt);
                    event.isTopEdge = (event.edgeZone == ScreenEdgeZone::Top);
                    const auto mode = self.m_configuredTriggerMode.load(std::memory_order_relaxed);
                    if (gestureEnabled &&
                        !self.m_triggerButtonDown.load(std::memory_order_relaxed) &&
                        (mode == TriggerMode::RightOnly || mode == TriggerMode::Both || mode == TriggerMode::All)) {
                        self.m_activeTriggerDown.store(event.type, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(true, std::memory_order_relaxed);
                        self.m_cachedForegroundWindow = GetForegroundWindow();
                        uint8_t mods = 0;
                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOUSE_MOD_CTRL;
                        if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOUSE_MOD_ALT;
                        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOUSE_MOD_SHIFT;
                        self.m_cachedModifiers = mods;
                        self.m_cachedEdgeZone = event.edgeZone;
                        self.m_cachedIsTopEdge = event.isTopEdge;
                        shouldCapture = true;
                    }
                    easy::core::StatsManager::instance().recordRightClick();
                    break;
                }
                case WM_RBUTTONUP:
                    event.type = MouseEventType::RightUp;
                    if (self.m_activeTriggerDown.load(std::memory_order_relaxed) == MouseEventType::RightDown) {
                        shouldCapture = gestureEnabled;
                        self.m_triggerButtonDown.store(false, std::memory_order_relaxed);
                    }
                    break;
                case WM_MBUTTONDOWN: {
                    event.type = MouseEventType::MiddleDown;
                    event.edgeZone = detectScreenEdgeZone(data->pt);
                    event.isTopEdge = (event.edgeZone == ScreenEdgeZone::Top);
                    const auto mode = self.m_configuredTriggerMode.load(std::memory_order_relaxed);
                    if (gestureEnabled &&
                        !self.m_triggerButtonDown.load(std::memory_order_relaxed) &&
                        (mode == TriggerMode::MiddleOnly || mode == TriggerMode::Both || mode == TriggerMode::All)) {
                        self.m_activeTriggerDown.store(event.type, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(true, std::memory_order_relaxed);
                        self.m_cachedForegroundWindow = GetForegroundWindow();
                        uint8_t mods = 0;
                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOUSE_MOD_CTRL;
                        if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOUSE_MOD_ALT;
                        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOUSE_MOD_SHIFT;
                        self.m_cachedModifiers = mods;
                        self.m_cachedEdgeZone = event.edgeZone;
                        self.m_cachedIsTopEdge = event.isTopEdge;
                        shouldCapture = true;
                    }
                    break;
                }
                case WM_MBUTTONUP:
                    event.type = MouseEventType::MiddleUp;
                    if (self.m_activeTriggerDown.load(std::memory_order_relaxed) == MouseEventType::MiddleDown) {
                        shouldCapture = gestureEnabled;
                        self.m_triggerButtonDown.store(false, std::memory_order_relaxed);
                    }
                    break;
                case WM_XBUTTONDOWN: {
                    const WORD xbtn = HIWORD(data->mouseData);
                    event.type = (xbtn == XBUTTON2) ? MouseEventType::X2Down : MouseEventType::X1Down;
                    event.edgeZone = detectScreenEdgeZone(data->pt);
                    event.isTopEdge = (event.edgeZone == ScreenEdgeZone::Top);
                    const auto mode = self.m_configuredTriggerMode.load(std::memory_order_relaxed);
                    const bool allowX = (mode == TriggerMode::Both || mode == TriggerMode::All ||
                                         (mode == TriggerMode::X1Only && xbtn == XBUTTON1) ||
                                         (mode == TriggerMode::X2Only && xbtn == XBUTTON2));
                    if (gestureEnabled &&
                        !self.m_triggerButtonDown.load(std::memory_order_relaxed) && allowX) {
                        self.m_activeTriggerDown.store(event.type, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(true, std::memory_order_relaxed);
                        self.m_cachedForegroundWindow = GetForegroundWindow();
                        uint8_t mods = 0;
                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOUSE_MOD_CTRL;
                        if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOUSE_MOD_ALT;
                        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOUSE_MOD_SHIFT;
                        self.m_cachedModifiers = mods;
                        self.m_cachedEdgeZone = event.edgeZone;
                        self.m_cachedIsTopEdge = event.isTopEdge;
                        shouldCapture = true;
                    }
                    break;
                }
                case WM_XBUTTONUP: {
                    const WORD xbtn = HIWORD(data->mouseData);
                    event.type = (xbtn == XBUTTON2) ? MouseEventType::X2Up : MouseEventType::X1Up;
                    const auto active = self.m_activeTriggerDown.load(std::memory_order_relaxed);
                    if ((active == MouseEventType::X1Down && xbtn == XBUTTON1) ||
                        (active == MouseEventType::X2Down && xbtn == XBUTTON2)) {
                        shouldCapture = gestureEnabled;
                        self.m_triggerButtonDown.store(false, std::memory_order_relaxed);
                    }
                    break;
                }
                case WM_LBUTTONDOWN: {
                    event.type = MouseEventType::LeftDown;
                    event.edgeZone = detectScreenEdgeZone(data->pt);
                    event.isTopEdge = (event.edgeZone == ScreenEdgeZone::Top);
                    uint8_t mods = 0;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOUSE_MOD_CTRL;
                    if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOUSE_MOD_ALT;
                    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOUSE_MOD_SHIFT;

                    if (gestureEnabled && !self.m_triggerButtonDown.load(std::memory_order_relaxed) &&
                        (event.edgeZone != ScreenEdgeZone::None || mods != 0)) {
                        self.m_activeTriggerDown.store(event.type, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(true, std::memory_order_relaxed);
                        self.m_cachedForegroundWindow = GetForegroundWindow();
                        self.m_cachedModifiers = mods;
                        self.m_cachedEdgeZone = event.edgeZone;
                        self.m_cachedIsTopEdge = event.isTopEdge;
                        shouldCapture = true;
                    } else if (self.m_triggerButtonDown.load(std::memory_order_relaxed)) {
                        // 其它按键触发中如果按下左键，通知取消手势并放行左键
                        self.m_triggerButtonDown.store(false, std::memory_order_release);
                        event.foregroundWindow = self.m_cachedForegroundWindow;
                        event.modifiers = self.m_cachedModifiers;
                        event.edgeZone = self.m_cachedEdgeZone;
                        event.isTopEdge = self.m_cachedIsTopEdge;
                        self.processEvent(event);
                        shouldCapture = false;
                    } else {
                        shouldCapture = false;
                    }
                    easy::core::StatsManager::instance().recordLeftClick();
                    break;
                }
                case WM_LBUTTONUP:
                    event.type = MouseEventType::LeftUp;
                    if (self.m_activeTriggerDown.load(std::memory_order_relaxed) == MouseEventType::LeftDown) {
                        shouldCapture = gestureEnabled;
                        self.m_triggerButtonDown.store(false, std::memory_order_relaxed);
                    } else {
                        shouldCapture = false;
                    }
                    break;
                case WM_MOUSEWHEEL: {
                    short delta = HIWORD(data->mouseData);
                    event.type = delta > 0 ? MouseEventType::WheelUp : MouseEventType::WheelDown;
                    event.edgeZone = detectScreenEdgeZone(data->pt);
                    event.isTopEdge = (event.edgeZone == ScreenEdgeZone::Top);
                    shouldCapture = gestureEnabled &&
                        self.m_triggerButtonDown.load(std::memory_order_relaxed);
                    easy::core::StatsManager::instance().recordScroll();
                    break;
                }
                default:
                    break;
            }

            if (shouldCapture) {
                event.foregroundWindow = self.m_cachedForegroundWindow;
                event.modifiers = self.m_cachedModifiers;
                event.edgeZone = self.m_cachedEdgeZone;
                event.isTopEdge = self.m_cachedIsTopEdge;
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
    // 尝试直接回调（同步模式，支持拦截）
    // 复制回调后立即释放锁, 绝不在持有 m_callbackMutex 时执行回调:
    // 回调可能合成输入(SendInput)并同步重入本钩子, 持锁执行会递归锁 → 死锁异常。
    MouseEventCallback cb;
    {
        std::lock_guard lock(m_callbackMutex);
        cb = m_callback;
    }

    if (cb) {
        const auto start_time = std::chrono::steady_clock::now();
        
        bool intercepted = cb(event);
        
        const auto end_time = std::chrono::steady_clock::now();
        const auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        const auto durationMs = durationUs / 1000;

        // 移动事件是热路径，绝不能在钩子回调里写日志：一次 20ms 的回调再加
        // 文件 sink，会把后续每一帧都拖进同样的泥潭。
        if (durationMs > 15 && event.type != MouseEventType::Move) {
            LOG_WARN("鼠标钩子回调执行耗时过长: {} ms ({} us) type={}",
                     durationMs, durationUs, static_cast<int>(event.type));
        }
        if (durationMs > CIRCUIT_BREAKER_TIMEOUT_MS) {
            LOG_CRITICAL("【熔断告警】鼠标钩子回调耗时 {} ms (阈值 {} ms)，触发全局熔断机制保护系统！", durationMs, CIRCUIT_BREAKER_TIMEOUT_MS);
            m_circuitBreakerTripped.store(true, std::memory_order_relaxed);
            m_circuitBreakerTime = std::chrono::steady_clock::now();
            m_triggerButtonDown.store(false, std::memory_order_release);

            MouseHookFaultCallback fault;
            {
                std::lock_guard lock(m_callbackMutex);
                fault = m_faultCallback;
            }
            if (fault) {
                try {
                    fault();
                } catch (...) {
                    LOG_ERROR("鼠标钩子熔断复位回调异常");
                }
            }
        }

        return intercepted;
    }

    // 入队模式（异步处理不支持拦截）
    std::lock_guard lock(m_queueMutex);
    if (m_eventQueue.size() < MAX_QUEUE_SIZE) {
        m_eventQueue.push(event);
    }
    return false;
}

}  // namespace easy::gesture
