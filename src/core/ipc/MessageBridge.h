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
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
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

    /// 注销一个处理器，并等待已经进入该处理器的调用结束。
    void unregisterHandler(const std::string& method);

    /// 按命名空间前缀注销处理器（如 "capture."），并等待在途调用结束。
    size_t unregisterHandlersByPrefix(const std::string& prefix);

    // ── 消息处理 ─────────────────────────────────────────────────────────

    /// 处理来自前端的消息
    /// @param messageJson JSON-RPC 风格消息: {"id": 1, "method": "xxx", "params": {...}}
    /// @return 响应 JSON: {"id": 1, "result": {...}} 或 {"id": 1, "error": {...}}
    std::string handleMessage(const std::string& messageJson);

    // ── 事件推送 (C++ → JS) ─────────────────────────────────────────────

    /// 设置事件推送器（由 WebView2 初始化时调用）
    void setEventPusher(EventPusher pusher);

    /// 清空全部处理器和事件推送器。插件 DLL 卸载前必须调用，避免遗留
    /// std::function 的析构代码指向已经卸载的模块。
    void clearHandlers();

    /// 向前端推送事件
    void pushEvent(const std::string& eventName, const json& data = {});

    // ── 预置处理器注册 ──────────────────────────────────────────────────

    /// 注册所有内置处理器（配置、手势、截图等）
    void registerBuiltinHandlers();

private:
    MessageBridge() = default;

    struct HandlerSlot {
        MessageHandler handler;
        std::mutex mutex;
        std::condition_variable idle;
        size_t activeCalls = 0;
        bool accepting = true;
    };

    static void retireSlots(std::vector<std::shared_ptr<HandlerSlot>> slots);

    std::unordered_map<std::string, std::shared_ptr<HandlerSlot>> m_handlers;
    EventPusher m_eventPusher;
    mutable std::shared_mutex m_mutex;
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_IPC_MESSAGEBRIDGE_H
