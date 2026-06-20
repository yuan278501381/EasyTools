#include "core/stats/StatsManager.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace easy::core {

nlohmann::json DailyStats::toJson() const {
    nlohmann::json j;
    j["totalKeys"] = totalKeys;
    j["leftClicks"] = leftClicks;
    j["rightClicks"] = rightClicks;
    j["mouseDistance"] = mouseDistance;
    j["scrolls"] = scrolls;
    nlohmann::json mapJson = nlohmann::json::object();
    for (const auto& [k, v] : keyMap) {
        mapJson[std::to_string(k)] = v;
    }
    j["keyMap"] = mapJson;
    return j;
}

DailyStats DailyStats::fromJson(const nlohmann::json& j) {
    DailyStats stats;
    stats.totalKeys = j.value("totalKeys", 0ULL);
    stats.leftClicks = j.value("leftClicks", 0ULL);
    stats.rightClicks = j.value("rightClicks", 0ULL);
    stats.mouseDistance = j.value("mouseDistance", 0.0);
    stats.scrolls = j.value("scrolls", 0ULL);
    if (j.contains("keyMap") && j["keyMap"].is_object()) {
        for (auto& [k, v] : j["keyMap"].items()) {
            stats.keyMap[std::stoi(k)] = v.get<uint64_t>();
        }
    }
    return stats;
}

StatsManager& StatsManager::instance() {
    static StatsManager inst;
    return inst;
}

std::string StatsManager::getCurrentDateStr() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_s(&tm_now, &time_t_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d");
    return oss.str();
}

void StatsManager::initialize() {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return;

    m_currentDate = getCurrentDateStr();
    loadFromFile();
    m_initialized = true;
    LOG_INFO("按键统计管理器初始化成功, 日期={}", m_currentDate);
}

void StatsManager::shutdown() {
    std::lock_guard lock(m_mutex);
    if (!m_initialized) return;
    saveToFile();
    m_initialized = false;
    LOG_INFO("按键统计管理器已关闭");
}

void StatsManager::loadFromFile() {
    auto path = WinUtils::getExeDirectory() / L"data" / L"stats.json";
    if (!std::filesystem::exists(path)) {
        m_historyData = nlohmann::json::object();
        return;
    }
    try {
        std::ifstream ifs(path);
        m_historyData = nlohmann::json::parse(ifs);
        if (m_historyData.contains(m_currentDate)) {
            m_todayStats = DailyStats::fromJson(m_historyData[m_currentDate]);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("加载统计数据失败: {}", e.what());
        m_historyData = nlohmann::json::object();
    }
}

void StatsManager::saveToFile() {
    auto dir = WinUtils::getExeDirectory() / L"data";
    std::filesystem::create_directories(dir);
    auto path = dir / L"stats.json";
    
    m_historyData[m_currentDate] = m_todayStats.toJson();
    
    try {
        std::ofstream ofs(path);
        ofs << m_historyData.dump(4); // 格式化输出
    } catch (const std::exception& e) {
        LOG_ERROR("保存统计数据失败: {}", e.what());
    }
}

void StatsManager::checkDateRollover() {
    std::string today = getCurrentDateStr();
    if (today != m_currentDate) {
        // 保存昨天的数据
        saveToFile();
        m_currentDate = today;
        m_todayStats = DailyStats(); // 新的一天重置内存
        // 如果文件中已经有今天的数据(比如异常退出再进)，尝试加载
        if (m_historyData.contains(m_currentDate)) {
            m_todayStats = DailyStats::fromJson(m_historyData[m_currentDate]);
        }
    }
}

// 节流保存
void StatsManager::recordKey(int virtualKey) {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    m_todayStats.totalKeys++;
    m_todayStats.keyMap[virtualKey]++;
}

void StatsManager::recordLeftClick() {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    m_todayStats.leftClicks++;
}

void StatsManager::recordRightClick() {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    m_todayStats.rightClicks++;
}

void StatsManager::recordScroll() {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    m_todayStats.scrolls++;
}

void StatsManager::recordMouseDistance(double pixels) {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    m_todayStats.mouseDistance += pixels;
    
    // 每累加一定距离或操作后自动保存，这里做个简单节流
    uint64_t currentTick = GetTickCount64();
    if (currentTick - m_lastSaveTick > 60000) { // 至少一分钟存一次
        saveToFile();
        m_lastSaveTick = currentTick;
    }
}

DailyStats StatsManager::getTodayStats() {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    return m_todayStats;
}

nlohmann::json StatsManager::getHistory(int days) {
    std::lock_guard lock(m_mutex);
    checkDateRollover();
    
    // 为了防止还没 save 的 todayStats 没在 history 里面
    m_historyData[m_currentDate] = m_todayStats.toJson();

    nlohmann::json result = nlohmann::json::object();
    
    // 我们找出最近的 days 天。
    // 计算前推几天
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < days; ++i) {
        auto time_t_past = std::chrono::system_clock::to_time_t(now - std::chrono::hours(24 * i));
        std::tm tm_past{};
        localtime_s(&tm_past, &time_t_past);
        std::ostringstream oss;
        oss << std::put_time(&tm_past, "%Y-%m-%d");
        std::string dateStr = oss.str();
        
        if (m_historyData.contains(dateStr)) {
            result[dateStr] = m_historyData[dateStr];
        } else {
            result[dateStr] = DailyStats().toJson(); // 空数据
        }
    }
    
    return result;
}

void StatsManager::clearToday() {
    std::lock_guard lock(m_mutex);
    m_todayStats = DailyStats();
    saveToFile();
}

} // namespace easy::core
