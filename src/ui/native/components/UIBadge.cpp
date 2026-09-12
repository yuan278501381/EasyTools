#include "ui/native/components/UIBadge.h"
#include "ui/native/common/UITheme.h"
#include <algorithm>

namespace tools3000::ui::native {

UIBadge::UIBadge(const std::wstring& text, BadgeVariant variant)
    : m_text(text), m_variant(variant) {
}

void UIBadge::setText(const std::wstring& text) {
    if (m_text != text) {
        m_text = text;
        markNeedsLayout();
    }
}

void UIBadge::setVariant(BadgeVariant variant) {
    if (m_variant != variant) {
        m_variant = variant;
        markNeedsPaint();
    }
}

Size UIBadge::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableWidth;
    (void)availableHeight;
    if (m_text.empty()) {
        m_desiredSize = Size(0.0f, 0.0f);
        return m_desiredSize;
    }

    Size textSize = ctx.measureText(m_text, FontToken::Xs, FontWeight::SemiBold);
    float textW = textSize.width;
    if (textW <= 0.0f) {
        float estimated = 0.0f;
        for (wchar_t ch : m_text) {
            estimated += (ch > 127) ? 13.0f : 8.0f;
        }
        textW = estimated;
    }
    m_desiredSize = Size(textW + 28.0f, 22.0f);
    return m_desiredSize;
}

void UIBadge::onRenderContent(UIRenderContext& ctx) {
    if (m_text.empty()) return;
    const bool dark = UITheme::instance().isEffectiveDark();
    GlassmorphismRenderer::drawBadge(ctx, m_bounds, m_text, m_variant, dark);
}

} // namespace tools3000::ui::native
