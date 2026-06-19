// ─────────────────────────────────────────────────────────────────────────────
// MessageBridge.cpp — WebView2 ↔ C++ IPC 桥接层实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/config/ConfigManager.h"
#include "gesture/GestureEngine.h"

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

        // 执行处理器
        json result = it->second(params);

        json response = {
            {"id", id},
            {"result", result}
        };
        return response.dump();

    } catch (const json::parse_error& e) {
        LOG_ERROR("IPC 消息解析失败: {}", e.what());
        json response = {
            {"id", 0},
            {"error", {{"code", -32700}, {"message", "Parse error"}}}
        };
        return response.dump();

    } catch (const std::exception& e) {
        LOG_ERROR("IPC 处理器异常: {}", e.what());
        json response = {
            {"id", 0},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}
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

// ── 内置处理器 ───────────────────────────────────────────────────────────────

void MessageBridge::registerBuiltinHandlers() {
    // ── 配置相关 ─────────────────────────────────────────────────────────

    registerHandler("config.getAll", [](const json&) -> json {
        return json::parse(ConfigManager::instance().toJsonString());
    });

    registerHandler("config.get", [](const json& params) -> json {
        std::string key = params.value("key", "");
        auto& config = ConfigManager::instance();
        if (config.has(key)) {
            return config.get<json>(key);
        }
        return nullptr;
    });

    registerHandler("config.set", [](const json& params) -> json {
        std::string key = params.value("key", "");
        json value = params.value("value", json{});
        ConfigManager::instance().set(key, value);
        return {{"success", true}};
    });

    registerHandler("config.import", [](const json& params) -> json {
        std::string jsonStr = params.dump();
        ConfigManager::instance().fromJsonString(jsonStr);
        return {{"success", true}};
    });

    // ── 手势相关 ─────────────────────────────────────────────────────────

    registerHandler("gesture.getProfiles", [](const json&) -> json {
        auto& engine = easy::gesture::GestureEngine::instance();
        json result = json::array();
        // 遍历所有 profile
        auto* defaultProfile = engine.getProfile("default");
        if (defaultProfile) result.push_back(defaultProfile->toJson());
        auto* browserProfile = engine.getProfile("browser");
        if (browserProfile) result.push_back(browserProfile->toJson());
        return result;
    });

    registerHandler("gesture.updateProfile", [](const json& params) -> json {
        auto profile = easy::gesture::GestureProfile::fromJson(params);
        easy::gesture::GestureEngine::instance().setProfile(profile.name(), profile);
        easy::gesture::GestureEngine::instance().saveToConfig();
        return {{"success", true}};
    });

    registerHandler("gesture.setPaused", [](const json& params) -> json {
        bool paused = params.value("paused", false);
        easy::gesture::GestureEngine::instance().setPaused(paused);
        return {{"success", true}, {"paused", paused}};
    });

    registerHandler("gesture.getScopeRules", [](const json&) -> json {
        return easy::gesture::GestureEngine::instance().scopeRules().toJson();
    });

    registerHandler("gesture.updateScopeRules", [](const json& params) -> json {
        easy::gesture::GestureEngine::instance().scopeRules().loadFromJson(params);
        easy::gesture::GestureEngine::instance().saveToConfig();
        return {{"success", true}};
    });

    LOG_INFO("内置 IPC 处理器注册完成");
}

}  // namespace easy::core
