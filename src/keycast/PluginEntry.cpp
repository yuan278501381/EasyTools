#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/events/EventBus.h"
#include "core/utils/WinUtils.h"
#include "KeycastOverlay.h"
#include <windows.h>

namespace easy::keycast {

class KeycastPlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Keycast"; }
    const char* getVersion() const override { return easy::version::String; }

    bool initialize() override {
        LOG_INFO("Keycast Plugin initialize");
        
        // Initialize Overlay UI
        if (!KeycastOverlay::instance().init()) {
            LOG_ERROR("Failed to init KeycastOverlay");
            return false;
        }

        easy::core::KeyboardHook::instance().setKeycastCallback([](const std::string& sequence) {
            KeycastOverlay::instance().pushKey(sequence);
        });

        // 订阅全局主题与品牌色变更事件
        m_themeSubscription = easy::core::EventBus::instance().subscribe<easy::core::ThemeChangedEvent>([](const easy::core::ThemeChangedEvent&) {
            KeycastOverlay::instance().onThemeChanged();
        });

        auto& mb = easy::core::MessageBridge::instance();
        mb.registerHandler("keycast.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto s = KeycastOverlay::instance().getSettings();
            return {
                {"enabled", s.enabled},
                {"autoBypassFullscreen", s.autoBypassFullscreen},
                {"showKeyboard", s.showKeyboard},
                {"filterMode", s.filterMode},
                {"position", s.position},
                {"mergeRecentKeys", s.mergeRecentKeys},
                {"mergeTimeoutMs", s.mergeTimeoutMs},
                {"onlyShortcuts", s.filterMode == "smart_shortcuts"},
                {"displayDurationMs", s.displayDurationMs},
                {"fontSize", s.fontSize},
                {"textColor", s.textColor},
                {"backgroundColor", s.backgroundColor},
                {"modifierKeycapColor", s.modifierKeycapColor},
                {"modifierKeycapOpacity", s.modifierKeycapOpacity},
                {"modifierTextColor", s.modifierTextColor},
                {"firstKeyAnim", s.firstKeyAnim},
                {"subsequentKeyAnim", s.subsequentKeyAnim},
                {"rowCascadeAnim", s.rowCascadeAnim},
                {"exitDriftAnim", s.exitDriftAnim}
            };
        });

        mb.registerHandler("keycast.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            if (!params.is_object() || params.empty()) {
                return {{"success", false}, {"error", "no settings supplied"}};
            }
            auto& overlay = KeycastOverlay::instance();
            auto s = overlay.getSettings();

            if (params.contains("enabled") && params["enabled"].is_boolean()) {
                s.enabled = params["enabled"].get<bool>();
            }
            if (params.contains("autoBypassFullscreen") && params["autoBypassFullscreen"].is_boolean()) {
                s.autoBypassFullscreen = params["autoBypassFullscreen"].get<bool>();
            }
            if (params.contains("showKeyboard") && params["showKeyboard"].is_boolean()) {
                s.showKeyboard = params["showKeyboard"].get<bool>();
            }
            if (params.contains("filterMode") && params["filterMode"].is_string()) {
                s.filterMode = params["filterMode"].get<std::string>();
            }
            if (params.contains("position") && params["position"].is_string()) {
                s.position = params["position"].get<std::string>();
            }
            if (params.contains("mergeRecentKeys") && params["mergeRecentKeys"].is_boolean()) {
                s.mergeRecentKeys = params["mergeRecentKeys"].get<bool>();
            }
            if (params.contains("mergeTimeoutMs") && params["mergeTimeoutMs"].is_number_integer()) {
                s.mergeTimeoutMs = params["mergeTimeoutMs"].get<int>();
            }
            if (params.contains("onlyShortcuts") && params["onlyShortcuts"].is_boolean()) {
                bool only = params["onlyShortcuts"].get<bool>();
                s.filterMode = only ? "smart_shortcuts" : "all_keys";
            }
            if (params.contains("displayDurationMs") && params["displayDurationMs"].is_number_integer()) {
                s.displayDurationMs = params["displayDurationMs"].get<int>();
            }
            if (params.contains("fontSize") && params["fontSize"].is_number_integer()) {
                s.fontSize = params["fontSize"].get<int>();
            }
            if (params.contains("textColor") && params["textColor"].is_string()) {
                s.textColor = params["textColor"].get<std::string>();
            }
            if (params.contains("backgroundColor") && params["backgroundColor"].is_string()) {
                s.backgroundColor = params["backgroundColor"].get<std::string>();
            }
            if (params.contains("modifierKeycapColor") && params["modifierKeycapColor"].is_string()) {
                s.modifierKeycapColor = params["modifierKeycapColor"].get<std::string>();
            }
            if (params.contains("modifierKeycapOpacity") && params["modifierKeycapOpacity"].is_number_integer()) {
                s.modifierKeycapOpacity = params["modifierKeycapOpacity"].get<int>();
            }
            if (params.contains("modifierTextColor") && params["modifierTextColor"].is_string()) {
                s.modifierTextColor = params["modifierTextColor"].get<std::string>();
            }
            if (params.contains("firstKeyAnim") && params["firstKeyAnim"].is_string()) {
                s.firstKeyAnim = params["firstKeyAnim"].get<std::string>();
            }
            if (params.contains("subsequentKeyAnim") && params["subsequentKeyAnim"].is_string()) {
                s.subsequentKeyAnim = params["subsequentKeyAnim"].get<std::string>();
            }
            if (params.contains("rowCascadeAnim") && params["rowCascadeAnim"].is_boolean()) {
                s.rowCascadeAnim = params["rowCascadeAnim"].get<bool>();
            }
            if (params.contains("exitDriftAnim") && params["exitDriftAnim"].is_boolean()) {
                s.exitDriftAnim = params["exitDriftAnim"].get<bool>();
            }

            overlay.updateSettings(s);
            return {{"success", true}};
        });

        mb.registerHandler("keycast.resetDefaults", [](const nlohmann::json&) -> nlohmann::json {
            KeycastOverlay::instance().resetDefaults();
            return {{"success", true}};
        });

        mb.registerHandler("keycast.trigger", [](const nlohmann::json&) -> nlohmann::json {
            KeycastOverlay::instance().pushKey("Ctrl + Alt + K");
            return {{"success", true}};
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("Keycast Plugin shutdown");
        
        if (m_themeSubscription != 0) {
            easy::core::EventBus::instance().unsubscribeAndWait(m_themeSubscription);
            m_themeSubscription = 0;
        }
        easy::core::MessageBridge::instance().unregisterHandlersByPrefix("keycast.");
        easy::core::KeyboardHook::instance().setKeycastCallback(nullptr);
        
        // Cleanup Overlay UI
        KeycastOverlay::instance().cleanup();
        easy::core::WinUtils::trimWorkingSet();
    }

private:
    easy::core::SubscriptionId m_themeSubscription = 0;
};

extern "C" __declspec(dllexport) easy::core::IPlugin* CreatePlugin() {
    static KeycastPlugin instance;
    return &instance;
}

extern "C" __declspec(dllexport) std::uint32_t GetPluginAbiVersion() {
    return easy::core::CurrentPluginAbiVersion;
}

} // namespace easy::keycast

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
