#include "ui/native/components/UISettingRow.h"
#include "ui/native/components/UIToggle.h"
#include "ui/native/common/UITheme.h"
#include <algorithm>

namespace tools3000::ui::native {

UISettingRow::UISettingRow(const std::wstring& label,
                           const std::wstring& description,
                           std::shared_ptr<UIElement> control)
    : m_label(label), m_description(description), m_control(control) {
    m_layoutParams.padding = Thickness(0.0f, 6.0f, 0.0f, 6.0f);
    if (m_control) {
        m_children.push_back(m_control);
    }
}

void UISettingRow::setControl(std::shared_ptr<UIElement> control) {
    if (m_control) {
        removeChild(m_control);
    }
    m_control = control;
    if (m_control) {
        addChild(m_control);
    }
    markNeedsLayout();
}

Size UISettingRow::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    float controlW = 0.0f;
    float controlH = 0.0f;

    if (m_control && m_control->isVisible()) {
        Size cSize = m_control->measure(availableWidth * 0.5f, 100.0f, ctx);
        controlW = cSize.width;
        controlH = cSize.height;
    }

    float textMaxW = std::max(60.0f, availableWidth - controlW - 24.0f);
    Size labelSize = ctx.measureText(m_label, FontToken::Base, FontWeight::SemiBold, FontFamilyType::Sans, textMaxW);
    Size descSize(0.0f, 0.0f);
    if (!m_description.empty()) {
        descSize = ctx.measureText(m_description, FontToken::Sm, FontWeight::Medium, FontFamilyType::Sans, textMaxW);
    }

    float textH = labelSize.height + (descSize.height > 0.0f ? (descSize.height + 4.0f) : 0.0f);
    float rowH = std::max(textH, controlH) + m_layoutParams.padding.vertical();

    m_desiredSize = Size(availableWidth, std::max(38.0f, rowH));
    return m_desiredSize;
}

void UISettingRow::layout(const Rect& bounds, UIRenderContext& ctx) {
    m_bounds = bounds;
    if (!m_visible) return;

    if (m_control) {
        try { m_control->setParent(shared_from_this()); } catch (...) {}
        m_control->setWindowHost(getWindowHost());
    }

    Rect inner = bounds.inset(m_layoutParams.padding);

    if (m_control && m_control->isVisible()) {
        Size cSize = m_control->desiredSize();
        float ctrlTop = inner.top + (inner.height() - cSize.height) * 0.5f;
        const float rightMargin = 4.0f;
        Rect ctrlRect(inner.right - cSize.width - rightMargin, ctrlTop, inner.right - rightMargin, ctrlTop + cSize.height);
        m_control->layout(ctrlRect, ctx);
    }
}

bool UISettingRow::onMouseDown(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled) {
        if (m_control && m_control->isEnabled()) {
            auto toggle = std::dynamic_pointer_cast<UIToggle>(m_control);
            if (toggle) {
                toggle->toggle();
                return true;
            }
        }
    }
    return UIElement::onMouseDown(e);
}

void UISettingRow::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    Rect inner = m_bounds.inset(m_layoutParams.padding);
    float controlW = (m_control && m_control->isVisible()) ? (m_control->desiredSize().width + 24.0f) : 0.0f;
    Rect textRect(inner.left, inner.top, inner.right - controlW, inner.bottom);

    if (!m_description.empty()) {
        float labelH = 20.0f;
        float descH = 18.0f;
        float totalTextH = labelH + descH + 2.0f;
        float startY = textRect.top + (textRect.height() - totalTextH) * 0.5f;

        Rect labelBounds(textRect.left, startY, textRect.right, startY + labelH);
        ctx.drawText(m_label, labelBounds, p.textPrimary, FontToken::Base, FontWeight::SemiBold,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

        Rect descBounds(textRect.left, labelBounds.bottom + 2.0f, textRect.right, labelBounds.bottom + 2.0f + descH);
        ctx.drawText(m_description, descBounds, p.textMuted, FontToken::Sm, FontWeight::Medium,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    } else {
        ctx.drawText(m_label, textRect, p.textPrimary, FontToken::Base, FontWeight::SemiBold,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    }
}

} // namespace tools3000::ui::native
