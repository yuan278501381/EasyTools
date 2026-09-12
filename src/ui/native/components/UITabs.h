#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UITabs.h — Tools3000 原生平滑物理滑块分段选择与 Tab 控制器组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UITABS_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UITABS_H

#include "ui/native/core/UIElement.h"
#include "ui/native/common/UIAnimation.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>
#include <vector>
#include <functional>

namespace tools3000::ui::native {

struct TabItem {
    std::string id;
    std::wstring label;
    IconType icon = IconType::None;
    std::wstring badge;
};

class UITabs : public UIElement {
public:
    UITabs(const std::vector<TabItem>& tabs = {},
           const std::string& activeId = "",
           std::function<void(const std::string&)> onTabChange = nullptr);

    void setTabs(const std::vector<TabItem>& tabs);
    void setActiveTab(const std::string& id, bool animated = true);
    const std::string& getActiveTab() const { return m_activeId; }

    void setOnTabChange(std::function<void(const std::string&)> cb) { m_onTabChange = std::move(cb); }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    void layout(const Rect& bounds, UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    void onMouseLeave() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    Rect getTabRect(size_t index) const;
    size_t getActiveIndex() const;

private:
    std::vector<TabItem> m_tabs;
    std::string m_activeId;
    std::function<void(const std::string&)> m_onTabChange;

    AnimatedFloat m_indicatorX{0.0f};
    AnimatedFloat m_indicatorWidth{0.0f};
    int m_hoveredIndex = -1;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UITABS_H
