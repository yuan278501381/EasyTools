// ─────────────────────────────────────────────────────────────────────────────
// HotkeyManager.cpp — 全局快捷键管理器实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/hotkey/HotkeyManager.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <sstream>
#include <algorithm>
#include <cctype>

namespace easy::core {

// ── HotkeyDef 序列化 ────────────────────────────────────────────────────────

std::string HotkeyDef::toString() const {
    std::string result;
    auto mods = static_cast<UINT>(modifiers);
    if (mods & MOD_CONTROL) result += "Ctrl+";
    if (mods & MOD_ALT)     result += "Alt+";
    if (mods & MOD_SHIFT)   result += "Shift+";
    if (mods & MOD_WIN)     result += "Win+";

    static const std::unordered_map<UINT, std::string> specialKeys = {
        {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"},
        {VK_TAB, "Tab"}, {VK_DELETE, "Delete"}, {VK_INSERT, "Insert"},
        {VK_HOME, "Home"}, {VK_END, "End"}, {VK_PRIOR, "PageUp"},
        {VK_NEXT, "PageDown"}, {VK_UP, "Up"}, {VK_DOWN, "Down"},
        {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_SNAPSHOT, "PrintScreen"},
    };
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        result += "F" + std::to_string(virtualKey - VK_F1 + 1);
    } else if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
               (virtualKey >= '0' && virtualKey <= '9')) {
        result += static_cast<char>(virtualKey);
    } else if (const auto it = specialKeys.find(virtualKey); it != specialKeys.end()) {
        result += it->second;
    } else {
        result += "0x" + std::format("{:02X}", virtualKey);
    }

    return result;
}

std::optional<HotkeyDef> HotkeyDef::fromString(const std::string& str) {
    HotkeyDef def;
    if (str.empty()) return std::nullopt;
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start <= str.size()) {
        const size_t end = str.find('+', start);
        tokens.push_back(str.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (tokens.empty() || tokens.back().empty()) return std::nullopt;

    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        std::string token = tokens[i];
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (token == "ctrl" || token == "control") def.modifiers = def.modifiers | ModKey::Ctrl;
        else if (token == "alt") def.modifiers = def.modifiers | ModKey::Alt;
        else if (token == "shift") def.modifiers = def.modifiers | ModKey::Shift;
        else if (token == "win" || token == "meta") def.modifiers = def.modifiers | ModKey::Win;
        else return std::nullopt;
    }
    std::string remaining = tokens.back();

    // 特殊键映射
    static const std::unordered_map<std::string, UINT> specialKeys = {
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
        {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
        {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
        {"F13", VK_F13}, {"F14", VK_F14}, {"F15", VK_F15}, {"F16", VK_F16},
        {"F17", VK_F17}, {"F18", VK_F18}, {"F19", VK_F19}, {"F20", VK_F20},
        {"F21", VK_F21}, {"F22", VK_F22}, {"F23", VK_F23}, {"F24", VK_F24},
        {"Space", VK_SPACE}, {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE},
        {"Tab", VK_TAB}, {"Delete", VK_DELETE}, {"Insert", VK_INSERT},
        {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
        {"Up", VK_UP}, {"Down", VK_DOWN}, {"Left", VK_LEFT}, {"Right", VK_RIGHT},
        {"PrintScreen", VK_SNAPSHOT},
    };

    if (remaining.size() == 1) {
        remaining[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(remaining[0])));
    }
    auto it = specialKeys.find(remaining);
    if (it != specialKeys.end()) {
        def.virtualKey = it->second;
    } else if (remaining.size() == 1 && std::isalnum(static_cast<unsigned char>(remaining[0]))) {
        def.virtualKey = static_cast<UINT>(std::toupper(static_cast<unsigned char>(remaining[0])));
    } else if (remaining.starts_with("0x")) {
        try {
            const auto value = std::stoul(remaining.substr(2), nullptr, 16);
            if (value == 0 || value > 0xFF) return std::nullopt;
            def.virtualKey = static_cast<UINT>(value);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }

    return def;
}

// ── HotkeyManager ────────────────────────────────────────────────────────────

HotkeyManager& HotkeyManager::instance() {
    static HotkeyManager inst;
    return inst;
}

void HotkeyManager::initialize(HWND messageWindow) {
    m_hwnd = messageWindow;
    LOG_INFO("快捷键管理器已初始化");
}

void HotkeyManager::shutdown() {
    std::lock_guard lock(m_mutex);
    for (const auto& [name, entry] : m_hotkeys) {
        UnregisterHotKey(m_hwnd, entry.id);
        LOG_DEBUG("注销快捷键: name={}, def={}", name, entry.def.toString());
    }
    m_hotkeys.clear();
    m_idToName.clear();
    LOG_INFO("快捷键管理器已关闭, 所有快捷键已注销");
}

bool HotkeyManager::registerHotkey(const std::string& name, const HotkeyDef& def, HotkeyCallback callback) {
    TraceId::Scope scope;
    std::lock_guard lock(m_mutex);

    // 检查名称冲突
    if (m_hotkeys.contains(name)) {
        LOG_WARN("快捷键名称已存在, 将先注销旧绑定: name={}", name);
        UnregisterHotKey(m_hwnd, m_hotkeys[name].id);
        m_idToName.erase(m_hotkeys[name].id);
        m_hotkeys.erase(name);
    }

    int id = generateId();
    UINT mods = static_cast<UINT>(def.modifiers) | MOD_NOREPEAT;

    if (!RegisterHotKey(m_hwnd, id, mods, def.virtualKey)) {
        DWORD err = GetLastError();
        LOG_ERROR("注册快捷键失败: name={}, def={}, error={}", name, def.toString(), err);
        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            LOG_ERROR("快捷键 {} 已被其他程序占用", def.toString());
        }
        return false;
    }

    HotkeyEntry entry{id, def, name, std::move(callback)};
    m_hotkeys[name] = std::move(entry);
    m_idToName[id] = name;

    LOG_INFO("注册快捷键成功: name={}, def={}, id={}", name, def.toString(), id);
    return true;
}

void HotkeyManager::unregisterHotkey(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) {
        LOG_WARN("尝试注销不存在的快捷键: name={}", name);
        return;
    }
    UnregisterHotKey(m_hwnd, it->second.id);
    m_idToName.erase(it->second.id);
    m_hotkeys.erase(it);
    LOG_INFO("已注销快捷键: name={}", name);
}

bool HotkeyManager::rebindHotkey(const std::string& name, const HotkeyDef& newDef) {
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) {
        LOG_WARN("尝试重绑定不存在的快捷键: name={}", name);
        return false;
    }

    // 先注销旧的
    UnregisterHotKey(m_hwnd, it->second.id);

    // 注册新的
    UINT mods = static_cast<UINT>(newDef.modifiers) | MOD_NOREPEAT;
    if (!RegisterHotKey(m_hwnd, it->second.id, mods, newDef.virtualKey)) {
        LOG_ERROR("重绑定快捷键失败: name={}, newDef={}", name, newDef.toString());
        // 尝试恢复旧绑定
        UINT oldMods = static_cast<UINT>(it->second.def.modifiers) | MOD_NOREPEAT;
        RegisterHotKey(m_hwnd, it->second.id, oldMods, it->second.def.virtualKey);
        return false;
    }

    LOG_INFO("快捷键重绑定成功: name={}, {} → {}", name, it->second.def.toString(), newDef.toString());
    it->second.def = newDef;
    return true;
}

bool HotkeyManager::isConflict(const HotkeyDef& def) const {
    std::lock_guard lock(m_mutex);
    for (const auto& [name, entry] : m_hotkeys) {
        if (entry.def == def) return true;
    }
    return false;
}

void HotkeyManager::handleHotkeyMessage(WPARAM wParam) {
    int id = static_cast<int>(wParam);
    std::string name;
    HotkeyCallback callback;

    {
        std::lock_guard lock(m_mutex);
        auto it = m_idToName.find(id);
        if (it == m_idToName.end()) return;

        name = it->second;
        auto entryIt = m_hotkeys.find(name);
        if (entryIt == m_hotkeys.end()) return;

        callback = entryIt->second.callback;
    }

    LOG_DEBUG("快捷键触发: name={}", name);

    if (callback) {
        TraceId::Scope scope;
        try {
            callback();
        } catch (const std::exception& e) {
            LOG_ERROR("快捷键回调异常: name={}, error={}", name, e.what());
        }
    }
}

std::vector<HotkeyEntry> HotkeyManager::getAllHotkeys() const {
    std::lock_guard lock(m_mutex);
    std::vector<HotkeyEntry> result;
    result.reserve(m_hotkeys.size());
    for (const auto& [name, entry] : m_hotkeys) {
        result.push_back(entry);
    }
    return result;
}

int HotkeyManager::generateId() {
    return m_nextId.fetch_add(1);
}

}  // namespace easy::core
