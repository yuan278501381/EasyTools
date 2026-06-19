#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureEngine — 手势引擎主入口
//
// 职责:
//   1. 协调 MouseHook → GestureRecognizer → ScopeRuleEngine → GestureProfile
//   2. 管理手势的完整生命周期: 开始 → 采集 → 识别 → 匹配 → 执行
//   3. 手势轨迹可视化控制
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_GESTUREENGINE_H
#define EASYTOOLS_GESTURE_GESTUREENGINE_H

#include "gesture/MouseHook.h"
#include "gesture/GestureRecognizer.h"
#include "gesture/GestureAction.h"
#include "gesture/GestureProfile.h"
#include "gesture/ScopeRule.h"

#include <memory>
#include <unordered_map>
#include <atomic>
#include <functional>

namespace easy::gesture {

/// 手势状态
enum class GestureState {
    Idle,       // 空闲，等待触发
    Tracking,   // 正在追踪手势轨迹
    Executing   // 正在执行手势动作
};

/// 手势轨迹可视化回调（由 UI 层实现）
using TrailRenderCallback = std::function<void(const std::vector<TrackPoint>& points,
                                                const std::vector<Direction>& directions)>;

class GestureEngine {
public:
    static GestureEngine& instance();

    /// 启动手势引擎
    bool start();

    /// 停止手势引擎
    void stop();

    /// 暂停/恢复手势
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    /// 当前状态
    GestureState state() const { return m_state.load(); }

    // ── Profile 管理 ─────────────────────────────────────────────────────

    /// 添加/更新 Profile
    void setProfile(const std::string& name, const GestureProfile& profile);

    /// 获取 Profile
    GestureProfile* getProfile(const std::string& name);

    /// 获取全局默认 Profile
    GestureProfile& defaultProfile() { return m_profiles["default"]; }

    // ── 作用域规则 ───────────────────────────────────────────────────────

    ScopeRuleEngine& scopeRules() { return m_scopeRules; }

    // ── 识别器配置 ───────────────────────────────────────────────────────

    void setRecognizerConfig(const RecognizerConfig& config);

    // ── 轨迹可视化 ──────────────────────────────────────────────────────

    void setTrailCallback(TrailRenderCallback callback);

    /// 从配置文件加载全部状态
    void loadFromConfig();

    /// 保存全部状态到配置文件
    void saveToConfig();

private:
    GestureEngine();
    GestureEngine(const GestureEngine&) = delete;
    GestureEngine& operator=(const GestureEngine&) = delete;

    /// 处理鼠标事件（由 MouseHook 回调驱动）
    void onMouseEvent(const MouseEvent& event);

    /// 开始手势追踪
    void beginTracking(const MouseEvent& event);

    /// 结束手势追踪并执行
    void endTracking(const MouseEvent& event);

    /// 更新追踪中的轨迹
    void updateTracking(const MouseEvent& event);

    /// 根据当前前台窗口查找适用的 Profile
    GestureProfile* resolveProfile(HWND hwnd);

    // 组件
    GestureRecognizer m_recognizer;
    ScopeRuleEngine m_scopeRules;
    std::unordered_map<std::string, GestureProfile> m_profiles;

    // 状态
    std::atomic<GestureState> m_state{GestureState::Idle};
    std::atomic<bool> m_paused{false};
    HWND m_gestureStartWindow = nullptr;  // 手势开始时的前台窗口

    // 轨迹可视化
    TrailRenderCallback m_trailCallback;

    // 触发按键 (默认右键)
    MouseEventType m_triggerDown = MouseEventType::RightDown;
    MouseEventType m_triggerUp   = MouseEventType::RightUp;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREENGINE_H
