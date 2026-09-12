#include "ui/native/components/UISettingGroup.h"
#include "ui/native/common/UITheme.h"
#include <algorithm>

namespace tools3000::ui::native {

UISettingGroup::UISettingGroup(const std::wstring& title, IconType icon)
    : m_title(title), m_icon(icon) {
    m_layoutParams.direction = FlexDirection::Column;
    m_layoutParams.gap = 14.0f;
    m_layoutParams.padding = Thickness(0.0f, 6.0f, 0.0f, 16.0f);
}

Size UISettingGroup::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    float totalH = 0.0f;

    if (!m_title.empty() || m_icon != IconType::None) {
        totalH += 28.0f; // 头部标题区域
    }

    if (!m_children.empty()) {
        LayoutParams groupBodyParams = m_layoutParams;
        groupBodyParams.padding = Thickness(0.0f);
        Size contentSize = UILayout::measureChildren(m_children, groupBodyParams, availableWidth, availableHeight, ctx);
        totalH += contentSize.height;
    }

    m_desiredSize = Size(availableWidth, totalH + m_layoutParams.padding.vertical());
    return m_desiredSize;
}

void UISettingGroup::layout(const Rect& bounds, UIRenderContext& ctx) {
    m_bounds = bounds;
    if (!m_visible) return;

    for (auto& child : m_children) {
        if (child) {
            try { child->setParent(shared_from_this()); } catch (...) {}
            child->setWindowHost(getWindowHost());
        }
    }

    float headerH = (!m_title.empty() || m_icon != IconType::None) ? 28.0f : 0.0f;
    m_contentRect = Rect(bounds.left, bounds.top + headerH + m_layoutParams.padding.top,
                         bounds.right, bounds.bottom - m_layoutParams.padding.bottom);

    if (!m_children.empty()) {
        LayoutParams groupBodyParams = m_layoutParams;
        groupBodyParams.padding = Thickness(0.0f);
        UILayout::arrangeChildren(m_children, groupBodyParams, m_contentRect, ctx);
    }
}

void UISettingGroup::onRenderContent(UIRenderContext& ctx) {
    if (m_title.empty() && m_icon == IconType::None) return;

    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    float currentX = m_bounds.left;
    const float headerTop = m_bounds.top + m_layoutParams.padding.top;

    if (m_icon != IconType::None) {
        Rect iconRect(currentX, headerTop + 3.0f, currentX + 18.0f, headerTop + 21.0f);
        VectorIconRenderer::drawIcon(ctx, m_icon, iconRect, p.primary, 1.8f);
        currentX += 26.0f;
    }

    if (!m_title.empty()) {
        Rect titleRect(currentX, headerTop, m_bounds.right, headerTop + 24.0f);
        ctx.drawText(m_title, titleRect, p.textPrimary, FontToken::Lg, FontWeight::Bold,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    }
}

} // namespace tools3000::ui::native
