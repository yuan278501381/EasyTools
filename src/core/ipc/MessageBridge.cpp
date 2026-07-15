// ─────────────────────────────────────────────────────────────────────────────
// MessageBridge.cpp — WebView2 ↔ C++ IPC 桥接层实现
// ─────────────────────────────────────────────────────────────────────────────

#include "core/ipc/MessageBridge.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/utils/WinUtils.h"
#include "core/stats/StatsManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"

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
    std::unique_lock lock(m_mutex);
    m_handlers[method] = std::move(handler);
    LOG_TRACE("注册 IPC 处理器: method={}", method);
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

        MessageHandler handler;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_handlers.find(method);
            if (it != m_handlers.end()) handler = it->second;
        }
        if (!handler) {
            LOG_WARN("未知的 IPC 方法: {}", method);
            json response = {
                {"id", id},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}
            };
            return response.dump();
        }

        json result = handler(params);
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
    decltype(m_handlers) handlers;
    EventPusher eventPusher;
    {
        std::unique_lock lock(m_mutex);
        handlers.swap(m_handlers);
        eventPusher.swap(m_eventPusher);
    }
    eventPusher = nullptr;
    handlers.clear();
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

void MessageBridge::registerBuiltinHandlers() {
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
            hotkeys.push_back({{"name", entry.name}, {"shortcut", entry.def.toString()}});
        }
        std::sort(hotkeys.begin(), hotkeys.end(), [](const json& a, const json& b) {
            return a.value("name", "") < b.value("name", "");
        });
        return hotkeys;
    });
    registerHandler("hotkey.rebind", [](const json& params) -> json {
        const std::string name = canonicalHotkeyName(params.value("name", ""));
        const std::string text = params.value("hotkey", "");
        const auto parsed = HotkeyDef::fromString(text);
        if (name.empty() || !parsed) {
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
        if (!manager.rebindHotkey(name, *parsed)) {
            return {{"success", false}, {"error", "hotkey is unavailable"},
                    {"name", name}, {"shortcut", previous->def.toString()}};
        }
        if (!ConfigManager::instance().set("/hotkeys/" + name, parsed->toString())) {
            if (!manager.rebindHotkey(name, previous->def)) {
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
        };
    });
    registerHandler("general.updateSettings", [](const json& params) -> json {
        static const std::unordered_set<std::string> boolKeys = {
            "autoStart", "minimizeToTray", "checkUpdates", "keycastEnabled"
        };
        static const std::unordered_set<std::string> themes = {"system", "light", "dark"};
        static const std::unordered_set<std::string> logLevels = {"trace", "debug", "info", "warn", "error"};
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
            if (key == "logLevel" && (!value.is_string() || !logLevels.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid log level"}};
            }
            static const std::unordered_set<std::string> languages = {"auto", "zh-CN", "en-US"};
            if (key == "language" && (!value.is_string() ||
                !languages.contains(value.get<std::string>()))) {
                return {{"success", false}, {"error", "invalid language"}};
            }
            if (!boolKeys.contains(key) && key != "theme" && key != "logLevel" && key != "language") {
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

    LOG_INFO("内置核心 IPC 处理器注册完成（含性能监控、配置管理、系统信息）");
}

}  // namespace easy::core
