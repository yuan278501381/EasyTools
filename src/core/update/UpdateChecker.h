#pragma once

#include "core/utils/Export.h"
#include "EasyToolsVersion.h"

#include <atomic>
#include <mutex>
#include <string_view>
#include <thread>

namespace easy::core {

/// Bounded, non-blocking GitHub release checker.
class EASYCORE_API UpdateChecker final {
public:
    static UpdateChecker& instance();

    /// Starts a background check. A started check emits `update.result`.
    bool checkAsync(bool force = false);
    void shutdown();

    static constexpr const char* CurrentVersion = easy::version::String;
    static bool isNewerVersion(std::string_view candidate, std::string_view current);

private:
    UpdateChecker() = default;
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    void workerMain(std::stop_token stopToken);

    std::mutex m_workerMutex;
    std::mutex m_requestMutex;
    std::jthread m_worker;
    void* m_activeRequest = nullptr;
    std::atomic<bool> m_busy{false};
    std::atomic<bool> m_stopping{false};
};

}  // namespace easy::core
