#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BuiltinCommandDispatcher — 内置命令分发器
//
// 设计目标: 让 gesture 模块在执行 BuiltinCommand 时既能完成窗口管理类操作
// (最大化/最小化/关闭/锁屏/虚拟桌面…)，又能触发应用级功能 (截图/录屏/暂停手势)
// 而 **不反向依赖** capture / tray 等高层模块。
//
//   • 窗口管理类命令: 直接用 Win32 实现，作用于当前前台窗口。
//   • 应用级命令: 通过 main.cpp 在启动时注册的回调 (Handler) 路由，
//     从而保持 gesture → capture/tray 的单向依赖。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_BUILTINCOMMANDS_H
#define EASYTOOLS_GESTURE_BUILTINCOMMANDS_H

#include "gesture/GestureAction.h"

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace easy::gesture {

class BuiltinCommandDispatcher {
public:
    using Handler = std::function<void()>;

    static BuiltinCommandDispatcher& instance();

    /// 注册应用级命令的回调 (由 main.cpp 在初始化时调用)。
    /// 例如 TakeScreenshot / StartRecording / PauseGestures。
    void registerHandler(BuiltinCommand cmd, Handler handler);

    /// 清理所有应用级命令回调。调用方必须先停止手势动作线程。
    void clearHandlers();

    /// 执行一条内置命令。窗口管理类命令优先作用于鼠标下方的目标窗口。
    /// 线程安全: 仅读取 m_handlers (注册发生在启动阶段，执行发生在手势线程)。
    void execute(BuiltinCommand cmd, void* targetWindow = nullptr) const;

private:
    BuiltinCommandDispatcher() = default;
    BuiltinCommandDispatcher(const BuiltinCommandDispatcher&) = delete;
    BuiltinCommandDispatcher& operator=(const BuiltinCommandDispatcher&) = delete;

    /// 通过已注册的 Handler 触发应用级命令; 未注册时记录警告。返回是否已处理。
    bool dispatchAppCommand(BuiltinCommand cmd) const;

    std::unordered_map<BuiltinCommand, Handler> m_handlers;
    mutable std::shared_mutex m_mutex;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_BUILTINCOMMANDS_H
