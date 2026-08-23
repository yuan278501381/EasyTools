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
#include "core/utils/ShellContextMenuService.h"
#include "core/stats/StatsManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"
#include "core/events/EventBus.h"

#include <algorithm>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <shobjidl.h>
#include <shellapi.h>

namespace {

using json = nlohmann::json;
constexpr size_t MaxBridgeMessageBytes = 1024 * 1024;

std::string bridgeErrorResponse(int id, int code, std::string_view message) {
    return json{{"id", id}, {"error", {{"code", code}, {"message", std::string(message)}}}}.dump();
}

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

namespace {

constexpr wchar_t AUTOSTART_TASK_NAME[] = L"EasyTools_Autostart";

bool executeSilentCommand(const std::wstring& cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmdBuffer = cmd;

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode == 0;
}

bool removeRegistryAutoStart() {
    constexpr wchar_t keyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, L"EasyTools");
        RegCloseKey(key);
    }
    return true;
}

bool setRegistryAutoStart(bool enabled) {
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

bool isAutoStartEnabled() {
    std::wstring queryCmd = L"schtasks.exe /query /tn \"" + std::wstring(AUTOSTART_TASK_NAME) + L"\"";
    if (executeSilentCommand(queryCmd)) {
        return true;
    }
    constexpr wchar_t keyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        LSTATUS status = RegQueryValueExW(key, L"EasyTools", nullptr, &type, nullptr, nullptr);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS) return true;
    }
    return false;
}

} // namespace

bool setAutoStart(bool enabled) {
    wchar_t exePath[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (length == 0 || length >= std::size(exePath)) {
        return setRegistryAutoStart(enabled);
    }

    std::wstring exeStr(exePath, length);

    if (enabled) {
        // 幂等性清理：确保旧注册表 Run 项无冗余残留
        removeRegistryAutoStart();

        // 注册 Windows 任务计划（Task Scheduler）：
        // /sc onlogon: 用户登录时触发
        // /delay 0000:10: 延时 10 秒（排在系统与常规自启软件最后，零抢占 I/O）
        // /rl highest: 以最高特权（管理员身份）免 UAC 弹窗静默运行
        // /f: 强制原子化覆盖更新（保证多次安装/覆盖安装/路径变更时严格幂等）
        std::wstring schCmd = L"schtasks.exe /create /tn \"" + std::wstring(AUTOSTART_TASK_NAME) +
                              L"\" /tr \"\\\"" + exeStr + L"\\\" --silent\" /sc onlogon /delay 0000:10 /rl highest /f";

        bool taskOk = executeSilentCommand(schCmd);
        if (taskOk) {
            LOG_INFO("AutoStart Task Scheduler task '{}' created/updated idempotently with 10s delay & highest privileges.",
                     "EasyTools_Autostart");
            return true;
        }

        LOG_WARN("Task Scheduler autostart failed, falling back to Registry Run key.");
        return setRegistryAutoStart(true);
    } else {
        // 幂等性删除任务计划（/f 强制删除，静默忽略不存在错误）
        std::wstring delCmd = L"schtasks.exe /delete /tn \"" + std::wstring(AUTOSTART_TASK_NAME) + L"\" /f";
        executeSilentCommand(delCmd);
        removeRegistryAutoStart();
        LOG_INFO("AutoStart disabled idempotently (Task Scheduler and Registry entries cleared).");
        return true;
    }
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
    if (messageJson.size() > MaxBridgeMessageBytes) {
        LOG_WARN("拒绝过大的 IPC 消息: {} bytes", messageJson.size());
        return bridgeErrorResponse(id, -32600, "Request exceeds 1 MiB limit");
    }
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

        const auto start = std::chrono::steady_clock::now();
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
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        LOG_DEBUG("IPC 响应完成: method={}, id={}, 耗时={}us", method, id, elapsedUs);

        json response = {
            {"id", id},
            {"result", result}
        };
        return response.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("IPC 处理器异常: id={}, error={}", id, e.what());
        json response = {
            {"id", id},
            {"error", {{"code", -32603}, {"message", std::string("Internal error: ") + e.what()}}}
        };
        return response.dump();
    } catch (...) {
        LOG_ERROR("IPC 处理器未知异常: id={}", id);
        json response = {
            {"id", id},
            {"error", {{"code", -32603}, {"message", "Internal error: unknown exception"}}}
        };
        return response.dump();
    }
}

// ── 异步方法线程池 ──────────────────────────────────────────────────────────
//
// 队列里只保存原始消息文本和响应回调，处理器是任务真正执行时才从 m_handlers
// 查找的。因此排队中的任务不会持有插件 DLL 内的 std::function，插件卸载不会
// 留下指向已卸载模块的悬空调用。

namespace {

constexpr size_t AsyncWorkerCount = 4;

/// 队列上限。搜索场景下堆积几乎都来自连续击键，因此过载时丢弃最旧的一条，
/// 并立刻给它回一个错误响应，避免前端 Promise 悬挂到超时。
constexpr size_t MaxQueuedAsyncJobs = 64;

std::mutex g_workerPoolMutex;

}  // namespace

struct MessageBridge::WorkerPool {
    struct Job {
        std::string message;
        AsyncResponder responder;
    };

    std::vector<std::thread> threads;
    std::deque<Job> jobs;
    std::mutex mutex;
    std::condition_variable cv;
    bool stopping = false;
};

MessageBridge::WorkerPool& MessageBridge::ensureWorkerPool() {
    std::lock_guard guard(g_workerPoolMutex);
    if (m_workerPool) return *m_workerPool;

    auto* pool = new WorkerPool();
    pool->threads.reserve(AsyncWorkerCount);
    for (size_t i = 0; i < AsyncWorkerCount; ++i) {
        pool->threads.emplace_back([pool]() {
            for (;;) {
                WorkerPool::Job job;
                {
                    std::unique_lock lock(pool->mutex);
                    pool->cv.wait(lock, [pool] { return pool->stopping || !pool->jobs.empty(); });
                    if (pool->jobs.empty()) return;  // 仅在 stopping 时成立
                    job = std::move(pool->jobs.front());
                    pool->jobs.pop_front();
                }
                std::string response = MessageBridge::instance().handleMessage(job.message);
                try {
                    job.responder(std::move(response));
                } catch (const std::exception& e) {
                    LOG_ERROR("异步 IPC 响应回调异常: {}", e.what());
                } catch (...) {
                    LOG_ERROR("异步 IPC 响应回调未知异常");
                }
            }
        });
    }
    m_workerPool = pool;
    LOG_DEBUG("异步 IPC 线程池已启动: threads={}", AsyncWorkerCount);
    return *pool;
}

void MessageBridge::shutdownWorkerPool() {
    WorkerPool* pool = nullptr;
    {
        std::lock_guard guard(g_workerPoolMutex);
        pool = m_workerPool;
        m_workerPool = nullptr;
    }
    if (!pool) return;

    std::deque<WorkerPool::Job> abandoned;
    {
        std::lock_guard lock(pool->mutex);
        pool->stopping = true;
        abandoned.swap(pool->jobs);
    }
    pool->cv.notify_all();
    for (auto& thread : pool->threads) {
        if (thread.joinable()) thread.join();
    }

    // 让仍在排队的请求立刻失败，而不是让前端等到超时。
    for (auto& job : abandoned) {
        if (!job.responder) continue;
        json response = {
            {"id", 0},
            {"error", {{"code", -32000}, {"message", "Bridge is shutting down"}}}
        };
        try {
            job.responder(response.dump());
        } catch (...) {
        }
    }
    delete pool;
    LOG_DEBUG("异步 IPC 线程池已关闭");
}

void MessageBridge::markMethodAsync(const std::string& method) {
    {
        std::unique_lock lock(m_mutex);
        m_asyncMethods.insert(method);
    }
    ensureWorkerPool();
    LOG_DEBUG("IPC 方法已标记为异步执行: {}", method);
}

void MessageBridge::handleMessageAsync(const std::string& messageJson, AsyncResponder responder) {
    if (!responder) return;

    if (messageJson.size() > MaxBridgeMessageBytes) {
        LOG_WARN("拒绝过大的异步 IPC 消息: {} bytes", messageJson.size());
        responder(bridgeErrorResponse(0, -32600, "Request exceeds 1 MiB limit"));
        return;
    }

    int id = 0;
    bool runAsync = false;
    try {
        auto request = json::parse(messageJson);
        id = request.value("id", 0);
        const std::string method = request.value("method", "");
        std::shared_lock lock(m_mutex);
        runAsync = m_asyncMethods.find(method) != m_asyncMethods.end();
    } catch (...) {
        // 解析失败时交给同步路径，由它统一产出格式正确的错误响应。
        runAsync = false;
    }

    if (!runAsync) {
        responder(handleMessage(messageJson));
        return;
    }

    auto& pool = ensureWorkerPool();
    WorkerPool::Job evicted;
    bool hasEvicted = false;
    {
        std::lock_guard lock(pool.mutex);
        if (pool.stopping) {
            json response = {
                {"id", id},
                {"error", {{"code", -32000}, {"message", "Bridge is shutting down"}}}
            };
            responder(response.dump());
            return;
        }
        if (pool.jobs.size() >= MaxQueuedAsyncJobs) {
            evicted = std::move(pool.jobs.front());
            pool.jobs.pop_front();
            hasEvicted = true;
        }
        pool.jobs.push_back(WorkerPool::Job{messageJson, std::move(responder)});
    }
    pool.cv.notify_one();

    if (hasEvicted && evicted.responder) {
        LOG_WARN("异步 IPC 队列过载, 丢弃最旧的待处理请求");
        json response = {
            {"id", 0},
            {"error", {{"code", -32000}, {"message", "Request dropped: bridge queue overloaded"}}}
        };
        try {
            evicted.responder(response.dump());
        } catch (...) {
        }
    }
}

void MessageBridge::setEventPusher(EventPusher pusher) {
    std::unique_lock lock(m_mutex);
    m_eventPusher = std::move(pusher);
    LOG_DEBUG("事件推送器已设置");
}

void MessageBridge::clearHandlers() {
    // Stop async dispatch first: queued jobs look their handler up at execution
    // time, so draining them before clearing m_handlers avoids spurious
    // "method not found" responses during shutdown.
    shutdownWorkerPool();

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
        m_asyncMethods.clear();
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
        config.set("/plugins/" + id + "/enabled", false);
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
            const bool isExt = isExtensionInstalled(plugin.id) ||
                (plugin.id != "gesture" && plugin.id != "capture" && plugin.id != "search" && plugin.id != "keycast");
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
                {"error", plugin.error},
                {"isExtension", isExt}
            });
        }

        // 合并已安装的扩展插件
        auto& config = ConfigManager::instance();
        auto installedList = config.get<std::vector<std::string>>("/plugins/installedExtensions", {});
        const auto& catalog = getMarketplaceCatalog();
        for (const auto& item : catalog) {
            if (std::find(installedList.begin(), installedList.end(), item.id) != installedList.end() &&
                existingIds.find(item.id) == existingIds.end()) {
                plugins.push_back({
                    {"id", item.id},
                    {"name", item.name},
                    {"version", item.version},
                    {"fileName", "Plugin_" + item.id + ".dll"},
                    {"abiVersion", item.abiVersion},
                    {"capabilities", item.capabilities},
                    {"permissions", item.permissions},
                    {"enabled", false},
                    {"active", false},
                    {"restartRequired", false},
                    {"state", "unavailable"},
                    {"error", "扩展包尚未安装；旧版仅记录了目录状态"},
                    {"isExtension", true}
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
            return {
                {"success", false},
                {"restartRequired", false},
                {"error", "扩展市场安装器尚未开放，无法启用未加载的扩展包"}
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
                {"available", false},
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
        return {
            {"success", false},
            {"id", id},
            {"restartRequired", false},
            {"error", "扩展市场当前为预览目录，安全安装与签名校验完成前不会伪装安装成功"}
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
    registerHandler("stats.getKeyboardLockStates", [](const json&) -> json {
#if defined(_WIN32)
        const bool numLock = (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        const bool capsLock = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        const bool scrollLock = (::GetKeyState(VK_SCROLL) & 0x0001) != 0;
        return {
            {"numLock", numLock},
            {"capsLock", capsLock},
            {"scrollLock", scrollLock}
        };
#else
        return {
            {"numLock", false},
            {"capsLock", false},
            {"scrollLock", false}
        };
#endif
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
                {"armed", entry.armed},
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
            {"autoStart", config.get<bool>("/general/autoStart", isAutoStartEnabled())},
            {"runAsAdmin", config.get<bool>("/general/runAsAdmin", true)},
            {"elevated", WinUtils::isCurrentProcessElevated()},
            {"minimizeToTray", config.get<bool>("/general/minimizeToTray", true)},
            {"checkUpdates", config.get<bool>("/general/checkUpdates", true)},
            {"keycastEnabled", config.get<bool>("/general/keycastEnabled", false)},
            {"language", config.get<std::string>("/general/language", "auto")},
            {"logLevel", config.get<std::string>("/general/logLevel", "info")},
            {"theme", config.get<std::string>("/general/theme", "system")},
            {"accentColor", config.get<std::string>("/general/accentColor", "blue")},
        };
    });
    registerHandler("general.updateSettings", [](const json& params) -> json {
        static const std::unordered_set<std::string> boolKeys = {
            "autoStart", "runAsAdmin", "minimizeToTray", "checkUpdates", "keycastEnabled"
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
                config.get<std::string>("/general/accentColor", "blue")
            });
        }
        return {{"success", true}};
    });
    registerHandler("capture.browseDirectory", [](const json&) -> json {
        const auto selected = choosePath(false, true);
        return selected ? json(WinUtils::wstringToUtf8(selected->wstring())) : json(nullptr);
    });
    registerHandler("system.openFile", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        bool ok = WinUtils::openFile(wide);
        return {{"success", ok}};
    });
    registerHandler("system.openFolder", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        bool ok = WinUtils::openFolderAndSelectItem(wide);
        return {{"success", ok}};
    });
    registerHandler("system.copyText", [](const json& params) -> json {
        const std::string text = params.value("text", "");
        if (text.empty()) return {{"success", false}};
        const auto wide = WinUtils::utf8ToWstring(text);
        if (!OpenClipboard(nullptr)) return {{"success", false}};
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (hGlob) {
            void* pBuf = GlobalLock(hGlob);
            if (pBuf) {
                memcpy(pBuf, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(hGlob);
                SetClipboardData(CF_UNICODETEXT, hGlob);
            }
        }
        CloseClipboard();
        return {{"success", true}};
    });
    registerHandler("system.openFileAsAdmin", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        bool ok = WinUtils::openFileAsAdmin(wide);
        return {{"success", ok}};
    });
    registerHandler("system.showFileProperties", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        bool ok = WinUtils::showFileProperties(wide);
        return {{"success", ok}};
    });
    registerHandler("system.openWithNotepad", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        bool ok = WinUtils::openWithNotepad(wide);
        return {{"success", ok}};
    });
    registerHandler("system.renamePath", [](const json& params) -> json {
        const std::string oldPath = params.value("oldPath", params.value("path", ""));
        const std::string newName = params.value("newName", params.value("name", ""));
        if (oldPath.empty() || newName.empty()) return {{"success", false}, {"error", "invalid parameters"}};
        
        const auto wideOld = WinUtils::utf8ToWstring(oldPath);
        std::filesystem::path oldP(wideOld);
        std::error_code ec;
        if (!std::filesystem::exists(oldP, ec)) {
            return {{"success", false}, {"error", "源文件或目录不存在"}};
        }
        std::filesystem::path newP = oldP.parent_path() / WinUtils::utf8ToWstring(newName);
        if (std::filesystem::exists(newP, ec)) {
            return {{"success", false}, {"error", "目标同名文件或目录已存在"}};
        }
        std::filesystem::rename(oldP, newP, ec);
        if (ec) {
            return {{"success", false}, {"error", ec.message()}};
        }
        return {
            {"success", true},
            {"newPath", WinUtils::wstringToUtf8(newP.wstring())},
            {"newName", newName}
        };
    });
    registerHandler("system.showShellContextMenu", [](const json& params) -> json {
        const std::string path = params.value("path", params.value("filepath", ""));
        if (path.empty()) return {{"success", false}, {"error", "path is required"}};
        const auto wide = WinUtils::utf8ToWstring(path);
        const bool started = ShellContextMenuService::instance().showAsync(wide);
        return {{"success", started}, {"busy", !started}};
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
