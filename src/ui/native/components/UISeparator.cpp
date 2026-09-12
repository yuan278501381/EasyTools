#include "ui/native/components/UISeparator.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"

namespace tools3000::ui::native {

UISeparator::UISeparator(bool vertical) : m_vertical(vertical) {
    if (m_vertical) {
        m_layoutParams.margin = Thickness(4.0f, 0.0f, 4.0f, 0.0f);
    } else {
        m_layoutParams.margin = Thickness(0.0f, 4.0f, 0.0f, 4.0f);
    }
}

Size UISeparator::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    if (m_vertical) {
        m_desiredSize = Size(1.0f, availableHeight);
    } else {
        m_desiredSize = Size(availableWidth, 1.0f);
    }
    return m_desiredSize;
}

void UISeparator::onRenderContent(UIRenderContext& ctx) {
    const bool dark = UITheme::instance().isEffectiveDark();
    if (m_vertical) {
        float midX = m_bounds.left + m_bounds.width() * 0.5f;
        GlassmorphismRenderer::drawSeparator(ctx, Point(midX, m_bounds.top), Point(midX, m_bounds.bottom), dark);
    } else {
        float midY = m_bounds.top + m_bounds.height() * 0.5f;
        GlassmorphismRenderer::drawSeparator(ctx, Point(m_bounds.left, midY), Point(m_bounds.right, midY), dark);
    }
}

} // namespace tools3000::ui::native
