#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include <windows.h>
#include <shellapi.h>
#include <nlohmann/json.hpp>
#include <string>

namespace easy::search {

class SearchPlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Search"; }
    const char* getVersion() const override { return "1.0.0"; }

    bool initialize() override {
        LOG_INFO("SearchPlugin: 初始化搜索引擎");

        auto& mb = easy::core::MessageBridge::instance();
        
        mb.registerHandler("search.query", [](const nlohmann::json& params) -> nlohmann::json {
            std::string query = params.value("query", "");
            if (query.empty()) {
                return {{"results", nlohmann::json::array()}};
            }

            char buffer[65536] = {0};
            DWORD bytesRead = 0;
            BOOL success = CallNamedPipeA(
                "\\\\.\\pipe\\EasyToolsSearchPipe",
                (LPVOID)query.c_str(),
                query.size(),
                buffer,
                sizeof(buffer) - 1,
                &bytesRead,
                2000 // 2 seconds timeout
            );

            if (success && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                try {
                    return nlohmann::json::parse(buffer);
                } catch (...) {
                    LOG_ERROR("SearchPlugin: 无法解析 JSON 结果");
                }
            } else {
                LOG_ERROR("SearchPlugin: 命名管道调用失败, error={}", GetLastError());
            }

            return {{"results", nlohmann::json::array()}};
        });

        mb.registerHandler("search.openFile", [](const nlohmann::json& params) -> nlohmann::json {
            std::string filepath = params.value("filepath", "");
            if (!filepath.empty()) {
                HINSTANCE result = ShellExecuteA(NULL, "open", filepath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                if ((INT_PTR)result <= 32) {
                    LOG_ERROR("SearchPlugin: 无法打开文件 {}, error={}", filepath, (INT_PTR)result);
                    return {{"success", false}};
                }
                return {{"success", true}};
            }
            return {{"success", false}};
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("SearchPlugin: 关闭");
    }
};

} // namespace easy::search

extern "C" __declspec(dllexport) easy::core::IPlugin* createPlugin() {
    return new easy::search::SearchPlugin();
}

extern "C" __declspec(dllexport) void destroyPlugin(easy::core::IPlugin* plugin) {
    delete plugin;
}
