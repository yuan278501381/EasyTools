#include "ui/native/components/UICodeBadge.h"
#include "ui/native/common/UITheme.h"
#include <algorithm>

namespace tools3000::ui::native {

UICodeBadge::UICodeBadge(const std::wstring& code) : m_code(code) {
}

void UICodeBadge::setCode(const std::wstring& code) {
    if (m_code != code) {
        m_code = code;
        markNeedsLayout();
    }
}

Size UICodeBadge::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableWidth;
    (void)availableHeight;
    if (m_code.empty()) {
        m_desiredSize = Size(0.0f, 0.0f);
        return m_desiredSize;
    }

    Size measured = ctx.measureText(m_code, FontToken::Xs, FontWeight::Medium, FontFamilyType::Mono);
    float codeW = measured.width;
    if (codeW <= 0.0f) {
        codeW = static_cast<float>(m_code.length()) * 8.5f;
    }
    m_desiredSize = Size(codeW + 16.0f, 22.0f);
    return m_desiredSize;
}

void UICodeBadge::onRenderContent(UIRenderContext& ctx) {
    if (m_code.empty()) return;
    const bool dark = UITheme::instance().isEffectiveDark();
    GlassmorphismRenderer::drawCodeBadge(ctx, m_bounds, m_code, dark);
}

} // namespace tools3000::ui::native
