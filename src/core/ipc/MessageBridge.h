#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MessageBridge — WebView2 ↔ C++ IPC 桥接层
//
// 职责:
//   1. 定义 JS ↔ C++ 通信协议（JSON-RPC 风格）
//   2. 路由前端请求到正确的 C++ 处理器
//   3. 推送 C++ 事件到前端
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_IPC_MESSAGEBRIDGE_H
#define EASYTOOLS_CORE_IPC_MESSAGEBRIDGE_H

#include "core/utils/Export.h"

#include <string>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace easy::core {

using json = nlohmann::json;

/// 消息处理器类型: 接收请求参数，返回响应
using MessageHandler = std::function<json(const json& params)>;

/// 事件推送器类型: 由 WebView2 层实现，用于向 JS 发送消息
using EventPusher = std::function<void(const std::string& eventName, const json& data)>;

class EASYCORE_API MessageBridge {
public:
    static MessageBridge& instance();

    // ── 注册处理器 (C++ 侧) ──────────────────────────────────────────────

    /// 注册消息处理器
    /// @param method 方法名 (如 "gesture.getProfiles", "config.get")
    void registerHandler(const std::string& method, MessageHandler handler);

    // ── 消息处理 ─────────────────────────────────────────────────────────

    /// 处理来自前端的消息
    /// @param messageJson JSON-RPC 风格消息: {"id": 1, "method": "xxx", "params": {...}}
    /// @return 响应 JSON: {"id": 1, "result": {...}} 或 {"id": 1, "error": {...}}
    std::string handleMessage(const std::string& messageJson);

    // ── 事件推送 (C++ → JS) ─────────────────────────────────────────────

    /// 设置事件推送器（由 WebView2 初始化时调用）
    void setEventPusher(EventPusher pusher);

    /// 向前端推送事件
    void pushEvent(const std::string& eventName, const json& data = {});

    // ── 预置处理器注册 ──────────────────────────────────────────────────

    /// 注册所有内置处理器（配置、手势、截图等）
    void registerBuiltinHandlers();

private:
    MessageBridge() = default;

    std::unordered_map<std::string, MessageHandler> m_handlers;
    EventPusher m_eventPusher;
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_IPC_MESSAGEBRIDGE_H
