#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MouseHook — 低级鼠标钩子
//
// 使用 WH_MOUSE_LL 全局钩子拦截鼠标事件。
// 钩子回调仅做坐标采集和入队，不做任何重计算。
// 通过线程安全队列将数据传递给 GestureRecognizer 工作线程。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_MOUSEHOOK_H
#define EASYTOOLS_GESTURE_MOUSEHOOK_H

#include <windows.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>

namespace easy::gesture {

/// 鼠标事件类型
enum class MouseEventType {
    Move,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    LeftDown,
    LeftUp,
    WheelUp,
    WheelDown
};

/// 修饰键位掩码
static constexpr uint8_t MOUSE_MOD_CTRL  = 0x01;
static constexpr uint8_t MOUSE_MOD_ALT   = 0x02;
static constexpr uint8_t MOUSE_MOD_SHIFT = 0x04;

/// 鼠标事件数据（从钩子回调中采集）
struct MouseEvent {
    MouseEventType type;
    POINT position;                                               // 屏幕坐标
    std::chrono::steady_clock::time_point timestamp;              // 高精度时间戳
    HWND foregroundWindow = nullptr;                              // 事件发生时的前台窗口
    uint8_t modifiers = 0;                                        // 修饰键状态 (MOUSE_MOD_CTRL | MOUSE_MOD_ALT | MOUSE_MOD_SHIFT)
};

/// 鼠标事件回调，返回 true 表示拦截此事件不传递给下层
using MouseEventCallback = std::function<bool(const MouseEvent&)>;

class MouseHook {
public:
    static MouseHook& instance();

    /// 安装鼠标钩子
    bool install();

    /// 卸载鼠标钩子
    void uninstall();

    /// 暂停/恢复钩子处理（钩子仍安装，但不分发事件）
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    /// 设置事件回调（由 GestureEngine 设置）
    void setEventCallback(MouseEventCallback callback);

    /// 获取事件队列中的待处理事件（批量获取，减少锁竞争）
    std::vector<MouseEvent> drainEvents(size_t maxCount = 64);

    /// 钩子是否已安装
    bool isInstalled() const { return m_hookHandle != nullptr; }

private:
    MouseHook() = default;
    MouseHook(const MouseHook&) = delete;
    MouseHook& operator=(const MouseHook&) = delete;

    /// 钩子回调（static，因为 SetWindowsHookEx 要求 C 风格函数指针）
    static LRESULT CALLBACK lowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    /// 将事件入队或同步回调处理。返回 true 表示需要拦截
    bool processEvent(const MouseEvent& event);

    HHOOK m_hookHandle = nullptr;
    std::atomic<bool> m_paused{false};

    // ── 防御性编程：超时熔断自愈 (Circuit Breaker) ──
    std::atomic<bool> m_circuitBreakerTripped{false};
    std::chrono::steady_clock::time_point m_circuitBreakerTime;
    static constexpr int CIRCUIT_BREAKER_TIMEOUT_MS = 100;     // 触发熔断的执行耗时阈值 (100ms)
    static constexpr int CIRCUIT_BREAKER_COOLDOWN_MS = 3000;   // 熔断冷却恢复时间 (3秒)

    // 线程安全事件队列
    std::mutex m_queueMutex;
    std::queue<MouseEvent> m_eventQueue;
    static constexpr size_t MAX_QUEUE_SIZE = 4096;  // 防止内存爆炸

    // 直接回调模式（可选，用于低延迟场景）
    MouseEventCallback m_callback;
    std::mutex m_callbackMutex;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_MOUSEHOOK_H
