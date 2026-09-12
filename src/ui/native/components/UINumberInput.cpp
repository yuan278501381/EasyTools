#include "ui/native/components/UINumberInput.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <algorithm>

namespace tools3000::ui::native {

UINumberInput::UINumberInput(int value, int minVal, int maxVal, int step,
                             const std::wstring& unit,
                             std::function<void(int)> onChange)
    : m_value(std::clamp(value, minVal, maxVal)),
      m_minVal(minVal),
      m_maxVal(maxVal),
      m_step(step),
      m_unit(unit),
      m_onChange(std::move(onChange)) {
}

void UINumberInput::setValue(int val) {
    int clamped = std::clamp(val, m_minVal, m_maxVal);
    if (m_value != clamped) {
        m_value = clamped;
        markNeedsPaint();
    }
}

Size UINumberInput::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableWidth;
    (void)availableHeight;
    (void)ctx;
    m_desiredSize = Size(110.0f, 32.0f);
    return m_desiredSize;
}

Rect UINumberInput::getUpButtonRect() const {
    return Rect(m_bounds.right - 22.0f, m_bounds.top + 2.0f, m_bounds.right - 2.0f, m_bounds.top + m_bounds.height() * 0.5f);
}

Rect UINumberInput::getDownButtonRect() const {
    return Rect(m_bounds.right - 22.0f, m_bounds.top + m_bounds.height() * 0.5f, m_bounds.right - 2.0f, m_bounds.bottom - 2.0f);
}

bool UINumberInput::onMouseDown(const UIMouseEvent& e) {
    if (e.button != MouseButton::Left || !m_enabled) return false;

    if (getUpButtonRect().contains(e.position)) {
        setValue(m_value + m_step);
        if (m_onChange) m_onChange(m_value);
        return true;
    }

    if (getDownButtonRect().contains(e.position)) {
        setValue(m_value - m_step);
        if (m_onChange) m_onChange(m_value);
        return true;
    }

    return false;
}

bool UINumberInput::onMouseWheel(const UIMouseEvent& e) {
    if (!m_enabled) return false;

    if (e.wheelDelta > 0.0f) {
        setValue(m_value + m_step);
    } else {
        setValue(m_value - m_step);
    }
    if (m_onChange) m_onChange(m_value);
    return true;
}

void UINumberInput::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    const float radius = 6.0f;
    Color bg = dark ? Color::fromHex(0x0f0f19, 0.75f) : Color::fromHex(0xffffff, 0.90f);
    Color border = dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.10f);

    ctx.fillRoundedRect(m_bounds, radius, bg);
    ctx.drawRoundedRect(m_bounds, radius, border, 1.0f);

    // 绘制数字文本 + 单位
    std::wstring displayStr = std::to_wstring(m_value);
    if (!m_unit.empty()) {
        displayStr += L" " + m_unit;
    }

    Rect textRect(m_bounds.left + 10.0f, m_bounds.top, m_bounds.right - 24.0f, m_bounds.bottom);
    ctx.drawText(displayStr, textRect, p.textPrimary, FontToken::Base, FontWeight::Medium,
                 FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

    // 绘制上下步进微图标
    Rect upRect = getUpButtonRect();
    Rect downRect = getDownButtonRect();

    VectorIconRenderer::drawIcon(ctx, IconType::ChevronUp, upRect, p.textMuted, 1.8f);
    VectorIconRenderer::drawIcon(ctx, IconType::ChevronDown, downRect, p.textMuted, 1.8f);
}

} // namespace tools3000::ui::native
