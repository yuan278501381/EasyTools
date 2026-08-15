// ─────────────────────────────────────────────────────────────────────────────
// PerformanceMonitor.cpp — 运行时性能监控实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/stats/PerformanceMonitor.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
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
    j["handleCount"]         = handleCount;
    j["gdiObjectCount"]      = gdiObjectCount;
    j["userObjectCount"]     = userObjectCount;
    j["ioReadBytes"]         = ioReadBytes;
    j["ioWriteBytes"]        = ioWriteBytes;
    j["uptimeSec"]           = uptimeSec;
    j["screenshotLatencyMs"] = screenshotLatencyMs;
    j["gestureLatencyMs"]    = gestureLatencyMs;
    j["uiRenderLatencyMs"]   = uiRenderLatencyMs;
    j["timestamp"]           = timestamp;

    nlohmann::json plugins = nlohmann::json::object();
    for (const auto& [name, ms] : pluginInitMs) {
        plugins[name] = ms;
    }
    j["pluginInitMs"] = plugins;

    nlohmann::json latencyJson = nlohmann::json::object();
    for (const auto& [name, summary] : latencies) {
        latencyJson[name] = {
            {"lastMs", summary.lastMs},
            {"meanMs", summary.meanMs},
            {"p95Ms", summary.p95Ms},
            {"maxMs", summary.maxMs},
            {"sampleCount", summary.sampleCount}
        };
    }
    j["latencies"] = std::move(latencyJson);
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
    m_elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - m_start).count();
    m_stopped = true;
    PerformanceMonitor::instance().recordLatency(m_label, m_elapsedMs);
    LOG_TRACE("PerfTimer [{}]: {:.2f} ms", m_label, m_elapsedMs);
}

double PerfTimer::elapsedMs() const {
    if (m_stopped) return m_elapsedMs;
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
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;

    m_intervalMs = std::clamp(intervalMs, 250, 60'000);
    m_startedAt = std::chrono::steady_clock::now();

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
    LOG_INFO("PerformanceMonitor: 启动, 采样间隔={}ms", m_intervalMs);
}

void PerformanceMonitor::stop() {
    const bool wasRunning = m_running.exchange(false);
    m_wakeCv.notify_all();
    if (m_sampleThread.joinable()) {
        m_sampleThread.join();
    }
    if (wasRunning) LOG_INFO("PerformanceMonitor: 已停止");
}

PerfMetrics PerformanceMonitor::getMetrics() const {
    std::lock_guard lock(m_mutex);
    auto metrics = m_currentMetrics;
    metrics.latencies = latencySummariesLocked();
    return metrics;
}

void PerformanceMonitor::recordLatency(const std::string& subsystem, double ms) {
    if (subsystem.empty() || subsystem.size() > 128 || !std::isfinite(ms) || ms < 0.0) return;
    std::lock_guard lock(m_mutex);
    if (subsystem == "screenshot") {
        m_currentMetrics.screenshotLatencyMs = ms;
    } else if (subsystem == "gesture") {
        m_currentMetrics.gestureLatencyMs = ms;
    } else if (subsystem == "ui_render") {
        m_currentMetrics.uiRenderLatencyMs = ms;
    }
    auto& samples = m_latencySamples[subsystem];
    samples.push_back(ms);
    if (samples.size() > MAX_LATENCY_SAMPLES) samples.pop_front();
    ++m_latencySampleCounts[subsystem];
    LOG_TRACE("PerformanceMonitor: 记录延迟 [{}] = {:.2f} ms", subsystem, ms);
}

void PerformanceMonitor::recordPluginInit(const std::string& pluginName, double ms) {
    std::lock_guard lock(m_mutex);
    m_currentMetrics.pluginInitMs[pluginName] = ms;
    LOG_DEBUG("PerformanceMonitor: 插件初始化 [{}] = {:.2f} ms", pluginName, ms);
}

std::vector<PerfMetrics> PerformanceMonitor::getHistory(int count) const {
    std::lock_guard lock(m_mutex);
    if (count <= 0 || m_history.empty()) return {};
    int n = std::min(count, static_cast<int>(m_history.size()));
    return std::vector<PerfMetrics>(m_history.end() - n, m_history.end());
}

void PerformanceMonitor::sampleLoop() {
    while (m_running.load()) {
        sampleOnce();
        // One waitable sleep per interval keeps idle wakeups and battery use low,
        // while stop() can still wake the thread immediately.
        std::unique_lock wakeLock(m_wakeMutex);
        m_wakeCv.wait_for(wakeLock, std::chrono::milliseconds(m_intervalMs),
                          [this] { return !m_running.load(); });
    }
}

void PerformanceMonitor::sampleOnce() {
    PerfMetrics metrics;

    sampleMemory(metrics);
    sampleCpu(metrics);
    sampleProcessResources(metrics);
    if (m_startedAt != std::chrono::steady_clock::time_point{}) {
        metrics.uptimeSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_startedAt).count();
    }

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
        metrics.latencies           = latencySummariesLocked();

        m_currentMetrics = metrics;

        m_history.push_back(metrics);
        if (static_cast<int>(m_history.size()) > MAX_HISTORY) {
            m_history.erase(m_history.begin());
        }
    }
}

std::unordered_map<std::string, LatencySummary>
PerformanceMonitor::latencySummariesLocked() const {
    std::unordered_map<std::string, LatencySummary> summaries;
    summaries.reserve(m_latencySamples.size());
    for (const auto& [name, samples] : m_latencySamples) {
        if (samples.empty()) continue;
        std::vector<double> sorted(samples.begin(), samples.end());
        std::sort(sorted.begin(), sorted.end());
        const auto rank = static_cast<std::size_t>(
            std::ceil(sorted.size() * 0.95));
        LatencySummary summary;
        summary.lastMs = samples.back();
        summary.meanMs = std::accumulate(samples.begin(), samples.end(), 0.0) /
                         static_cast<double>(samples.size());
        summary.p95Ms = sorted[std::min(sorted.size() - 1, std::max<std::size_t>(1, rank) - 1)];
        summary.maxMs = sorted.back();
        if (const auto count = m_latencySampleCounts.find(name);
            count != m_latencySampleCounts.end()) {
            summary.sampleCount = count->second;
        }
        summaries.emplace(name, summary);
    }
    return summaries;
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

void PerformanceMonitor::sampleProcessResources(PerfMetrics& metrics) {
    DWORD handleCount = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handleCount)) {
        metrics.handleCount = handleCount;
    }
    metrics.gdiObjectCount = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    metrics.userObjectCount = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);

    IO_COUNTERS io{};
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
        metrics.ioReadBytes = io.ReadTransferCount;
        metrics.ioWriteBytes = io.WriteTransferCount;
    }
}

}  // namespace easy::core
