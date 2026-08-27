#pragma once

#include "core/utils/Export.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

namespace easy::core {

/// 宿主拥有的 Shell STA 工作器。插件只提交路径，绝不让执行中的线程跨越 DLL 生命周期。
class EASYCORE_API ShellContextMenuService final {
public:
    static ShellContextMenuService& instance();

    bool showAsync(std::wstring path, int screenX = -1, int screenY = -1);
    void shutdown();

private:
    ShellContextMenuService() = default;
    ~ShellContextMenuService();
    ShellContextMenuService(const ShellContextMenuService&) = delete;
    ShellContextMenuService& operator=(const ShellContextMenuService&) = delete;

    void run(std::wstring path, int screenX, int screenY, std::stop_token stop);

    std::mutex m_mutex;
    std::jthread m_worker;
    std::atomic<HWND> m_helperWindow{nullptr};
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_stopping{false};
};

}  // namespace easy::core
