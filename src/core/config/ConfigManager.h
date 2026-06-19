#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ConfigManager — 配置管理器
//
// 特性:
//   1. JSON 格式持久化（基于 nlohmann::json）
//   2. 文件变更监控 + 热加载（ReadDirectoryChangesW）
//   3. 类型安全的 get/set 接口
//   4. 默认值回退机制
//   5. 配置变更通知（观察者模式）
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_CONFIG_CONFIGMANAGER_H
#define EASYTOOLS_CORE_CONFIG_CONFIGMANAGER_H

#include <string>
#include <string_view>
#include <filesystem>
#include <mutex>
#include <functional>
#include <vector>
#include <optional>
#include <atomic>
#include <thread>

#include <nlohmann/json.hpp>

namespace easy::core {

using json = nlohmann::json;
using ConfigChangeCallback = std::function<void(const std::string& key)>;

class ConfigManager {
public:
    /// 获取单例实例
    static ConfigManager& instance();

    /// 初始化配置管理器
    /// @param configDir 配置文件所在目录
    /// @param filename 配置文件名（默认 "config.json"）
    void initialize(const std::filesystem::path& configDir, const std::string& filename = "config.json");

    /// 关闭（停止文件监控线程）
    void shutdown();

    // ── 类型安全的读写接口 ───────────────────────────────────────────────

    /// 获取配置值（支持 JSON Pointer 路径，如 "/gesture/trigger/button"）
    /// @tparam T 目标类型
    /// @param key JSON Pointer 路径
    /// @param defaultValue 当 key 不存在时的回退值
    template <typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const {
        std::lock_guard lock(m_mutex);
        try {
            auto ptr = json::json_pointer(key);
            if (m_config.contains(ptr)) {
                return m_config.at(ptr).get<T>();
            }
        } catch (...) {
            // JSON Pointer 解析失败或类型不匹配，返回默认值
        }
        return defaultValue;
    }

    /// 设置配置值并自动保存到文件
    template <typename T>
    void set(const std::string& key, const T& value) {
        {
            std::lock_guard lock(m_mutex);
            auto ptr = json::json_pointer(key);
            m_config[ptr] = value;
        }
        save();
        notifyChange(key);
    }

    /// 检查 key 是否存在
    bool has(const std::string& key) const;

    /// 删除指定 key
    void remove(const std::string& key);

    /// 获取完整配置的 JSON 字符串（用于 IPC 传输给前端）
    std::string toJsonString(int indent = 2) const;

    /// 从 JSON 字符串批量更新（用于前端 IPC 回写）
    void fromJsonString(const std::string& jsonStr);

    // ── 配置变更通知 ─────────────────────────────────────────────────────

    /// 注册变更回调（返回回调 ID，用于取消注册）
    size_t onChange(ConfigChangeCallback callback);

    /// 取消注册变更回调
    void removeOnChange(size_t callbackId);

    // ── 导入/导出 ────────────────────────────────────────────────────────

    /// 导出配置到文件
    bool exportTo(const std::filesystem::path& filePath) const;

    /// 从文件导入配置（合并模式，不会清除未包含的 key）
    bool importFrom(const std::filesystem::path& filePath);

    ~ConfigManager();

private:
    ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /// 从文件加载配置
    void load();

    /// 保存配置到文件
    void save() const;

    /// 通知所有观察者
    void notifyChange(const std::string& key);

    /// 文件变更监控线程函数
    void watchFileChanges();

    json m_config;
    std::filesystem::path m_configFilePath;
    mutable std::mutex m_mutex;

    // 变更通知
    std::vector<std::pair<size_t, ConfigChangeCallback>> m_callbacks;
    std::atomic<size_t> m_nextCallbackId{0};

    // 文件监控
    std::thread m_watchThread;
    std::atomic<bool> m_watchRunning{false};
    HANDLE m_watchStopEvent = nullptr;
};

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_CONFIG_CONFIGMANAGER_H
