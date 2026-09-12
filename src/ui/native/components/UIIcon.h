#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIIcon.h — Tools3000 原生几何矢量微图标组件 (Zero Emoji)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UIICON_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UIICON_H

#include "ui/native/core/UIElement.h"
#include "ui/native/graphics/VectorIconRenderer.h"

namespace tools3000::ui::native {

enum class IconColorRole {
    Default,    // 跟随主题 palette().textPrimary
    Primary,    // 跟随主题 palette().primary
    Secondary,  // 跟随主题 palette().textSecondary
    Muted,      // 跟随主题 palette().textMuted
    Custom      // 显式固定 m_color
};

class UIIcon : public UIElement {
public:
    UIIcon(IconType icon = IconType::None,
           float size = 16.0f,
           const Color& color = Color(),
           IconColorRole role = IconColorRole::Default);

    void setIcon(IconType icon);
    IconType getIcon() const { return m_icon; }

    void setIconSize(float size);
    float getIconSize() const { return m_iconSize; }

    void setColor(const Color& color);
    void setIconColorRole(IconColorRole role) { m_colorRole = role; markNeedsPaint(); }
    IconColorRole getIconColorRole() const { return m_colorRole; }

    void setStrokeWidth(float width);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    IconType m_icon = IconType::None;
    float m_iconSize = 16.0f;
    Color m_color;
    IconColorRole m_colorRole = IconColorRole::Default;
    float m_strokeWidth = 1.8f;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UIICON_H
