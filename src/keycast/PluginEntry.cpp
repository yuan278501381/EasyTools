#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/KeyboardHook.h"
#include "KeycastOverlay.h"
#include <windows.h>
#include <string>

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

        applyEnabled(easy::core::ConfigManager::instance().get<bool>(
            "/general/keycastEnabled", false));
        m_configCallbackId = easy::core::ConfigManager::instance().onChange(
            [this](const std::string& key) {
                if (key == "*" || key == "/general" || key == "/general/keycastEnabled") {
                    applyEnabled(easy::core::ConfigManager::instance().get<bool>(
                        "/general/keycastEnabled", false));
                }
            });

        return true;
    }

    void shutdown() override {
        LOG_INFO("Keycast Plugin shutdown");
        
        easy::core::ConfigManager::instance().removeOnChange(m_configCallbackId);
        easy::core::KeyboardHook::instance().setKeycastCallback(nullptr);
        
        // Cleanup Overlay UI
        KeycastOverlay::instance().cleanup();
    }

private:
    void applyEnabled(bool enabled) {
        if (enabled) {
            easy::core::KeyboardHook::instance().setKeycastCallback([](const std::string& sequence) {
                KeycastOverlay::instance().pushKey(sequence);
            });
        } else {
            easy::core::KeyboardHook::instance().setKeycastCallback(nullptr);
        }
        LOG_INFO("Keycast Plugin: enabled={}", enabled);
    }

    size_t m_configCallbackId = 0;
};

extern "C" __declspec(dllexport) easy::core::IPlugin* CreatePlugin() {
    static KeycastPlugin instance;
    return &instance;
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
