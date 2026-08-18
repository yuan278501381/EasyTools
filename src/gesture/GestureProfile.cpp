// ─────────────────────────────────────────────────────────────────────────────
// GestureProfile.cpp — 手势配置集实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureProfile.h"
#include "core/logger/Logger.h"

namespace easy::gesture {

GestureProfile::GestureProfile(const std::string& name) : m_name(name) {}

void GestureProfile::addMapping(const GestureMapping& mapping) {
    // 如果已存在，覆盖
    auto it = m_codeIndex.find(mapping.gestureCode);
    if (it != m_codeIndex.end()) {
        m_mappings[it->second] = mapping;
        LOG_DEBUG("覆盖手势映射: profile={}, code={}, action={}",
                  m_name, mapping.gestureCode, mapping.action.name);
    } else {
        m_codeIndex[mapping.gestureCode] = m_mappings.size();
        m_mappings.push_back(mapping);
        LOG_DEBUG("添加手势映射: profile={}, code={}, action={}",
                  m_name, mapping.gestureCode, mapping.action.name);
    }
}

void GestureProfile::removeMapping(const std::string& gestureCode) {
    auto it = m_codeIndex.find(gestureCode);
    if (it != m_codeIndex.end()) {
        m_mappings.erase(m_mappings.begin() + static_cast<ptrdiff_t>(it->second));
        rebuildIndex();
    }
}

void GestureProfile::setMappingEnabled(const std::string& gestureCode, bool enabled) {
    auto it = m_codeIndex.find(gestureCode);
    if (it != m_codeIndex.end()) {
        m_mappings[it->second].enabled = enabled;
        LOG_DEBUG("设置手势状态: profile={}, code={}, enabled={}", m_name, gestureCode, enabled);
    }
}

std::optional<GestureAction> GestureProfile::findAction(const std::string& gestureCode) const {
    auto it = m_codeIndex.find(gestureCode);
    if (it != m_codeIndex.end()) {
        if (!m_mappings[it->second].enabled) {
            LOG_DEBUG("手势已禁用，忽略动作: profile={}, code={}", m_name, gestureCode);
            return std::nullopt;
        }
        return m_mappings[it->second].action;
    }
    return std::nullopt;
}

std::optional<GestureMapping> GestureProfile::findMapping(const std::string& gestureCode) const {
    auto it = m_codeIndex.find(gestureCode);
    if (it != m_codeIndex.end()) {
        return m_mappings[it->second];
    }
    return std::nullopt;
}

void GestureProfile::reorderMappings(const std::vector<std::string>& orderedCodes) {
    std::vector<GestureMapping> newMappings;
    newMappings.reserve(m_mappings.size());
    std::unordered_set<std::string> placed;

    for (const auto& code : orderedCodes) {
        if (auto it = m_codeIndex.find(code); it != m_codeIndex.end()) {
            newMappings.push_back(m_mappings[it->second]);
            placed.insert(code);
        }
    }

    // 保留未在 orderedCodes 中的已有映射
    for (const auto& m : m_mappings) {
        if (!placed.contains(m.gestureCode)) {
            newMappings.push_back(m);
        }
    }

    m_mappings = std::move(newMappings);
    rebuildIndex();
}

bool GestureProfile::moveMapping(size_t fromIndex, size_t toIndex) {
    if (fromIndex >= m_mappings.size() || toIndex >= m_mappings.size() || fromIndex == toIndex) {
        return false;
    }
    auto item = m_mappings[fromIndex];
    m_mappings.erase(m_mappings.begin() + static_cast<ptrdiff_t>(fromIndex));
    m_mappings.insert(m_mappings.begin() + static_cast<ptrdiff_t>(toIndex), item);
    rebuildIndex();
    return true;
}

TriggerModeState GestureProfile::getTriggerState(const std::string& triggerKey) const {
    if (auto it = m_triggerStates.find(triggerKey); it != m_triggerStates.end()) {
        return it->second;
    }
    return TriggerModeState::Default;
}

void GestureProfile::setTriggerState(const std::string& triggerKey, TriggerModeState state) {
    if (state == TriggerModeState::Default) {
        m_triggerStates.erase(triggerKey);
    } else {
        m_triggerStates[triggerKey] = state;
    }
    LOG_DEBUG("设置触发方式状态: profile={}, trigger={}, state={}",
              m_name, triggerKey, triggerStateToString(state));
}

void GestureProfile::setAllTriggerStates(TriggerModeState state) {
    static const std::vector<std::string> kKnownTriggers = {
        "right", "middle", "left", "xbutton1", "xbutton2",
        "edge_top_slide", "edge_top_wheel", "edge_top_right", "edge_top_middle", "edge_top_left"
    };

    if (state == TriggerModeState::Default) {
        m_triggerStates.clear();
    } else {
        for (const auto& key : kKnownTriggers) {
            m_triggerStates[key] = state;
        }
    }
    LOG_INFO("批量设置触发方式: profile={}, state={}", m_name, triggerStateToString(state));
}

bool GestureProfile::hasGesture(const std::string& gestureCode) const {
    return m_codeIndex.contains(gestureCode);
}

std::vector<std::string> GestureProfile::detectConflicts(const std::string& gestureCode) const {
    std::vector<std::string> conflicts;
    for (const auto& [code, _] : m_codeIndex) {
        // 前缀冲突检测：新手势是已有手势的前缀，或已有手势是新手势的前缀
        if (code != gestureCode) {
            if (gestureCode.starts_with(code) || code.starts_with(gestureCode)) {
                conflicts.push_back(code);
            }
        }
    }
    return conflicts;
}

void GestureProfile::rebuildIndex() {
    m_codeIndex.clear();
    for (size_t i = 0; i < m_mappings.size(); ++i) {
        m_codeIndex[m_mappings[i].gestureCode] = i;
    }
}

// ── 默认 Profile 工厂 ───────────────────────────────────────────────────────

GestureProfile GestureProfile::createDefaultGlobal() {
    GestureProfile profile("default");

    auto addKeys = [&](const std::string& code, const std::string& name,
                       const std::string& keys, const std::string& desc = "") {
        GestureMapping mapping;
        mapping.gestureCode = code;
        mapping.action.type = ActionType::SendKeys;
        mapping.action.name = name;
        mapping.action.description = desc;
        mapping.action.keyStroke = KeyStroke::fromString(keys);
        profile.addMapping(mapping);
    };

    auto addBuiltin = [&](const std::string& code, const std::string& name,
                          BuiltinCommand cmd, const std::string& desc = "") {
        GestureMapping mapping;
        mapping.gestureCode = code;
        mapping.action.type = ActionType::BuiltinCommand;
        mapping.action.name = name;
        mapping.action.description = desc;
        mapping.action.builtinCmd = cmd;
        profile.addMapping(mapping);
    };

    // 默认手势集 (与 WGestures 2 等世界级手势工具标准对齐)
    addKeys("L",       "后退",               "Alt+Left",         "浏览器/文件管理器后退");
    addKeys("R",       "前进",               "Alt+Right",        "浏览器/文件管理器前进");
    addBuiltin("U",    "最大化/还原",        BuiltinCommand::MaximizeWindow, "最大化或还原当前窗口");
    addBuiltin("D",    "最小化",             BuiltinCommand::MinimizeWindow, "最小化当前窗口");
    addKeys("D-R",     "关闭标签页/窗口",     "Ctrl+W",           "关闭当前标签页或窗口");
    addKeys("R-U",     "恢复关闭标签页",     "Ctrl+Shift+T",     "恢复最近关闭的标签页");
    addKeys("U-R",     "下一个标签页",       "Ctrl+Tab",         "切换到下一个标签页");
    addKeys("U-L",     "上一个标签页",       "Ctrl+Shift+Tab",   "切换到上一个标签页");
    addKeys("D-U",     "刷新",               "F5",               "刷新页面");
    addKeys("U-D",     "新建标签页",         "Ctrl+T",           "新建标签页");
    addKeys("L-D",     "显示桌面",           "Win+D",            "一键显示/隐藏桌面");
    addKeys("R-D",     "任务视图",           "Win+Tab",          "打开 Windows 任务视图");
    addKeys("D-R-D",   "屏幕截图",           "Win+Shift+S",      "唤起屏幕截图工具");
    addKeys("U-L",     "复制",               "Ctrl+C",           "复制选中内容");
    addKeys("D-L",     "粘贴",               "Ctrl+V",           "粘贴内容");
    addKeys("L-U",     "剪切",               "Ctrl+X",           "剪切选中内容");
    addKeys("R-L",     "全选",               "Ctrl+A",           "全选当前内容");
    addKeys("L-R",     "撤销",               "Ctrl+Z",           "撤销上一步操作");
    addKeys("U-D-U",   "强制刷新",           "Ctrl+F5",          "强制刷新忽略缓存");
    addKeys("D-U-D",   "任务管理器",         "Ctrl+Shift+Escape","打开 Windows 任务管理器");

    LOG_INFO("创建默认全局手势配置集, 手势数量={}", profile.getMappings().size());
    return profile;
}

GestureProfile GestureProfile::createBrowserProfile() {
    GestureProfile profile("browser");

    GestureMapping restoreTab;
    restoreTab.gestureCode = "R-D";
    restoreTab.action.type = ActionType::SendKeys;
    restoreTab.action.name = "恢复关闭的标签页";
    restoreTab.action.description = "恢复最近关闭的标签页 (浏览器专用)";
    restoreTab.action.keyStroke = KeyStroke::fromString("Ctrl+Shift+T");
    profile.addMapping(restoreTab);

    GestureMapping paste;
    paste.gestureCode = "D-R";
    paste.action.type = ActionType::SendKeys;
    paste.action.name = "粘贴";
    paste.action.description = "粘贴 (浏览器 Profile 中下右映射为粘贴)";
    paste.action.keyStroke = KeyStroke::fromString("Ctrl+V");
    profile.addMapping(paste);

    LOG_INFO("创建浏览器专用手势配置集, 手势数量={}", profile.getMappings().size());
    return profile;
}

// ── JSON 序列化 ──────────────────────────────────────────────────────────────

nlohmann::json GestureProfile::toJson() const {
    nlohmann::json j;
    j["name"] = m_name;
    j["mappings"] = nlohmann::json::array();
    for (const auto& mapping : m_mappings) {
        j["mappings"].push_back(mapping.toJson());
    }

    j["triggerStates"] = nlohmann::json::object();
    for (const auto& [key, state] : m_triggerStates) {
        j["triggerStates"][key] = triggerStateToString(state);
    }
    return j;
}

GestureProfile GestureProfile::fromJson(const nlohmann::json& j) {
    GestureProfile profile(j.value("name", "default"));
    if (j.contains("mappings") && j["mappings"].is_array()) {
        for (const auto& item : j["mappings"]) {
            profile.addMapping(GestureMapping::fromJson(item));
        }
    }

    if (j.contains("triggerStates") && j["triggerStates"].is_object()) {
        for (const auto& [key, val] : j["triggerStates"].items()) {
            if (val.is_string()) {
                profile.setTriggerState(key, triggerStateFromString(val.get<std::string>()));
            }
        }
    }
    return profile;
}

}  // namespace easy::gesture
