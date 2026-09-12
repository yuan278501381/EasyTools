#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIToggle.h — Tools3000 原生平滑物理弹簧滑动开关组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UITOGGLE_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UITOGGLE_H

#include "ui/native/core/UIElement.h"
#include "ui/native/common/UIAnimation.h"
#include <string>
#include <functional>

namespace tools3000::ui::native {

class UIToggle : public UIElement {
public:
    UIToggle(bool checked = false, std::function<void(bool)> onToggle = nullptr);

    void setChecked(bool checked, bool animated = true);
    bool isChecked() const { return m_checked; }
    void toggle();

    void setLabel(const std::wstring& label) { m_label = label; markNeedsLayout(); }
    void setDescription(const std::wstring& desc) { m_description = desc; markNeedsLayout(); }

    void setOnToggle(std::function<void(bool)> callback) { m_onToggle = std::move(callback); }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    bool m_checked = false;
    std::wstring m_label;
    std::wstring m_description;
    std::function<void(bool)> m_onToggle;

    AnimatedFloat m_thumbProgress{0.0f}; // 0.0f = off, 1.0f = on
    AnimatedFloat m_hoverAlpha{0.0f};
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UITOGGLE_H
