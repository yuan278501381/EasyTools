#include "ui/native/components/UIToggle.h"
#include "ui/native/common/UITheme.h"
#include <algorithm>

namespace tools3000::ui::native {

UIToggle::UIToggle(bool checked, std::function<void(bool)> onToggle)
    : m_checked(checked), m_onToggle(std::move(onToggle)) {
    m_thumbProgress.setImmediate(checked ? 1.0f : 0.0f);
}

void UIToggle::setChecked(bool checked, bool animated) {
    if (m_checked != checked) {
        m_checked = checked;
        if (animated) {
            m_thumbProgress.setTarget(m_checked ? 1.0f : 0.0f, 160.0f, EasingType::SmoothCubic);
        } else {
            m_thumbProgress.setImmediate(m_checked ? 1.0f : 0.0f);
        }
        markNeedsPaint();
    }
}

void UIToggle::toggle() {
    if (!m_enabled) return;
    setChecked(!m_checked, true);
    if (m_onToggle) {
        m_onToggle(m_checked);
    }
}

Size UIToggle::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    const float switchW = 44.0f;
    const float switchH = 24.0f;

    if (m_label.empty() && m_description.empty()) {
        m_desiredSize = Size(switchW, switchH);
        return m_desiredSize;
    }

    float textMaxW = (availableWidth > switchW + 16.0f) ? (availableWidth - switchW - 16.0f) : 10000.0f;
    Size labelSize = ctx.measureText(m_label, FontToken::Base, FontWeight::Medium, FontFamilyType::Sans, textMaxW);
    Size descSize(0.0f, 0.0f);
    if (!m_description.empty()) {
        descSize = ctx.measureText(m_description, FontToken::Sm, FontWeight::Medium, FontFamilyType::Sans, textMaxW);
    }

    float textW = std::max(labelSize.width, descSize.width);
    float textH = labelSize.height + (descSize.height > 0.0f ? (descSize.height + 2.0f) : 0.0f);

    float totalW = textW + 16.0f + switchW;
    float totalH = std::max(switchH, textH);

    m_desiredSize = Size(totalW, totalH);
    return m_desiredSize;
}

bool UIToggle::update(float dt) {
    bool dirty = m_thumbProgress.update(dt);
    dirty |= m_hoverAlpha.update(dt);
    if (dirty) {
        markNeedsPaint();
    }
    bool active = m_thumbProgress.isAnimating() || m_hoverAlpha.isAnimating();
    active |= UIElement::update(dt);
    return active;
}

void UIToggle::onMouseEnter() {
    UIElement::onMouseEnter();
    m_hoverAlpha.setTarget(1.0f, 150.0f, EasingType::SmoothCubic);
}

void UIToggle::onMouseLeave() {
    UIElement::onMouseLeave();
    m_hoverAlpha.setTarget(0.0f, 180.0f, EasingType::SmoothCubic);
}

bool UIToggle::onMouseDown(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled) {
        toggle();
        return true;
    }
    return false;
}

void UIToggle::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    const float switchW = 44.0f;
    const float switchH = 24.0f;
    const float radius = switchH * 0.5f;

    // 默认开关靠右排布
    Rect switchRect;
    if (m_label.empty() && m_description.empty()) {
        switchRect = Rect(m_bounds.left + (m_bounds.width() - switchW) * 0.5f,
                          m_bounds.top + (m_bounds.height() - switchH) * 0.5f,
                          m_bounds.left + (m_bounds.width() + switchW) * 0.5f,
                          m_bounds.top + (m_bounds.height() + switchH) * 0.5f);
    } else {
        switchRect = Rect(m_bounds.right - switchW,
                          m_bounds.top + (m_bounds.height() - switchH) * 0.5f,
                          m_bounds.right,
                          m_bounds.top + (m_bounds.height() + switchH) * 0.5f);

        // 绘制左侧文本标签与说明
        Rect textBounds(m_bounds.left, m_bounds.top, switchRect.left - 12.0f, m_bounds.bottom);
        if (!m_description.empty()) {
            Rect labelRect(textBounds.left, textBounds.top + (textBounds.height() - 34.0f) * 0.5f,
                           textBounds.right, textBounds.top + (textBounds.height() - 34.0f) * 0.5f + 18.0f);
            ctx.drawText(m_label, labelRect, p.textPrimary, FontToken::Base, FontWeight::Medium,
                         FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

            Rect descRect(textBounds.left, labelRect.bottom + 2.0f, textBounds.right, labelRect.bottom + 18.0f);
            ctx.drawText(m_description, descRect, p.textMuted, FontToken::Sm, FontWeight::Medium,
                         FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
        } else {
            ctx.drawText(m_label, textBounds, p.textPrimary, FontToken::Base, FontWeight::Medium,
                         FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
        }
    }

    // 1. 绘制滑道底色 (关闭态暗灰/浅灰，开启态鲜活主题色，结合插值)
    const float progress = m_thumbProgress.get();
    Color offTrack = dark ? Color::fromHex(0x27273a, 0.85f) : Color::fromHex(0xe2e8f0, 0.95f);
    Color onTrack = p.primary;
    Color currentTrack = Color::lerp(offTrack, onTrack, progress);

    ctx.fillRoundedRect(switchRect, radius, currentTrack);

    // 滑道顶部 1px 细微晶高光线
    Color trackSheen = Color(1.0f, 1.0f, 1.0f, progress > 0.5f ? 0.25f : (dark ? 0.10f : 0.65f));
    ctx.drawLine(
        Point(switchRect.left + radius * 0.5f, switchRect.top + 0.8f),
        Point(switchRect.right - radius * 0.5f, switchRect.top + 0.8f),
        trackSheen, 1.0f
    );

    // 外边框
    Color borderColor = Color::lerp(
        dark ? Color(1.0f, 1.0f, 1.0f, 0.10f) : Color(0.06f, 0.09f, 0.16f, 0.08f),
        p.primary.withAlpha(0.6f),
        progress
    );
    ctx.drawRoundedRect(switchRect, radius, borderColor, 1.0f);

    // 2. 绘制滑块 (圆球 + 双层物理柔和下沉投影)
    const float thumbDiameter = 18.0f;
    const float thumbRadius = thumbDiameter * 0.5f;
    const float minX = switchRect.left + 3.0f + thumbRadius;
    const float maxX = switchRect.right - 3.0f - thumbRadius;
    const float thumbX = minX + (maxX - minX) * progress;
    const float thumbY = switchRect.top + switchH * 0.5f;

    // 滑块双层柔和微阴影
    ctx.fillCircle(Point(thumbX, thumbY + 1.2f), thumbRadius + 0.8f, Color(0.0f, 0.0f, 0.0f, dark ? 0.38f : 0.18f));
    ctx.fillCircle(Point(thumbX, thumbY + 2.5f), thumbRadius + 2.0f, Color(0.0f, 0.0f, 0.0f, dark ? 0.20f : 0.09f));

    // 滑块本体 (纯白陶瓷)
    ctx.fillCircle(Point(thumbX, thumbY), thumbRadius, Color(1.0f, 1.0f, 1.0f, 1.0f));

    // 滑块顶部微晶高光弧
    ctx.drawCircle(Point(thumbX, thumbY), thumbRadius, Color(1.0f, 1.0f, 1.0f, 0.95f), 1.0f);
}

} // namespace tools3000::ui::native
