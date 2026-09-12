#include "ui/native/components/UICard.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <algorithm>

namespace tools3000::ui::native {

UICard::UICard(const std::wstring& title, const std::wstring& subtitle)
    : m_title(title), m_subtitle(subtitle) {
    m_layoutParams.padding = Thickness(20.0f, 18.0f, 20.0f, 18.0f);
    m_layoutParams.gap = 12.0f;
    m_layoutParams.direction = FlexDirection::Column;
}

void UICard::setHeaderAction(std::shared_ptr<UIElement> action) {
    if (m_headerAction) {
        removeChild(m_headerAction);
    }
    m_headerAction = action;
    if (m_headerAction) {
        addChild(m_headerAction);
    }
    markNeedsLayout();
}

void UICard::onMouseEnter() {
    UIElement::onMouseEnter();
}

void UICard::onMouseLeave() {
    UIElement::onMouseLeave();
}

Size UICard::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    float contentW = availableWidth - m_layoutParams.padding.horizontal();
    float totalH = m_layoutParams.padding.vertical();

    // 头部尺寸
    if (!m_title.empty() || !m_subtitle.empty() || m_headerAction) {
        float textH = 0.0f;
        if (!m_title.empty()) textH += 22.0f;
        if (!m_subtitle.empty()) textH += (!m_title.empty() ? 2.0f : 0.0f) + 20.0f;
        float headerH = textH;

        if (m_headerAction && m_headerAction->isVisible()) {
            Size actionSize = m_headerAction->measure(contentW, 100.0f, ctx);
            headerH = std::max(headerH, actionSize.height);
        }
        totalH += headerH + 16.0f; // 头部与内容区间距
    }

    // 子项测量（排除已在头部单独计量的 m_headerAction）
    std::vector<std::shared_ptr<UIElement>> bodyChildren;
    for (const auto& c : m_children) {
        if (c != m_headerAction) {
            bodyChildren.push_back(c);
        }
    }

    if (!bodyChildren.empty()) {
        LayoutParams bodyParams = m_layoutParams;
        bodyParams.padding = Thickness(0.0f);
        Size bodySize = UILayout::measureChildren(bodyChildren, bodyParams, contentW, availableHeight, ctx);
        totalH += bodySize.height;
    }

    m_desiredSize = Size(availableWidth, totalH);
    return m_desiredSize;
}

void UICard::layout(const Rect& bounds, UIRenderContext& ctx) {
    m_bounds = bounds;
    if (!m_visible) return;

    for (auto& child : m_children) {
        if (child) {
            try { child->setParent(shared_from_this()); } catch (...) {}
            child->setWindowHost(getWindowHost());
        }
    }

    Rect inner = bounds.inset(m_layoutParams.padding);
    float currentY = inner.top;

    if (!m_title.empty() || !m_subtitle.empty() || m_headerAction) {
        float textH = 0.0f;
        if (!m_title.empty()) textH += 22.0f;
        if (!m_subtitle.empty()) textH += (!m_title.empty() ? 2.0f : 0.0f) + 20.0f;
        float headerH = textH;

        if (m_headerAction && m_headerAction->isVisible()) {
            Size aSize = m_headerAction->desiredSize();
            headerH = std::max(headerH, aSize.height);
            m_headerAction->layout(Rect(inner.right - aSize.width, currentY, inner.right, currentY + aSize.height), ctx);
        }
        currentY += headerH + 16.0f;
    }

    m_bodyRect = Rect(inner.left, currentY, inner.right, inner.bottom);

    std::vector<std::shared_ptr<UIElement>> bodyChildren;
    for (const auto& c : m_children) {
        if (c != m_headerAction) {
            bodyChildren.push_back(c);
        }
    }

    if (!bodyChildren.empty()) {
        LayoutParams bodyParams = m_layoutParams;
        bodyParams.padding = Thickness(0.0f);
        UILayout::arrangeChildren(bodyChildren, bodyParams, m_bodyRect, ctx);
    }
}

void UICard::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    // 绘制微晶卡片底板
    GlassmorphismRenderer::drawCard(ctx, m_bounds, 12.0f, m_hovered, dark);

    // 绘制标题与副标题
    Rect inner = m_bounds.inset(m_layoutParams.padding);
    float currentY = inner.top;

    if (!m_title.empty()) {
        float maxTitleW = (m_headerAction && m_headerAction->isVisible())
            ? (inner.width() - m_headerAction->desiredSize().width - 12.0f)
            : inner.width();
        Rect titleRect(inner.left, currentY, inner.left + maxTitleW, currentY + 22.0f);
        ctx.drawText(m_title, titleRect, p.textPrimary, FontToken::Md, FontWeight::SemiBold,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
        currentY += 22.0f;
    }

    if (!m_subtitle.empty()) {
        float maxSubW = (m_headerAction && m_headerAction->isVisible())
            ? (inner.width() - m_headerAction->desiredSize().width - 12.0f)
            : inner.width();
        Rect subRect(inner.left, currentY + 2.0f, inner.left + maxSubW, currentY + 20.0f);
        ctx.drawText(m_subtitle, subRect, p.textMuted, FontToken::Sm, FontWeight::Medium,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    }

    // 头部下方精细水平分割线 (对齐 React .uikit-card__header 边框)
    if ((!m_title.empty() || !m_subtitle.empty()) && !m_children.empty()) {
        float dividerY = m_bodyRect.top - 8.0f;
        Color headerDivider = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.06f, 0.09f, 0.16f, 0.08f);
        ctx.drawLine(Point(inner.left, dividerY), Point(inner.right, dividerY), headerDivider, 1.0f);
    }

    // 行与行之间的微晶分割线 (对齐 React .uikit-setting-row + .uikit-setting-row 顶边框)
    std::vector<std::shared_ptr<UIElement>> bodyChildren;
    for (const auto& c : m_children) {
        if (c != m_headerAction && c->isVisible()) {
            bodyChildren.push_back(c);
        }
    }
    if (bodyChildren.size() > 1) {
        Color rowDivider = dark ? Color(1.0f, 1.0f, 1.0f, 0.05f) : Color(0.06f, 0.09f, 0.16f, 0.05f);
        for (size_t i = 0; i + 1 < bodyChildren.size(); ++i) {
            float lineY = bodyChildren[i]->bounds().bottom + m_layoutParams.gap * 0.5f;
            ctx.drawLine(Point(inner.left, lineY), Point(inner.right, lineY), rowDivider, 1.0f);
        }
    }
}

} // namespace tools3000::ui::native
