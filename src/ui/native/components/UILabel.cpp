#include "ui/native/components/UILabel.h"
#include "ui/native/common/UITheme.h"

namespace tools3000::ui::native {

UILabel::UILabel(const std::wstring& text,
                 FontToken font,
                 FontWeight weight,
                 FontFamilyType family)
    : m_text(text), m_fontToken(font), m_fontWeight(weight), m_fontFamily(family) {
}

void UILabel::setText(const std::wstring& text) {
    if (m_text != text) {
        m_text = text;
        markNeedsLayout();
    }
}

void UILabel::setColor(const Color& color) {
    m_color = color;
    m_colorRole = TextColorRole::Custom;
    markNeedsPaint();
}

void UILabel::setColorRole(TextColorRole role) {
    if (m_colorRole != role) {
        m_colorRole = role;
        markNeedsPaint();
    }
}

void UILabel::setFontToken(FontToken token) {
    if (m_fontToken != token) {
        m_fontToken = token;
        markNeedsLayout();
    }
}

void UILabel::setFontWeight(FontWeight weight) {
    if (m_fontWeight != weight) {
        m_fontWeight = weight;
        markNeedsLayout();
    }
}

void UILabel::setAlignment(TextAlignmentH alignH, TextAlignmentV alignV) {
    m_alignH = alignH;
    m_alignV = alignV;
    markNeedsPaint();
}

void UILabel::setWordWrap(bool wrap) {
    if (m_wordWrap != wrap) {
        m_wordWrap = wrap;
        markNeedsLayout();
    }
}

void UILabel::setEllipsis(bool ellipsis) {
    if (m_ellipsis != ellipsis) {
        m_ellipsis = ellipsis;
        markNeedsPaint();
    }
}

Size UILabel::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    if (!m_visible || m_text.empty()) {
        m_desiredSize = Size(0.0f, 0.0f);
        return m_desiredSize;
    }

    float maxW = (availableWidth > 0.0f) ? availableWidth : 10000.0f;
    Size measured = ctx.measureText(m_text, m_fontToken, m_fontWeight, m_fontFamily, maxW);
    m_desiredSize = Size(measured.width, measured.height);
    return m_desiredSize;
}

void UILabel::onRenderContent(UIRenderContext& ctx) {
    if (m_text.empty()) return;

    const auto& p = UITheme::instance().palette();
    Color renderColor;
    switch (m_colorRole) {
    case TextColorRole::Primary:
        renderColor = p.textPrimary;
        break;
    case TextColorRole::Secondary:
        renderColor = p.textSecondary;
        break;
    case TextColorRole::Muted:
        renderColor = p.textMuted;
        break;
    case TextColorRole::Disabled:
        renderColor = p.textDisabled;
        break;
    case TextColorRole::Accent:
        renderColor = p.primary;
        break;
    case TextColorRole::Custom:
        renderColor = m_color;
        break;
    case TextColorRole::Default:
    default:
        renderColor = p.textPrimary;
        break;
    }

    ctx.drawText(
        m_text,
        m_bounds,
        renderColor,
        m_fontToken,
        m_fontWeight,
        m_fontFamily,
        m_alignH,
        m_alignV,
        m_wordWrap,
        m_ellipsis
    );
}

} // namespace tools3000::ui::native
