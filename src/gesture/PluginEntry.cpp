#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/events/EventBus.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/utils/WinUtils.h"
#include "gesture/GestureEngine.h"
#include "gesture/MouseHook.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/HotCornerEngine.h"
#include "gesture/RadialMenuOverlay.h"
#include "gesture/GestureTrailOverlay.h"
#include "gesture/GestureInputPolicy.h"
#include "EasyToolsVersion.h"
#include <algorithm>
#include <array>
#include <optional>
#include <unordered_set>
#include <windows.h>

namespace easy::gesture {

namespace {

int hotCornerCommandIndex(const std::string& value) {
    if (value.empty()) return -1;
    if (value == "capture") return static_cast<int>(BuiltinCommand::TakeScreenshot);
    if (value == "search") return static_cast<int>(BuiltinCommand::ToggleSearch);
    try {
        const int index = std::stoi(value);
        return index >= 0 && index <= static_cast<int>(BuiltinCommand::PasteAsPin) ? index : -1;
    } catch (...) {
        return -1;
    }
}

constexpr std::array<std::pair<const char*, HotCorner>, 4> kHotCorners{{
    {"topLeft", HotCorner::TopLeft},
    {"topRight", HotCorner::TopRight},
    {"bottomLeft", HotCorner::BottomLeft},
    {"bottomRight", HotCorner::BottomRight},
}};

std::optional<std::string> parseHotCornerCommand(const nlohmann::json& value) {
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.empty() || text == "capture" || text == "search") return text;
        try {
            const int index = std::stoi(text);
            if (std::to_string(index) == text && index >= 0 &&
                index <= static_cast<int>(BuiltinCommand::PasteAsPin)) {
                return text;
            }
        } catch (...) {}
        return std::nullopt;
    }

    int index = -1;
    if (value.is_number_integer()) index = value.get<int>();
    else if (value.is_object() && value.contains("commandIndex") &&
             value["commandIndex"].is_number_integer()) {
        index = value["commandIndex"].get<int>();
    } else {
        return std::nullopt;
    }
    if (index == -1) return std::string{};
    if (index < 0 || index > static_cast<int>(BuiltinCommand::PasteAsPin)) {
        return std::nullopt;
    }
    return std::to_string(index);
}

nlohmann::json updateHotCornerSettings(const nlohmann::json& params) {
    using json = nlohmann::json;
    if (!params.is_object() || params.empty()) {
        return {{"success", false}, {"error", "no hot-corner settings supplied"}};
    }

    static const std::unordered_set<std::string> allowedKeys = {
        "enabled", "delay", "triggerDelay", "corners",
        "topLeft", "topRight", "bottomLeft", "bottomRight"
    };
    for (const auto& [key, value] : params.items()) {
        if (!allowedKeys.contains(key)) {
            return {{"success", false}, {"error", "unsupported setting: " + key}};
        }
    }

    auto& engine = HotCornerEngine::instance();
    json desired = {
        {"enabled", engine.isEnabled()},
        {"triggerDelay", engine.triggerDelay()},
        {"topLeft", engine.getCornerAction(HotCorner::TopLeft)},
        {"topRight", engine.getCornerAction(HotCorner::TopRight)},
        {"bottomLeft", engine.getCornerAction(HotCorner::BottomLeft)},
        {"bottomRight", engine.getCornerAction(HotCorner::BottomRight)},
    };

    if (params.contains("enabled")) {
        if (!params["enabled"].is_boolean()) {
            return {{"success", false}, {"error", "enabled must be boolean"}};
        }
        desired["enabled"] = params["enabled"];
    }

    const char* delayKey = params.contains("delay") ? "delay" :
                           (params.contains("triggerDelay") ? "triggerDelay" : nullptr);
    if (delayKey) {
        if (!params[delayKey].is_number_integer()) {
            return {{"success", false}, {"error", "delay must be an integer"}};
        }
        desired["triggerDelay"] = std::clamp(params[delayKey].get<int>(), 100, 2000);
    }

    if (params.contains("corners")) {
        if (!params["corners"].is_object()) {
            return {{"success", false}, {"error", "corners must be an object"}};
        }
        for (const auto& [key, value] : params["corners"].items()) {
            const auto known = std::ranges::find_if(kHotCorners, [&key](const auto& entry) {
                return key == entry.first;
            });
            if (known == kHotCorners.end()) {
                return {{"success", false}, {"error", "unsupported corner: " + key}};
            }
            const auto command = parseHotCornerCommand(value);
            if (!command) {
                return {{"success", false}, {"error", "invalid command for " + key}};
            }
            desired[key] = *command;
        }
    }

    for (const auto& [key, corner] : kHotCorners) {
        if (!params.contains(key)) continue;
        const auto command = parseHotCornerCommand(params[key]);
        if (!command) {
            return {{"success", false}, {"error", std::string("invalid command for ") + key}};
        }
        desired[key] = *command;
    }

    if (!easy::core::ConfigManager::instance().mergePatch(
            {{"gesture", {{"hotCorners", desired}}}}, "/gesture/hotCorners")) {
        return {{"success", false}, {"error", "failed to persist hot-corner settings"}};
    }

    engine.setEnabled(desired["enabled"].get<bool>());
    engine.setTriggerDelay(desired["triggerDelay"].get<int>());
    for (const auto& [key, corner] : kHotCorners) {
        engine.setCornerAction(corner, desired[key].get<std::string>());
    }
    return {{"success", true}, {"settings", std::move(desired)}};
}

easy::core::HotkeyDef configuredHotkey(const std::string& name,
                                       const easy::core::HotkeyDef& fallback) {
    const auto text = easy::core::ConfigManager::instance().get<std::string>(
        "/hotkeys/" + name, fallback.toString());
    if (text.empty()) return {};
    return easy::core::HotkeyDef::fromString(text).value_or(fallback);
}

}  // namespace

class GesturePlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Gesture"; }
    const char* getVersion() const override { return easy::version::String; }

    bool initialize() override {
        LOG_INFO("GesturePlugin: 初始化手势引擎");

        // 注册内置命令处理器
        using easy::gesture::BuiltinCommand;
        auto& dispatcher = easy::gesture::BuiltinCommandDispatcher::instance();
        dispatcher.registerHandler(BuiltinCommand::PauseGestures, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                auto& engine = easy::gesture::GestureEngine::instance();
                engine.setPaused(!engine.isPaused());
            });
        });
        dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                easy::core::EventBus::instance().publish(easy::core::ActionTriggerScreenshotEvent{});
            });
        });
        dispatcher.registerHandler(BuiltinCommand::StartRecording, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                easy::core::EventBus::instance().publish(easy::core::ActionToggleRecordingEvent{});
            });
        });
        dispatcher.registerHandler(BuiltinCommand::ToggleSearch, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                easy::core::MessageBridge::instance().handleMessage(R"({"method":"search.toggle"})");
            });
        });
        dispatcher.registerHandler(BuiltinCommand::PasteAsPin, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                easy::core::MessageBridge::instance().handleMessage(R"({"method":"capture.pasteAsPin"})");
            });
        });
        dispatcher.registerHandler(BuiltinCommand::ShowRadialMenu, []() {
            easy::core::MainThreadDispatcher::instance().post([]() {
                LOG_INFO("GesturePlugin: 内置命令触发轮盘菜单");
                POINT pt;
                GetCursorPos(&pt);
                easy::gesture::RadialMenuOverlay::instance().show(pt);
            });
        });

        // 状态变化回调
        auto& gestureEngine = easy::gesture::GestureEngine::instance();
        gestureEngine.setPauseChangedCallback([](bool paused) -> bool {
            auto& config = easy::core::ConfigManager::instance();
            const bool alreadySaved =
                config.get<bool>("/gesture/paused", !paused) == paused &&
                config.get<bool>("/gesture/enabled", paused) == !paused;
            if (!alreadySaved && !config.mergePatch({
                    {"gesture", {{"paused", paused}, {"enabled", !paused}}}
                }, "/gesture")) {
                return false;
            }
            easy::core::MessageBridge::instance().pushEvent("gesture.stateChanged", {
                {"paused", paused},
                {"enabled", !paused}
            });
            return true;
        });

        // 注册 IPC 处理器
        
        auto& mb = easy::core::MessageBridge::instance();
        auto& bus = easy::core::EventBus::instance();

        m_pauseSubscription = bus.subscribe<easy::core::ActionToggleGesturePauseEvent>([](const easy::core::ActionToggleGesturePauseEvent&) {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });
        m_cancelSubscription = bus.subscribe<easy::core::CancelTransientUiEvent>([](const easy::core::CancelTransientUiEvent&) {
            easy::gesture::GestureEngine::instance().cancelActiveGesture();
            easy::gesture::RadialMenuOverlay::instance().hide();
        });
        m_themeSubscription = bus.subscribe<easy::core::ThemeChangedEvent>([](const easy::core::ThemeChangedEvent&) {
            easy::gesture::GestureTrailOverlay::instance().reloadThemeColors();
        });
        
        mb.registerHandler("gesture.togglePause", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            const bool success = engine.setPaused(!engine.isPaused());
            return {{"success", success}, {"paused", engine.isPaused()},
                    {"enabled", !engine.isPaused()}};
        });

        auto& hotkeys = easy::core::HotkeyManager::instance();
        hotkeys.registerHotkey("Pause Gestures", configuredHotkey("Pause Gestures", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'W'}), []() {
            auto& engine = easy::gesture::GestureEngine::instance();
            engine.setPaused(!engine.isPaused());
        });

        mb.registerHandler("gesture.getProfiles", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            nlohmann::json result = nlohmann::json::array();
            for (const auto& profile : engine.getProfiles()) result.push_back(profile.toJson());
            return result;
        });

        mb.registerHandler("gesture.getState", [](const nlohmann::json&) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"paused", engine.isPaused()},
                {"enabled", !engine.isPaused()},
                {"triggerButton", engine.triggerButton()},
                {"trailVisible", engine.trailVisible()},
                {"autoBypassFullscreen", engine.autoBypassFullscreen()},
                {"targetMode", engine.targetMode()},
                {"trailColorMode", config.get<std::string>("/gesture/trailColorMode", "auto")},
                {"trailColor", config.get<std::string>("/gesture/trailColor", "#8B5CF6")},
                {"trailWidth", config.get<float>("/gesture/trailWidth", 4.0f)},
                {"trailOutlineWidth", config.get<float>("/gesture/trailOutlineWidth", 2.5f)},
                {"elevated", easy::core::WinUtils::isCurrentProcessElevated()},
                {"runAsAdmin", config.get<bool>("/general/runAsAdmin", false)}
            };
        });

        mb.registerHandler("gesture.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& engine = easy::gesture::GestureEngine::instance();
            auto& config = easy::core::ConfigManager::instance();
            if (!params.is_object() || params.empty()) {
                return {{"success", false}, {"error", "no gesture settings supplied"}};
            }
            static const std::unordered_set<std::string> allowed = {
                "enabled", "paused", "triggerButton", "trailVisible", "autoBypassFullscreen",
                "targetMode", "trailColorMode", "trailColor", "trailWidth", "trailOutlineWidth"
            };
            for (const auto& [key, value] : params.items()) {
                if (!allowed.contains(key)) {
                    return {{"success", false}, {"error", "unsupported setting: " + key}};
                }
            }
            if ((params.contains("enabled") && !params["enabled"].is_boolean()) ||
                (params.contains("paused") && !params["paused"].is_boolean()) ||
                (params.contains("trailVisible") && !params["trailVisible"].is_boolean()) ||
                (params.contains("autoBypassFullscreen") && !params["autoBypassFullscreen"].is_boolean()) ||
                (params.contains("targetMode") && !params["targetMode"].is_string()) ||
                (params.contains("trailColorMode") && !params["trailColorMode"].is_string()) ||
                (params.contains("trailColor") && !params["trailColor"].is_string()) ||
                (params.contains("trailWidth") && !params["trailWidth"].is_number()) ||
                (params.contains("trailOutlineWidth") && !params["trailOutlineWidth"].is_number())) {
                return {{"success", false}, {"error", "setting has invalid type"}};
            }
            if (params.contains("enabled") && params.contains("paused") &&
                params["enabled"].get<bool>() == params["paused"].get<bool>()) {
                return {{"success", false}, {"error", "enabled and paused conflict"}};
            }

            std::string trigger = engine.triggerButton();
            if (params.contains("triggerButton")) {
                if (!params["triggerButton"].is_string()) {
                    return {{"success", false}, {"error", "triggerButton must be a string"}};
                }
                trigger = params["triggerButton"].get<std::string>();
                if (trigger != "right" && trigger != "middle" && trigger != "both") {
                    return {{"success", false}, {"error", "invalid triggerButton"}};
                }
            }

            bool paused = engine.isPaused();
            if (params.contains("enabled")) paused = !params["enabled"].get<bool>();
            if (params.contains("paused")) paused = params["paused"].get<bool>();
            const bool trailVisible = params.value("trailVisible", engine.trailVisible());
            const bool autoBypassFullscreen = params.value("autoBypassFullscreen", engine.autoBypassFullscreen());
            std::string targetMode = params.value("targetMode", engine.targetMode());
            if (targetMode != "underPointer" && targetMode != "foreground") {
                return {{"success", false}, {"error", "invalid targetMode"}};
            }

            std::string trailColorMode = params.value("trailColorMode", config.get<std::string>("/gesture/trailColorMode", "auto"));
            std::string trailColor = params.value("trailColor", config.get<std::string>("/gesture/trailColor", "#8B5CF6"));
            float trailWidth = params.value("trailWidth", config.get<float>("/gesture/trailWidth", 4.0f));
            float trailOutlineWidth = clampTrailOutlineWidth(
                params.value("trailOutlineWidth", config.get<float>("/gesture/trailOutlineWidth", 2.5f)));

            nlohmann::json patch = {
                {"paused", paused}, {"enabled", !paused},
                {"triggerButton", trigger}, {"trailVisible", trailVisible},
                {"autoBypassFullscreen", autoBypassFullscreen},
                {"targetMode", targetMode},
                {"trailColorMode", trailColorMode},
                {"trailColor", trailColor},
                {"trailWidth", trailWidth},
                {"trailOutlineWidth", trailOutlineWidth}
            };
            if (!config.mergePatch({{"gesture", patch}}, "/gesture")) {
                return {{"success", false}, {"error", "failed to persist gesture settings"}};
            }
            engine.setTriggerButton(trigger);
            engine.setTrailVisible(trailVisible);
            engine.setAutoBypassFullscreen(autoBypassFullscreen);
            engine.setTargetMode(targetMode);
            easy::gesture::GestureTrailOverlay::instance().reloadThemeColors();

            const bool pauseApplied = engine.setPaused(paused);
            return {
                {"success", pauseApplied},
                {"paused", engine.isPaused()},
                {"enabled", !engine.isPaused()},
                {"triggerButton", engine.triggerButton()},
                {"trailVisible", engine.trailVisible()},
                {"autoBypassFullscreen", engine.autoBypassFullscreen()},
                {"targetMode", engine.targetMode()},
                {"trailColorMode", trailColorMode},
                {"trailColor", trailColor},
                {"trailWidth", trailWidth},
                {"trailOutlineWidth", trailOutlineWidth}
            };
        });

        mb.registerHandler("gesture.updateProfile", [](const nlohmann::json& params) -> nlohmann::json {
            if (!params.is_object() || !params.contains("name") || !params["name"].is_string() ||
                params["name"].get<std::string>().empty() ||
                !params.contains("mappings") || !params["mappings"].is_array()) {
                return {{"success", false}, {"error", "invalid profile"}};
            }
            auto profile = easy::gesture::GestureProfile::fromJson(params);
            auto& engine = easy::gesture::GestureEngine::instance();
            const auto previous = engine.getProfile(profile.name());
            engine.setProfile(profile.name(), profile);
            if (!engine.saveToConfig()) {
                if (previous) engine.setProfile(previous->name(), *previous);
                else engine.removeProfile(profile.name());
                return {{"success", false}, {"error", "failed to persist profile"}};
            }
            return {{"success", true}};
        });

        mb.registerHandler("gesture.setTriggerState", [](const nlohmann::json& params) -> nlohmann::json {
            std::string profName = params.value("profile", "default");
            std::string trigger = params.value("trigger", "");
            std::string stateStr = params.value("state", "default");
            if (trigger.empty()) return {{"success", false}, {"error", "trigger is required"}};

            auto& engine = easy::gesture::GestureEngine::instance();
            auto profile = engine.getProfile(profName);
            if (!profile) profile = easy::gesture::GestureProfile(profName);

            profile->setTriggerState(trigger, easy::gesture::triggerStateFromString(stateStr));
            engine.setProfile(profName, *profile);
            bool ok = engine.saveToConfig();
            return {{"success", ok}};
        });

        mb.registerHandler("gesture.setTriggerBatch", [](const nlohmann::json& params) -> nlohmann::json {
            std::string profName = params.value("profile", "default");
            std::string stateStr = params.value("state", "default");

            auto& engine = easy::gesture::GestureEngine::instance();
            auto profile = engine.getProfile(profName);
            if (!profile) profile = easy::gesture::GestureProfile(profName);

            profile->setAllTriggerStates(easy::gesture::triggerStateFromString(stateStr));
            engine.setProfile(profName, *profile);
            bool ok = engine.saveToConfig();
            return {{"success", ok}};
        });

        mb.registerHandler("gesture.reorderMappings", [](const nlohmann::json& params) -> nlohmann::json {
            std::string profName = params.value("profile", "default");
            auto& engine = easy::gesture::GestureEngine::instance();
            auto profile = engine.getProfile(profName);
            if (!profile) return {{"success", false}, {"error", "profile not found"}};

            if (params.contains("fromIndex") && params.contains("toIndex")) {
                size_t fromIdx = params["fromIndex"].get<size_t>();
                size_t toIdx = params["toIndex"].get<size_t>();
                profile->moveMapping(fromIdx, toIdx);
            } else if (params.contains("orderedCodes") && params["orderedCodes"].is_array()) {
                std::vector<std::string> codes = params["orderedCodes"].get<std::vector<std::string>>();
                profile->reorderMappings(codes);
            }

            engine.setProfile(profName, *profile);
            bool ok = engine.saveToConfig();
            return {{"success", ok}};
        });

        mb.registerHandler("gesture.setPaused", [](const nlohmann::json& params) -> nlohmann::json {
            bool paused = params.value("paused", false);
            const bool success = easy::gesture::GestureEngine::instance().setPaused(paused);
            return {
                {"success", success},
                {"paused", easy::gesture::GestureEngine::instance().isPaused()},
                {"enabled", !easy::gesture::GestureEngine::instance().isPaused()}
            };
        });

        mb.registerHandler("gesture.getScopeRules", [](const nlohmann::json&) -> nlohmann::json {
            return easy::gesture::GestureEngine::instance().scopeRules().toJson();
        });

        mb.registerHandler("gesture.updateScopeRules", [](const nlohmann::json& params) -> nlohmann::json {
            const nlohmann::json& rules = (params.is_object() && params.contains("rules")) ? params["rules"] : params;
            if (!rules.is_array()) {
                return {{"success", false}, {"error", "rules must be an array"}};
            }
            auto& engine = easy::gesture::GestureEngine::instance();
            const auto previous = engine.scopeRules().toJson();
            engine.scopeRules().loadFromJson(rules);
            if (!engine.saveToConfig()) {
                engine.scopeRules().loadFromJson(previous);
                return {{"success", false}, {"error", "failed to persist scope rules"}};
            }
            return {{"success", true}};
        });

        // 注册 HotCorner 相关 IPC
        mb.registerHandler("gesture.updateHotCorners", [](const nlohmann::json& params) -> nlohmann::json {
            return updateHotCornerSettings(params);
        });

        // hotcorner.getSettings —— 返回四个角的动作和启用状态
        mb.registerHandler("hotcorner.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& hce = easy::gesture::HotCornerEngine::instance();
            auto& config = easy::core::ConfigManager::instance();
            LOG_DEBUG("IPC: hotcorner.getSettings 查询触发角配置");
            return {
                {"enabled", hce.isEnabled()},
                {"delay", config.get<int>("/gesture/hotCorners/triggerDelay", 300)},
                {"corners", {
                    {"topLeft", {{"commandIndex", hotCornerCommandIndex(hce.getCornerAction(easy::gesture::HotCorner::TopLeft))}}},
                    {"topRight", {{"commandIndex", hotCornerCommandIndex(hce.getCornerAction(easy::gesture::HotCorner::TopRight))}}},
                    {"bottomLeft", {{"commandIndex", hotCornerCommandIndex(hce.getCornerAction(easy::gesture::HotCorner::BottomLeft))}}},
                    {"bottomRight", {{"commandIndex", hotCornerCommandIndex(hce.getCornerAction(easy::gesture::HotCorner::BottomRight))}}}
                }}
            };
        });

        // hotcorner.updateSettings —— 更新角落动作/延迟/启用状态
        mb.registerHandler("hotcorner.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            LOG_INFO("IPC: hotcorner.updateSettings 更新触发角配置");
            return updateHotCornerSettings(params);
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
            if (itemsJson.size() > 16) {
                return {{"success", false}, {"error", "too many radial-menu items"}};
            }
            std::vector<easy::gesture::RadialMenuItem> items;
            items.reserve(itemsJson.size());
            for (const auto& ij : itemsJson) {
                if (!ij.is_object() || !ij.contains("label") || !ij["label"].is_string() ||
                    !ij.contains("command") || !ij["command"].is_string()) {
                    return {{"success", false}, {"error", "invalid radial-menu item"}};
                }
                const auto label = ij["label"].get<std::string>();
                const auto command = ij["command"].get<std::string>();
                if (label.empty() || label.size() > 80 || command.size() > 256) {
                    return {{"success", false}, {"error", "invalid radial-menu item length"}};
                }
                items.push_back({
                    label,
                    command
                });
            }

            // 持久化到配置
            if (!config.set("/gesture/radialMenu/items", itemsJson)) {
                return {{"success", false}, {"error", "failed to persist radial-menu items"}};
            }
            easy::gesture::RadialMenuOverlay::instance().setItems(items);
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
        hce.setEnabled(config.get<bool>("/gesture/hotCorners/enabled", false));
        hce.setTriggerDelay(std::clamp(config.get<int>("/gesture/hotCorners/triggerDelay", 300), 100, 2000));

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

        if (!gestureEngine.start()) {
            LOG_ERROR("GesturePlugin: 手势引擎启动失败");
            return false;
        }
        hce.start();
        return true;
    }

    void shutdown() override {
        LOG_INFO("GesturePlugin: 卸载手势引擎");
        auto& bridge = easy::core::MessageBridge::instance();
        bridge.unregisterHandlersByPrefix("gesture.");
        bridge.unregisterHandlersByPrefix("hotcorner.");
        bridge.unregisterHandlersByPrefix("radialmenu.");
        easy::core::HotkeyManager::instance().unregisterHotkey("Pause Gestures");
        easy::core::EventBus::instance().unsubscribeAndWait(m_pauseSubscription);
        m_pauseSubscription = 0;
        easy::core::EventBus::instance().unsubscribeAndWait(m_cancelSubscription);
        m_cancelSubscription = 0;
        easy::core::EventBus::instance().unsubscribeAndWait(m_themeSubscription);
        m_themeSubscription = 0;

        auto& gestureEngine = easy::gesture::GestureEngine::instance();
        gestureEngine.saveToConfig();
        gestureEngine.stop();
        gestureEngine.setPauseChangedCallback(nullptr);
        easy::gesture::HotCornerEngine::instance().stop();
        easy::gesture::BuiltinCommandDispatcher::instance().clearHandlers();
    }

private:
    easy::core::SubscriptionId m_pauseSubscription = 0;
    easy::core::SubscriptionId m_cancelSubscription = 0;
    easy::core::SubscriptionId m_themeSubscription = 0;
};

} // namespace easy::gesture

PLUGIN_API easy::core::IPlugin* CreatePlugin() {
    static easy::gesture::GesturePlugin instance;
    return &instance;
}

PLUGIN_API std::uint32_t GetPluginAbiVersion() {
    return easy::core::CurrentPluginAbiVersion;
}
