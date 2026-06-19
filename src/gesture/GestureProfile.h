#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureProfile — 手势配置集
//
// 每个 Profile 包含一组手势 → 动作的映射。
// 全局默认 Profile + 按应用的专属 Profile（如浏览器专用）。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_GESTUREPROFILE_H
#define EASYTOOLS_GESTURE_GESTUREPROFILE_H

#include "gesture/GestureAction.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <nlohmann/json.hpp>

namespace easy::gesture {

class GestureProfile {
public:
    explicit GestureProfile(const std::string& name = "default");

    /// Profile 名称
    const std::string& name() const { return m_name; }

    /// 添加手势映射
    void addMapping(const GestureMapping& mapping);

    /// 删除手势映射
    void removeMapping(const std::string& gestureCode);

    /// 根据手势编码查找动作
    std::optional<GestureAction> findAction(const std::string& gestureCode) const;

    /// 检查手势编码是否已存在
    bool hasGesture(const std::string& gestureCode) const;

    /// 检测手势冲突（前缀冲突: "L" 和 "L-U" 冲突）
    std::vector<std::string> detectConflicts(const std::string& gestureCode) const;

    /// 获取所有映射
    const std::vector<GestureMapping>& getMappings() const { return m_mappings; }

    /// 清除所有映射
    void clearMappings() { m_mappings.clear(); m_codeIndex.clear(); }

    /// 创建默认全局 Profile
    static GestureProfile createDefaultGlobal();

    /// 创建浏览器专用 Profile
    static GestureProfile createBrowserProfile();

    /// 序列化
    nlohmann::json toJson() const;
    static GestureProfile fromJson(const nlohmann::json& j);

private:
    std::string m_name;
    std::vector<GestureMapping> m_mappings;
    std::unordered_map<std::string, size_t> m_codeIndex;  // gestureCode → m_mappings 索引

    void rebuildIndex();
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREPROFILE_H
