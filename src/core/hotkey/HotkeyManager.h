#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HotkeyManager — 全局快捷键管理器
//
// 特性:
//   1. 统一注册/注销全局快捷键（RegisterHotKey）
//   2. 快捷键冲突检测（注册失败时给出明确原因）
//   3. 快捷键 → 回调映射（支持 lambda / std::function）
//   4. 支持运行时动态修改快捷键绑定
//   5. 自动处理 WM_HOTKEY 消息分发
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_HOTKEY_HOTKEYMANAGER_H
#define EASYTOOLS_CORE_HOTKEY_HOTKEYMANAGER_H

#include "core/utils/Export.h"

#include <windows.h>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <optional>

namespace easy::core {

/// 修饰键枚举（可组合使用）
enum class ModKey : UINT {
    None  = 0,
    Alt   = MOD_ALT,
    Ctrl  = MOD_CONTROL,
    Shift = MOD_SHIFT,
    Win   = MOD_WIN
};

inline ModKey operator|(ModKey a, ModKey b) {
    return static_cast<ModKey>(static_cast<UINT>(a) | static_cast<UINT>(b));
}

/// 快捷键定义
struct HotkeyDef {
    ModKey modifiers = ModKey::None;
    UINT virtualKey  = 0;            // VK_xxx 虚拟键码

    /// 人类可读的字符串表示 (如 "Ctrl+Shift+A")
    std::string toString() const;

    /// 从字符串解析 (如 "Ctrl+Shift+A")
    static std::optional<HotkeyDef> fromString(const std::string& str);

    bool operator==(const HotkeyDef& other) const {
        return modifiers == other.modifiers && virtualKey == other.virtualKey;
    }
};

using HotkeyCallback = std::function<void()>;

/// 已注册的快捷键信息
struct HotkeyEntry {
    int id;                     // RegisterHotKey 的 ID
    HotkeyDef def;              // 快捷键定义
    std::string name;           // 人类可读名称 (如 "截图", "暂停手势")
    HotkeyCallback callback;    // 触发回调
};

class EASYCORE_API HotkeyManager {
public:
    static HotkeyManager& instance();

    /// 初始化（需要传入消息窗口的 HWND）
    void initialize(HWND messageWindow);

    /// 关闭（注销所有快捷键）
    void shutdown();

    /// 注册快捷键
    /// @return true=注册成功, false=快捷键冲突或注册失败
    bool registerHotkey(const std::string& name, const HotkeyDef& def, HotkeyCallback callback);

    /// 注销快捷键
    void unregisterHotkey(const std::string& name);

    /// 修改已注册快捷键的绑定
    bool rebindHotkey(const std::string& name, const HotkeyDef& newDef);

    /// 检查快捷键是否已被占用
    bool isConflict(const HotkeyDef& def) const;

    /// 处理 WM_HOTKEY 消息（在消息循环中调用）
    void handleHotkeyMessage(WPARAM wParam);

    /// 获取所有已注册快捷键（用于 UI 显示）
    std::vector<HotkeyEntry> getAllHotkeys() const;

private:
    HotkeyManager() = default;
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    int generateId();

    HWND m_hwnd = nullptr;
    std::unordered_map<std::string, HotkeyEntry> m_hotkeys;  // name → entry
    std::unordered_map<int, std::string> m_idToName;         // id → name (反向查找)
    mutable std::mutex m_mutex;
    std::atomic<int> m_nextId{1};
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_HOTKEY_HOTKEYMANAGER_H
