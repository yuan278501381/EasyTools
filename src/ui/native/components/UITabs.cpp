#include "ui/native/components/UITabs.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <algorithm>

namespace tools3000::ui::native {

UITabs::UITabs(const std::vector<TabItem>& tabs,
               const std::string& activeId,
               std::function<void(const std::string&)> onTabChange)
    : m_tabs(tabs), m_activeId(activeId), m_onTabChange(std::move(onTabChange)) {
    if (m_activeId.empty() && !m_tabs.empty()) {
        m_activeId = m_tabs[0].id;
    }
}

void UITabs::setTabs(const std::vector<TabItem>& tabs) {
    m_tabs = tabs;
    if (!m_tabs.empty()) {
        bool found = false;
        for (const auto& t : m_tabs) {
            if (t.id == m_activeId) { found = true; break; }
        }
        if (!found) m_activeId = m_tabs[0].id;
    }
    markNeedsLayout();
}

size_t UITabs::getActiveIndex() const {
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].id == m_activeId) return i;
    }
    return 0;
}

void UITabs::setActiveTab(const std::string& id, bool animated) {
    if (m_activeId != id) {
        m_activeId = id;
        size_t idx = getActiveIndex();
        Rect tr = getTabRect(idx);
        if (animated) {
            m_indicatorX.setTarget(tr.left, 240.0f, EasingType::SpringBounce);
            m_indicatorWidth.setTarget(tr.width(), 240.0f, EasingType::SpringBounce);
        } else {
            m_indicatorX.setImmediate(tr.left);
            m_indicatorWidth.setImmediate(tr.width());
        }
        markNeedsPaint();
    }
}

Rect UITabs::getTabRect(size_t index) const {
    if (m_tabs.empty()) return Rect();
    const float pad = 3.0f;
    const float availW = m_bounds.width() - pad * 2.0f;
    const float tabW = availW / m_tabs.size();
    float left = m_bounds.left + pad + index * tabW;
    return Rect(left, m_bounds.top + pad, left + tabW, m_bounds.bottom - pad);
}

Size UITabs::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    float totalW = 6.0f;
    for (const auto& tab : m_tabs) {
        Size sz = ctx.measureText(tab.label, FontToken::Base, FontWeight::Medium);
        totalW += sz.width + 28.0f;
        if (tab.icon != IconType::None) totalW += 20.0f;
    }
    float finalW = (availableWidth > 0.0f) ? std::min(availableWidth, totalW) : totalW;
    m_desiredSize = Size(std::max(120.0f, finalW), 36.0f);
    return m_desiredSize;
}

void UITabs::layout(const Rect& bounds, UIRenderContext& ctx) {
    bool boundsChanged = (m_bounds != bounds);
    UIElement::layout(bounds, ctx);
    if (boundsChanged && !m_tabs.empty()) {
        size_t idx = getActiveIndex();
        Rect tr = getTabRect(idx);
        m_indicatorX.setImmediate(tr.left);
        m_indicatorWidth.setImmediate(tr.width());
    }
}

bool UITabs::update(float dt) {
    bool dirty = m_indicatorX.update(dt);
    dirty |= m_indicatorWidth.update(dt);
    if (dirty) {
        markNeedsPaint();
    }
    bool active = m_indicatorX.isAnimating() || m_indicatorWidth.isAnimating();
    active |= UIElement::update(dt);
    return active;
}

bool UITabs::onMouseDown(const UIMouseEvent& e) {
    if (e.button != MouseButton::Left || !m_enabled) return false;

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (getTabRect(i).contains(e.position)) {
            setActiveTab(m_tabs[i].id, true);
            if (m_onTabChange) {
                m_onTabChange(m_activeId);
            }
            return true;
        }
    }
    return false;
}

bool UITabs::onMouseMove(const UIMouseEvent& e) {
    int oldH = m_hoveredIndex;
    m_hoveredIndex = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (getTabRect(i).contains(e.position)) {
            m_hoveredIndex = static_cast<int>(i);
            break;
        }
    }
    if (m_hoveredIndex != oldH) {
        markNeedsPaint();
    }
    return true;
}

void UITabs::onMouseLeave() {
    UIElement::onMouseLeave();
    m_hoveredIndex = -1;
    markNeedsPaint();
}

void UITabs::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    // 1. 底槽胶囊背景
    const float radius = m_bounds.height() * 0.5f;
    Color trackBg = dark ? Color::fromHex(0x12111a, 0.85f) : Color::fromHex(0xe2e8f0, 0.70f);
    Color trackBorder = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.06f, 0.09f, 0.16f, 0.08f);

    ctx.fillRoundedRect(m_bounds, radius, trackBg);
    ctx.drawRoundedRect(m_bounds, radius, trackBorder, 1.0f);

    if (m_tabs.empty()) return;

    // 初始化滑块指示器位置
    size_t activeIdx = getActiveIndex();
    Rect activeRect = getTabRect(activeIdx);
    if (m_indicatorWidth.get() <= 0.001f) {
        m_indicatorX.setImmediate(activeRect.left);
        m_indicatorWidth.setImmediate(activeRect.width());
    }

    // 2. 绘制平滑滑动的激活胶囊微晶指示器
    Rect indicatorRect(
        m_indicatorX.get(),
        m_bounds.top + 3.0f,
        m_indicatorX.get() + m_indicatorWidth.get(),
        m_bounds.bottom - 3.0f
    );
    const float indRadius = indicatorRect.height() * 0.5f;

    Color indBg = dark ? Color::fromHex(0x28253b, 0.96f) : Color::fromHex(0xffffff, 0.98f);
    ctx.fillRoundedRect(indicatorRect, indRadius, indBg);

    // 顶部高光
    ctx.drawLine(
        Point(indicatorRect.left + indRadius * 0.5f, indicatorRect.top + 0.8f),
        Point(indicatorRect.right - indRadius * 0.5f, indicatorRect.top + 0.8f),
        Color(1.0f, 1.0f, 1.0f, dark ? 0.20f : 0.80f), 1.0f
    );

    Color indBorder = dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.08f);
    ctx.drawRoundedRect(indicatorRect, indRadius, indBorder, 1.0f);

    // 3. 绘制各 Tab 标签文本与图标
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        Rect tr = getTabRect(i);
        bool isActive = (i == activeIdx);
        Color textColor = isActive ? p.textPrimary : p.textMuted;
        if (static_cast<int>(i) == m_hoveredIndex && !isActive) {
            textColor = p.textSecondary;
        }

        ctx.drawText(m_tabs[i].label, tr, textColor, FontToken::Base,
                     isActive ? FontWeight::SemiBold : FontWeight::Medium,
                     FontFamilyType::Sans, TextAlignmentH::Center, TextAlignmentV::Center);
    }
}

} // namespace tools3000::ui::native
