#include "ui/native/components/UIIcon.h"
#include "ui/native/common/UITheme.h"

namespace tools3000::ui::native {

UIIcon::UIIcon(IconType icon, float size, const Color& color, IconColorRole role)
    : m_icon(icon), m_iconSize(size), m_color(color), m_colorRole(role) {
    if (color.a > 0.001f && role == IconColorRole::Default) {
        m_colorRole = IconColorRole::Custom;
    }
}

void UIIcon::setIcon(IconType icon) {
    if (m_icon != icon) {
        m_icon = icon;
        markNeedsPaint();
    }
}

void UIIcon::setIconSize(float size) {
    if (m_iconSize != size) {
        m_iconSize = size;
        markNeedsLayout();
    }
}

void UIIcon::setColor(const Color& color) {
    m_color = color;
    m_colorRole = IconColorRole::Custom;
    markNeedsPaint();
}

void UIIcon::setStrokeWidth(float width) {
    m_strokeWidth = width;
    markNeedsPaint();
}

Size UIIcon::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableWidth;
    (void)availableHeight;
    (void)ctx;
    m_desiredSize = Size(m_iconSize, m_iconSize);
    return m_desiredSize;
}

void UIIcon::onRenderContent(UIRenderContext& ctx) {
    if (m_icon == IconType::None) return;

    const auto& p = UITheme::instance().palette();
    Color iconColor;
    switch (m_colorRole) {
    case IconColorRole::Primary:
        iconColor = p.primary;
        break;
    case IconColorRole::Secondary:
        iconColor = p.textSecondary;
        break;
    case IconColorRole::Muted:
        iconColor = p.textMuted;
        break;
    case IconColorRole::Custom:
        iconColor = m_color;
        break;
    case IconColorRole::Default:
    default:
        iconColor = p.textPrimary;
        break;
    }

    VectorIconRenderer::drawIcon(ctx, m_icon, m_bounds, iconColor, m_strokeWidth);
}

} // namespace tools3000::ui::native
