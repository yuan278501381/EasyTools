// ─────────────────────────────────────────────────────────────────────────────
// ConfigManager.cpp — 配置管理器实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/config/ConfigManager.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <fstream>
#include <filesystem>

namespace easy::core {

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

void ConfigManager::initialize(const std::filesystem::path& configDir, const std::string& filename) {
    TraceId::Scope scope;

    std::filesystem::create_directories(configDir);
    m_configFilePath = configDir / filename;

    LOG_INFO("配置管理器初始化, 配置文件路径={}", m_configFilePath.string());

    load();

    // 启动文件监控线程
    m_watchStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_watchRunning = true;
    m_watchThread = std::thread([this]() { watchFileChanges(); });
}

void ConfigManager::shutdown() {
    LOG_INFO("配置管理器正在关闭...");

    // 停止文件监控
    m_watchRunning = false;
    if (m_watchStopEvent) {
        SetEvent(m_watchStopEvent);
    }
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
    if (m_watchStopEvent) {
        CloseHandle(m_watchStopEvent);
        m_watchStopEvent = nullptr;
    }

    save();
}

void ConfigManager::load() {
    std::lock_guard lock(m_mutex);

    if (!std::filesystem::exists(m_configFilePath)) {
        LOG_INFO("配置文件不存在, 将使用默认配置并创建文件: {}", m_configFilePath.string());
        m_config = json::object();
        save();
        return;
    }

    try {
        std::ifstream file(m_configFilePath);
        if (file.is_open()) {
            m_config = json::parse(file, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
            LOG_INFO("配置文件加载成功, 键数量={}", m_config.size());
        }
    } catch (const json::parse_error& e) {
        LOG_ERROR("配置文件解析失败: {}, 将使用默认配置", e.what());
        m_config = json::object();
    }
}

void ConfigManager::save() const {
    try {
        std::ofstream file(m_configFilePath);
        if (file.is_open()) {
            file << m_config.dump(2);
            LOG_TRACE("配置已保存到文件: {}", m_configFilePath.string());
        } else {
            LOG_ERROR("无法打开配置文件进行写入: {}", m_configFilePath.string());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("配置保存失败: {}", e.what());
    }
}

bool ConfigManager::has(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    try {
        auto ptr = json::json_pointer(key);
        return m_config.contains(ptr);
    } catch (...) {
        return false;
    }
}

void ConfigManager::remove(const std::string& key) {
    {
        std::lock_guard lock(m_mutex);
        try {
            // nlohmann::json 不直接支持 erase with pointer，需要手动导航
            // 这里简化处理：使用 JSON Patch
            auto ptr = json::json_pointer(key);
            if (m_config.contains(ptr)) {
                // 从路径中提取父路径和键名
                auto keyStr = key;
                auto lastSlash = keyStr.rfind('/');
                if (lastSlash != std::string::npos && lastSlash > 0) {
                    auto parentPath = keyStr.substr(0, lastSlash);
                    auto childKey = keyStr.substr(lastSlash + 1);
                    auto parentPtr = json::json_pointer(parentPath);
                    m_config.at(parentPtr).erase(childKey);
                } else if (lastSlash == 0) {
                    // 根级别的 key
                    auto childKey = keyStr.substr(1);
                    m_config.erase(childKey);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("删除配置项失败: key={}, error={}", key, e.what());
        }
    }
    save();
    notifyChange(key);
}

std::string ConfigManager::toJsonString(int indent) const {
    std::lock_guard lock(m_mutex);
    return m_config.dump(indent);
}

void ConfigManager::fromJsonString(const std::string& jsonStr) {
    TraceId::Scope scope;
    {
        std::lock_guard lock(m_mutex);
        try {
            auto incoming = json::parse(jsonStr);
            m_config.merge_patch(incoming);  // RFC 7396 合并
            LOG_INFO("配置已从 JSON 字符串批量更新");
        } catch (const json::parse_error& e) {
            LOG_ERROR("JSON 字符串解析失败: {}", e.what());
            return;
        }
    }
    save();
    notifyChange("*");  // 通知全量变更
}

size_t ConfigManager::onChange(ConfigChangeCallback callback) {
    size_t id = m_nextCallbackId.fetch_add(1);
    m_callbacks.emplace_back(id, std::move(callback));
    return id;
}

void ConfigManager::removeOnChange(size_t callbackId) {
    std::erase_if(m_callbacks, [callbackId](const auto& pair) {
        return pair.first == callbackId;
    });
}

void ConfigManager::notifyChange(const std::string& key) {
    for (const auto& [id, callback] : m_callbacks) {
        try {
            callback(key);
        } catch (const std::exception& e) {
            LOG_WARN("配置变更回调执行异常: callbackId={}, error={}", id, e.what());
        }
    }
}

bool ConfigManager::exportTo(const std::filesystem::path& filePath) const {
    TraceId::Scope scope;
    try {
        std::lock_guard lock(m_mutex);
        std::ofstream file(filePath);
        if (file.is_open()) {
            file << m_config.dump(2);
            LOG_INFO("配置已导出到: {}", filePath.string());
            return true;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("配置导出失败: {}", e.what());
    }
    return false;
}

bool ConfigManager::importFrom(const std::filesystem::path& filePath) {
    TraceId::Scope scope;
    try {
        std::ifstream file(filePath);
        if (file.is_open()) {
            auto incoming = json::parse(file);
            {
                std::lock_guard lock(m_mutex);
                m_config.merge_patch(incoming);
            }
            save();
            notifyChange("*");
            LOG_INFO("配置已从文件导入(合并模式): {}", filePath.string());
            return true;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("配置导入失败: {}", e.what());
    }
    return false;
}

void ConfigManager::watchFileChanges() {
    auto dir = m_configFilePath.parent_path();
    auto fileName = m_configFilePath.filename().wstring();

    HANDLE hDir = CreateFileW(
        dir.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        LOG_WARN("无法监控配置文件目录: {}", dir.string());
        return;
    }

    LOG_DEBUG("配置文件监控已启动: {}", dir.string());

    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    alignas(DWORD) char buffer[4096];
    HANDLE waitHandles[] = {overlapped.hEvent, m_watchStopEvent};

    while (m_watchRunning) {
        DWORD bytesReturned = 0;
        ResetEvent(overlapped.hEvent);

        BOOL result = ReadDirectoryChangesW(
            hDir,
            buffer, sizeof(buffer),
            FALSE,  // 不监控子目录
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned,
            &overlapped,
            nullptr
        );

        if (!result) break;

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            // 文件变更事件
            if (GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE) && bytesReturned > 0) {
                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
                std::wstring changedFile(info->FileName, info->FileNameLength / sizeof(wchar_t));

                if (changedFile == fileName) {
                    // 防抖：等待 200ms 后再加载（避免文件写入过程中的中间状态）
                    Sleep(200);
                    LOG_INFO("检测到配置文件变更, 正在热加载...");
                    load();
                    notifyChange("*");
                }
            }
        } else {
            // 收到停止信号
            break;
        }
    }

    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    LOG_DEBUG("配置文件监控已停止");
}

ConfigManager::~ConfigManager() {
    if (m_watchRunning) {
        shutdown();
    }
}

}  // namespace easy::core
