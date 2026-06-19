// ─────────────────────────────────────────────────────────────────────────────
// ScopeRule.cpp — 作用域规则引擎实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/ScopeRule.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <tlhelp32.h>

namespace easy::gesture {

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
                // 简单通配符: 将 * 转为正则 .*
                std::wstring pattern = wideClass;
                // 转义所有特殊字符，然后把 * 替换为 .*
                std::wstring regexStr;
                for (wchar_t ch : pattern) {
                    if (ch == L'*') regexStr += L".*";
                    else if (ch == L'?') regexStr += L".";
                    else if (ch == L'.' || ch == L'(' || ch == L')' || ch == L'[' || ch == L']') {
                        regexStr += L"\\";
                        regexStr += ch;
                    } else {
                        regexStr += ch;
                    }
                }
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
                std::wstring regexStr;
                for (wchar_t ch : wideProcName) {
                    if (ch == L'*') regexStr += L".*";
                    else if (ch == L'?') regexStr += L".";
                    else {
                        regexStr += ch;
                    }
                }
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
    rule.matchMode = static_cast<MatchMode>(j.value("matchMode", 0));
    rule.effect = static_cast<RuleEffect>(j.value("effect", 0));
    rule.profileName = j.value("profileName", "");
    return rule;
}

// ── ScopeRuleEngine ──────────────────────────────────────────────────────────

std::optional<std::string> ScopeRuleEngine::evaluate(HWND hwnd) const {
    auto info = getWindowInfo(hwnd);

    // 按优先级排序: 句柄规则 > 类名规则 > 进程规则
    // 先检查句柄规则
    for (const auto& rule : m_rules) {
        if (rule.windowHandle != nullptr && rule.matches(hwnd, info.processName, info.className)) {
            if (rule.effect == RuleEffect::Disable) return std::nullopt;
            if (rule.effect == RuleEffect::UseProfile) return rule.profileName;
            return "";  // Enable, 使用全局默认
        }
    }

    // 再检查类名规则
    for (const auto& rule : m_rules) {
        if (rule.windowHandle == nullptr && !rule.windowClass.empty()
            && rule.matches(hwnd, info.processName, info.className)) {
            if (rule.effect == RuleEffect::Disable) return std::nullopt;
            if (rule.effect == RuleEffect::UseProfile) return rule.profileName;
            return "";
        }
    }

    // 最后检查进程规则
    for (const auto& rule : m_rules) {
        if (rule.windowHandle == nullptr && rule.windowClass.empty() && !rule.processName.empty()
            && rule.matches(hwnd, info.processName, info.className)) {
            if (rule.effect == RuleEffect::Disable) return std::nullopt;
            if (rule.effect == RuleEffect::UseProfile) return rule.profileName;
            return "";
        }
    }

    // 没有匹配的规则，使用全局默认
    return "";
}

void ScopeRuleEngine::addRule(const ScopeRule& rule) {
    m_rules.push_back(rule);
    LOG_DEBUG("添加作用域规则: id={}, name={}, process={}, class={}",
              rule.id, rule.name, rule.processName, rule.windowClass);
}

void ScopeRuleEngine::removeRule(const std::string& ruleId) {
    std::erase_if(m_rules, [&](const ScopeRule& r) { return r.id == ruleId; });
}

void ScopeRuleEngine::addRules(const std::vector<ScopeRule>& rules) {
    for (const auto& rule : rules) {
        addRule(rule);
    }
}

void ScopeRuleEngine::removeRules(const std::vector<std::string>& ruleIds) {
    for (const auto& id : ruleIds) {
        removeRule(id);
    }
}

void ScopeRuleEngine::loadFromJson(const nlohmann::json& j) {
    m_rules.clear();
    if (j.is_array()) {
        for (const auto& item : j) {
            m_rules.push_back(ScopeRule::fromJson(item));
        }
    }
    LOG_INFO("作用域规则已加载, 规则数量={}", m_rules.size());
}

nlohmann::json ScopeRuleEngine::toJson() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& rule : m_rules) {
        j.push_back(rule.toJson());
    }
    return j;
}

ScopeRuleEngine::WindowInfo ScopeRuleEngine::getWindowInfo(HWND hwnd) const {
    WindowInfo info;
    info.hwnd = hwnd;
    info.className = easy::core::WinUtils::getWindowClassName(hwnd);

    auto procName = easy::core::WinUtils::getForegroundProcessName();
    info.processName = procName.value_or(L"");

    return info;
}

}  // namespace easy::gesture
