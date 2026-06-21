// ─────────────────────────────────────────────────────────────────────────────
// GestureAction.cpp — 手势动作执行与序列化
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureAction.h"
#include "gesture/BuiltinCommands.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/lua/LuaEngine.h"

#include <windows.h>
#include <shellapi.h>
#include <array>

namespace easy::gesture {

// ── KeyStroke ────────────────────────────────────────────────────────────────

KeyStroke KeyStroke::fromString(const std::string& str) {
    KeyStroke ks;
    std::string remaining = str;

    // 解析修饰键
    auto consume = [&](const std::string& prefix, uint8_t mod) {
        size_t pos = remaining.find(prefix);
        if (pos != std::string::npos) {
            remaining.erase(pos, prefix.size());
            ks.modifiers |= mod;
        }
    };

    consume("Ctrl+",  MOD_CONTROL);
    consume("Alt+",   MOD_ALT);
    consume("Shift+", MOD_SHIFT);
    consume("Win+",   MOD_WIN);

    // 特殊键
    static const std::unordered_map<std::string, uint16_t> keyMap = {
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5},
        {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10},
        {"F11", VK_F11}, {"F12", VK_F12},
        {"Tab", VK_TAB}, {"Enter", VK_RETURN}, {"Space", VK_SPACE},
        {"Escape", VK_ESCAPE}, {"Delete", VK_DELETE},
        {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"Up", VK_UP}, {"Down", VK_DOWN},
    };

    auto it = keyMap.find(remaining);
    if (it != keyMap.end()) {
        ks.virtualKey = it->second;
    } else if (remaining.size() >= 3 && remaining.starts_with("0x")) {
        try {
            ks.virtualKey = static_cast<uint16_t>(std::stoul(remaining.substr(2), nullptr, 16));
        } catch (...) {
            ks.virtualKey = 0;
        }
    } else if (remaining.size() == 1) {
        ks.virtualKey = static_cast<uint16_t>(std::toupper(static_cast<unsigned char>(remaining[0])));
    }

    return ks;
}

std::string KeyStroke::toString() const {
    std::string result;
    if (modifiers & MOD_CONTROL) result += "Ctrl+";
    if (modifiers & MOD_ALT)     result += "Alt+";
    if (modifiers & MOD_SHIFT)   result += "Shift+";
    if (modifiers & MOD_WIN)     result += "Win+";

    // 虚拟键码 → 名称 (简化版)
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        result += "F" + std::to_string(virtualKey - VK_F1 + 1);
    } else {
        result += "0x" + std::format("{:02X}", virtualKey);
    }
    return result;
}

// ── KeyStroke::send ──────────────────────────────────────────────────────────

void KeyStroke::send() const {
    if (virtualKey == 0) {
        LOG_WARN("KeyStroke::send 被调用但 virtualKey 为空, 跳过");
        return;
    }

    // 构造 INPUT 数组: 按下修饰键 → 主键 → 逆序释放
    std::vector<INPUT> inputs;
    inputs.reserve(10);

    auto addKey = [&](WORD vk, bool up) {
        INPUT inp{};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(inp);
    };

    // 修饰键按下顺序
    static constexpr std::array<std::pair<uint8_t, WORD>, 4> kMods = {{
        {MOD_CONTROL, VK_CONTROL}, {MOD_ALT, VK_MENU},
        {MOD_SHIFT, VK_SHIFT},     {MOD_WIN, VK_LWIN},
    }};

    for (auto [mod, vk] : kMods) {
        if (modifiers & mod) addKey(vk, /*up=*/false);
    }
    addKey(virtualKey, /*up=*/false);
    addKey(virtualKey, /*up=*/true);
    // 逆序释放修饰键
    for (auto it = kMods.rbegin(); it != kMods.rend(); ++it) {
        if (modifiers & it->first) addKey(it->second, /*up=*/true);
    }

    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        LOG_WARN("SendInput 未完全发送: expected={}, sent={}, lastError={}",
                 inputs.size(), sent, GetLastError());
    }
}

// ── GestureAction::execute ───────────────────────────────────────────────────

void GestureAction::execute() const {
    easy::core::TraceId::Scope scope;

    switch (type) {
        case ActionType::SendKeys: {
            LOG_DEBUG("执行手势动作: SendKeys, keys={}", keyStroke.toString());
            keyStroke.send();
            break;
        }

        case ActionType::LuaScript: {
            LOG_DEBUG("执行手势动作: LuaScript, script={}", luaScript.substr(0, std::min(luaScript.size(), (size_t)100)));
            easy::core::LuaEngine::instance().executeScript(luaScript);
            break;
        }

        case ActionType::BuiltinCommand: {
            LOG_DEBUG("执行手势动作: BuiltinCommand, cmd={}", static_cast<int>(builtinCmd));
            BuiltinCommandDispatcher::instance().execute(builtinCmd);
            break;
        }

        case ActionType::RunProgram: {
            LOG_DEBUG("执行手势动作: RunProgram, path={}", programPath);
            ShellExecuteA(nullptr, "open", programPath.c_str(),
                          programArgs.empty() ? nullptr : programArgs.c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        }
    }
}

// ── JSON 序列化 ──────────────────────────────────────────────────────────────

nlohmann::json GestureAction::toJson() const {
    nlohmann::json j;
    j["type"] = static_cast<int>(type);
    j["name"] = name;
    j["description"] = description;

    switch (type) {
        case ActionType::SendKeys:
            j["keyStroke"] = keyStroke.toString();
            break;
        case ActionType::LuaScript:
            j["luaScript"] = luaScript;
            break;
        case ActionType::BuiltinCommand:
            j["builtinCmd"] = static_cast<int>(builtinCmd);
            break;
        case ActionType::RunProgram:
            j["programPath"] = programPath;
            j["programArgs"] = programArgs;
            break;
    }
    return j;
}

GestureAction GestureAction::fromJson(const nlohmann::json& j) {
    GestureAction action;
    action.type = static_cast<ActionType>(j.value("type", 0));
    action.name = j.value("name", "");
    action.description = j.value("description", "");

    switch (action.type) {
        case ActionType::SendKeys:
            action.keyStroke = KeyStroke::fromString(j.value("keyStroke", ""));
            break;
        case ActionType::LuaScript:
            action.luaScript = j.value("luaScript", "");
            break;
        case ActionType::BuiltinCommand:
            action.builtinCmd = static_cast<BuiltinCommand>(j.value("builtinCmd", 0));
            break;
        case ActionType::RunProgram:
            action.programPath = j.value("programPath", "");
            action.programArgs = j.value("programArgs", "");
            break;
    }
    return action;
}

nlohmann::json GestureMapping::toJson() const {
    nlohmann::json j;
    j["gestureCode"] = gestureCode;
    j["action"] = action.toJson();
    return j;
}

GestureMapping GestureMapping::fromJson(const nlohmann::json& j) {
    GestureMapping mapping;
    mapping.gestureCode = j.value("gestureCode", "");
    if (j.contains("action")) {
        mapping.action = GestureAction::fromJson(j["action"]);
    }
    return mapping;
}

}  // namespace easy::gesture
