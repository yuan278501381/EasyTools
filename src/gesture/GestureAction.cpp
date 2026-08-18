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

    // 虚拟键码 → 名称
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= '0' && virtualKey <= '9') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        result += "F" + std::to_string(virtualKey - VK_F1 + 1);
    } else {
        static const std::unordered_map<uint16_t, std::string> revMap = {
            {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_SPACE, "Space"},
            {VK_ESCAPE, "Escape"}, {VK_DELETE, "Delete"},
            {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"}, {VK_DOWN, "Down"},
            {VK_MEDIA_NEXT_TRACK, "MediaNext"}, {VK_MEDIA_PREV_TRACK, "MediaPrev"},
            {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"}, {VK_VOLUME_MUTE, "VolumeMute"},
            {VK_VOLUME_UP, "VolumeUp"}, {VK_VOLUME_DOWN, "VolumeDown"},
        };
        if (auto it = revMap.find(virtualKey); it != revMap.end()) {
            result += it->second;
        } else {
            result += "0x" + std::format("{:02X}", virtualKey);
        }
    }
    return result;
}

namespace {

// 判断是否为全局多媒体控制或全局系统键 (不需要且不应切换目标窗口焦点)
bool isGlobalKey(WORD vk) {
    return (vk >= VK_VOLUME_MUTE && vk <= VK_MEDIA_PLAY_PAUSE) || // 0xAD - 0xB3 多媒体音量/播放
           (vk >= 0xB4 && vk <= 0xB7) ||                          // 启动应用键
           (vk == VK_SNAPSHOT);                                   // PrintScreen
}

void activateTargetWindow(HWND targetHwnd) {
    if (!targetHwnd || !IsWindow(targetHwnd)) return;
    HWND curFg = GetForegroundWindow();
    if (curFg == targetHwnd) return;

    DWORD curThread = GetCurrentThreadId();
    DWORD fgThread = curFg ? GetWindowThreadProcessId(curFg, nullptr) : 0;
    DWORD targetThread = GetWindowThreadProcessId(targetHwnd, nullptr);

    if (fgThread && fgThread != curThread) AttachThreadInput(curThread, fgThread, TRUE);
    if (targetThread && targetThread != curThread) AttachThreadInput(curThread, targetThread, TRUE);

    if (IsIconic(targetHwnd)) ShowWindow(targetHwnd, SW_RESTORE);
    SetForegroundWindow(targetHwnd);
    BringWindowToTop(targetHwnd);

    if (fgThread && fgThread != curThread) AttachThreadInput(curThread, fgThread, FALSE);
    if (targetThread && targetThread != curThread) AttachThreadInput(curThread, targetThread, FALSE);
}

} // namespace

// ── KeyStroke::send ──────────────────────────────────────────────────────────

void KeyStroke::send(void* targetWindowPtr) const {
    if (virtualKey == 0) {
        LOG_WARN("KeyStroke::send 被调用但 virtualKey 为空, 跳过");
        return;
    }

    HWND targetHwnd = static_cast<HWND>(targetWindowPtr);

    // 1. 如果是非全局媒体键，且明确指定了鼠标下方目标窗口，优先激活该窗口确保按键精准生效
    if (!isGlobalKey(virtualKey) && targetHwnd) {
        activateTargetWindow(targetHwnd);
    } else if (!isGlobalKey(virtualKey)) {
        HWND fg = GetForegroundWindow();
        if (!fg) {
            POINT pt;
            GetCursorPos(&pt);
            fg = WindowFromPoint(pt);
            if (fg) fg = GetAncestor(fg, GA_ROOT);
            if (fg) {
                SetForegroundWindow(fg);
            }
        }
    }

    // 2. 构造原子性 INPUT 数组: 按下修饰键 → 主键按下 → 主键释放 → 逆序释放修饰键
    // 必须在单次 SendInput 调用中原子性提交全部 Down 与 Up，绝不在中间插入 Sleep，彻底杜绝修饰键粘滞与幽灵按键问题
    std::vector<INPUT> inputs;
    inputs.reserve(10);

    auto addKey = [&](WORD vk, bool up) {
        INPUT inp{};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        inp.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0);
        if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_UP || vk == VK_DOWN || vk == VK_DELETE || vk == VK_INSERT ||
            vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
            vk == VK_APPS) {
            inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        inputs.push_back(inp);
    };

    // 修饰键按下顺序
    static constexpr std::array<std::pair<uint8_t, WORD>, 4> kMods = {{
        {MOD_CONTROL, VK_CONTROL}, {MOD_ALT, VK_MENU},
        {MOD_SHIFT, VK_SHIFT},     {MOD_WIN, VK_LWIN},
    }};

    // 按下修饰键
    for (auto [mod, vk] : kMods) {
        if (modifiers & mod) addKey(vk, /*up=*/false);
    }
    // 按下主键
    addKey(virtualKey, /*up=*/false);

    // 释放主键
    addKey(virtualKey, /*up=*/true);
    // 逆序释放修饰键
    for (auto it = kMods.rbegin(); it != kMods.rend(); ++it) {
        if (modifiers & it->first) addKey(it->second, /*up=*/true);
    }

    // 原子性提交整组按键
    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        LOG_WARN("SendInput 未完全发送: expected={}, sent={}, lastError={}",
                 inputs.size(), sent, GetLastError());
    }
}

// ── GestureAction::execute ───────────────────────────────────────────────────

void GestureAction::execute(void* targetWindowPtr) const {
    easy::core::TraceId::Scope scope;

    switch (type) {
        case ActionType::SendKeys: {
            LOG_DEBUG("执行手势动作: SendKeys, keys={}", keyStroke.toString());
            keyStroke.send(targetWindowPtr);
            break;
        }

        case ActionType::LuaScript: {
            LOG_DEBUG("执行手势动作: LuaScript, name={}, script={}", name, luaScript.substr(0, std::min(luaScript.size(), (size_t)100)));
            easy::core::ScriptContext ctx;
            ctx.scriptId = "gesture:" + (name.empty() ? std::to_string(std::hash<std::string>{}(luaScript)) : name);
            ctx.scriptName = name.empty() ? "自定义手势脚本" : name;
            ctx.requestedPerms = requestedPermissions.empty()
                ? easy::core::LuaPermission::Safe
                : easy::core::parseLuaPermissions(requestedPermissions);
            ctx.interactive = true;
            easy::core::LuaEngine::instance().authorizeAndExecute(luaScript, ctx);
            break;
        }

        case ActionType::BuiltinCommand: {
            LOG_DEBUG("执行手势动作: BuiltinCommand, cmd={}", static_cast<int>(builtinCmd));
            BuiltinCommandDispatcher::instance().execute(builtinCmd, targetWindowPtr);
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
            if (!requestedPermissions.empty()) {
                j["permissions"] = requestedPermissions;
            }
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
            if (j.contains("permissions") && j["permissions"].is_array()) {
                action.requestedPermissions = j["permissions"].get<std::vector<std::string>>();
            }
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
