#pragma once

#ifndef EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H
#define EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H

#include "core/utils/Export.h"
#include "core/plugin/IPlugin.h"
#include <string>
#include <vector>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace easy::core {

struct PluginInstance {
    std::string name;
    IPlugin* plugin = nullptr;
#ifdef _WIN32
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif
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

private:
    PluginManager() = default;
    ~PluginManager() = default;

    std::vector<PluginInstance> m_plugins;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_PLUGIN_PLUGINMANAGER_H
