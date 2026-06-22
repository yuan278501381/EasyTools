#include "core/plugin/PluginManager.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <filesystem>

namespace easy::core {

PluginManager& PluginManager::instance() {
    static PluginManager inst;
    return inst;
}

bool PluginManager::loadPlugins(const std::string& directory) {
    std::filesystem::path dirPath = easy::core::WinUtils::utf8ToWstring(directory);
    
    // 如果是相对路径，则相对于 exe 所在目录
    if (dirPath.is_relative()) {
        dirPath = easy::core::WinUtils::getExeDirectory().parent_path() / dirPath;
    }

    if (!std::filesystem::exists(dirPath)) {
        LOG_WARN("插件目录不存在: {}", directory);
        return false;
    }

    LOG_INFO("开始扫描插件目录: {}", directory);

    for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == L".dll") {
            std::wstring filename = entry.path().filename().wstring();
            if (filename.find(L"Plugin_") == 0) {
                LOG_INFO("发现插件 DLL: {}", easy::core::WinUtils::wstringToUtf8(filename));

                HMODULE hMod = LoadLibraryW(entry.path().c_str());
                if (!hMod) {
                    LOG_ERROR("无法加载插件 DLL: {}, error={}", easy::core::WinUtils::wstringToUtf8(filename), GetLastError());
                    continue;
                }

                using CreatePluginFunc = IPlugin* (*)();
                CreatePluginFunc createFunc = reinterpret_cast<CreatePluginFunc>(GetProcAddress(hMod, "CreatePlugin"));

                if (!createFunc) {
                    LOG_ERROR("插件 DLL 未导出 CreatePlugin: {}", easy::core::WinUtils::wstringToUtf8(filename));
                    FreeLibrary(hMod);
                    continue;
                }

                IPlugin* plugin = createFunc();
                if (!plugin) {
                    LOG_ERROR("CreatePlugin 返回 null: {}", easy::core::WinUtils::wstringToUtf8(filename));
                    FreeLibrary(hMod);
                    continue;
                }

                PluginInstance inst;
                inst.name = plugin->getName();
                inst.plugin = plugin;
                inst.handle = hMod;

                m_plugins.push_back(inst);
                LOG_INFO("插件已成功加载: {} (v{})", inst.name, plugin->getVersion());
            }
        }
    }

    return true;
}

void PluginManager::initializePlugins() {
    for (auto& inst : m_plugins) {
        LOG_INFO("初始化插件: {}", inst.name);
        if (!inst.plugin->initialize()) {
            LOG_ERROR("插件初始化失败: {}", inst.name);
        }
    }
}

void PluginManager::shutdownPlugins() {
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
        LOG_INFO("卸载插件: {}", it->name);
        it->plugin->shutdown();
        FreeLibrary(it->handle);
    }
    m_plugins.clear();
}

} // namespace easy::core
