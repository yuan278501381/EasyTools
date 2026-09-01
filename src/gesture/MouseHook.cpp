// ─────────────────────────────────────────────────────────────────────────────
// MouseHook.cpp — 低级鼠标钩子实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/MouseHook.h"
#include "gesture/GestureInputPolicy.h"
#include "core/logger/Logger.h"
#include "core/stats/StatsManager.h"
#include "core/events/EventBus.h"
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

    static thread_local bool s_reentry = false;

    if (nCode >= 0 && !s_reentry) {
        if (self.m_circuitBreakerTripped.load(std::memory_order_relaxed)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - self.m_circuitBreakerTime).count();
            if (elapsed > CIRCUIT_BREAKER_COOLDOWN_MS) {
                LOG_INFO("鼠标钩子熔断冷却期满，自动尝试恢复工作状态");
                self.m_circuitBreakerTripped.store(false, std::memory_order_relaxed);
            } else {
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
                    shouldCapture = gestureEnabled &&
                        self.m_triggerButtonDown.load(std::memory_order_relaxed);
                    
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
                    uint8_t mods = 0;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOUSE_MOD_CTRL;
                    if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOUSE_MOD_ALT;
                    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOUSE_MOD_SHIFT;

                    HWND hitWindow = WindowFromPoint(data->pt);
                    if (hitWindow) {
                        if (const HWND root = GetAncestor(hitWindow, GA_ROOT)) hitWindow = root;
                    }
                    wchar_t hitClass[256] = {};
                    if (hitWindow) GetClassNameW(hitWindow, hitClass, 256);

                    const HWND searchWindow = FindWindowW(L"EasyTools_SearchWindow", nullptr);
                    bool inSearchBounds = false;
                    if (searchWindow && IsWindow(searchWindow)) {
                        if (GetPropW(searchWindow, L"EasyTools_ShellMenuActive")) {
                            inSearchBounds = true;
                        } else {
                            RECT searchRc{};
                            GetWindowRect(searchWindow, &searchRc);
                            if (PtInRect(&searchRc, data->pt)) {
                                inSearchBounds = true;
                            }
                        }
                    }

                    const bool nativeSearchMenu = shouldBypassGestureForNativeSearchMenu(
                        hitClass, (mods & MOUSE_MOD_SHIFT) != 0) || ((mods & MOUSE_MOD_SHIFT) != 0 && inSearchBounds);
                    if (gestureEnabled &&
                        !nativeSearchMenu &&
                        !self.m_triggerButtonDown.load(std::memory_order_relaxed) &&
                        (mode == TriggerMode::RightOnly || mode == TriggerMode::Both || mode == TriggerMode::All)) {
                        self.m_activeTriggerDown.store(event.type, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(true, std::memory_order_relaxed);
                        self.m_cachedForegroundWindow = GetForegroundWindow();
                        self.m_cachedModifiers = mods;
                        self.m_cachedEdgeZone = event.edgeZone;
                        self.m_cachedIsTopEdge = event.isTopEdge;
                        shouldCapture = true;
                    }
                    if (nativeSearchMenu) {
                        self.m_activeTriggerDown.store(MouseEventType::Move, std::memory_order_relaxed);
                        self.m_triggerButtonDown.store(false, std::memory_order_release);
                        shouldCapture = false;
                    }
                    easy::core::StatsManager::instance().recordRightClick();
                    break;
                }
                case WM_RBUTTONUP:
                    event.type = MouseEventType::RightUp;
                    if (self.m_activeTriggerDown.load(std::memory_order_relaxed) == MouseEventType::RightDown) {
                        shouldCapture = gestureEnabled;
                        self.m_triggerButtonDown.store(false, std::memory_order_relaxed);
                    } else {
                        shouldCapture = false;
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

            int btn = -1;
            if (event.type == MouseEventType::LeftDown) btn = 0;
            else if (event.type == MouseEventType::RightDown) btn = 1;
            else if (event.type == MouseEventType::MiddleDown) btn = 2;

            if (btn != -1 || event.type == MouseEventType::Move) {
                easy::core::EventBus::instance().publish(easy::core::MouseActivityEvent{btn, event.position.x, event.position.y});
            }

            if (shouldCapture) {
                event.foregroundWindow = self.m_cachedForegroundWindow;
                event.modifiers = self.m_cachedModifiers;
                event.edgeZone = self.m_cachedEdgeZone;
                event.isTopEdge = self.m_cachedIsTopEdge;

                if (self.processEvent(event)) {
                    return 1;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("MouseHook 发生未捕获异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("MouseHook 发生未知异常");
        }
    }

    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
}

bool MouseHook::processEvent(const MouseEvent& event) {
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

        if (durationMs > 15 && event.type != MouseEventType::Move) {
            LOG_WARN("鼠标钩子回调执行耗时过长: {} ms ({} us) type={}",
                     durationMs, durationUs, static_cast<int>(event.type));
        }

        if (durationMs > CIRCUIT_BREAKER_TIMEOUT_MS) {
            LOG_ERROR("鼠标钩子执行耗时过长(当前耗时 {} ms), 触发安全熔断机制, 将暂停工作 {} ms 以保护系统响应",
                      durationMs, CIRCUIT_BREAKER_COOLDOWN_MS);
            m_circuitBreakerTripped.store(true, std::memory_order_relaxed);
            m_circuitBreakerTime = std::chrono::steady_clock::now();
            
            MouseHookFaultCallback faultCb;
            {
                std::lock_guard lock(m_callbackMutex);
                faultCb = m_faultCallback;
            }
            if (faultCb) {
                faultCb();
            }
            return false;
        }

        return intercepted;
    }

    if (event.type == MouseEventType::RightDown ||
        event.type == MouseEventType::MiddleDown ||
        event.type == MouseEventType::X1Down ||
        event.type == MouseEventType::X2Down) {
        return false;
    }

    {
        std::lock_guard lock(m_queueMutex);
        if (m_eventQueue.size() >= MAX_QUEUE_SIZE) {
            m_eventQueue.pop();
        }
        m_eventQueue.push(event);
    }
    return false;
}

}  // namespace easy::gesture
