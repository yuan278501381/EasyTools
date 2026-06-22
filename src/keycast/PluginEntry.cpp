#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "KeycastOverlay.h"
#include <windows.h>
#include <string>

namespace easy::keycast {

class KeycastPlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Keycast"; }
    const char* getVersion() const override { return "1.0.0"; }

    bool initialize() override {
        LOG_INFO("Keycast Plugin initialize");
        
        // Initialize Overlay UI
        if (!KeycastOverlay::instance().init()) {
            LOG_ERROR("Failed to init KeycastOverlay");
            return false;
        }

        // Register to receive keystrokes
        easy::core::KeyboardHook::instance().setKeycastCallback([](const std::string& sequence) {
            KeycastOverlay::instance().pushKey(sequence);
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("Keycast Plugin shutdown");
        
        // Unregister callback
        easy::core::KeyboardHook::instance().setKeycastCallback(nullptr);
        
        // Cleanup Overlay UI
        KeycastOverlay::instance().cleanup();
    }
};

extern "C" __declspec(dllexport) easy::core::IPlugin* createPlugin() {
    return new KeycastPlugin();
}

extern "C" __declspec(dllexport) void destroyPlugin(easy::core::IPlugin* plugin) {
    delete plugin;
}

} // namespace easy::keycast

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
