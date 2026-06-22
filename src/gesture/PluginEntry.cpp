#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "gesture/GestureEngine.h"
#include "gesture/MouseHook.h"
#include "gesture/BuiltinCommands.h"
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
            easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.triggerScreenshot"})");
        });
        dispatcher.registerHandler(BuiltinCommand::StartRecording, []() {
            easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.toggleRecording"})");
        });
        dispatcher.registerHandler(BuiltinCommand::ToggleSearch, []() {
            easy::core::MessageBridge::instance().handleMessage(R"({"method":"search.toggle"})");
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

        gestureEngine.loadFromConfig();
        easy::gesture::MouseHook::instance().install();
        gestureEngine.start();
        return true;
    }

    void shutdown() override {
        LOG_INFO("GesturePlugin: 卸载手势引擎");
        auto& gestureEngine = easy::gesture::GestureEngine::instance();
        gestureEngine.saveToConfig();
        gestureEngine.stop();
        easy::gesture::MouseHook::instance().uninstall();
    }
};

} // namespace easy::gesture

PLUGIN_API easy::core::IPlugin* CreatePlugin() {
    static easy::gesture::GesturePlugin instance;
    return &instance;
}
