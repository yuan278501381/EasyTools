#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeScopeRulesManager.h — Tools3000 纯原生进程/类名作用域规则管理中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_NATIVESCOPERULESMANAGER_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_NATIVESCOPERULESMANAGER_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

namespace tools3000::ui::native {

struct ScopeRuleDef {
    std::string id;
    std::wstring name;
    bool enabled = true;
    std::string targetKind;   // "process" 或 "class"
    std::wstring targetValue; // 例如 L"chrome.exe", L"Chrome_WidgetWin_1"
    int matchMode = 0;        // 0: 精确, 1: 包含
    int effect = 2;           // 1: 禁用手势, 2: 独立手势
    std::wstring profileName;
};

struct RunningAppInfo {
    std::wstring title;
    std::wstring processName;
    std::wstring windowClass;
    HWND hwnd = nullptr;
};

class NativeScopeRulesManager : public UIElement {
public:
    NativeScopeRulesManager();
    virtual ~NativeScopeRulesManager() override;

    /// 获取全部规则
    const std::vector<ScopeRuleDef>& getRules() const { return m_rules; }

    /// 规则更新持久化回调
    void setOnRulesChanged(std::function<void(const std::vector<ScopeRuleDef>&)> cb) {
        m_onRulesChanged = std::move(cb);
    }

    /// 添加新规则
    void addRule(const ScopeRuleDef& rule);

    /// 删除规则
    void deleteRule(const std::string& id);

    /// 切换单条规则启用状态
    void toggleRuleEnabled(const std::string& id);

    /// 重置为系统默认预设规则
    void resetToDefault();

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 鼠标交互 ────────────────────────────────────────────────────────────
    virtual bool onMouseDown(const UIMouseEvent& e) override;
    virtual bool onMouseMove(const UIMouseEvent& e) override;

private:
    void initDefaultRules();
    void scanRunningWindows();
    void startCrosshairCountdown();
    void captureForegroundWindow();
    void saveRules();

private:
    std::vector<ScopeRuleDef> m_rules;
    std::unordered_set<std::string> m_selectedIds;

    // 编辑弹窗状态
    bool m_modalOpen = false;
    ScopeRuleDef m_editingRule;
    bool m_isNewRule = false;
    int m_modalTab = 0; // 0: 运行中应用, 1: 常用预设

    // 运行中的窗口列表缓存
    std::vector<RunningAppInfo> m_runningApps;

    // 十字准星倒计时拾取状态
    int m_countdownSeconds = -1;
    float m_countdownElapsed = 0.0f;

    std::function<void(const std::vector<ScopeRuleDef>&)> m_onRulesChanged;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_NATIVESCOPERULESMANAGER_H
