#pragma once

#ifndef EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H
#define EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H

#include "core/utils/Export.h"
#include "core/plugin/IPlugin.h"
#include "core/plugin/PluginManifest.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace easy::core {

struct PluginInstance {
    std::string id;
    std::string name;
    std::string version;
    std::string fileName;
    std::filesystem::path path;
    PluginManifest manifest;
    IPlugin* plugin = nullptr;
    bool configuredEnabled = true;
    bool enabledAtLaunch = true;
    bool initialized = false;
    std::string error;
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
};

/// 面向 UI/诊断接口的只读插件快照，不暴露 DLL 句柄和插件对象指针。
struct PluginStatus {
    std::string id;
    std::string name;
    std::string version;
    std::string fileName;
    std::uint32_t abiVersion = 0;
    std::vector<std::string> capabilities;
    std::vector<std::string> permissions;
    std::string executionModel;
    bool enabled = true;
    bool active = false;
    bool restartRequired = false;
    std::string state;
    std::string error;
};

class EASYCORE_API PluginManager {
public:
    static PluginManager& instance();

    /// 从指定目录扫描并加载所有以 Plugin_ 开头的 DLL
    bool loadPlugins(const std::string& directory);

    /// 初始化所有已加载的插件
    void initializePlugins();

    /// 卸载所有插件
    void shutdownPlugins();

    /// 返回稳定排序的插件运行状态快照。
    std::vector<PluginStatus> getPluginStatuses() const;

    /// 持久化插件开关。为保证 DLL 回调与线程彻底释放，变更在下次启动生效。
    bool setPluginEnabled(const std::string& id, bool enabled, bool& restartRequired,
                          std::string& error);

    /// 查询插件本次启动时是否获准加载。
    bool isEnabledAtLaunch(const std::string& id) const;

private:
    PluginManager() = default;
    ~PluginManager() = default;

    mutable std::mutex m_mutex;
    bool m_shuttingDown = false;
    std::vector<PluginInstance> m_plugins;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H
