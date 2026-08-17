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
#include <mutex>
#include <condition_variable>
#include <deque>
#include <optional>
#include <shared_mutex>
#include <thread>

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
using PauseChangedCallback = std::function<bool(bool paused)>;

class GestureEngine {
public:
    static GestureEngine& instance();

    /// 启动手势引擎
    bool start();

    /// 停止手势引擎
    void stop();

    /// 暂停/恢复手势
    /// 返回 false 表示状态持久化失败，运行时状态已自动回滚。
    bool setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }
    void setPauseChangedCallback(PauseChangedCallback callback);

    /// 触发按钮配置
    void setTriggerButton(const std::string& button);
    std::string triggerButton() const;

    /// 轨迹可视化开关
    void setTrailVisible(bool visible);
    bool trailVisible() const { return m_trailVisible.load(); }

    /// 全屏应用/游戏自动免打扰开关
    void setAutoBypassFullscreen(bool enable);
    bool autoBypassFullscreen() const { return m_autoBypassFullscreen.load(); }

    /// 当前状态
    GestureState state() const { return m_state.load(); }

    // ── Profile 管理 ─────────────────────────────────────────────────────

    /// 添加/更新 Profile
    void setProfile(const std::string& name, const GestureProfile& profile);

    /// 获取 Profile 快照。禁止向调用方暴露容器内指针，避免并发更新后悬空。
    std::optional<GestureProfile> getProfile(const std::string& name) const;

    /// 获取全部 Profile 的稳定快照，按名称排序。
    std::vector<GestureProfile> getProfiles() const;

    /// 删除 Profile（用于持久化失败后的回滚）。
    bool removeProfile(const std::string& name);

    // ── 作用域规则 ───────────────────────────────────────────────────────

    ScopeRuleEngine& scopeRules() { return m_scopeRules; }

    // ── 识别器配置 ───────────────────────────────────────────────────────

    void setRecognizerConfig(const RecognizerConfig& config);

    // ── 轨迹可视化 ──────────────────────────────────────────────────────

    void setTrailCallback(TrailRenderCallback callback);

    /// 从配置文件加载全部状态
    void loadFromConfig();

    /// 保存全部状态到配置文件
    bool saveToConfig();

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

    void enqueueAction(GestureAction action, std::string traceId);
    void actionWorkerLoop(std::stop_token stopToken);

    /// 根据当前前台窗口查找适用的 Profile
    std::optional<GestureProfile> resolveProfile(HWND hwnd) const;

    // 组件
    GestureRecognizer m_recognizer;
    ScopeRuleEngine m_scopeRules;
    std::unordered_map<std::string, GestureProfile> m_profiles;
    mutable std::shared_mutex m_profileMutex;

    // 状态
    std::atomic<GestureState> m_state{GestureState::Idle};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_trailVisible{true};
    std::atomic<bool> m_autoBypassFullscreen{false};
    HWND m_gestureStartWindow = nullptr;  // 手势开始时的前台窗口
    std::string m_gestureTraceId;         // 当前手势的 TraceId, 贯穿 按下→移动→抬起→执行
    uint8_t m_gestureModifiers = 0;       // 手势开始时的修饰键状态
    std::chrono::steady_clock::time_point m_trackingStartTime; // 手势开始追踪的时间点
    std::optional<GestureProfile> m_activeProfile;       // 本次手势激活的 Profile 缓存
    std::vector<Direction> m_lastRecognizedDirections; // 缓存方向序列
    PauseChangedCallback m_pauseChangedCallback;

    // 轨迹可视化
    TrailRenderCallback m_trailCallback;
    mutable std::mutex m_callbackMutex;

    // 触发按键 (默认右键)
    std::atomic<MouseEventType> m_triggerDown{MouseEventType::RightDown};
    std::atomic<MouseEventType> m_triggerUp{MouseEventType::RightUp};
    // 一次手势从按下到抬起必须使用同一触发键，即使用户此时在设置页切换配置。
    MouseEventType m_activeTriggerDown = MouseEventType::RightDown;
    MouseEventType m_activeTriggerUp = MouseEventType::RightUp;

    std::mutex m_mutex;

    struct ActionJob {
        GestureAction action;
        std::string traceId;
    };
    std::mutex m_actionMutex;
    std::condition_variable_any m_actionCv;
    std::deque<ActionJob> m_actionQueue;
    std::jthread m_actionWorker;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREENGINE_H
