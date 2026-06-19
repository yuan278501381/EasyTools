// ─────────────────────────────────────────────────────────────────────────────
// Logger.cpp — 统一日志系统实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/async.h>

#include <filesystem>
#include <vector>

namespace easy::core {

static std::shared_ptr<spdlog::logger> s_logger;

void Logger::initialize(const LoggerConfig& config) {
    try {
        // ── 初始化异步线程池 ─────────────────────────────────────────────
        spdlog::init_thread_pool(8192, 1);  // 队列大小 8192, 1 个后台线程

        // ── 构建多 Sink ─────────────────────────────────────────────────
        std::vector<spdlog::sink_ptr> sinks;

        // Sink 1: 控制台 (彩色)
        if (config.enableConsole) {
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_level(config.consoleLevel);
            consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid:%t] %v");
            sinks.push_back(consoleSink);
        }

        // Sink 2: 文件 (按大小滚动)
        std::string logDir = config.logDir.empty()
            ? WinUtils::wstringToUtf8(WinUtils::getLogDirectory().wstring())
            : config.logDir;

        std::filesystem::create_directories(logDir);

        auto logFilePath = (std::filesystem::path(logDir) / (config.logFileName + ".log")).string();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath,
            config.maxFileSize,
            config.maxFileCount
        );
        fileSink->set_level(config.fileLevel);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [tid:%t] %v");
        sinks.push_back(fileSink);

        // Sink 3: MSVC 输出窗口 (仅 Debug 构建)
#ifdef _DEBUG
        if (config.enableMsvcSink) {
            auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            msvcSink->set_level(spdlog::level::debug);
            sinks.push_back(msvcSink);
        }
#endif

        // ── 创建异步 Logger ─────────────────────────────────────────────
        s_logger = std::make_shared<spdlog::async_logger>(
            "easytools",
            sinks.begin(), sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block
        );
        s_logger->set_level(spdlog::level::trace);  // 总开关: 允许所有级别，由各 Sink 过滤
        s_logger->flush_on(spdlog::level::warn);     // WARN 及以上立即刷盘

        spdlog::register_logger(s_logger);
        spdlog::set_default_logger(s_logger);

        LOG_INFO("日志系统初始化完成, 日志目录={}", logDir);

    } catch (const spdlog::spdlog_ex& ex) {
        // 日志系统初始化失败时，用 OutputDebugString 输出错误
        OutputDebugStringA(("Logger initialization failed: " + std::string(ex.what()) + "\n").c_str());
    }
}

void Logger::shutdown() {
    if (s_logger) {
        LOG_INFO("日志系统正在关闭...");
        s_logger->flush();
    }
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger>& Logger::instance() {
    return s_logger;
}

void Logger::setLevel(spdlog::level::level_enum level) {
    if (s_logger) {
        s_logger->set_level(level);
        LOG_INFO("日志级别已切换为: {}", spdlog::level::to_string_view(level));
    }
}

}  // namespace easy::core
