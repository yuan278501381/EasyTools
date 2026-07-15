#include "core/plugin/PluginManager.h"
#include "core/events/EventBus.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace easy::core {

PluginManager& PluginManager::instance() {
    static PluginManager inst;
    return inst;
}

bool PluginManager::loadPlugins(const std::string& directory) {
    if (!m_plugins.empty()) {
        LOG_WARN("插件已加载，拒绝重复扫描");
        return false;
    }
    std::filesystem::path dirPath = easy::core::WinUtils::utf8ToWstring(directory);
    
    // 如果是相对路径，则相对于 exe 所在目录
    if (dirPath.is_relative()) {
        dirPath = easy::core::WinUtils::getExeDirectory().parent_path() / dirPath;
    }

    std::error_code filesystemError;
    if (!std::filesystem::is_directory(dirPath, filesystemError)) {
        LOG_WARN("插件目录不存在: {}", directory);
        return false;
    }

    LOG_INFO("开始扫描插件目录: {}", directory);

    std::vector<std::filesystem::path> pluginPaths;
    std::filesystem::directory_iterator pluginIt(dirPath, filesystemError);
    const std::filesystem::directory_iterator end;
    for (; pluginIt != end && !filesystemError; pluginIt.increment(filesystemError)) {
        const auto& entry = *pluginIt;
        if (entry.is_regular_file(filesystemError) && !filesystemError &&
            _wcsicmp(entry.path().extension().c_str(), L".dll") == 0) {
            std::wstring filename = entry.path().filename().wstring();
            if (filename.find(L"Plugin_") == 0) {
                pluginPaths.push_back(entry.path());
            }
        }
    }
    if (filesystemError) {
        LOG_ERROR("扫描插件目录失败: {}, error={}", directory, filesystemError.message());
        return false;
    }
    std::sort(pluginPaths.begin(), pluginPaths.end());
    std::unordered_set<std::string> pluginNames;

    for (const auto& pluginPath : pluginPaths) {
        const std::wstring filename = pluginPath.filename().wstring();
        LOG_INFO("发现插件 DLL: {}", easy::core::WinUtils::wstringToUtf8(filename));

        HMODULE hMod = LoadLibraryExW(
            pluginPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!hMod) {
            LOG_ERROR("无法加载插件 DLL: {}, error={}",
                      easy::core::WinUtils::wstringToUtf8(filename), GetLastError());
            continue;
        }

        using CreatePluginFunc = IPlugin* (*)();
        const auto createFunc = reinterpret_cast<CreatePluginFunc>(
            GetProcAddress(hMod, "CreatePlugin"));
        if (!createFunc) {
            LOG_ERROR("插件 DLL 未导出 CreatePlugin: {}",
                      easy::core::WinUtils::wstringToUtf8(filename));
            FreeLibrary(hMod);
            continue;
        }

        IPlugin* plugin = nullptr;
        try {
            plugin = createFunc();
        } catch (const std::exception& e) {
            LOG_ERROR("CreatePlugin 异常: {}, error={}",
                      easy::core::WinUtils::wstringToUtf8(filename), e.what());
        } catch (...) {
            LOG_ERROR("CreatePlugin 未知异常: {}", easy::core::WinUtils::wstringToUtf8(filename));
        }
        if (!plugin) {
            LOG_ERROR("CreatePlugin 返回 null: {}", easy::core::WinUtils::wstringToUtf8(filename));
            FreeLibrary(hMod);
            continue;
        }

        PluginInstance inst;
        try {
            const char* name = plugin->getName();
            const char* version = plugin->getVersion();
            if (!name || !*name || !version || !*version) {
                throw std::runtime_error("empty plugin metadata");
            }
            inst.name = name;
            inst.version = version;
        } catch (const std::exception& e) {
            LOG_ERROR("插件元数据异常: {}, error={}",
                      easy::core::WinUtils::wstringToUtf8(filename), e.what());
            FreeLibrary(hMod);
            continue;
        } catch (...) {
            LOG_ERROR("插件元数据未知异常: {}", easy::core::WinUtils::wstringToUtf8(filename));
            FreeLibrary(hMod);
            continue;
        }
        if (!pluginNames.insert(inst.name).second) {
            LOG_ERROR("插件名称重复，已拒绝加载: {}", inst.name);
            FreeLibrary(hMod);
            continue;
        }

        inst.plugin = plugin;
        inst.handle = hMod;
        m_plugins.push_back(std::move(inst));
        const auto& loaded = m_plugins.back();
        LOG_INFO("插件已成功加载: {} (v{})", loaded.name, loaded.version);
    }

    return !m_plugins.empty();
}

void PluginManager::initializePlugins() {
    for (auto& inst : m_plugins) {
        LOG_INFO("初始化插件: {}", inst.name);
        try {
            inst.initialized = inst.plugin->initialize();
        } catch (const std::exception& e) {
            LOG_ERROR("插件初始化异常: {}, error={}", inst.name, e.what());
            inst.initialized = false;
        } catch (...) {
            LOG_ERROR("插件初始化未知异常: {}", inst.name);
            inst.initialized = false;
        }
        if (!inst.initialized) {
            LOG_ERROR("插件初始化失败: {}", inst.name);
            // initialize() may fail after partially registering callbacks.
            try { inst.plugin->shutdown(); } catch (...) {}
        }
    }
}

void PluginManager::shutdownPlugins() {
    // Phase 1: let every plugin stop its workers and release its own windows
    // while every DLL is still loaded.
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        if (!it->initialized) continue;
        LOG_INFO("停止插件: {}", it->name);
        try {
            it->plugin->shutdown();
        } catch (const std::exception& e) {
            LOG_ERROR("插件关闭异常: {}, error={}", it->name, e.what());
        } catch (...) {
            LOG_ERROR("插件关闭未知异常: {}", it->name);
        }
        it->initialized = false;
    }

    // Phase 2: destroy all callbacks owned by external core singletons before
    // FreeLibrary. Otherwise std::function destructors can jump into unloaded
    // plugin code during process shutdown.
    MainThreadDispatcher::instance().drain();
    HotkeyManager::instance().shutdown();
    EventBus::instance().clearAll();
    MessageBridge::instance().clearHandlers();

    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        LOG_INFO("卸载插件 DLL: {}", it->name);
        FreeLibrary(it->handle);
    }
    m_plugins.clear();
}

} // namespace easy::core
