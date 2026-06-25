// ─────────────────────────────────────────────────────────────────────────────
// PerformanceMonitor.cpp — 运行时性能监控实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/stats/PerformanceMonitor.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace easy::core {

// ─────────────────────────────────────────────────────────────────────────────
// PerfMetrics
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json PerfMetrics::toJson() const {
    nlohmann::json j;
    j["memoryMB"]            = memoryMB;
    j["privateMemoryMB"]     = privateMemoryMB;
    j["cpuPercent"]          = cpuPercent;
    j["screenshotLatencyMs"] = screenshotLatencyMs;
    j["gestureLatencyMs"]    = gestureLatencyMs;
    j["uiRenderLatencyMs"]   = uiRenderLatencyMs;
    j["timestamp"]           = timestamp;

    nlohmann::json plugins = nlohmann::json::object();
    for (const auto& [name, ms] : pluginInitMs) {
        plugins[name] = ms;
    }
    j["pluginInitMs"] = plugins;
    return j;
}

// ─────────────────────────────────────────────────────────────────────────────
// PerfTimer
// ─────────────────────────────────────────────────────────────────────────────

PerfTimer::PerfTimer(const std::string& label)
    : m_label(label)
    , m_start(std::chrono::steady_clock::now()) {
}

PerfTimer::~PerfTimer() {
    stop();
}

void PerfTimer::stop() {
    if (m_stopped) return;
    m_stopped = true;
    double ms = elapsedMs();
    PerformanceMonitor::instance().recordLatency(m_label, ms);
    LOG_TRACE("PerfTimer [{}]: {:.2f} ms", m_label, ms);
}

double PerfTimer::elapsedMs() const {
    auto end = m_stopped ? m_start : std::chrono::steady_clock::now();
    if (m_stopped) {
        // 已停止时无法再取精确时间，返回 0 表示需要外部记录
        // 实际上 stop() 里已经记录了
        return 0.0;
    }
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_start).count();
}

// ─────────────────────────────────────────────────────────────────────────────
// PerformanceMonitor
// ─────────────────────────────────────────────────────────────────────────────

PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor monitor;
    return monitor;
}

PerformanceMonitor::~PerformanceMonitor() {
    stop();
}

void PerformanceMonitor::start(int intervalMs) {
    if (m_running.load()) return;

    m_intervalMs = intervalMs;
    m_running = true;

    // 初始化 CPU 计算基准
    FILETIME creation, exitTime, kernel, user;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        ULARGE_INTEGER k, u;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;
        m_lastKernelTime = k.QuadPart;
        m_lastUserTime   = u.QuadPart;
    }
    m_lastSampleTime = GetTickCount64();

    m_sampleThread = std::thread(&PerformanceMonitor::sampleLoop, this);
    LOG_INFO("PerformanceMonitor: 启动, 采样间隔={}ms", intervalMs);
}

void PerformanceMonitor::stop() {
    m_running = false;
    if (m_sampleThread.joinable()) {
        m_sampleThread.join();
    }
    LOG_INFO("PerformanceMonitor: 已停止");
}

PerfMetrics PerformanceMonitor::getMetrics() const {
    std::lock_guard lock(m_mutex);
    return m_currentMetrics;
}

void PerformanceMonitor::recordLatency(const std::string& subsystem, double ms) {
    std::lock_guard lock(m_mutex);
    if (subsystem == "screenshot") {
        m_currentMetrics.screenshotLatencyMs = ms;
    } else if (subsystem == "gesture") {
        m_currentMetrics.gestureLatencyMs = ms;
    } else if (subsystem == "ui_render") {
        m_currentMetrics.uiRenderLatencyMs = ms;
    }
    LOG_TRACE("PerformanceMonitor: 记录延迟 [{}] = {:.2f} ms", subsystem, ms);
}

void PerformanceMonitor::recordPluginInit(const std::string& pluginName, double ms) {
    std::lock_guard lock(m_mutex);
    m_currentMetrics.pluginInitMs[pluginName] = ms;
    LOG_DEBUG("PerformanceMonitor: 插件初始化 [{}] = {:.2f} ms", pluginName, ms);
}

std::vector<PerfMetrics> PerformanceMonitor::getHistory(int count) const {
    std::lock_guard lock(m_mutex);
    int n = std::min(count, static_cast<int>(m_history.size()));
    return std::vector<PerfMetrics>(m_history.end() - n, m_history.end());
}

void PerformanceMonitor::sampleLoop() {
    while (m_running.load()) {
        sampleOnce();
        // 使用小步休眠以快速响应 stop()
        for (int elapsed = 0; elapsed < m_intervalMs && m_running.load(); elapsed += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void PerformanceMonitor::sampleOnce() {
    PerfMetrics metrics;

    sampleMemory(metrics);
    sampleCpu(metrics);

    // 时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
    localtime_s(&localTm, &time_t);
    std::ostringstream oss;
    oss << std::put_time(&localTm, "%Y-%m-%dT%H:%M:%S");
    metrics.timestamp = oss.str();

    // 合并延迟数据（保留上一次的延迟记录，它们由外部主动上报）
    {
        std::lock_guard lock(m_mutex);
        metrics.screenshotLatencyMs = m_currentMetrics.screenshotLatencyMs;
        metrics.gestureLatencyMs    = m_currentMetrics.gestureLatencyMs;
        metrics.uiRenderLatencyMs   = m_currentMetrics.uiRenderLatencyMs;
        metrics.pluginInitMs        = m_currentMetrics.pluginInitMs;

        m_currentMetrics = metrics;

        m_history.push_back(metrics);
        if (static_cast<int>(m_history.size()) > MAX_HISTORY) {
            m_history.erase(m_history.begin());
        }
    }
}

void PerformanceMonitor::sampleMemory(PerfMetrics& metrics) {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc))) {
        metrics.memoryMB        = pmc.WorkingSetSize / (1024.0 * 1024.0);
        metrics.privateMemoryMB = pmc.PrivateUsage / (1024.0 * 1024.0);
    }
}

void PerformanceMonitor::sampleCpu(PerfMetrics& metrics) {
    FILETIME creation, exitTime, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        return;
    }

    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;   u.HighPart = user.dwHighDateTime;

    uint64_t currentTime = GetTickCount64();
    uint64_t deltaWall = (currentTime - m_lastSampleTime);
    if (deltaWall == 0) deltaWall = 1;

    uint64_t deltaKernel = k.QuadPart - m_lastKernelTime;
    uint64_t deltaUser   = u.QuadPart - m_lastUserTime;

    // FILETIME 单位是 100ns，换算为 ms 后除以实际墙钟 ms
    // 除以 CPU 核心数得到整机 CPU 百分比
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int cpuCount = si.dwNumberOfProcessors;
    if (cpuCount == 0) cpuCount = 1;

    double cpuMs = (deltaKernel + deltaUser) / 10000.0;  // 100ns → ms
    metrics.cpuPercent = (cpuMs / deltaWall) * 100.0 / cpuCount;
    if (metrics.cpuPercent > 100.0) metrics.cpuPercent = 100.0;

    m_lastKernelTime = k.QuadPart;
    m_lastUserTime   = u.QuadPart;
    m_lastSampleTime = currentTime;
}

}  // namespace easy::core
