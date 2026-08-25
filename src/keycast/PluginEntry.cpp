#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
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

        auto& mb = easy::core::MessageBridge::instance();
        mb.registerHandler("keycast.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& overlay = KeycastOverlay::instance();
            return {
                {"autoBypassFullscreen", overlay.autoBypassFullscreen()}
            };
        });

        mb.registerHandler("keycast.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            if (!params.is_object() || params.empty()) {
                return {{"success", false}, {"error", "no settings supplied"}};
            }
            if (params.contains("autoBypassFullscreen")) {
                if (!params["autoBypassFullscreen"].is_boolean()) {
                    return {{"success", false}, {"error", "autoBypassFullscreen must be boolean"}};
                }
                const bool val = params["autoBypassFullscreen"].get<bool>();
                easy::core::ConfigManager::instance().set("/keycast/autoBypassFullscreen", val);
                KeycastOverlay::instance().setAutoBypassFullscreen(val);
            }
            return {{"success", true}};
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("Keycast Plugin shutdown");
        
        easy::core::MessageBridge::instance().unregisterHandlersByPrefix("keycast.");
        easy::core::KeyboardHook::instance().setKeycastCallback(nullptr);
        
        // Cleanup Overlay UI
        KeycastOverlay::instance().cleanup();
    }

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
