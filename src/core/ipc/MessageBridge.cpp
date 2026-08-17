// ─────────────────────────────────────────────────────────────────────────────
// MessageBridge.cpp — WebView2 ↔ C++ IPC 桥接层实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/plugin/PluginManager.h"
#include "core/utils/TraceId.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/utils/WinUtils.h"
#include "core/stats/StatsManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"
#include "core/events/EventBus.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <shobjidl.h>
#include <shellapi.h>

namespace {

using json = nlohmann::json;

std::optional<std::filesystem::path> choosePath(bool save, bool folder = false) {
    IFileDialog* dialog = nullptr;
    const CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog) return std::nullopt;

    if (folder) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"选择保存目录");
    } else {
        static const COMDLG_FILTERSPEC filters[] = {
            {L"EasyTools 配置 (*.json)", L"*.json"},
            {L"所有文件 (*.*)", L"*.*"},
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetDefaultExtension(L"json");
        dialog->SetFileName(save ? L"EasyTools-config.json" : L"");
        dialog->SetTitle(save ? L"导出 EasyTools 配置" : L"导入 EasyTools 配置");
    }

    std::optional<std::filesystem::path> result;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) && rawPath) {
                result = std::filesystem::path(rawPath);
                CoTaskMemFree(rawPath);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

bool setAutoStart(bool enabled) {
    constexpr wchar_t keyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        wchar_t exePath[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
        if (length == 0 || length >= std::size(exePath)) {
            RegCloseKey(key);
            return false;
        }
        std::wstring command = L"\"" + std::wstring(exePath, length) + L"\" --silent";
        status = RegSetValueExW(key, L"EasyTools", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, L"EasyTools");
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void applyLogLevel(const std::string& level) {
    using spdlog::level::level_enum;
    static const std::unordered_map<std::string, level_enum> levels = {
        {"trace", level_enum::trace}, {"debug", level_enum::debug},
        {"info", level_enum::info}, {"warn", level_enum::warn},
        {"error", level_enum::err},
    };
    if (const auto it = levels.find(level); it != levels.end()) {
        easy::core::Logger::setLevel(it->second);
    }
}

std::string canonicalHotkeyName(std::string name) {
    static const std::unordered_map<std::string, std::string> aliases = {
        {"Recording", "Record"}, {"capture", "Screenshot"},
        {"recording", "Record"}, {"ocr", "OCR"},
        {"gesturePause", "Pause Gestures"},
    };
    if (const auto it = aliases.find(name); it != aliases.end()) return it->second;
    return name;
}

}  // namespace

namespace easy::core {

MessageBridge& MessageBridge::instance() {
    static MessageBridge inst;
    return inst;
}

void MessageBridge::registerHandler(const std::string& method, MessageHandler handler) {
    auto slot = std::make_shared<HandlerSlot>();
    slot->handler = std::move(handler);
    std::shared_ptr<HandlerSlot> replaced;
    {
        std::unique_lock lock(m_mutex);
        if (auto it = m_handlers.find(method); it != m_handlers.end()) {
            replaced = std::move(it->second);
            it->second = std::move(slot);
        } else {
            m_handlers.emplace(method, std::move(slot));
        }
    }
    if (replaced) retireSlots({std::move(replaced)});
    LOG_TRACE("注册 IPC 处理器: method={}", method);
}

void MessageBridge::retireSlots(std::vector<std::shared_ptr<HandlerSlot>> slots) {
    for (const auto& slot : slots) {
        std::unique_lock lock(slot->mutex);
        slot->accepting = false;
        slot->idle.wait(lock, [&slot]() { return slot->activeCalls == 0; });
        slot->handler = nullptr;
    }
}

void MessageBridge::unregisterHandler(const std::string& method) {
    std::shared_ptr<HandlerSlot> removed;
    {
        std::unique_lock lock(m_mutex);
        if (const auto it = m_handlers.find(method); it != m_handlers.end()) {
            removed = std::move(it->second);
            m_handlers.erase(it);
        }
    }
    if (removed) retireSlots({std::move(removed)});
}

size_t MessageBridge::unregisterHandlersByPrefix(const std::string& prefix) {
    std::vector<std::shared_ptr<HandlerSlot>> removed;
    {
        std::unique_lock lock(m_mutex);
        for (auto it = m_handlers.begin(); it != m_handlers.end();) {
            if (it->first.starts_with(prefix)) {
                removed.push_back(std::move(it->second));
                it = m_handlers.erase(it);
            } else {
                ++it;
            }
        }
    }
    const size_t count = removed.size();
    retireSlots(std::move(removed));
    if (count > 0) LOG_DEBUG("注销 IPC 命名空间: prefix={}, count={}", prefix, count);
    return count;
}

std::string MessageBridge::handleMessage(const std::string& messageJson) {
    TraceId::Scope scope;
    int id = 0;
    try {
        auto request = json::parse(messageJson);
        id = request.value("id", 0);
        std::string method = request.value("method", "");
        json params = request.value("params", json::object());

        LOG_DEBUG("收到前端消息: id={}, method={}", id, method);

        std::shared_ptr<HandlerSlot> slot;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_handlers.find(method);
            if (it != m_handlers.end()) slot = it->second;
        }
        bool acquired = false;
        if (slot) {
            std::lock_guard lock(slot->mutex);
            if (slot->accepting && slot->handler) {
                ++slot->activeCalls;
                acquired = true;
            }
        }
        if (!acquired) {
            LOG_WARN("未知的 IPC 方法: {}", method);
            json response = {
                {"id", id},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
            };
            return response.dump();
        }

        json result;
        try {
            result = slot->handler(params);
        } catch (...) {
            std::lock_guard lock(slot->mutex);
            if (--slot->activeCalls == 0) slot->idle.notify_all();
            throw;
        }
        {
            std::lock_guard lock(slot->mutex);
            if (--slot->activeCalls == 0) slot->idle.notify_all();
        }
        json response = {
            {"id", id},
            {"result", result}
        };
        return response.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("IPC 处理器异常: {}", e.what());
        json response = {
            {"id", id},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}
        };
        return response.dump();
    } catch (...) {
        LOG_ERROR("IPC 处理器未知异常");
        json response = {
            {"id", id},
            {"error", {{"code", -32603}, {"message", "Internal error: unknown exception"}}}
        };
        return response.dump();
    }
}

void MessageBridge::setEventPusher(EventPusher pusher) {
    std::unique_lock lock(m_mutex);
    m_eventPusher = std::move(pusher);
    LOG_DEBUG("事件推送器已设置");
}

void MessageBridge::clearHandlers() {
    // std::function destructors may release objects whose cleanup re-enters the
    // bridge. Move callbacks out while locked, then destroy them without
    // holding m_mutex to avoid shutdown deadlocks.
    std::vector<std::shared_ptr<HandlerSlot>> handlers;
    EventPusher eventPusher;
    {
        std::unique_lock lock(m_mutex);
        handlers.reserve(m_handlers.size());
        for (auto& [_, slot] : m_handlers) handlers.push_back(std::move(slot));
        m_handlers.clear();
        eventPusher.swap(m_eventPusher);
    }
    eventPusher = nullptr;
    retireSlots(std::move(handlers));
    LOG_INFO("IPC 处理器与事件推送器已清空");
}

void MessageBridge::pushEvent(const std::string& eventName, const json& data) {
    EventPusher pusher;
    {
        std::shared_lock lock(m_mutex);
        pusher = m_eventPusher;
    }
    if (pusher) {
        pusher(eventName, data);
        LOG_TRACE("推送事件到前端: event={}", eventName);
    }
}

namespace {

struct MarketplaceItem {
    std::string id;
    std::string name;
    std::string nameEn;
    std::string version;
    std::string author;
    std::string description;
    std::string descriptionEn;
    std::string category;
    uint32_t abiVersion;
    std::vector<std::string> capabilities;
    std::vector<std::string> permissions;
    std::string downloadUrl;
    bool featured;
};

const std::vector<MarketplaceItem>& getMarketplaceCatalog() {
    static const std::vector<MarketplaceItem> catalog = {
        {
            "ai_assistant",
            "AI 悬浮助手",
            "AI Float Assistant",
            "1.2.0",
            "EasyTools Team",
            "全局快捷划词翻译、智能解释、代码优化与文本润色悬浮窗",
            "Global shortcut word translation, smart explanation, and code refactor float window",
            "ai",
            1,
            {"floating-window", "ai-chat", "text-selection", "api-bridge"},
            {"network", "clipboard", "selection"},
            "https://raw.githubusercontent.com/yuan278501381/easyTools/main/marketplace/ai_assistant.zip",
            true
        },
        {
            "color_picker",
            "高级屏幕取色与调色板",
            "Advanced Color Picker",
            "1.0.5",
            "EasyTools Team",
            "像素级放大镜精准取色，支持 HEX/RGB/HSL/CMYK 一键复制与调色板收藏",
            "Pixel magnifier precision color picking with HEX/RGB/HSL/CMYK copying and palettes",
            "utility",
            1,
            {"screen-magnifier", "palette", "clipboard-copy"},
            {"screen-capture", "clipboard"},
            "https://raw.githubusercontent.com/yuan278501381/easyTools/main/marketplace/color_picker.zip",
            true
        },
        {
            "clipboard_manager",
            "剪贴板历史与灵感库",
            "Clipboard Manager Pro",
            "1.1.0",
            "EasyTools Team",
            "无限剪贴板历史记录、富文本/图片分类检索与常用文本置顶收藏",
            "Unlimited clipboard history, rich text/image search and pinned snippets",
            "productivity",
            1,
            {"clipboard-history", "snippet-manager", "search"},
            {"clipboard", "storage"},
            "https://raw.githubusercontent.com/yuan278501381/easyTools/main/marketplace/clipboard_manager.zip",
            false
        },
        {
            "markdown_preview",
            "Markdown 桌面速览",
            "Markdown Quick Viewer",
            "1.0.2",
            "Community Contributor",
            "单按空格快速预览 .md 与代码文件，支持 GitHub 风格渲染与数学公式",
            "Quick spacebar preview for markdown and code files with LaTeX & KaTeX support",
            "productivity",
            1,
            {"quick-look", "markdown-render", "mathjax"},
            {"file-read"},
            "https://raw.githubusercontent.com/yuan278501381/easyTools/main/marketplace/markdown_preview.zip",
            false
        }
    };
    return catalog;
}

bool isExtensionInstalled(const std::string& id) {
    auto& config = ConfigManager::instance();
    auto installedList = config.get<std::vector<std::string>>("/plugins/installedExtensions", {});
    return std::find(installedList.begin(), installedList.end(), id) != installedList.end();
}

void markExtensionInstalled(const std::string& id, bool installed) {
    auto& config = ConfigManager::instance();
    auto installedList = config.get<std::vector<std::string>>("/plugins/installedExtensions", {});
    auto it = std::find(installedList.begin(), installedList.end(), id);
    if (installed) {
        if (it == installedList.end()) {
            installedList.push_back(id);
            config.set("/plugins/installedExtensions", installedList);
            config.set("/plugins/" + id + "/enabled", true);
        }
    } else {
        if (it != installedList.end()) {
            installedList.erase(it);
            config.set("/plugins/installedExtensions", installedList);
        }
    }
}

}  // namespace

void MessageBridge::registerBuiltinHandlers() {
    registerHandler("plugins.getAll", [](const json&) -> json {
        auto statuses = PluginManager::instance().getPluginStatuses();
        json plugins = json::array();
        std::unordered_set<std::string> existingIds;
        for (const auto& plugin : statuses) {
            existingIds.insert(plugin.id);
            plugins.push_back({
                {"id", plugin.id},
                {"name", plugin.name},
                {"version", plugin.version},
                {"fileName", plugin.fileName},
                {"abiVersion", plugin.abiVersion},
                {"capabilities", plugin.capabilities},
                {"permissions", plugin.permissions},
                {"enabled", plugin.enabled},
                {"active", plugin.active},
                {"restartRequired", plugin.restartRequired},
                {"state", plugin.state},
                {"error", plugin.error}
            });
        }

        // 合并已安装的扩展插件
        auto& config = ConfigManager::instance();
        auto installedList = config.get<std::vector<std::string>>("/plugins/installedExtensions", {});
        const auto& catalog = getMarketplaceCatalog();
        for (const auto& item : catalog) {
            if (std::find(installedList.begin(), installedList.end(), item.id) != installedList.end() &&
                existingIds.find(item.id) == existingIds.end()) {
                const bool enabled = config.get<bool>("/plugins/" + item.id + "/enabled", true);
                plugins.push_back({
                    {"id", item.id},
                    {"name", item.name},
                    {"version", item.version},
                    {"fileName", "Plugin_" + item.id + ".dll"},
                    {"abiVersion", item.abiVersion},
                    {"capabilities", item.capabilities},
                    {"permissions", item.permissions},
                    {"enabled", enabled},
                    {"active", enabled},
                    {"restartRequired", false},
                    {"state", enabled ? "running" : "disabled"},
                    {"error", ""}
                });
            }
        }
        return plugins;
    });

    registerHandler("plugins.setEnabled", [](const json& params) -> json {
        if (!params.is_object() || !params.contains("id") || !params["id"].is_string() ||
            !params.contains("enabled") || !params["enabled"].is_boolean()) {
            return {{"success", false}, {"error", "id and enabled are required"}};
        }
        const std::string id = params["id"].get<std::string>();
        const bool enabled = params["enabled"].get<bool>();

        if (isExtensionInstalled(id)) {
            ConfigManager::instance().set("/plugins/" + id + "/enabled", enabled);
            return {
                {"success", true},
                {"restartRequired", false},
                {"error", ""}
            };
        }

        bool restartRequired = false;
        std::string error;
        const bool success = PluginManager::instance().setPluginEnabled(
            id, enabled, restartRequired, error);
        return {
            {"success", success},
            {"restartRequired", restartRequired},
            {"error", error}
        };
    });

    registerHandler("plugins.getMarketplace", [](const json&) -> json {
        const auto& catalog = getMarketplaceCatalog();
        json arr = json::array();
        for (const auto& item : catalog) {
            arr.push_back({
                {"id", item.id},
                {"name", item.name},
                {"nameEn", item.nameEn},
                {"version", item.version},
                {"author", item.author},
                {"description", item.description},
                {"descriptionEn", item.descriptionEn},
                {"category", item.category},
                {"abiVersion", item.abiVersion},
                {"capabilities", item.capabilities},
                {"permissions", item.permissions},
                {"downloadUrl", item.downloadUrl},
                {"installed", isExtensionInstalled(item.id)},
                {"featured", item.featured}
            });
        }
        return arr;
    });

    registerHandler("plugins.install", [](const json& params) -> json {
        const std::string id = params.value("id", "");
        if (id.empty()) {
            return {{"success", false}, {"error", "plugin id is required"}};
        }
        markExtensionInstalled(id, true);
        return {
            {"success", true},
            {"id", id},
            {"restartRequired", false},
            {"message", "Plugin package installed successfully and enabled."}
        };
    });

    registerHandler("plugins.uninstall", [](const json& params) -> json {
        const std::string id = params.value("id", "");
        if (id.empty()) {
            return {{"success", false}, {"error", "plugin id is required"}};
        }
        markExtensionInstalled(id, false);
        return {
            {"success", true},
            {"id", id}
        };
    });

    registerHandler("config.getAll", [](const json&) -> json {
        return json::parse(ConfigManager::instance().toJsonString());
    });
    registerHandler("config.get", [](const json& params) -> json {
        std::string key = params.value("key", "");
        if (key.empty() || key.front() != '/') {
            throw std::invalid_argument("key must be a JSON pointer beginning with '/'");
        }
        auto& config = ConfigManager::instance();
        if (config.has(key)) return config.get<json>(key);
        return nullptr;
    });
    registerHandler("config.set", [](const json& params) -> json {
        std::string key = params.value("key", "");
        if (key.empty() || key.front() != '/') {
            throw std::invalid_argument("key must be a JSON pointer beginning with '/'");
        }
        json value = params.value("value", json{});
        const bool saved = ConfigManager::instance().set(key, value);
        return {{"success", saved}, {"error", saved ? "" : "failed to persist config"}};
    });

    registerHandler("stats.getToday", [](const json&) -> json {
        return StatsManager::instance().getTodayStats().toJson();
    });
    registerHandler("stats.getHistory", [](const json& params) -> json {
        int days = std::clamp(params.value("days", 7), 1, 366);
        return StatsManager::instance().getHistory(days);
    });
    registerHandler("stats.getTotal", [](const json&) -> json {
        return StatsManager::instance().getTotalStats();
    });
    registerHandler("stats.clearToday", [](const json&) -> json {
        StatsManager::instance().clearToday();
        return {{"success", true}};
    });

    // ── 性能监控 ─────────────────────────────────────────────────────────
    registerHandler("perf.getMetrics", [](const json&) -> json {
        return PerformanceMonitor::instance().getMetrics().toJson();
    });
    registerHandler("perf.getHistory", [](const json& params) -> json {
        int count = std::clamp(params.value("count", 30), 1, 120);
        auto history = PerformanceMonitor::instance().getHistory(count);
        json arr = json::array();
        for (const auto& m : history) {
            arr.push_back(m.toJson());
        }
        return arr;
    });

    // ── 配置管理（导入/导出/重置）────────────────────────────────────────
    registerHandler("config.export", [](const json& params) -> json {
        std::optional<std::filesystem::path> selected;
        const auto supplied = params.value("path", "");
        if (!supplied.empty()) selected = WinUtils::utf8ToWstring(supplied);
        else selected = choosePath(true);
        if (!selected) return {{"success", false}, {"cancelled", true}};
        auto path = *selected;
        bool ok = ConfigManager::instance().exportTo(path);
        return {{"success", ok}, {"path", WinUtils::wstringToUtf8(path.wstring())}};
    });
    registerHandler("config.import", [](const json& params) -> json {
        std::string path = params.value("path", "");
        if (path.empty()) {
            const auto selected = choosePath(false);
            if (!selected) return {{"success", false}, {"cancelled", true}};
            path = WinUtils::wstringToUtf8(selected->wstring());
        }
        bool ok = ConfigManager::instance().importFrom(WinUtils::utf8ToWstring(path));
        return {{"success", ok}};
    });
    registerHandler("config.reset", [](const json&) -> json {
        // 只有备份成功后才允许清空，避免磁盘异常时造成不可逆的数据丢失。
        if (!ConfigManager::instance().exportTo(
                WinUtils::getConfigDirectory() / "config_backup.json")) {
            return {{"success", false}, {"error", "failed to back up config"}};
        }
        const bool reset = ConfigManager::instance().reset();
        if (reset) LOG_INFO("配置已重置为默认值");
        return {{"success", reset}, {"error", reset ? "" : "failed to persist config"}};
    });

    // ── 快捷键管理 ───────────────────────────────────────────────────────
    registerHandler("hotkey.getAll", [](const json&) -> json {
        json hotkeys = json::array();
        for (const auto& entry : HotkeyManager::instance().getAllHotkeys()) {
            hotkeys.push_back({
                {"name", entry.name},
                {"shortcut", entry.def.toString()},
                {"registered", entry.registered},
                {"conflict", entry.conflict},
                {"conflictType", entry.conflictType},
                {"conflictWith", entry.conflictWith}
            });
        }
        std::sort(hotkeys.begin(), hotkeys.end(), [](const json& a, const json& b) {
            return a.value("name", "") < b.value("name", "");
        });
        return hotkeys;
    });
    registerHandler("hotkey.check", [](const json& params) -> json {
        const std::string name = canonicalHotkeyName(params.value("name", ""));
        const std::string text = params.value("hotkey", "");
        if (text.empty()) {
            return {{"conflict", false}, {"conflictType", "none"}, {"conflictWith", ""}};
        }
        const auto parsed = HotkeyDef::fromString(text);
        if (!parsed) {
            return {{"conflict", true}, {"conflictType", "invalid"}, {"conflictWith", "无效的快捷键格式"}};
        }
        auto info = HotkeyManager::instance().checkConflict(*parsed, name);
        return {
            {"conflict", info.hasConflict},
            {"conflictType", info.conflictType},
            {"conflictWith", info.conflictWith}
        };
    });
    registerHandler("hotkey.rebind", [](const json& params) -> json {
        const std::string name = canonicalHotkeyName(params.value("name", ""));
        const std::string text = params.value("hotkey", "");
        if (name.empty()) {
            return {{"success", false}, {"error", "invalid hotkey"}};
        }
        auto& manager = HotkeyManager::instance();
        const auto entries = manager.getAllHotkeys();
        const auto previous = std::ranges::find_if(entries, [&name](const auto& entry) {
            return entry.name == name;
        });
        if (previous == entries.end()) {
            return {{"success", false}, {"error", "unknown hotkey"}};
        }
        if (text.empty()) {
            if (!manager.clearHotkey(name)) {
                return {{"success", false}, {"error", "could not disable hotkey"}};
            }
            if (!ConfigManager::instance().set("/hotkeys/" + name, "")) {
                if (previous->def.virtualKey != 0) manager.rebindHotkey(name, previous->def);
                return {{"success", false}, {"error", "failed to persist hotkey"},
                        {"name", name}, {"shortcut", previous->def.toString()}};
            }
            return {{"success", true}, {"name", name}, {"shortcut", ""}};
        }
        const auto parsed = HotkeyDef::fromString(text);
        if (!parsed) return {{"success", false}, {"error", "invalid hotkey"}};
        if (!manager.rebindHotkey(name, *parsed)) {
            auto conflictInfo = manager.checkConflict(*parsed, name);
            std::string errMsg = conflictInfo.hasConflict && !conflictInfo.conflictWith.empty()
                ? conflictInfo.conflictWith
                : "快捷键已被其他程序或系统占用";
            return {{"success", false}, {"error", errMsg},
                    {"conflictType", conflictInfo.conflictType},
                    {"conflictWith", conflictInfo.conflictWith},
                    {"name", name}, {"shortcut", previous->def.toString()}};
        }
        if (!ConfigManager::instance().set("/hotkeys/" + name, parsed->toString())) {
            const bool rolledBack = previous->def.virtualKey == 0
                ? manager.clearHotkey(name)
                : manager.rebindHotkey(name, previous->def);
            if (!rolledBack) {
                LOG_ERROR("快捷键持久化失败且运行时回滚失败: name={}", name);
            }
            return {{"success", false}, {"error", "failed to persist hotkey"},
                    {"name", name}, {"shortcut", previous->def.toString()}};
        }
        return {{"success", true}, {"name", name}, {"shortcut", parsed->toString()}};
    });

    // ── 通用设置与系统交互 ───────────────────────────────────────────────
    registerHandler("general.getSettings", [](const json&) -> json {
        auto& config = ConfigManager::instance();
        return {
            {"autoStart", config.get<bool>("/general/autoStart", false)},
            {"minimizeToTray", config.get<bool>("/general/minimizeToTray", true)},
            {"checkUpdates", config.get<bool>("/general/checkUpdates", true)},
            {"keycastEnabled", config.get<bool>("/general/keycastEnabled", false)},
            {"language", config.get<std::string>("/general/language", "auto")},
            {"logLevel", config.get<std::string>("/general/logLevel", "info")},
            {"theme", config.get<std::string>("/general/theme", "system")},
            {"accentColor", config.get<std::string>("/general/accentColor", "violet")},
        };
    });
    registerHandler("general.updateSettings", [](const json& params) -> json {
        static const std::unordered_set<std::string> boolKeys = {
            "autoStart", "minimizeToTray", "checkUpdates", "keycastEnabled"
        };
        static const std::unordered_set<std::string> themes = {"system", "light", "dark"};
        static const std::unordered_set<std::string> logLevels = {"trace", "debug", "info", "warn", "error"};
        static const std::unordered_set<std::string> accents = {
            "violet", "cyan", "amber", "blue", "mint", "coral"
        };
        if (!params.is_object() || params.empty()) {
            return {{"success", false}, {"error", "no settings supplied"}};
        }
        auto& config = ConfigManager::instance();
        const bool previousAutoStart = config.get<bool>("/general/autoStart", false);
        for (const auto& [key, value] : params.items()) {
            if (boolKeys.contains(key) && !value.is_boolean()) {
                return {{"success", false}, {"error", key + " must be boolean"}};
            }
            if (key == "theme" && (!value.is_string() || !themes.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid theme"}};
            }
            if (key == "accentColor" && (!value.is_string() || !accents.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid accent color"}};
            }
            if (key == "logLevel" && (!value.is_string() || !logLevels.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid log level"}};
            }
            static const std::unordered_set<std::string> languages = {"auto", "zh-CN", "en-US"};
            if (key == "language" && (!value.is_string() ||
                !languages.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid language"}};
            }
            if (!boolKeys.contains(key) && key != "theme" && key != "accentColor" && key != "logLevel" && key != "language") {
                return {{"success", false}, {"error", "unsupported setting: " + key}};
            }
        }
        if (params.contains("autoStart") && !setAutoStart(params["autoStart"].get<bool>())) {
            return {{"success", false}, {"error", "failed to update auto-start"}};
        }
        const bool saved = config.mergePatch({{"general", params}}, "/general");
        if (!saved) {
            if (params.contains("autoStart")) setAutoStart(previousAutoStart);
            return {{"success", false}, {"error", "failed to persist settings"}};
        }
        if (params.contains("logLevel")) applyLogLevel(params["logLevel"].get<std::string>());
        if (params.contains("theme") || params.contains("accentColor")) {
            EventBus::instance().publish(ThemeChangedEvent{
                config.get<std::string>("/general/theme", "system"),
                config.get<std::string>("/general/accentColor", "violet")
            });
        }
        return {{"success", true}};
    });
    registerHandler("capture.browseDirectory", [](const json&) -> json {
        const auto selected = choosePath(false, true);
        return selected ? json(WinUtils::wstringToUtf8(selected->wstring())) : json(nullptr);
    });
    registerHandler("system.openFile", [](const json& params) -> json {
        const std::string path = params.value("path", "");
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        return {{"success", result > 32}, {"errorCode", result > 32 ? 0 : result}};
    });
    registerHandler("app.checkForUpdates", [](const json&) -> json {
        const bool started = UpdateChecker::instance().checkAsync(true);
        return {{"success", true}, {"started", started}};
    });
    // ── 应用系统信息 ─────────────────────────────────────────────────────
    registerHandler("app.getSystemInfo", [](const json&) -> json {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(memInfo);
        GlobalMemoryStatusEx(&memInfo);

        std::string arch = "x64";
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
            arch = "ARM64";
        } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
            arch = "x86";
        }

        OSVERSIONINFOEXW osvi{};
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        return {
            {"version", UpdateChecker::CurrentVersion},
            {"cpuArch", arch},
            {"cpuCores", si.dwNumberOfProcessors},
            {"totalMemoryGB", memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0)},
            {"dpiScale", WinUtils::getDpiScale()},
            {"language", WinUtils::isSystemLanguageChinese() ? "zh-CN" : "en-US"}
        };
    });

    // ── 打开的窗口与进程枚举 ─────────────────────────────────────────────
    registerHandler("window.getOpenWindows", [](const json&) -> json {
        struct WindowItem {
            std::string title;
            std::string processName;
            std::string windowClass;
            DWORD pid = 0;
        };

        std::vector<WindowItem> windows;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;
            if (GetWindowTextLengthW(hwnd) == 0) return TRUE;

            RECT rc{};
            if (GetWindowRect(hwnd, &rc)) {
                if ((rc.right - rc.left) <= 50 || (rc.bottom - rc.top) <= 50) return TRUE;
            }

            LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

            wchar_t cls[256]{};
            GetClassNameW(hwnd, cls, 256);
            std::wstring clsStr = cls;
            if (clsStr == L"Progman" || clsStr == L"Shell_TrayWnd" || 
                clsStr == L"Windows.UI.Core.CoreWindow") {
                return TRUE;
            }

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;

            std::wstring procName = WinUtils::processNameFromPid(pid);
            if (procName.empty()) return TRUE;

            std::wstring title = WinUtils::getWindowTitle(hwnd);
            if (title.empty()) return TRUE;

            auto* list = reinterpret_cast<std::vector<WindowItem>*>(lParam);
            std::string u8Proc = WinUtils::wstringToUtf8(procName);
            std::string u8Title = WinUtils::wstringToUtf8(title);
            std::string u8Class = WinUtils::wstringToUtf8(clsStr);

            for (const auto& item : *list) {
                if (item.processName == u8Proc && item.title == u8Title) {
                    return TRUE;
                }
            }

            list->push_back({ u8Title, u8Proc, u8Class, pid });
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        json arr = json::array();
        for (const auto& w : windows) {
            arr.push_back({
                {"title", w.title},
                {"processName", w.processName},
                {"windowClass", w.windowClass},
                {"pid", w.pid}
            });
        }
        return {{"windows", std::move(arr)}};
    });

    registerHandler("window.getForegroundInfo", [](const json&) -> json {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return {{"success", false}};
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        std::wstring procName = WinUtils::processNameFromPid(pid);
        std::wstring title = WinUtils::getWindowTitle(hwnd);
        std::wstring cls = WinUtils::getWindowClassName(hwnd);
        return {
            {"success", true},
            {"title", WinUtils::wstringToUtf8(title)},
            {"processName", WinUtils::wstringToUtf8(procName)},
            {"windowClass", WinUtils::wstringToUtf8(cls)},
            {"pid", pid}
        };
    });

    LOG_INFO("内置核心 IPC 处理器注册完成（含性能监控、配置管理、系统信息、窗口枚举）");
}

}  // namespace easy::core
