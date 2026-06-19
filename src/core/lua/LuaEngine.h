#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// LuaEngine — Lua 脚本运行时
//
// 职责:
//   1. 管理 Lua (sol2) 虚拟机状态
//   2. 注入 C++ API 供 Lua 脚本调用 (easyTools.*)
//   3. 安全地执行脚本字符串，捕获异常
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_LUAENGINE_H
#define EASYTOOLS_CORE_LUAENGINE_H

#include <sol/sol.hpp>
#include <string>
#include <memory>

namespace easy::core {

class LuaEngine {
public:
    static LuaEngine& instance();

    /// 初始化虚拟机并绑定 API
    bool initialize();

    /// 执行一段 Lua 脚本
    /// \param script Lua 源码
    /// \return 成功返回 true，失败返回 false 并记录错误日志
    bool executeScript(const std::string& script);

private:
    LuaEngine() = default;
    ~LuaEngine() = default;

    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    void bindApi();

    std::unique_ptr<sol::state> m_lua;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_LUAENGINE_H
