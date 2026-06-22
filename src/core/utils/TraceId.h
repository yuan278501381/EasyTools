#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TraceId — 分布式追踪 ID 生成器
// 格式: YYYYMMDD-HHmmss-XXXX (日期-时间-随机4位)
// 用途: 每个用户操作/请求分配唯一 TraceId，贯穿整条日志链
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_UTILS_TRACEID_H
#define EASYTOOLS_CORE_UTILS_TRACEID_H

#include "core/utils/Export.h"

#include <string>
#include <chrono>
#include <random>
#include <format>
#include <atomic>

namespace easy::core {

class EASYCORE_API TraceId {
public:
    /// 生成一个新的 TraceId
    /// @return 格式: "20260619-180215-A3F7"
    static std::string generate() {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &timeT);

        // 4位16进制随机后缀 —— 保证同一秒内的唯一性
        static std::atomic<uint32_t> counter{0};
        static thread_local std::mt19937 rng{std::random_device{}()};
        uint16_t suffix = static_cast<uint16_t>(rng()) ^ static_cast<uint16_t>(counter.fetch_add(1));

        return std::format("{:04d}{:02d}{:02d}-{:02d}{:02d}{:02d}-{:04X}",
                           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                           tm.tm_hour, tm.tm_min, tm.tm_sec,
                           suffix);
    }

    /// 获取当前线程的活跃 TraceId
    static const std::string& current() {
        std::string& id = storage();
        if (id.empty()) {
            id = generate();
        }
        return id;
    }

    /// 设置当前线程的活跃 TraceId（用于跨线程传递）
    static void setCurrent(const std::string& traceId) {
        storage() = traceId;
    }

    /// 开启一个全新的 TraceId 并返回它（标记一次新的用户操作的开始）
    static std::string begin() {
        std::string id = generate();
        storage() = id;
        return id;
    }

    /// RAII 作用域守卫 —— 在作用域内自动设置/恢复 TraceId
    class Scope {
    public:
        explicit Scope(const std::string& traceId = TraceId::generate()) : m_previous(TraceId::current()) {
            TraceId::setCurrent(traceId);
        }
        ~Scope() { TraceId::setCurrent(m_previous); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        [[nodiscard]] const std::string& id() const { return TraceId::current(); }

    private:
        std::string m_previous;
    };

private:
    /// 单一的线程局部存储 —— current()/setCurrent() 必须共享同一个变量,
    /// 否则 setCurrent 写入的与 current 读取的不是同一对象 (历史 bug)。
    static std::string& storage() {
        thread_local std::string currentId;
        return currentId;
    }
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_TRACEID_H
