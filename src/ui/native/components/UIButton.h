#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIButton.h — Tools3000 原生多态微晶胶囊按键组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UIBUTTON_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UIBUTTON_H

#include "ui/native/core/UIElement.h"
#include "ui/native/common/UIAnimation.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>
#include <functional>

namespace tools3000::ui::native {

enum class ButtonVariant {
    Primary,
    Secondary,
    Ghost,
    Danger,
    Confirm
};

enum class ButtonSize {
    Sm,
    Md,
    Lg
};

class UIButton : public UIElement {
public:
    UIButton(const std::wstring& text = L"",
             ButtonVariant variant = ButtonVariant::Secondary,
             ButtonSize size = ButtonSize::Md);

    void setText(const std::wstring& text);
    const std::wstring& getText() const { return m_text; }

    void setIcon(IconType icon) { m_icon = icon; markNeedsLayout(); }
    IconType getIcon() const { return m_icon; }

    void setVariant(ButtonVariant variant);
    ButtonVariant variant() const { return m_variant; }
    void setButtonSize(ButtonSize size);
    void setOnClick(std::function<void()> callback) { m_onClick = std::move(callback); }

    void setIsNavItem(bool isNav) { m_isNavItem = isNav; markNeedsLayout(); markNeedsPaint(); }
    bool isNavItem() const { return m_isNavItem; }

    void setActive(bool active) { if (m_isActive != active) { m_isActive = active; markNeedsPaint(); } }
    bool isActive() const { return m_isActive; }

    void setWindowControl(bool isControl, bool isClose = false) {
        m_isWindowControl = isControl;
        m_isCloseBtn = isClose;
        markNeedsLayout();
        markNeedsPaint();
    }
    void setIsWindowControl(bool isControl, bool isClose = false) { setWindowControl(isControl, isClose); }
    bool isWindowControl() const { return m_isWindowControl; }
    bool isCloseBtn() const { return m_isCloseBtn; }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onMouseUp(const UIMouseEvent& e) override;
    void onMouseEnter() override;
    void onMouseLeave() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_text;
    IconType m_icon = IconType::None;
    ButtonVariant m_variant = ButtonVariant::Secondary;
    ButtonSize m_size = ButtonSize::Md;
    std::function<void()> m_onClick;

    bool m_isNavItem = false;
    bool m_isActive = false;
    bool m_isWindowControl = false;
    bool m_isCloseBtn = false;

    AnimatedFloat m_hoverAlpha{0.0f};
    AnimatedFloat m_pressScale{1.0f};
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UIBUTTON_H
