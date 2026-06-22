#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// LuaEngine — Lua 脚本运行时
//
// 职责:
//   1. 管理 Lua (sol2) 虚拟机状态
//   2. 注入 C++ API 供 Lua 脚本调用 (easy.* 命名空间，对齐需求文档)
//   3. 安全地执行脚本字符串/文件，捕获异常
//   4. 轻量沙箱: 不开放 io 库，os 仅暴露白名单函数
//
// 线程模型:
//   sol::state 非线程安全。手势线程与设置界面线程都可能触发脚本执行，
//   因此所有 execute* 入口用一把互斥锁串行化。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_LUAENGINE_H
#define EASYTOOLS_CORE_LUAENGINE_H

#include "core/utils/Export.h"

#include <sol/sol.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace easy::core {

class EASYCORE_API LuaEngine {
public:
    static LuaEngine& instance();

    /// 初始化虚拟机并绑定 API。可重复调用 (重新构建干净的状态)。
    bool initialize();

    /// 释放虚拟机。
    void shutdown();

    /// 执行一段 Lua 源码。成功返回 true；失败记录错误日志并返回 false。
    bool executeScript(const std::string& script);

    /// 执行磁盘上的 Lua 脚本文件 (UTF-8 路径)。
    bool executeFile(const std::string& utf8Path);

private:
    LuaEngine() = default;
    ~LuaEngine() = default;
    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    // 在锁内执行已加载/编译好的可调用对象，统一错误处理。
    bool runProtected(const std::function<sol::protected_function_result()>& fn,
                      const char* context);

    // ── API 绑定 (按命名空间拆分，便于维护) ─────────────────────────────────
    void bindApi();
    void bindLog(sol::table& easy);
    void bindKeyboard(sol::table& easy);
    void bindMouse(sol::table& easy);
    void bindClipboard(sol::table& easy);
    void bindShell(sol::table& easy);
    void bindWindow(sol::table& easy);
    void bindScreen(sol::table& easy);
    void bindFs(sol::table& easy);
    void bindHttp(sol::table& easy);
    void bindUi(sol::table& easy);
    void bindUrl(sol::table& easy);

    std::unique_ptr<sol::state> m_lua;
    std::mutex m_mutex;  // 串行化所有脚本执行
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_LUAENGINE_H
