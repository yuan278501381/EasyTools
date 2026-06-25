#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PerformanceMonitor — 运行时性能监控器
//
// 职责:
//   1. 实时采集进程级性能指标（内存、CPU）
//   2. 记录各子系统的关键延迟（截图启动、手势识别、UI 渲染）
//   3. 通过 IPC 暴露给前端仪表盘
//   4. 周期性自动采样，不阻塞业务线程
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_STATS_PERFORMANCEMONITOR_H
#define EASYTOOLS_CORE_STATS_PERFORMANCEMONITOR_H

#include "core/utils/Export.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace easy::core {

/// 性能指标快照
struct PerfMetrics {
    // 进程级
    double memoryMB = 0.0;            // Working Set (MB)
    double privateMemoryMB = 0.0;     // Private Bytes (MB)
    double cpuPercent = 0.0;          // CPU 使用率 (0~100)

    // 子系统延迟（毫秒）
    double screenshotLatencyMs = 0.0; // 截图快捷键按下到覆盖层显示
    double gestureLatencyMs = 0.0;    // 手势识别延迟
    double uiRenderLatencyMs = 0.0;   // 最近一次 D2D 渲染帧耗时

    // 各插件初始化耗时
    std::unordered_map<std::string, double> pluginInitMs;

    // 采样时间
    std::string timestamp;

    nlohmann::json toJson() const;
};

/// 延迟计时器（RAII 风格）
/// 用法: { PerfTimer timer("screenshot"); /* 被测代码 */ }
class PerfTimer {
public:
    explicit PerfTimer(const std::string& label);
    ~PerfTimer();

    /// 手动停止计时（析构时也会自动停止）
    void stop();

    /// 获取已过毫秒数（未停止时返回当前经过时间）
    double elapsedMs() const;

private:
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
    bool m_stopped = false;
};

class EASYCORE_API PerformanceMonitor {
public:
    static PerformanceMonitor& instance();

    /// 启动后台采样线程
    void start(int intervalMs = 2000);

    /// 停止采样
    void stop();

    /// 获取最新性能指标
    PerfMetrics getMetrics() const;

    /// 记录子系统延迟（由各模块主动上报）
    void recordLatency(const std::string& subsystem, double ms);

    /// 记录插件初始化耗时
    void recordPluginInit(const std::string& pluginName, double ms);

    /// 获取历史指标（最近 N 条采样）
    std::vector<PerfMetrics> getHistory(int count = 30) const;

private:
    PerformanceMonitor() = default;
    ~PerformanceMonitor();
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    void sampleLoop();
    void sampleOnce();

    /// 读取当前进程内存使用
    void sampleMemory(PerfMetrics& metrics);

    /// 读取 CPU 使用率
    void sampleCpu(PerfMetrics& metrics);

    std::atomic<bool> m_running{false};
    std::thread m_sampleThread;
    int m_intervalMs = 2000;

    mutable std::mutex m_mutex;
    PerfMetrics m_currentMetrics;
    std::vector<PerfMetrics> m_history;
    static constexpr int MAX_HISTORY = 120;  // 最多保留 120 条（4 分钟 @ 2秒间隔）

    // CPU 计算用的上一次采样值
    uint64_t m_lastKernelTime = 0;
    uint64_t m_lastUserTime = 0;
    uint64_t m_lastSampleTime = 0;
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_STATS_PERFORMANCEMONITOR_H
