// ─────────────────────────────────────────────────────────────────────────────
// HotkeyManager.cpp — 全局快捷键管理器实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/hotkey/HotkeyManager.h"
#include "core/hotkey/HotkeyPolicy.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <memory>

namespace easy::core {

namespace {

constexpr auto HotkeyDispatchTimeout = std::chrono::seconds(10);

bool isForeignHotkeyThread(DWORD ownerThreadId) noexcept {
    return ownerThreadId != 0 && ownerThreadId != GetCurrentThreadId();
}

template <typename Result, typename Operation>
Result dispatchToHotkeyThread(DWORD ownerThreadId,
                              const char* operationName,
                              Result failureResult,
                              Operation&& operation) {
    struct DispatchState {
        std::promise<Result> promise;
        std::atomic<bool> cancelled{false};
    };

    auto state = std::make_shared<DispatchState>();
    auto future = state->promise.get_future();
    const bool posted = MainThreadDispatcher::instance().post(
        [state, ownerThreadId, operationName,
         operation = std::forward<Operation>(operation), failureResult]() mutable {
            if (state->cancelled.load(std::memory_order_acquire)) return;
            if (GetCurrentThreadId() != ownerThreadId) {
                LOG_ERROR("快捷键操作派发到了错误线程: operation={}, expected={}, actual={}",
                          operationName, ownerThreadId, GetCurrentThreadId());
                state->promise.set_value(std::move(failureResult));
                return;
            }
            try {
                state->promise.set_value(operation());
            } catch (const std::exception& e) {
                LOG_ERROR("快捷键主线程操作异常: operation={}, error={}", operationName, e.what());
                state->promise.set_value(std::move(failureResult));
            } catch (...) {
                LOG_ERROR("快捷键主线程操作未知异常: operation={}", operationName);
                state->promise.set_value(std::move(failureResult));
            }
        });
    if (!posted) {
        LOG_ERROR("快捷键操作无法派发到窗口线程: operation={}", operationName);
        return failureResult;
    }
    if (future.wait_for(HotkeyDispatchTimeout) != std::future_status::ready) {
        state->cancelled.store(true, std::memory_order_release);
        LOG_ERROR("快捷键操作等待窗口线程超时: operation={}", operationName);
        return failureResult;
    }
    return future.get();
}

template <typename Operation>
void dispatchToHotkeyThread(DWORD ownerThreadId,
                            const char* operationName,
                            Operation&& operation) {
    (void)dispatchToHotkeyThread<bool>(
        ownerThreadId, operationName, false,
        [operation = std::forward<Operation>(operation)]() mutable {
            operation();
            return true;
        });
}

}  // namespace

// ── HotkeyDef 序列化 ────────────────────────────────────────────────────────

std::string HotkeyDef::toString() const {
    if (virtualKey == 0) return {};
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
    DWORD ownerThreadId = 0;
    if (messageWindow) ownerThreadId = GetWindowThreadProcessId(messageWindow, nullptr);
    if (ownerThreadId == 0) ownerThreadId = GetCurrentThreadId();
    m_ownerThreadId.store(ownerThreadId, std::memory_order_release);
    LOG_INFO("快捷键管理器已初始化");
}

void HotkeyManager::shutdown() {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        dispatchToHotkeyThread(ownerThreadId, "shutdown", [this]() { shutdown(); });
        return;
    }
    std::lock_guard lock(m_mutex);
    for (const auto& [name, entry] : m_hotkeys) {
        if (entry.registered) UnregisterHotKey(m_hwnd, entry.id);
        LOG_DEBUG("注销快捷键: name={}, def={}", name, entry.def.toString());
    }
    m_hotkeys.clear();
    m_idToName.clear();
    m_hwnd = nullptr;
    m_ownerThreadId.store(0, std::memory_order_release);
    LOG_INFO("快捷键管理器已关闭, 所有快捷键已注销");
}

bool HotkeyManager::registerHotkey(const std::string& name, const HotkeyDef& def, HotkeyCallback callback) {
    TraceId::Scope scope;
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        return dispatchToHotkeyThread<bool>(
            ownerThreadId, "registerHotkey", false,
            [this, name, def, callback = std::move(callback)]() mutable {
                return registerHotkey(name, def, std::move(callback));
            });
    }
    std::lock_guard lock(m_mutex);

    // 检查名称冲突
    if (m_hotkeys.contains(name)) {
        LOG_WARN("快捷键名称已存在, 将先注销旧绑定: name={}", name);
        if (m_hotkeys[name].registered) UnregisterHotKey(m_hwnd, m_hotkeys[name].id);
        m_idToName.erase(m_hotkeys[name].id);
        m_hotkeys.erase(name);
    }

    int id = generateId();
    if (def.virtualKey == 0) {
        m_hotkeys.emplace(name, HotkeyEntry{id, def, name, std::move(callback), false, true});
        LOG_INFO("快捷键已禁用: name={}", name);
        return true;
    }
    UINT mods = static_cast<UINT>(def.modifiers) | MOD_NOREPEAT;

    const bool registered = RegisterHotKey(m_hwnd, id, mods, def.virtualKey) != FALSE;
    HotkeyEntry entry{id, def, name, std::move(callback), registered, true};
    m_hotkeys[name] = std::move(entry);
    if (!registered) {
        DWORD err = GetLastError();
        LOG_ERROR("注册快捷键失败: name={}, def={}, error={}", name, def.toString(), err);
        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            LOG_ERROR("快捷键 {} 已被其他程序占用", def.toString());
        }
        // Retain the entry and callback. The settings UI can now display the
        // conflict and let the user choose a different combination at runtime.
        return false;
    }

    m_idToName[id] = name;

    LOG_INFO("注册快捷键成功: name={}, def={}, id={}", name, def.toString(), id);
    return true;
}

void HotkeyManager::unregisterHotkey(const std::string& name) {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        dispatchToHotkeyThread(ownerThreadId, "unregisterHotkey",
                               [this, name]() { unregisterHotkey(name); });
        return;
    }
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) {
        LOG_WARN("尝试注销不存在的快捷键: name={}", name);
        return;
    }
    if (it->second.registered) UnregisterHotKey(m_hwnd, it->second.id);
    m_idToName.erase(it->second.id);
    m_hotkeys.erase(it);
    LOG_INFO("已注销快捷键: name={}", name);
}

bool HotkeyManager::rebindHotkey(const std::string& name, const HotkeyDef& newDef) {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        return dispatchToHotkeyThread<bool>(
            ownerThreadId, "rebindHotkey", false,
            [this, name, newDef]() { return rebindHotkey(name, newDef); });
    }
    if (newDef.virtualKey == 0) return clearHotkey(name);
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) {
        LOG_WARN("尝试重绑定不存在的快捷键: name={}", name);
        return false;
    }

    const auto oldDef = it->second.def;
    const bool oldRegistered = it->second.registered;
    const bool armed = it->second.armed;
    if (oldRegistered) {
        UnregisterHotKey(m_hwnd, it->second.id);
        m_idToName.erase(it->second.id);
        it->second.registered = false;
    }

    if (!armed) {
        it->second.def = newDef;
        it->second.registered = false;
        LOG_INFO("快捷键重绑定成功（会话外不占用）: name={}, {} → {}",
                 name, oldDef.toString(), newDef.toString());
        return true;
    }

    // 注册新的
    UINT mods = static_cast<UINT>(newDef.modifiers) | MOD_NOREPEAT;
    if (!RegisterHotKey(m_hwnd, it->second.id, mods, newDef.virtualKey)) {
        LOG_ERROR("重绑定快捷键失败: name={}, newDef={}", name, newDef.toString());
        it->second.registered = false;
        if (oldRegistered && oldDef.virtualKey != 0) {
            UINT oldMods = static_cast<UINT>(oldDef.modifiers) | MOD_NOREPEAT;
            if (RegisterHotKey(m_hwnd, it->second.id, oldMods, oldDef.virtualKey)) {
                it->second.registered = true;
                m_idToName[it->second.id] = name;
            }
        }
        return false;
    }

    LOG_INFO("快捷键重绑定成功: name={}, {} → {}", name, it->second.def.toString(), newDef.toString());
    it->second.def = newDef;
    it->second.registered = true;
    m_idToName[it->second.id] = name;
    return true;
}

bool HotkeyManager::clearHotkey(const std::string& name) {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        return dispatchToHotkeyThread<bool>(
            ownerThreadId, "clearHotkey", false,
            [this, name]() { return clearHotkey(name); });
    }
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) return false;
    if (it->second.registered) UnregisterHotKey(m_hwnd, it->second.id);
    m_idToName.erase(it->second.id);
    it->second.def = {};
    it->second.registered = false;
    LOG_INFO("快捷键已禁用: name={}", name);
    return true;
}

bool HotkeyManager::setHotkeyArmed(const std::string& name, bool armed) {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        return dispatchToHotkeyThread<bool>(
            ownerThreadId, "setHotkeyArmed", false,
            [this, name, armed]() { return setHotkeyArmed(name, armed); });
    }
    std::lock_guard lock(m_mutex);
    auto it = m_hotkeys.find(name);
    if (it == m_hotkeys.end()) {
        LOG_WARN("尝试武装不存在的快捷键: name={}", name);
        return false;
    }

    it->second.armed = armed;
    if (it->second.def.virtualKey == 0) {
        it->second.registered = false;
        return true;
    }

    if (!armed) {
        if (it->second.registered) {
            if (m_hwnd) UnregisterHotKey(m_hwnd, it->second.id);
            m_idToName.erase(it->second.id);
            it->second.registered = false;
            LOG_INFO("快捷键已卸下，交还给前台应用: name={}, def={}", name, it->second.def.toString());
        }
        return true;
    }

    if (it->second.registered) return true;
    if (!m_hwnd) {
        it->second.registered = false;
        return false;
    }

    UINT mods = static_cast<UINT>(it->second.def.modifiers) | MOD_NOREPEAT;
    const bool ok = RegisterHotKey(m_hwnd, it->second.id, mods, it->second.def.virtualKey) != FALSE;
    it->second.registered = ok;
    if (ok) {
        m_idToName[it->second.id] = name;
        LOG_INFO("快捷键已重新占用: name={}, def={}", name, it->second.def.toString());
    } else {
        LOG_ERROR("重新占用快捷键失败: name={}, def={}, error={}",
                  name, it->second.def.toString(), GetLastError());
    }
    return ok;
}

bool HotkeyManager::isConflict(const HotkeyDef& def) const {
    std::lock_guard lock(m_mutex);
    for (const auto& [name, entry] : m_hotkeys) {
        if (entry.registered && def.virtualKey != 0 && entry.def == def) return true;
    }
    return false;
}

HotkeyConflictInfo HotkeyManager::checkConflict(const HotkeyDef& def, const std::string& currentName) const {
    const DWORD ownerThreadId = m_ownerThreadId.load(std::memory_order_acquire);
    if (isForeignHotkeyThread(ownerThreadId)) {
        return dispatchToHotkeyThread<HotkeyConflictInfo>(
            ownerThreadId, "checkConflict",
            HotkeyConflictInfo{true, "unavailable", "无法在窗口线程检查快捷键冲突"},
            [this, def, currentName]() { return checkConflict(def, currentName); });
    }
    if (def.virtualKey == 0) {
        return {false, "none", ""};
    }

    std::lock_guard lock(m_mutex);

    // 1. 优先检查内部插件与功能冲突
    for (const auto& [name, entry] : m_hotkeys) {
        if (name != currentName && entry.def.virtualKey != 0 && entry.def == def) {
            return {true, "internal", "与 [" + name + "] 冲突"};
        }
    }

    // 2. 检查系统/第三方外部软件冲突 (通过试探性注册探针)
    if (m_hwnd && IsWindow(m_hwnd)) {
        UINT mods = static_cast<UINT>(def.modifiers) | MOD_NOREPEAT;
        constexpr int PROBE_HOTKEY_ID = 0xBEEF;
        if (!RegisterHotKey(m_hwnd, PROBE_HOTKEY_ID, mods, def.virtualKey)) {
            DWORD err = GetLastError();
            if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
                return {true, "external", "已被第三方应用程序或系统快捷键占用"};
            }
        } else {
            UnregisterHotKey(m_hwnd, PROBE_HOTKEY_ID);
        }
    }

    return {false, "none", ""};
}

void HotkeyManager::handleHotkeyMessage(WPARAM wParam) {
    if (m_paused.load(std::memory_order_relaxed)) {
        LOG_DEBUG("快捷键已暂停触发 (录制模式中)");
        return;
    }
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

    // 统计各快捷键出现的次数以判断内部冲突
    std::unordered_map<std::string, std::vector<std::string>> keyUsage;
    for (const auto& [name, entry] : m_hotkeys) {
        if (entry.def.virtualKey != 0) {
            keyUsage[entry.def.toString()].push_back(name);
        }
    }

    for (const auto& [name, entry] : m_hotkeys) {
        HotkeyEntry item = entry;
        if (item.def.virtualKey == 0) {
            item.conflict = false;
            item.conflictType = "none";
            item.conflictWith = "";
        } else {
            const auto& users = keyUsage[item.def.toString()];
            if (users.size() > 1) {
                // 内部冲突 (多个插件绑定了同一热键)
                item.conflict = true;
                item.conflictType = "internal";
                std::string others;
                for (const auto& u : users) {
                    if (u != name) {
                        if (!others.empty()) others += ", ";
                        others += u;
                    }
                }
                item.conflictWith = "与插件/功能 [" + others + "] 冲突";
            } else if (hotkeyLooksExternallyConflicted(
                           item.def.virtualKey != 0, item.armed, item.registered)) {
                // 外部冲突 (被其它第三方软件占用)
                item.conflict = true;
                item.conflictType = "external";
                item.conflictWith = "已被第三方软件或系统快捷键占用";
            } else {
                item.conflict = false;
                item.conflictType = "none";
                item.conflictWith = "";
            }
        }
        result.push_back(item);
    }
    return result;
}

int HotkeyManager::generateId() {
    return m_nextId.fetch_add(1);
}

}  // namespace easy::core
