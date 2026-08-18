#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureAction — 手势动作抽象
//
// 定义手势触发后执行什么动作:
//   - 发送快捷键 (SendInput)
//   - 运行 Lua 脚本
//   - 执行内置命令 (关闭窗口/最大化/最小化等)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_GESTUREACTION_H
#define EASYTOOLS_GESTURE_GESTUREACTION_H

#include <string>
#include <variant>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace easy::gesture {

/// 动作类型
enum class ActionType {
    SendKeys,       // 发送按键组合 (如 Ctrl+C)
    LuaScript,      // 执行 Lua 脚本
    BuiltinCommand, // 内置命令
    RunProgram      // 运行外部程序
};

/// 内置命令枚举
enum class BuiltinCommand {
    CloseWindow,            // 关闭窗口
    CloseTab,               // 关闭标签页
    MaximizeWindow,         // 最大化
    MinimizeWindow,         // 最小化
    RestoreWindow,          // 还原窗口
    ShowDesktop,            // 显示桌面
    SwitchDesktop,          // 切换虚拟桌面
    TaskView,               // 任务视图
    LockScreen,             // 锁屏
    PauseGestures,          // 暂停手势
    TakeScreenshot,         // 截图
    StartRecording,         // 开始录屏
    RestoreClosedTab,       // 恢复最近关闭的标签页
    ToggleTopmost,          // 窗口置顶切换
    ToggleWindowTransparency, // 窗口透明度切换
    WebSearch,              // 选中文本直接网页搜索
    ToggleSearch,           // 切换搜索框
    ShowRadialMenu,         // 显示呼出轮盘
    PasteAsPin,             // 将剪贴板内容作为贴图显示
    MediaNext,              // 下一曲 (全局多媒体)
    MediaPrev,              // 上一曲 (全局多媒体)
    MediaPlayPause,         // 播放 / 暂停 (全局多媒体)
    VolumeUp,               // 音量增加 (全局多媒体)
    VolumeDown,             // 音量减小 (全局多媒体)
    VolumeMute              // 静音切换 (全局多媒体)
};

/// 按键定义 (用于 SendKeys 类型)
struct KeyStroke {
    uint8_t modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN 的组合
    uint16_t virtualKey = 0; // VK_xxx

    /// 从字符串解析 (如 "Ctrl+C")
    static KeyStroke fromString(const std::string& str);
    std::string toString() const;

    /// 通过 SendInput 合成这组按键 (按下修饰键 → 主键 → 逆序释放)
    /// 若提供了 targetWindow 且非全局多媒体键，则先将输入焦点精准切换至目标窗口。
    void send(void* targetWindow = nullptr) const;
};

/// 手势动作定义
struct GestureAction {
    ActionType type;
    std::string name;                // 人类可读名称 (如 "复制", "粘贴")
    std::string description;         // 描述

    // 根据 type 使用不同字段:
    KeyStroke keyStroke;             // type == SendKeys
    std::string luaScript;           // type == LuaScript (Lua 代码或脚本文件路径)
    std::vector<std::string> requestedPermissions; // type == LuaScript (声明的细粒度权限列表)
    BuiltinCommand builtinCmd;       // type == BuiltinCommand
    std::string programPath;         // type == RunProgram
    std::string programArgs;         // type == RunProgram

    /// 执行动作 (可传入鼠标下方的目标窗口句柄 HWND)
    void execute(void* targetWindow = nullptr) const;

    /// 序列化/反序列化
    nlohmann::json toJson() const;
    static GestureAction fromJson(const nlohmann::json& j);
};

/// 手势映射条目: 方向编码 → 动作
struct GestureMapping {
    std::string id;                  // 唯一标识 (若为空可由 gestureCode 生成)
    bool enabled = true;             // 单项手势启用/禁用
    bool instantExecute = false;     // 识别到方向时立即执行 (无需等待松开按键)
    bool silentToast = false;        // 执行时不显示手势名称卡片
    std::string gestureCode;         // 方向编码 (如 "L", "DR", "L-U-R")
    GestureAction action;

    nlohmann::json toJson() const;
    static GestureMapping fromJson(const nlohmann::json& j);
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREACTION_H
