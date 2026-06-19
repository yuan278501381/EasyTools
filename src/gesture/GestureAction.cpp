// ─────────────────────────────────────────────────────────────────────────────
// GestureAction.cpp — 手势动作执行与序列化
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureAction.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <windows.h>
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

// ── GestureAction::execute ───────────────────────────────────────────────────

void GestureAction::execute() const {
    easy::core::TraceId::Scope scope;

    switch (type) {
        case ActionType::SendKeys: {
            LOG_DEBUG("执行手势动作: SendKeys, keys={}", keyStroke.toString());

            // 构造 INPUT 数组
            std::vector<INPUT> inputs;

            // 按下修饰键
            auto addModDown = [&](uint8_t mod, WORD vk) {
                if (keyStroke.modifiers & mod) {
                    INPUT inp{};
                    inp.type = INPUT_KEYBOARD;
                    inp.ki.wVk = vk;
                    inputs.push_back(inp);
                }
            };
            addModDown(MOD_CONTROL, VK_CONTROL);
            addModDown(MOD_ALT, VK_MENU);
            addModDown(MOD_SHIFT, VK_SHIFT);
            addModDown(MOD_WIN, VK_LWIN);

            // 按下主键
            {
                INPUT inp{};
                inp.type = INPUT_KEYBOARD;
                inp.ki.wVk = keyStroke.virtualKey;
                inputs.push_back(inp);
            }

            // 释放主键
            {
                INPUT inp{};
                inp.type = INPUT_KEYBOARD;
                inp.ki.wVk = keyStroke.virtualKey;
                inp.ki.dwFlags = KEYEVENTF_KEYUP;
                inputs.push_back(inp);
            }

            // 释放修饰键（逆序）
            auto addModUp = [&](uint8_t mod, WORD vk) {
                if (keyStroke.modifiers & mod) {
                    INPUT inp{};
                    inp.type = INPUT_KEYBOARD;
                    inp.ki.wVk = vk;
                    inp.ki.dwFlags = KEYEVENTF_KEYUP;
                    inputs.push_back(inp);
                }
            };
            addModUp(MOD_WIN, VK_LWIN);
            addModUp(MOD_SHIFT, VK_SHIFT);
            addModUp(MOD_ALT, VK_MENU);
            addModUp(MOD_CONTROL, VK_CONTROL);

            UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            if (sent != inputs.size()) {
                LOG_WARN("SendInput 未完全发送: expected={}, sent={}", inputs.size(), sent);
            }
            break;
        }

        case ActionType::LuaScript: {
            LOG_DEBUG("执行手势动作: LuaScript, script={}", luaScript.substr(0, 100));
            // TODO: 通过 LuaEngine 执行脚本
            break;
        }

        case ActionType::BuiltinCommand: {
            LOG_DEBUG("执行手势动作: BuiltinCommand, cmd={}", static_cast<int>(builtinCmd));
            // TODO: 调用内置命令处理器
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
