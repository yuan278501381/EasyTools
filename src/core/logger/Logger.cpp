// ─────────────────────────────────────────────────────────────────────────────
// Logger.cpp — 统一日志系统实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/async.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <mutex>
#include <vector>

namespace easy::core {

namespace {

// spdlog's default Windows vcpkg build uses narrow CRT filenames. Feeding it
// a UTF-8 std::string makes _fsopen interpret those bytes as the active ANSI
// code page. Keep the entire file path native-wide and write through Win32 so
// usernames and directories outside the system code page remain lossless.
class WideRotatingFileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    WideRotatingFileSink(std::filesystem::path path, size_t maxFileSize, size_t maxFileCount)
        : m_path(std::move(path)),
          m_maxFileSize((std::max)(size_t{1}, maxFileSize)),
          m_maxFileCount((std::max)(size_t{1}, maxFileCount)) {
        open(false);
    }

    ~WideRotatingFileSink() override {
        close();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(message, formatted);
        if (m_size > 0 && m_size + formatted.size() > m_maxFileSize) {
            rotate();
        }

        size_t offset = 0;
        while (offset < formatted.size()) {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                formatted.size() - offset,
                static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
            DWORD written = 0;
            if (!WriteFile(m_file, formatted.data() + offset, chunk, &written, nullptr) || written == 0) {
                throw spdlog::spdlog_ex(
                    "WriteFile failed for Unicode log path, error=" + std::to_string(GetLastError()));
            }
            offset += written;
            m_size += written;
        }
    }

    void flush_() override {
        if (m_file != INVALID_HANDLE_VALUE && !FlushFileBuffers(m_file)) {
            throw spdlog::spdlog_ex(
                "FlushFileBuffers failed for Unicode log path, error=" + std::to_string(GetLastError()));
        }
    }

private:
    std::filesystem::path rotatedPath(size_t index) const {
        auto result = m_path;
        result += L"." + std::to_wstring(index);
        return result;
    }

    void open(bool truncate) {
        const DWORD disposition = truncate ? CREATE_ALWAYS : OPEN_ALWAYS;
        m_file = CreateFileW(
            m_path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) {
            throw spdlog::spdlog_ex(
                "CreateFileW failed for Unicode log path, error=" + std::to_string(GetLastError()));
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(m_file, &size)) {
            const DWORD error = GetLastError();
            close();
            throw spdlog::spdlog_ex(
                "GetFileSizeEx failed for Unicode log path, error=" + std::to_string(error));
        }
        m_size = static_cast<size_t>((std::max)(LONGLONG{0}, size.QuadPart));
    }

    void close() noexcept {
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
    }

    void rotate() {
        close();
        for (size_t index = m_maxFileCount; index > 0; --index) {
            const auto source = index == 1 ? m_path : rotatedPath(index - 1);
            const auto target = rotatedPath(index);
            if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (!MoveFileExW(source.c_str(), target.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                const DWORD error = GetLastError();
                // Re-open the active file before propagating so subsequent
                // logger error handling never owns a permanently closed sink.
                open(false);
                throw spdlog::spdlog_ex(
                    "MoveFileExW failed while rotating Unicode log, error=" + std::to_string(error));
            }
        }
        open(true);
    }

    std::filesystem::path m_path;
    size_t m_maxFileSize = 0;
    size_t m_maxFileCount = 0;
    size_t m_size = 0;
    HANDLE m_file = INVALID_HANDLE_VALUE;
};

}  // namespace

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
        const std::filesystem::path logDir = config.logDir.empty()
            ? WinUtils::getLogDirectory()
            : config.logDir;

        std::filesystem::create_directories(logDir);

        const auto logFilePath = logDir / WinUtils::utf8ToWstring(config.logFileName + ".log");
        auto fileSink = std::make_shared<WideRotatingFileSink>(
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
        s_logger->flush_on(spdlog::level::info);   // INFO 及以上立即刷盘
        spdlog::flush_every(std::chrono::seconds(1));

        spdlog::register_logger(s_logger);
        spdlog::set_default_logger(s_logger);

        LOG_INFO("日志系统初始化完成, 日志目录={}", WinUtils::wstringToUtf8(logDir.wstring()));

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
        LOG_INFO_L("日志级别已切换为: {}", "Log level switched to: {}", spdlog::level::to_string_view(level));
    }
}

#include <windows.h>
#include <atomic>

static std::atomic<uint8_t> s_logLanguage{static_cast<uint8_t>(LogLanguage::ZhCN)};

void Logger::setLanguage(const std::string& langCode) {
    if (langCode == "en-US" || langCode == "en") {
        s_logLanguage.store(static_cast<uint8_t>(LogLanguage::EnUS), std::memory_order_relaxed);
    } else if (langCode == "zh-TW" || langCode == "zh-HK") {
        s_logLanguage.store(static_cast<uint8_t>(LogLanguage::ZhTW), std::memory_order_relaxed);
    } else if (langCode == "auto") {
        LANGID langId = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(langId) == LANG_ENGLISH) {
            s_logLanguage.store(static_cast<uint8_t>(LogLanguage::EnUS), std::memory_order_relaxed);
        } else if (PRIMARYLANGID(langId) == LANG_CHINESE) {
            if (SUBLANGID(langId) == SUBLANG_CHINESE_TRADITIONAL || SUBLANGID(langId) == SUBLANG_CHINESE_HONGKONG) {
                s_logLanguage.store(static_cast<uint8_t>(LogLanguage::ZhTW), std::memory_order_relaxed);
            } else {
                s_logLanguage.store(static_cast<uint8_t>(LogLanguage::ZhCN), std::memory_order_relaxed);
            }
        } else {
            s_logLanguage.store(static_cast<uint8_t>(LogLanguage::EnUS), std::memory_order_relaxed);
        }
    } else {
        s_logLanguage.store(static_cast<uint8_t>(LogLanguage::ZhCN), std::memory_order_relaxed);
    }
}

LogLanguage Logger::getLanguage() {
    return static_cast<LogLanguage>(s_logLanguage.load(std::memory_order_relaxed));
}

}  // namespace easy::core

