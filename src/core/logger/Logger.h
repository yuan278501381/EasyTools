#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Logger — 统一日志系统
//
// 特性:
//   1. 专业日志级别: TRACE / DEBUG / INFO / WARN / ERROR / CRITICAL
//   2. 自动携带 TraceId —— 每条日志可追溯到触发它的用户操作
//   3. 多 Sink: 控制台(彩色) + 文件(按大小滚动) + MSVC 输出窗口
//   4. 高性能: 基于 spdlog 异步日志，不阻塞业务线程
//   5. 线程安全: 所有方法可从任意线程调用
//
// 使用方式:
//   LOG_INFO("截图完成, 尺寸={}x{}", width, height);
//   LOG_ERROR("钩子安装失败, errorCode={}", GetLastError());
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_LOGGER_LOGGER_H
#define EASYTOOLS_CORE_LOGGER_LOGGER_H

#include <string>
#include <memory>
#include <source_location>
#include <spdlog/spdlog.h>

namespace easy::core {

/// 日志配置
struct LoggerConfig {
    std::string logDir;                        // 日志文件目录
    std::string logFileName = "easytools";     // 日志文件名前缀
    spdlog::level::level_enum consoleLevel = spdlog::level::info;   // 控制台日志级别
    spdlog::level::level_enum fileLevel    = spdlog::level::debug;  // 文件日志级别
    size_t maxFileSize  = 10 * 1024 * 1024;    // 单文件最大 10MB
    size_t maxFileCount = 5;                   // 保留最多 5 个文件
    bool enableConsole  = true;                // 是否启用控制台输出
    bool enableMsvcSink = true;                // 是否启用 MSVC Output 窗口
};

class Logger {
public:
    /// 初始化日志系统（应在 main 入口处调用一次）
    static void initialize(const LoggerConfig& config);

    /// 关闭日志系统（刷写缓冲区）
    static void shutdown();

    /// 获取底层 spdlog logger 实例
    static std::shared_ptr<spdlog::logger>& instance();

    /// 设置全局日志级别
    static void setLevel(spdlog::level::level_enum level);

    Logger() = delete;
};

}  // namespace easy::core

// ─────────────────────────────────────────────────────────────────────────────
// 便捷宏 —— 自动注入 TraceId + 源码位置
// ─────────────────────────────────────────────────────────────────────────────

#include "core/utils/TraceId.h"

#define LOG_TRACE(fmt, ...)   \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->trace("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...)   \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->debug("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#define LOG_INFO(fmt, ...)    \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->info("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#define LOG_WARN(fmt, ...)    \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->warn("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...)   \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->error("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#define LOG_CRITICAL(fmt, ...) \
    if (easy::core::Logger::instance())  \
        easy::core::Logger::instance()->critical("[{}] " fmt, easy::core::TraceId::current(), ##__VA_ARGS__)

#endif  // EASYTOOLS_CORE_LOGGER_LOGGER_H
