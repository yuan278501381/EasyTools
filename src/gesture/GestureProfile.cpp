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

std::optional<GestureAction> GestureProfile::findAction(const std::string& gestureCode) const {
    auto it = m_codeIndex.find(gestureCode);
    if (it != m_codeIndex.end()) {
        return m_mappings[it->second].action;
    }
    return std::nullopt;
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

    // 默认手势集
    addKeys("L",     "后退",       "Alt+Left",      "浏览器/文件管理器后退");
    addKeys("R",     "前进",       "Alt+Right",     "浏览器/文件管理器前进");
    addKeys("U",     "关闭窗口",   "Alt+F4",        "关闭当前窗口");
    addKeys("D",     "新建标签页", "Ctrl+T",        "新建标签页");
    addKeys("UL",    "复制",       "Ctrl+C",        "复制选中内容");
    addKeys("DR",    "关闭标签页", "Ctrl+W",        "关闭当前标签页");
    addKeys("LU",    "剪切",       "Ctrl+X",        "剪切选中内容");
    addBuiltin("UR", "最大化",     BuiltinCommand::MaximizeWindow, "最大化当前窗口");
    addBuiltin("DL", "最小化",     BuiltinCommand::MinimizeWindow, "最小化当前窗口");
    addKeys("U-R",   "下一个标签页", "Ctrl+Tab",    "切换到下一个标签页");
    addKeys("U-L",   "上一个标签页", "Ctrl+Shift+Tab", "切换到上一个标签页");
    addKeys("D-U",   "刷新",       "F5",            "刷新页面");
    addKeys("U-D",   "撤销",       "Ctrl+Z",        "撤销操作");
    addKeys("R-L",   "全选",       "Ctrl+A",        "全选");

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
    return j;
}

GestureProfile GestureProfile::fromJson(const nlohmann::json& j) {
    GestureProfile profile(j.value("name", "default"));
    if (j.contains("mappings") && j["mappings"].is_array()) {
        for (const auto& item : j["mappings"]) {
            profile.addMapping(GestureMapping::fromJson(item));
        }
    }
    return profile;
}

}  // namespace easy::gesture
