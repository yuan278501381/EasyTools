// ─────────────────────────────────────────────────────────────────────────────
// MessageBridge.cpp — WebView2 ↔ C++ IPC 桥接层实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/stats/StatsManager.h"
#include "core/stats/PerformanceMonitor.h"

namespace easy::core {

MessageBridge& MessageBridge::instance() {
    static MessageBridge inst;
    return inst;
}

void MessageBridge::registerHandler(const std::string& method, MessageHandler handler) {
    m_handlers[method] = std::move(handler);
    LOG_TRACE("注册 IPC 处理器: method={}", method);
}

std::string MessageBridge::handleMessage(const std::string& messageJson) {
    TraceId::Scope scope;
    try {
        auto request = json::parse(messageJson);
        int id = request.value("id", 0);
        std::string method = request.value("method", "");
        json params = request.value("params", json::object());

        LOG_DEBUG("收到前端消息: id={}, method={}", id, method);

        auto it = m_handlers.find(method);
        if (it == m_handlers.end()) {
            LOG_WARN("未知的 IPC 方法: {}", method);
            json response = {
                {"id", id},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
            };
            return response.dump();
        }

        json result = it->second(params);
        json response = {
            {"id", id},
            {"result", result}
        };
        return response.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("IPC 处理器异常: {}", e.what());
        json response = {
            {"id", 0},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}
        };
        return response.dump();
    } catch (...) {
        LOG_ERROR("IPC 处理器未知异常");
        json response = {
            {"id", 0},
            {"error", {{"code", -32603}, {"message", "Internal error: unknown exception"}}}
        };
        return response.dump();
    }
}

void MessageBridge::setEventPusher(EventPusher pusher) {
    m_eventPusher = std::move(pusher);
    LOG_DEBUG("事件推送器已设置");
}

void MessageBridge::pushEvent(const std::string& eventName, const json& data) {
    if (m_eventPusher) {
        m_eventPusher(eventName, data);
        LOG_TRACE("推送事件到前端: event={}", eventName);
    }
}

void MessageBridge::registerBuiltinHandlers() {
    registerHandler("config.getAll", [](const json&) -> json {
        return json::parse(ConfigManager::instance().toJsonString());
    });
    registerHandler("config.get", [](const json& params) -> json {
        std::string key = params.value("key", "");
        auto& config = ConfigManager::instance();
        if (config.has(key)) return config.get<json>(key);
        return nullptr;
    });
    registerHandler("config.set", [](const json& params) -> json {
        std::string key = params.value("key", "");
        json value = params.value("value", json{});
        ConfigManager::instance().set(key, value);
        return {{"success", true}};
    });

    registerHandler("stats.getToday", [](const json&) -> json {
        return StatsManager::instance().getTodayStats().toJson();
    });
    registerHandler("stats.getHistory", [](const json& params) -> json {
        int days = params.value("days", 7);
        return StatsManager::instance().getHistory(days);
    });
    registerHandler("stats.getTotal", [](const json&) -> json {
        return StatsManager::instance().getTotalStats();
    });
    registerHandler("stats.clearToday", [](const json&) -> json {
        StatsManager::instance().clearToday();
        return {{"success", true}};
    });

    // ── 性能监控 ─────────────────────────────────────────────────────────
    registerHandler("perf.getMetrics", [](const json&) -> json {
        return PerformanceMonitor::instance().getMetrics().toJson();
    });
    registerHandler("perf.getHistory", [](const json& params) -> json {
        int count = params.value("count", 30);
        auto history = PerformanceMonitor::instance().getHistory(count);
        json arr = json::array();
        for (const auto& m : history) {
            arr.push_back(m.toJson());
        }
        return arr;
    });

    // ── 配置管理（导入/导出/重置）────────────────────────────────────────
    registerHandler("config.export", [](const json&) -> json {
        auto path = WinUtils::getConfigDirectory() / "config_export.json";
        bool ok = ConfigManager::instance().exportTo(path);
        return {{"success", ok}, {"path", path.string()}};
    });
    registerHandler("config.import", [](const json& params) -> json {
        std::string path = params.value("path", "");
        if (path.empty()) {
            return {{"success", false}, {"error", "path is required"}};
        }
        bool ok = ConfigManager::instance().importFrom(path);
        return {{"success", ok}};
    });
    registerHandler("config.reset", [](const json&) -> json {
        auto configPath = WinUtils::getConfigDirectory() / "config.json";
        // 备份当前配置，然后清空
        ConfigManager::instance().exportTo(
            WinUtils::getConfigDirectory() / "config_backup.json");
        ConfigManager::instance().fromJsonString("{}");
        LOG_INFO("配置已重置为默认值");
        return {{"success", true}};
    });

    // ── 快捷键管理 ───────────────────────────────────────────────────────
    registerHandler("hotkey.getAll", [](const json&) -> json {
        // 由 HotkeyManager 实现，但 core 层不依赖 hotkey 模块
        // 通过 ConfigManager 读取快捷键配置
        auto& config = ConfigManager::instance();
        json hotkeys = json::array();
        if (config.has("/hotkeys")) {
            try {
                hotkeys = config.get<json>("/hotkeys");
            } catch (...) {}
        }
        return hotkeys;
    });

    // ── 应用系统信息 ─────────────────────────────────────────────────────
    registerHandler("app.getSystemInfo", [](const json&) -> json {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);

        std::string arch = "x64";
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
            arch = "ARM64";
        } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
            arch = "x86";
        }

        OSVERSIONINFOEXW osvi{};
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        return {
            {"version", "1.0.0"},
            {"cpuArch", arch},
            {"cpuCores", si.dwNumberOfProcessors},
            {"totalMemoryGB", memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0)},
            {"dpiScale", WinUtils::getDpiScale()},
            {"language", WinUtils::isSystemLanguageChinese() ? "zh-CN" : "en-US"}
        };
    });

    LOG_INFO("内置核心 IPC 处理器注册完成（含性能监控、配置管理、系统信息）");
}

}  // namespace easy::core
