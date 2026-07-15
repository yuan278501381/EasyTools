// ─────────────────────────────────────────────────────────────────────────────
// ScopeRule.cpp — 作用域规则引擎实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/ScopeRule.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <tlhelp32.h>
#include <unordered_map>
#include <list>

namespace easy::gesture {

namespace {

std::wstring wildcardToRegex(const std::wstring& pattern) {
    std::wstring result;
    result.reserve(pattern.size() * 2);
    for (const wchar_t ch : pattern) {
        if (ch == L'*') result += L".*";
        else if (ch == L'?') result += L'.';
        else {
            if (std::wstring_view(L".^$|()[]{}+\\").find(ch) != std::wstring_view::npos) {
                result += L'\\';
            }
            result += ch;
        }
    }
    return result;
}

}  // namespace

// ── ScopeRule::matches ───────────────────────────────────────────────────────

bool ScopeRule::matches(HWND hwnd, const std::wstring& procName, const std::wstring& className) const {
    if (!enabled) return false;

    // 优先级 1: 窗口句柄精确匹配
    if (windowHandle != nullptr) {
        return hwnd == windowHandle;
    }

    // 优先级 2: 窗口类名匹配
    if (!windowClass.empty()) {
        std::wstring wideClass = easy::core::WinUtils::utf8ToWstring(windowClass);
        switch (matchMode) {
            case MatchMode::Exact:
                if (className == wideClass) return true;
                break;
            case MatchMode::Wildcard: {
                const std::wstring regexStr = wildcardToRegex(wideClass);
                try {
                    std::wregex re(regexStr, std::regex_constants::icase);
                    if (std::regex_match(className, re)) return true;
                } catch (...) {}
                break;
            }
            case MatchMode::Regex: {
                try {
                    std::wregex re(wideClass, std::regex_constants::icase);
                    if (std::regex_match(className, re)) return true;
                } catch (...) {}
                break;
            }
        }
    }

    // 优先级 3: 进程名匹配
    if (!processName.empty()) {
        std::wstring wideProcName = easy::core::WinUtils::utf8ToWstring(processName);
        switch (matchMode) {
            case MatchMode::Exact: {
                // 不区分大小写比较
                std::wstring lowerProc = procName;
                std::wstring lowerTarget = wideProcName;
                std::transform(lowerProc.begin(), lowerProc.end(), lowerProc.begin(), ::towlower);
                std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::towlower);
                if (lowerProc == lowerTarget) return true;
                break;
            }
            case MatchMode::Wildcard: {
                const std::wstring regexStr = wildcardToRegex(wideProcName);
                try {
                    std::wregex re(regexStr, std::regex_constants::icase);
                    if (std::regex_match(procName, re)) return true;
                } catch (...) {}
                break;
            }
            case MatchMode::Regex: {
                try {
                    std::wregex re(wideProcName, std::regex_constants::icase);
                    if (std::regex_match(procName, re)) return true;
                } catch (...) {}
                break;
            }
        }
    }

    return false;
}

// ── ScopeRule JSON ───────────────────────────────────────────────────────────

nlohmann::json ScopeRule::toJson() const {
    return {
        {"id", id},
        {"name", name},
        {"enabled", enabled},
        {"processName", processName},
        {"windowClass", windowClass},
        {"matchMode", static_cast<int>(matchMode)},
        {"effect", static_cast<int>(effect)},
        {"profileName", profileName}
    };
}

ScopeRule ScopeRule::fromJson(const nlohmann::json& j) {
    ScopeRule rule;
    rule.id = j.value("id", "");
    rule.name = j.value("name", "");
    rule.enabled = j.value("enabled", true);
    rule.processName = j.value("processName", "");
    rule.windowClass = j.value("windowClass", "");
    const int matchMode = j.value("matchMode", 0);
    const int effect = j.value("effect", 0);
    rule.matchMode = static_cast<MatchMode>(std::clamp(matchMode, 0, 2));
    rule.effect = static_cast<RuleEffect>(std::clamp(effect, 0, 2));
    rule.profileName = j.value("profileName", "");
    return rule;
}

// ── ScopeRuleEngine ──────────────────────────────────────────────────────────

std::optional<std::string> ScopeRuleEngine::evaluate(HWND hwnd) const {
    std::lock_guard lock(m_mutex);
    // ── LRU 缓存：按 (PID, className) 缓存评估结果 ──────────────────
    // 在鼠标钩子热路径上，前台窗口很少变化，缓存命中率 >95%
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    auto className = easy::core::WinUtils::getWindowClassName(hwnd);
    uint64_t cacheKey = (static_cast<uint64_t>(pid) << 32) | std::hash<std::wstring>{}(className);

    {
        auto it = m_evaluateCache.find(cacheKey);
        if (it != m_evaluateCache.end()) {
            // 缓存命中 — 将此 key 移到 LRU 头部
            m_cacheLru.splice(m_cacheLru.begin(), m_cacheLru, it->second.lruIt);
            return it->second.result;
        }
    }

    auto info = getWindowInfo(hwnd);

    // 按优先级排序: 句柄规则 > 类名规则 > 进程规则
    std::optional<std::string> result = "";
    bool matchedHigherPriority = false;

    // 先检查句柄规则
    for (const auto& rule : m_rules) {
        if (rule.windowHandle != nullptr && rule.matches(hwnd, info.processName, info.className)) {
            matchedHigherPriority = true;
            if (rule.effect == RuleEffect::Disable) { result = std::nullopt; break; }
            if (rule.effect == RuleEffect::UseProfile) { result = rule.profileName; break; }
            break;
        }
    }

    // 再检查类名规则（如果句柄规则未匹配）
    if (!matchedHigherPriority && result.has_value() && result.value().empty()) {
        for (const auto& rule : m_rules) {
            if (rule.windowHandle == nullptr && !rule.windowClass.empty()
                && rule.matches(hwnd, info.processName, info.className)) {
                matchedHigherPriority = true;
                if (rule.effect == RuleEffect::Disable) { result = std::nullopt; break; }
                if (rule.effect == RuleEffect::UseProfile) { result = rule.profileName; break; }
                break;
            }
        }
    }

    // 最后检查进程规则
    if (!matchedHigherPriority && result.has_value() && result.value().empty()) {
        for (const auto& rule : m_rules) {
            if (rule.windowHandle == nullptr && rule.windowClass.empty() && !rule.processName.empty()
                && rule.matches(hwnd, info.processName, info.className)) {
                if (rule.effect == RuleEffect::Disable) { result = std::nullopt; break; }
                if (rule.effect == RuleEffect::UseProfile) { result = rule.profileName; break; }
                break;
            }
        }
    }

    // 写入 LRU 缓存
    if (m_cacheLru.size() >= MAX_CACHE_SIZE) {
        auto evictKey = m_cacheLru.back();
        m_cacheLru.pop_back();
        m_evaluateCache.erase(evictKey);
    }
    m_cacheLru.push_front(cacheKey);
    m_evaluateCache[cacheKey] = {result, m_cacheLru.begin()};

    return result;
}

void ScopeRuleEngine::addRule(const ScopeRule& rule) {
    std::lock_guard lock(m_mutex);
    m_rules.push_back(rule);
    invalidateCache();  // 规则变更时清除缓存
    LOG_DEBUG("添加作用域规则: id={}, name={}, process={}, class={}",
              rule.id, rule.name, rule.processName, rule.windowClass);
}

void ScopeRuleEngine::removeRule(const std::string& ruleId) {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_rules, [&](const ScopeRule& r) { return r.id == ruleId; });
    invalidateCache();  // 规则变更时清除缓存
}

void ScopeRuleEngine::addRules(const std::vector<ScopeRule>& rules) {
    std::lock_guard lock(m_mutex);
    for (const auto& rule : rules) {
        m_rules.push_back(rule);
    }
    invalidateCache();
}

void ScopeRuleEngine::removeRules(const std::vector<std::string>& ruleIds) {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_rules, [&](const ScopeRule& rule) {
        return std::find(ruleIds.begin(), ruleIds.end(), rule.id) != ruleIds.end();
    });
    invalidateCache();
}

void ScopeRuleEngine::loadFromJson(const nlohmann::json& j) {
    std::lock_guard lock(m_mutex);
    m_rules.clear();
    if (j.is_array()) {
        for (const auto& item : j) {
            m_rules.push_back(ScopeRule::fromJson(item));
        }
    }
    invalidateCache();  // 规则变更时清除缓存
    LOG_INFO("作用域规则已加载, 规则数量={}", m_rules.size());
}

nlohmann::json ScopeRuleEngine::toJson() const {
    std::lock_guard lock(m_mutex);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& rule : m_rules) {
        j.push_back(rule.toJson());
    }
    return j;
}

std::vector<ScopeRule> ScopeRuleEngine::getRules() const {
    std::lock_guard lock(m_mutex);
    return m_rules;
}

void ScopeRuleEngine::clearRules() {
    std::lock_guard lock(m_mutex);
    m_rules.clear();
    invalidateCache();
}

ScopeRuleEngine::WindowInfo ScopeRuleEngine::getWindowInfo(HWND hwnd) const {
    WindowInfo info;
    info.hwnd = hwnd;
    info.className = easy::core::WinUtils::getWindowClassName(hwnd);

    // PID → 进程名 LRU 缓存（避免在热路径上重复调用 OpenProcess + QueryFullProcessImageName）
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    auto it = m_processNameCache.find(pid);
    if (it != m_processNameCache.end()) {
        info.processName = it->second;
    } else {
        info.processName = easy::core::WinUtils::processNameFromPid(pid);
        // 缓存上限 64 个进程名
        if (m_processNameCache.size() >= 64) {
            m_processNameCache.clear();  // 简单策略：满了全清
        }
        m_processNameCache[pid] = info.processName;
    }

    return info;
}

void ScopeRuleEngine::invalidateCache() const {
    m_evaluateCache.clear();
    m_cacheLru.clear();
    m_processNameCache.clear();
    LOG_TRACE("ScopeRuleEngine: 缓存已失效");
}

}  // namespace easy::gesture
