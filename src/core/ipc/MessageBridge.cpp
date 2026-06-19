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

    registerHandler("gesture.getState", [](const json&) -> json {
        auto& engine = easy::gesture::GestureEngine::instance();
        return {
            {"paused", engine.isPaused()},
            {"enabled", !engine.isPaused()},
            {"triggerButton", engine.triggerButton()},
            {"trailVisible", engine.trailVisible()},
        };
    });

    registerHandler("gesture.updateSettings", [](const json& params) -> json {
        auto& engine = easy::gesture::GestureEngine::instance();
        auto& config = ConfigManager::instance();

        if (params.contains("enabled")) {
            engine.setPaused(!params["enabled"].get<bool>());
        }
        if (params.contains("paused")) {
            engine.setPaused(params["paused"].get<bool>());
        }
        if (params.contains("triggerButton")) {
            auto triggerButton = params["triggerButton"].get<std::string>();
            engine.setTriggerButton(triggerButton);
            config.set("/gesture/triggerButton", engine.triggerButton());
        }
        if (params.contains("trailVisible")) {
            bool trailVisible = params["trailVisible"].get<bool>();
            engine.setTrailVisible(trailVisible);
            config.set("/gesture/trailVisible", trailVisible);
        }

        return {
            {"success", true},
            {"paused", engine.isPaused()},
            {"enabled", !engine.isPaused()},
            {"triggerButton", engine.triggerButton()},
            {"trailVisible", engine.trailVisible()},
        };
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
        return {
            {"success", true},
            {"paused", easy::gesture::GestureEngine::instance().isPaused()},
            {"enabled", !easy::gesture::GestureEngine::instance().isPaused()},
        };
    });

    registerHandler("gesture.getScopeRules", [](const json&) -> json {
        return easy::gesture::GestureEngine::instance().scopeRules().toJson();
    });

    registerHandler("gesture.updateScopeRules", [](const json& params) -> json {
        easy::gesture::GestureEngine::instance().scopeRules().loadFromJson(params);
        easy::gesture::GestureEngine::instance().saveToConfig();
        return {{"success", true}};
    });

    // ── 截图设置 ─────────────────────────────────────────────────────────

    registerHandler("capture.getSettings", [](const json&) -> json {
        auto& config = ConfigManager::instance();
        return {
            {"format", config.get<std::string>("/capture/format", "png")},
            {"quality", config.get<int>("/capture/quality", 90)},
            {"saveToFile", config.get<bool>("/capture/saveToFile", true)},
            {"copyToClipboard", config.get<bool>("/capture/copyToClipboard", true)},
            {"savePath", config.get<std::string>("/capture/savePath", "")},
            {"showCrosshair", config.get<bool>("/capture/showCrosshair", true)},
            {"autoDetectWindow", config.get<bool>("/capture/autoDetectWindow", true)},
        };
    });

    registerHandler("capture.updateSettings", [](const json& params) -> json {
        auto& config = ConfigManager::instance();
        for (auto& [key, value] : params.items()) {
            config.set("/capture/" + key, value);
        }
        return {{"success", true}};
    });

    // ── 录屏设置 ─────────────────────────────────────────────────────────

    registerHandler("recording.getSettings", [](const json&) -> json {
        auto& config = ConfigManager::instance();
        return {
            {"format", config.get<std::string>("/recording/format", "mp4_h264")},
            {"fps", config.get<int>("/recording/fps", 30)},
            {"bitrate", config.get<int>("/recording/bitrate", 8)},
            {"includeAudio", config.get<bool>("/recording/includeAudio", false)},
            {"savePath", config.get<std::string>("/recording/savePath", "")},
        };
    });

    registerHandler("recording.updateSettings", [](const json& params) -> json {
        auto& config = ConfigManager::instance();
        for (auto& [key, value] : params.items()) {
            config.set("/recording/" + key, value);
        }
        return {{"success", true}};
    });

    // ── 通用设置 ─────────────────────────────────────────────────────────

    registerHandler("general.getSettings", [](const json&) -> json {
        auto& config = ConfigManager::instance();
        return {
            {"language", config.get<std::string>("/general/language", "zh-CN")},
            {"autoStart", config.get<bool>("/general/autoStart", false)},
            {"theme", config.get<std::string>("/general/theme", "light")},
            {"logLevel", config.get<std::string>("/general/logLevel", "info")},
            {"minimizeToTray", config.get<bool>("/general/minimizeToTray", true)},
            {"checkUpdates", config.get<bool>("/general/checkUpdates", true)},
        };
    });

    registerHandler("general.updateSettings", [](const json& params) -> json {
        auto& config = ConfigManager::instance();
        for (auto& [key, value] : params.items()) {
            config.set("/general/" + key, value);
        }

        // 特殊处理：开机自启动
        if (params.contains("autoStart")) {
            bool autoStart = params["autoStart"].get<bool>();
            // 写入注册表
            HKEY hKey;
            LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey);
            if (result == ERROR_SUCCESS) {
                if (autoStart) {
                    wchar_t exePath[MAX_PATH];
                    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                    RegSetValueExW(hKey, L"EasyTools", 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(exePath),
                                   (wcslen(exePath) + 1) * sizeof(wchar_t));
                } else {
                    RegDeleteValueW(hKey, L"EasyTools");
                }
                RegCloseKey(hKey);
            }
            LOG_INFO("开机自启动设置: {}", autoStart ? "启用" : "禁用");
        }

        return {{"success", true}};
    });

    // ── OCR 设置 ──────────────────────────────────────────────────────────

    registerHandler("ocr.getSettings", [](const json&) -> json {
        auto& config = ConfigManager::instance();
        return {
            {"engine", config.get<std::string>("/ocr/engine", "paddleocr")},
            {"language", config.get<std::string>("/ocr/language", "ch")},
            {"autoOcr", config.get<bool>("/ocr/autoOcr", false)},
            {"copyResult", config.get<bool>("/ocr/copyResult", true)},
        };
    });

    registerHandler("ocr.updateSettings", [](const json& params) -> json {
        auto& config = ConfigManager::instance();
        for (auto& [key, value] : params.items()) {
            config.set("/ocr/" + key, value);
        }
        return {{"success", true}};
    });

    LOG_INFO("内置 IPC 处理器注册完成 (config/gesture/capture/recording/general/ocr)");
}

}  // namespace easy::core
