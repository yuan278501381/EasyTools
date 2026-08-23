#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MouseHook — 低级鼠标钩子
//
// 使用 WH_MOUSE_LL 全局钩子拦截鼠标事件。
// 为了决定是否吞掉触发键，识别状态机必须同步运行；所有渲染、动作执行和
// 输入补发等昂贵操作都必须推迟到钩子回调返回后。
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
    X1Down,
    X1Up,
    X2Down,
    X2Up,
    LeftDown,
    LeftUp,
    WheelUp,
    WheelDown
};

/// 修饰键位掩码
static constexpr uint8_t MOUSE_MOD_CTRL  = 0x01;
static constexpr uint8_t MOUSE_MOD_ALT   = 0x02;
static constexpr uint8_t MOUSE_MOD_SHIFT = 0x04;

/// 屏幕边缘区域
enum class ScreenEdgeZone : uint8_t {
    None = 0,     // 常规屏幕区域
    Top = 1,      // 屏幕上边缘
    Bottom = 2,   // 屏幕底边缘
    Left = 3,     // 屏幕左边缘
    Right = 4     // 屏幕右边缘
};

/// 鼠标事件数据（从钩子回调中采集）
struct MouseEvent {
    MouseEventType type;
    POINT position;                                               // 屏幕坐标
    std::chrono::steady_clock::time_point timestamp;              // 高精度时间戳
    HWND foregroundWindow = nullptr;                              // 事件发生时的前台窗口
    uint8_t modifiers = 0;                                        // 修饰键状态 (MOUSE_MOD_CTRL | MOUSE_MOD_ALT | MOUSE_MOD_SHIFT)
    ScreenEdgeZone edgeZone = ScreenEdgeZone::None;               // 屏幕边缘区域
    bool isTopEdge = false;                                       // 兼容保留字段
};

/// 鼠标事件回调，返回 true 表示拦截此事件不传递给下层
using MouseEventCallback = std::function<bool(const MouseEvent&)>;

/// 钩子回调超时熔断后的复位通知。必须不在引擎互斥锁内调用。
using MouseHookFaultCallback = std::function<void()>;

/// 手势允许的触发按键模式
enum class TriggerMode : uint8_t {
    RightOnly = 0,        // 仅右键 (次要按键)
    MiddleOnly = 1,       // 仅滚轮中键
    Both = 2,             // 右键与中键 (兼容模式)
    All = 3,              // 全部支持 (右键、中键、侧键1、侧键2 自由组合)
    X1Only = 4,           // 仅侧键1
    X2Only = 5            // 仅侧键2
};

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

    /// 熔断触发时复位手势追踪，避免漏掉的抬起把下一笔手势永久吞掉
    void setFaultCallback(MouseHookFaultCallback callback);

    /// 设置触发键模式 (支持同时启用右键与中键)
    void setTriggerMode(TriggerMode mode);
    void setTriggerButton(MouseEventType downEvent);
    TriggerMode triggerMode() const { return m_configuredTriggerMode.load(std::memory_order_relaxed); }

    /// 获取事件队列中的待处理事件（批量获取，减少锁竞争）
    std::vector<MouseEvent> drainEvents(size_t maxCount = 64);

    /// 钩子是否已安装
    bool isInstalled() const { return m_hookHandle != nullptr; }

    /// 显式复位触发键状态（防止取消或异常时按键状态失步）
    void resetTriggerState() noexcept;

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
    std::atomic<bool> m_triggerButtonDown{false};
    std::atomic<TriggerMode> m_configuredTriggerMode{TriggerMode::Both};
    std::atomic<MouseEventType> m_configuredTriggerDown{MouseEventType::RightDown};
    std::atomic<MouseEventType> m_activeTriggerDown{MouseEventType::RightDown};

    HWND m_cachedForegroundWindow = nullptr;
    uint8_t m_cachedModifiers = 0;
    ScreenEdgeZone m_cachedEdgeZone = ScreenEdgeZone::None;
    bool m_cachedIsTopEdge = false;

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
    MouseHookFaultCallback m_faultCallback;
    std::mutex m_callbackMutex;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_MOUSEHOOK_H
