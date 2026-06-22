#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HotCornerEngine — 屏幕触发角引擎
//
// 职责:
//   1. 监控鼠标指针是否在屏幕的四个角落驻留
//   2. 在满足触发条件时执行相应的内置命令
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_HOTCORNER_ENGINE_H
#define EASYTOOLS_GESTURE_HOTCORNER_ENGINE_H

#include <windows.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <chrono>

namespace easy::gesture {

/// 屏幕触发角位置
enum class HotCorner {
    None,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

class HotCornerEngine {
public:
    static HotCornerEngine& instance();

    /// 启动引擎后台线程
    void start();

    /// 停止引擎后台线程
    void stop();

    /// 设置某个角落的动作命令 (空字符串代表禁用)
    void setCornerAction(HotCorner corner, const std::string& actionCmd);

    /// 获取角落动作
    std::string getCornerAction(HotCorner corner) const;

    /// 全局启用/禁用
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled.load(); }

    /// 设置触发延迟（毫秒）
    void setTriggerDelay(int ms) { m_triggerDelayMs.store(ms); }

private:
    HotCornerEngine() = default;
    ~HotCornerEngine();
    HotCornerEngine(const HotCornerEngine&) = delete;
    HotCornerEngine& operator=(const HotCornerEngine&) = delete;

    void workerThread();
    HotCorner detectCorner(POINT pt);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<int> m_triggerDelayMs{300}; // 默认停留 300 毫秒触发

    std::thread m_thread;

    std::string m_actions[4]; // 对应 TopLeft, TopRight, BottomLeft, BottomRight
    mutable std::mutex m_mutex;
};

} // namespace easy::gesture

#endif // EASYTOOLS_GESTURE_HOTCORNER_ENGINE_H
