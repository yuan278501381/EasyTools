#include "core/plugin/PluginManager.h"
#include "core/config/ConfigManager.h"
#include "core/events/EventBus.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/utils/WinUtils.h"
#include "EasyToolsVersion.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace easy::core {

namespace {

std::string pluginIdFromPath(const std::filesystem::path& path) {
    std::string value = WinUtils::wstringToUtf8(path.stem().wstring());
    constexpr std::string_view prefix = "Plugin_";
    if (value.starts_with(prefix)) value.erase(0, prefix.size());

    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch)) result.push_back(static_cast<char>(std::tolower(ch)));
        else if (ch == '-' || ch == '_') result.push_back(static_cast<char>(ch));
        else if (result.empty() || result.back() != '-') result.push_back('-');
    }
    return result;
}

std::string pluginConfigKey(const std::string& id) {
    return "/plugins/" + id + "/enabled";
}

bool configuredEnabled(const std::string& id) {
    auto& config = ConfigManager::instance();
    const auto key = pluginConfigKey(id);
    if (config.has(key)) return config.get<bool>(key, true);
    // 兼容旧版按键回显开关；其他核心插件默认启用。
    if (id == "keycast") return config.get<bool>("/general/keycastEnabled", false);
    return true;
}

} // namespace

PluginManager& PluginManager::instance() {
    static PluginManager inst;
    return inst;
}

bool PluginManager::loadPlugins(const std::string& directory) {
    std::lock_guard lock(m_mutex);
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
    std::unordered_set<std::string> pluginIds;

    for (const auto& pluginPath : pluginPaths) {
        const std::wstring filename = pluginPath.filename().wstring();
        PluginInstance inst;
        inst.id = pluginIdFromPath(pluginPath);
        inst.name = inst.id;
        inst.fileName = easy::core::WinUtils::wstringToUtf8(filename);
        inst.path = pluginPath;
        inst.configuredEnabled = configuredEnabled(inst.id);
        inst.enabledAtLaunch = inst.configuredEnabled;
        LOG_INFO("发现插件 DLL: {}", easy::core::WinUtils::wstringToUtf8(filename));

        if (!pluginIds.insert(inst.id).second) {
            inst.error = "duplicate plugin id";
            LOG_ERROR("插件 ID 重复，已拒绝加载: {}", inst.id);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        auto manifestPath = pluginPath;
        manifestPath.replace_extension(L".plugin.json");
        auto manifestResult = loadPluginManifest(manifestPath, inst.id, easy::version::String);
        if (!manifestResult) {
            inst.error = manifestResult.error;
            LOG_ERROR("插件清单校验失败: {}, error={}", inst.fileName, inst.error);
            m_plugins.push_back(std::move(inst));
            continue;
        }
        inst.manifest = std::move(manifestResult.manifest);
        inst.name = inst.manifest.name;
        inst.version = inst.manifest.version;
        if (!pluginNames.insert(inst.name).second) {
            inst.error = "duplicate plugin name";
            LOG_ERROR("插件名称重复，已拒绝加载: {}", inst.name);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        // 清单提供稳定 ID 和展示元数据。禁用模块无需加载 DLL；Capture 等大型
        // 模块因此不会连带映射 OpenCV/FFmpeg，显著降低冷启动 I/O。
        if (!inst.enabledAtLaunch) {
            LOG_INFO("插件已禁用，完全跳过 DLL 加载: {}", inst.id);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        HMODULE hMod = LoadLibraryExW(
            pluginPath.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!hMod) {
            const DWORD error = GetLastError();
            inst.error = "load failed (Windows error " + std::to_string(error) + ")";
            LOG_ERROR("无法加载插件 DLL: {}, error={}", inst.fileName, error);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        using GetPluginAbiVersionFunc = std::uint32_t (*)();
        const auto getAbiVersion = reinterpret_cast<GetPluginAbiVersionFunc>(
            GetProcAddress(hMod, "GetPluginAbiVersion"));
        if (!getAbiVersion) {
            inst.error = "missing GetPluginAbiVersion export";
            LOG_ERROR("插件 DLL 未导出 ABI 握手函数: {}", inst.fileName);
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
            continue;
        }
        std::uint32_t binaryAbiVersion = 0;
        try {
            binaryAbiVersion = getAbiVersion();
        } catch (...) {
            inst.error = "plugin ABI handshake failed";
        }
        if (!inst.error.empty() || binaryAbiVersion != inst.manifest.abiVersion) {
            if (inst.error.empty()) inst.error = "plugin binary ABI does not match manifest";
            LOG_ERROR("插件 ABI 校验失败: {}, manifest={}, binary={}",
                      inst.fileName, inst.manifest.abiVersion, binaryAbiVersion);
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        using CreatePluginFunc = IPlugin* (*)();
        const auto createFunc = reinterpret_cast<CreatePluginFunc>(
            GetProcAddress(hMod, inst.manifest.entryPoint.c_str()));
        if (!createFunc) {
            LOG_ERROR("插件 DLL 未导出 CreatePlugin: {}",
                      easy::core::WinUtils::wstringToUtf8(filename));
            inst.error = "missing CreatePlugin export";
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
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
            inst.error = "CreatePlugin returned null";
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
            continue;
        }

        try {
            const char* name = plugin->getName();
            const char* version = plugin->getVersion();
            if (!name || !*name || !version || !*version) {
                throw std::runtime_error("empty plugin metadata");
            }
            if (inst.name != name || inst.version != version) {
                throw std::runtime_error("binary metadata does not match manifest");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("插件元数据异常: {}, error={}",
                      easy::core::WinUtils::wstringToUtf8(filename), e.what());
            inst.error = std::string("invalid metadata: ") + e.what();
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
            continue;
        } catch (...) {
            LOG_ERROR("插件元数据未知异常: {}", easy::core::WinUtils::wstringToUtf8(filename));
            inst.error = "invalid metadata";
            FreeLibrary(hMod);
            m_plugins.push_back(std::move(inst));
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
    std::lock_guard lock(m_mutex);
    for (auto& inst : m_plugins) {
        if (!inst.enabledAtLaunch || !inst.plugin || !inst.handle) continue;
        LOG_INFO("初始化插件: {}", inst.name);
        const auto started = std::chrono::steady_clock::now();
        try {
            inst.initialized = inst.plugin->initialize();
        } catch (const std::exception& e) {
            LOG_ERROR("插件初始化异常: {}, error={}", inst.name, e.what());
            inst.initialized = false;
            inst.error = e.what();
        } catch (...) {
            LOG_ERROR("插件初始化未知异常: {}", inst.name);
            inst.initialized = false;
            inst.error = "unknown initialize exception";
        }
        if (!inst.initialized) {
            LOG_ERROR("插件初始化失败: {}", inst.name);
            if (inst.error.empty()) inst.error = "initialize returned false";
            // initialize() may fail after partially registering callbacks.
            try { inst.plugin->shutdown(); } catch (...) {}
        }
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        PerformanceMonitor::instance().recordPluginInit(inst.name, elapsedMs);
    }
}

void PluginManager::shutdownPlugins() {
    {
        std::lock_guard lock(m_mutex);
        if (m_shuttingDown) return;
        m_shuttingDown = true;
    }
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
        if (!it->handle) continue;
        LOG_INFO("卸载插件 DLL: {}", it->name);
        FreeLibrary(it->handle);
    }
    m_plugins.clear();
}

std::vector<PluginStatus> PluginManager::getPluginStatuses() const {
    std::lock_guard lock(m_mutex);
    if (m_shuttingDown) return {};
    std::vector<PluginStatus> result;
    result.reserve(m_plugins.size());
    for (const auto& inst : m_plugins) {
        const bool desiredEnabled = configuredEnabled(inst.id);
        const bool restartRequired = desiredEnabled != inst.enabledAtLaunch;
        std::string state;
        if (!inst.error.empty()) state = "failed";
        else if (restartRequired) state = "pendingRestart";
        else if (inst.initialized) state = "running";
        else if (!desiredEnabled) state = "disabled";
        else state = "failed";
        result.push_back({
            inst.id, inst.name, inst.version, inst.fileName,
            inst.manifest.abiVersion, inst.manifest.capabilities, inst.manifest.permissions,
            desiredEnabled, inst.initialized, restartRequired,
            std::move(state), inst.error
        });
    }
    return result;
}

bool PluginManager::setPluginEnabled(const std::string& id, bool enabled,
                                     bool& restartRequired, std::string& error) {
    std::lock_guard lock(m_mutex);
    if (m_shuttingDown) {
        error = "application is shutting down";
        return false;
    }
    auto it = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const PluginInstance& item) {
        return item.id == id;
    });
    if (it == m_plugins.end()) {
        error = "plugin not found";
        return false;
    }
    if (!it->error.empty() && enabled) {
        error = it->error;
        return false;
    }

    nlohmann::json patch = {{"plugins", {{id, {{"enabled", enabled}}}}}};
    if (id == "keycast") patch["general"] = {{"keycastEnabled", enabled}};
    if (!ConfigManager::instance().mergePatch(patch, "/plugins/" + id)) {
        error = "failed to persist plugin setting";
        return false;
    }
    it->configuredEnabled = enabled;
    restartRequired = it->configuredEnabled != it->enabledAtLaunch;
    return true;
}

bool PluginManager::isEnabledAtLaunch(const std::string& id) const {
    std::lock_guard lock(m_mutex);
    if (m_shuttingDown) return false;
    const auto it = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const PluginInstance& item) {
        return item.id == id;
    });
    return it != m_plugins.end() && it->enabledAtLaunch && it->error.empty();
}

} // namespace easy::core
