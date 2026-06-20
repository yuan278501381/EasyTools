#ifndef EASYTOOLS_CORE_STATS_STATSMANAGER_H
#define EASYTOOLS_CORE_STATS_STATSMANAGER_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>

namespace easy::core {

struct DailyStats {
    uint64_t totalKeys = 0;
    uint64_t leftClicks = 0;
    uint64_t rightClicks = 0;
    double mouseDistance = 0.0;
    uint64_t scrolls = 0;
    std::unordered_map<int, uint64_t> keyMap;

    nlohmann::json toJson() const;
    static DailyStats fromJson(const nlohmann::json& j);
};

class StatsManager {
public:
    static StatsManager& instance();

    void initialize();
    void shutdown();

    // 记录事件
    void recordKey(int virtualKey);
    void recordLeftClick();
    void recordRightClick();
    void recordScroll();
    void recordMouseDistance(double pixels);

    // 获取数据
    DailyStats getTodayStats();
    nlohmann::json getHistory(int days);
    void clearToday();

private:
    StatsManager() = default;
    ~StatsManager() = default;

    std::string getCurrentDateStr() const;
    void loadFromFile();
    void saveToFile();
    void checkDateRollover(); // 检查是否跨天并清空内存或保存上一天

    std::mutex m_mutex;
    std::string m_currentDate;
    DailyStats m_todayStats;

    // 保存所有历史数据的 JSON 对象 (或者只加载当天，但由于要返回 history，我们把所有的都存一个大 json)
    nlohmann::json m_historyData;
    
    std::atomic<bool> m_initialized{false};
    uint64_t m_lastSaveTick = 0;
};

} // namespace easy::core

#endif // EASYTOOLS_CORE_STATS_STATSMANAGER_H
