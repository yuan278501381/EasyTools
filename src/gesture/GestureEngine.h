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
using PauseChangedCallback = std::function<void(bool paused)>;

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
    void setPauseChangedCallback(PauseChangedCallback callback);

    /// 触发按钮配置
    void setTriggerButton(const std::string& button);
    std::string triggerButton() const;

    /// 轨迹可视化开关
    void setTrailVisible(bool visible);
    bool trailVisible() const { return m_trailVisible.load(); }

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

    /// 处理从鼠标钩子传来的事件，返回 true 表示拦截该事件
    bool onMouseEvent(const MouseEvent& event);

    /// 开始手势追踪
    void beginTracking(const MouseEvent& event);
    /// 更新手势轨迹
    void updateTracking(const MouseEvent& event);
    /// 结束手势追踪
    void endTracking(const MouseEvent& event);
    /// 取消手势追踪
    void cancelTracking();

    /// 把被吞掉的触发键点击补发出去 (无有效手势时还原右键/中键的正常点击)
    void reinjectTriggerClick();

    /// 根据当前前台窗口查找适用的 Profile
    GestureProfile* resolveProfile(HWND hwnd);

    // 组件
    GestureRecognizer m_recognizer;
    ScopeRuleEngine m_scopeRules;
    std::unordered_map<std::string, GestureProfile> m_profiles;

    // 状态
    std::atomic<GestureState> m_state{GestureState::Idle};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_trailVisible{true};
    HWND m_gestureStartWindow = nullptr;  // 手势开始时的前台窗口
    std::string m_gestureTraceId;         // 当前手势的 TraceId, 贯穿 按下→移动→抬起→执行
    PauseChangedCallback m_pauseChangedCallback;

    // 轨迹可视化
    TrailRenderCallback m_trailCallback;

    // 触发按键 (默认右键)
    MouseEventType m_triggerDown = MouseEventType::RightDown;
    MouseEventType m_triggerUp   = MouseEventType::RightUp;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREENGINE_H
