#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/events/EventBus.h"
#include "gesture/GestureEngine.h"
#include "gesture/MouseHook.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/HotCornerEngine.h"
#include "gesture/RadialMenuOverlay.h"
#include <windows.h>

namespace easy::gesture {

class GesturePlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Gesture"; }
    const char* getVersion() const override { return "1.0.0"; }

    bool initialize() override {
        LOG_INFO("GesturePlugin: 初始化手势引擎");

        // 注册内置命令处理器
        using easy::gesture::BuiltinCommand;
        auto& dispatcher = easy::gesture::BuiltinCommandDispatcher::instance();
        dispatcher.registerHandler(BuiltinCommand::PauseGestures, []() {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });
        dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, []() {
            easy::core::EventBus::instance().publish(easy::core::ActionTriggerScreenshotEvent{});
        });
        dispatcher.registerHandler(BuiltinCommand::StartRecording, []() {
            easy::core::EventBus::instance().publish(easy::core::ActionToggleRecordingEvent{});
        });
        dispatcher.registerHandler(BuiltinCommand::ToggleSearch, []() {
            easy::core::MessageBridge::instance().handleMessage(R"({"method":"search.toggle"})");
        });
        dispatcher.registerHandler(BuiltinCommand::PasteAsPin, []() {
            easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.pasteAsPin"})");
        });
        dispatcher.registerHandler(BuiltinCommand::ShowRadialMenu, []() {
            LOG_INFO("GesturePlugin: 内置命令触发轮盘菜单");
            POINT pt;
            GetCursorPos(&pt);
            easy::gesture::RadialMenuOverlay::instance().show(pt);
        });

        // 状态变化回调
        auto& gestureEngine = easy::gesture::GestureEngine::instance();
        gestureEngine.setPauseChangedCallback([](bool paused) {
            auto& config = easy::core::ConfigManager::instance();
            config.set("/gesture/paused", paused);
            config.set("/gesture/enabled", !paused);
            easy::core::MessageBridge::instance().pushEvent("gesture.stateChanged", {
                {"paused", paused},
                {"enabled", !paused}
            });
        });

        // 注册 IPC 处理器
        
        auto& mb = easy::core::MessageBridge::instance();
        auto& bus = easy::core::EventBus::instance();

        bus.subscribe<easy::core::ActionToggleGesturePauseEvent>([](const easy::core::ActionToggleGesturePauseEvent&) {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });
        
        mb.registerHandler("gesture.togglePause", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
            return {{"success", true}};
        });

        auto& hotkeys = easy::core::HotkeyManager::instance();
        hotkeys.registerHotkey("Pause Gestures", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'W'}, []() {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });

        mb.registerHandler("gesture.getProfiles", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            nlohmann::json result = nlohmann::json::array();
            auto* defaultProfile = engine.getProfile("default");
            if (defaultProfile) result.push_back(defaultProfile->toJson());
            auto* browserProfile = engine.getProfile("browser");
            if (browserProfile) result.push_back(browserProfile->toJson());
            return result;
        });

        mb.registerHandler("gesture.getState", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            return {
                {"paused", engine.isPaused()},
                {"enabled", !engine.isPaused()},
                {"triggerButton", engine.triggerButton()},
                {"trailVisible", engine.trailVisible()}
            };
        });

        mb.registerHandler("gesture.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            auto& config = easy::core::ConfigManager::instance();
            if (params.contains("enabled")) engine.setPaused(!params["enabled"].get<bool>());
            if (params.contains("paused")) engine.setPaused(params["paused"].get<bool>());
            if (params.contains("triggerButton")) {
                engine.setTriggerButton(params["triggerButton"].get<std::string>());
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
                {"trailVisible", engine.trailVisible()}
            };
        });

        mb.registerHandler("gesture.updateProfile", [](const nlohmann::json& params) -> nlohmann::json {
            auto profile = easy::gesture::GestureProfile::fromJson(params);
            easy::gesture::GestureEngine::instance().setProfile(profile.name(), profile);
            easy::gesture::GestureEngine::instance().saveToConfig();
            return {{"success", true}};
        });

        mb.registerHandler("gesture.setPaused", [](const nlohmann::json& params) -> nlohmann::json {
            bool paused = params.value("paused", false);
            easy::gesture::GestureEngine::instance().setPaused(paused);
            return {
                {"success", true},
                {"paused", easy::gesture::GestureEngine::instance().isPaused()},
                {"enabled", !easy::gesture::GestureEngine::instance().isPaused()}
            };
        });

        mb.registerHandler("gesture.getScopeRules", [](const nlohmann::json&) -> nlohmann::json {
            return easy::gesture::GestureEngine::instance().scopeRules().toJson();
        });

        mb.registerHandler("gesture.updateScopeRules", [](const nlohmann::json& params) -> nlohmann::json {
            const nlohmann::json& rules = (params.is_object() && params.contains("rules")) ? params["rules"] : params;
            easy::gesture::GestureEngine::instance().scopeRules().loadFromJson(rules);
            easy::gesture::GestureEngine::instance().saveToConfig();
            return {{"success", true}};
        });

        // 注册 HotCorner 相关 IPC
        mb.registerHandler("gesture.updateHotCorners", [](const nlohmann::json& params) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            auto& hce = easy::gesture::HotCornerEngine::instance();
            if (params.contains("topLeft")) hce.setCornerAction(easy::gesture::HotCorner::TopLeft, params["topLeft"].get<std::string>());
            if (params.contains("topRight")) hce.setCornerAction(easy::gesture::HotCorner::TopRight, params["topRight"].get<std::string>());
            if (params.contains("bottomLeft")) hce.setCornerAction(easy::gesture::HotCorner::BottomLeft, params["bottomLeft"].get<std::string>());
            if (params.contains("bottomRight")) hce.setCornerAction(easy::gesture::HotCorner::BottomRight, params["bottomRight"].get<std::string>());
            
            // 顺便保存到 Config
            config.set("/gesture/hotCorners/topLeft", hce.getCornerAction(easy::gesture::HotCorner::TopLeft));
            config.set("/gesture/hotCorners/topRight", hce.getCornerAction(easy::gesture::HotCorner::TopRight));
            config.set("/gesture/hotCorners/bottomLeft", hce.getCornerAction(easy::gesture::HotCorner::BottomLeft));
            config.set("/gesture/hotCorners/bottomRight", hce.getCornerAction(easy::gesture::HotCorner::BottomRight));

            return {{"success", true}};
        });

        // hotcorner.getSettings —— 返回四个角的动作和启用状态
        mb.registerHandler("hotcorner.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& hce = easy::gesture::HotCornerEngine::instance();
            LOG_DEBUG("IPC: hotcorner.getSettings 查询触发角配置");
            return {
                {"enabled", hce.isEnabled()},
                {"topLeft",     hce.getCornerAction(easy::gesture::HotCorner::TopLeft)},
                {"topRight",    hce.getCornerAction(easy::gesture::HotCorner::TopRight)},
                {"bottomLeft",  hce.getCornerAction(easy::gesture::HotCorner::BottomLeft)},
                {"bottomRight", hce.getCornerAction(easy::gesture::HotCorner::BottomRight)}
            };
        });

        // hotcorner.updateSettings —— 更新角落动作/延迟/启用状态
        mb.registerHandler("hotcorner.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            auto& hce = easy::gesture::HotCornerEngine::instance();
            LOG_INFO("IPC: hotcorner.updateSettings 更新触发角配置");

            if (params.contains("enabled")) {
                bool enabled = params["enabled"].get<bool>();
                hce.setEnabled(enabled);
                config.set("/gesture/hotCorners/enabled", enabled);
            }
            if (params.contains("triggerDelay")) {
                int delay = params["triggerDelay"].get<int>();
                hce.setTriggerDelay(delay);
                config.set("/gesture/hotCorners/triggerDelay", delay);
            }
            if (params.contains("topLeft")) {
                hce.setCornerAction(easy::gesture::HotCorner::TopLeft, params["topLeft"].get<std::string>());
                config.set("/gesture/hotCorners/topLeft", params["topLeft"].get<std::string>());
            }
            if (params.contains("topRight")) {
                hce.setCornerAction(easy::gesture::HotCorner::TopRight, params["topRight"].get<std::string>());
                config.set("/gesture/hotCorners/topRight", params["topRight"].get<std::string>());
            }
            if (params.contains("bottomLeft")) {
                hce.setCornerAction(easy::gesture::HotCorner::BottomLeft, params["bottomLeft"].get<std::string>());
                config.set("/gesture/hotCorners/bottomLeft", params["bottomLeft"].get<std::string>());
            }
            if (params.contains("bottomRight")) {
                hce.setCornerAction(easy::gesture::HotCorner::BottomRight, params["bottomRight"].get<std::string>());
                config.set("/gesture/hotCorners/bottomRight", params["bottomRight"].get<std::string>());
            }
            return {{"success", true}};
        });

        // 注册 RadialMenu 弹出接口
        mb.registerHandler("gesture.showRadialMenu", [](const nlohmann::json&) -> nlohmann::json {
            POINT pt;
            GetCursorPos(&pt);
            easy::gesture::RadialMenuOverlay::instance().show(pt);
            return {{"success", true}};
        });

        // radialmenu.getItems —— 返回菜单项列表
        mb.registerHandler("radialmenu.getItems", [](const nlohmann::json&) -> nlohmann::json {
            LOG_DEBUG("IPC: radialmenu.getItems 查询轮盘菜单项");
            auto& config = easy::core::ConfigManager::instance();
            auto items = config.get<nlohmann::json>("/gesture/radialMenu/items", nlohmann::json::array());
            return {{"items", items}};
        });

        // radialmenu.updateItems —— 更新菜单项
        mb.registerHandler("radialmenu.updateItems", [](const nlohmann::json& params) -> nlohmann::json {
            LOG_INFO("IPC: radialmenu.updateItems 更新轮盘菜单项");
            auto& config = easy::core::ConfigManager::instance();

            if (!params.contains("items") || !params["items"].is_array()) {
                LOG_WARN("radialmenu.updateItems: 缺少 items 数组参数");
                return {{"success", false}, {"error", "missing items array"}};
            }

            const auto& itemsJson = params["items"];
            std::vector<easy::gesture::RadialMenuItem> items;
            items.reserve(itemsJson.size());
            for (const auto& ij : itemsJson) {
                items.push_back({
                    ij.value("label", ""),
                    ij.value("command", "")
                });
            }
            easy::gesture::RadialMenuOverlay::instance().setItems(items);

            // 持久化到配置
            config.set("/gesture/radialMenu/items", itemsJson);
            LOG_INFO("radialmenu.updateItems: 已更新 {} 个菜单项", items.size());
            return {{"success", true}, {"count", items.size()}};
        });

        gestureEngine.loadFromConfig();
        
        // 加载 HotCorner 配置并启动
        auto& config = easy::core::ConfigManager::instance();
        auto& hce = easy::gesture::HotCornerEngine::instance();
        hce.setCornerAction(easy::gesture::HotCorner::TopLeft, config.get<std::string>("/gesture/hotCorners/topLeft", ""));
        hce.setCornerAction(easy::gesture::HotCorner::TopRight, config.get<std::string>("/gesture/hotCorners/topRight", "capture")); // 默认右上角截图
        hce.setCornerAction(easy::gesture::HotCorner::BottomLeft, config.get<std::string>("/gesture/hotCorners/bottomLeft", ""));
        hce.setCornerAction(easy::gesture::HotCorner::BottomRight, config.get<std::string>("/gesture/hotCorners/bottomRight", "search")); // 默认右下角搜索

        // 初始化 RadialMenu — 优先从配置加载，无配置时使用默认项
        auto savedRadialItems = config.get<nlohmann::json>("/gesture/radialMenu/items", nlohmann::json());
        if (savedRadialItems.is_array() && !savedRadialItems.empty()) {
            std::vector<easy::gesture::RadialMenuItem> rItems;
            rItems.reserve(savedRadialItems.size());
            for (const auto& ij : savedRadialItems) {
                rItems.push_back({ij.value("label", ""), ij.value("command", "")});
            }
            easy::gesture::RadialMenuOverlay::instance().setItems(rItems);
            LOG_INFO("GesturePlugin: 从配置加载 {} 个轮盘菜单项", rItems.size());
        } else {
            std::vector<easy::gesture::RadialMenuItem> rItems = {
                {"截图 (Top)", "capture"},
                {"搜索 (Right)", "search"},
                {"锁定 (Bottom)", "lock"},
                {"贴图 (Left)", "pin"}
            };
            easy::gesture::RadialMenuOverlay::instance().setItems(rItems);
            LOG_INFO("GesturePlugin: 使用默认轮盘菜单项, 数量={}", rItems.size());
        }

        easy::gesture::MouseHook::instance().install();
        gestureEngine.start();
        hce.start();
        return true;
    }

    void shutdown() override {
        LOG_INFO("GesturePlugin: 卸载手势引擎");
        auto& gestureEngine = easy::gesture::GestureEngine::instance();
        gestureEngine.saveToConfig();
        gestureEngine.stop();
        easy::gesture::HotCornerEngine::instance().stop();
        easy::gesture::MouseHook::instance().uninstall();
    }
};

} // namespace easy::gesture

PLUGIN_API easy::core::IPlugin* CreatePlugin() {
    static easy::gesture::GesturePlugin instance;
    return &instance;
}
