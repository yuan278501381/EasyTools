#include "ui/native/components/UISelect.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include "ui/native/window/NativeWindowHost.h"
#include <algorithm>

namespace tools3000::ui::native {

class UISelectDropdownOverlay : public UIElement {
public:
    UISelectDropdownOverlay(UISelect* owner,
                            const std::vector<SelectOption>& options,
                            const std::string& selectedValue,
                            const Rect& bounds)
        : m_owner(owner), m_options(options), m_selectedValue(selectedValue) {
        m_bounds = bounds;
        m_openProgress.setTarget(1.0f, 150.0f, EasingType::SmoothCubic);
    }

    Rect getOptionRect(size_t index) const {
        const float itemH = 30.0f;
        float top = m_bounds.top + 4.0f + index * itemH;
        return Rect(m_bounds.left + 4.0f, top, m_bounds.right - 4.0f, top + itemH);
    }

    std::shared_ptr<UIElement> hitTest(Point pt) override {
        if (!m_visible || !m_enabled) return nullptr;
        if (m_bounds.contains(pt)) return shared_from_this();
        return nullptr;
    }

    bool update(float dt) override {
        bool dirty = m_openProgress.update(dt);
        if (dirty) {
            markNeedsPaint();
        }
        return m_openProgress.isAnimating() || UIElement::update(dt);
    }

    bool onMouseMove(const UIMouseEvent& e) override {
        int oldIndex = m_hoveredIndex;
        m_hoveredIndex = -1;
        for (size_t i = 0; i < m_options.size(); ++i) {
            if (getOptionRect(i).contains(e.position)) {
                m_hoveredIndex = static_cast<int>(i);
                break;
            }
        }
        if (m_hoveredIndex != oldIndex) {
            markNeedsPaint();
        }
        return true;
    }

    bool onMouseDown(const UIMouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        for (size_t i = 0; i < m_options.size(); ++i) {
            if (getOptionRect(i).contains(e.position)) {
                std::string chosen = m_options[i].value;
                auto host = getWindowHost();
                if (m_owner) {
                    m_owner->setSelectedValue(chosen);
                    if (m_owner->getOnSelect()) {
                        m_owner->getOnSelect()(chosen);
                    }
                }
                if (host) {
                    host->closePopup();
                }
                return true;
            }
        }
        return true;
    }

    void onRenderContent(UIRenderContext& ctx) override {
        const auto& theme = UITheme::instance();
        const auto& p = theme.palette();
        const bool dark = theme.isEffectiveDark();

        GlassmorphismRenderer::drawGlassPanel(ctx, m_bounds, 8.0f, dark, 2);

        for (size_t i = 0; i < m_options.size(); ++i) {
            Rect optRect = getOptionRect(i);
            const bool isSelected = (m_options[i].value == m_selectedValue);
            const bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

            if (isHovered) {
                Color hBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.0f, 0.0f, 0.0f, 0.05f);
                ctx.fillRoundedRect(optRect, 5.0f, hBg);
            }

            Color textColor = isSelected ? p.primary : p.textPrimary;
            Rect optTextRect(optRect.left + 8.0f, optRect.top, optRect.right - 24.0f, optRect.bottom);
            ctx.drawText(m_options[i].label, optTextRect, textColor, FontToken::Base,
                         isSelected ? FontWeight::SemiBold : FontWeight::Medium,
                         FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

            if (isSelected) {
                Rect checkRect(optRect.right - 20.0f, optRect.top + (optRect.height() - 12.0f) * 0.5f,
                               optRect.right - 8.0f, optRect.top + (optRect.height() + 12.0f) * 0.5f);
                VectorIconRenderer::drawIcon(ctx, IconType::Check, checkRect, p.primary, 2.0f);
            }
        }
    }

private:
    UISelect* m_owner = nullptr;
    std::vector<SelectOption> m_options;
    std::string m_selectedValue;
    int m_hoveredIndex = -1;
    AnimatedFloat m_openProgress{0.0f};
};

UISelect::UISelect(const std::vector<SelectOption>& options,
                   const std::string& selectedValue,
                   std::function<void(const std::string&)> onSelect)
    : m_options(options), m_selectedValue(selectedValue), m_onSelect(std::move(onSelect)) {
}

void UISelect::setOptions(const std::vector<SelectOption>& options) {
    m_options = options;
    markNeedsLayout();
}

void UISelect::setSelectedValue(const std::string& val) {
    if (m_selectedValue != val) {
        m_selectedValue = val;
        markNeedsPaint();
    }
}

Size UISelect::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    float maxTextW = 60.0f;
    for (const auto& opt : m_options) {
        Size sz = ctx.measureText(opt.label, FontToken::Base, FontWeight::Medium);
        maxTextW = std::max(maxTextW, sz.width);
    }

    float width = maxTextW + 40.0f; // 文本 + 箭头 + 内边距
    if (availableWidth > 0.0f) {
        width = std::min(width, availableWidth);
    }
    m_desiredSize = Size(std::max(120.0f, width), 32.0f);
    return m_desiredSize;
}

bool UISelect::update(float dt) {
    bool dirty = m_openProgress.update(dt);
    dirty |= m_hoverAlpha.update(dt);
    if (dirty) {
        markNeedsPaint();
    }
    bool active = m_openProgress.isAnimating() || m_hoverAlpha.isAnimating();
    active |= UIElement::update(dt);
    return active;
}

void UISelect::onMouseEnter() {
    UIElement::onMouseEnter();
    m_hoverAlpha.setTarget(1.0f, 150.0f, EasingType::SmoothCubic);
}

void UISelect::onMouseLeave() {
    UIElement::onMouseLeave();
    m_hoverAlpha.setTarget(0.0f, 180.0f, EasingType::SmoothCubic);
}

void UISelect::onBlur() {
    UIElement::onBlur();
    if (m_isOpen) {
        auto host = getWindowHost();
        if (host && host->hasActivePopup()) {
            host->closePopup();
        }
        m_isOpen = false;
        m_openProgress.setTarget(0.0f, 150.0f, EasingType::SmoothCubic);
        markNeedsPaint();
    }
}

Rect UISelect::getDropdownRect() const {
    const float itemH = 30.0f;
    const float totalH = m_options.size() * itemH + 8.0f;
    auto host = getWindowHost();
    if (host && host->hwnd()) {
        RECT rc{};
        GetClientRect(host->hwnd(), &rc);
        float windowH = static_cast<float>(rc.bottom - rc.top) / host->getDpiScale();
        if (m_bounds.bottom + 4.0f + totalH > windowH && m_bounds.top - 4.0f - totalH > 0.0f) {
            return Rect(m_bounds.left, m_bounds.top - 4.0f - totalH, m_bounds.right, m_bounds.top - 4.0f);
        }
    }
    return Rect(m_bounds.left, m_bounds.bottom + 4.0f, m_bounds.right, m_bounds.bottom + 4.0f + totalH);
}

Rect UISelect::getOptionRect(size_t index) const {
    Rect drop = getDropdownRect();
    const float itemH = 30.0f;
    float top = drop.top + 4.0f + index * itemH;
    return Rect(drop.left + 4.0f, top, drop.right - 4.0f, top + itemH);
}

std::shared_ptr<UIElement> UISelect::hitTest(Point pt) {
    if (!m_visible || !m_enabled) return nullptr;

    if (m_bounds.contains(pt)) {
        return shared_from_this();
    }

    // 内联兼容测试模式 (未接入 NativeWindowHost 弹出层时)
    if (m_isOpen && !getWindowHost() && getDropdownRect().contains(pt)) {
        return shared_from_this();
    }

    return nullptr;
}

bool UISelect::onMouseDown(const UIMouseEvent& e) {
    if (e.button != MouseButton::Left || !m_enabled) return false;

    // 内联兼容模式：处于展开态并点击到选项
    if (m_isOpen && !getWindowHost()) {
        Rect drop = getDropdownRect();
        if (drop.contains(e.position)) {
            for (size_t i = 0; i < m_options.size(); ++i) {
                if (getOptionRect(i).contains(e.position)) {
                    m_selectedValue = m_options[i].value;
                    if (m_onSelect) {
                        m_onSelect(m_selectedValue);
                    }
                    break;
                }
            }
        }
        m_isOpen = false;
        m_openProgress.setTarget(0.0f, 150.0f, EasingType::SmoothCubic);
        markNeedsPaint();
        return true;
    }

    if (m_bounds.contains(e.position)) {
        auto host = getWindowHost();
        if (host) {
            if (host->hasActivePopup()) {
                host->closePopup();
                m_isOpen = false;
                markNeedsPaint();
                return true;
            }

            m_isOpen = true;
            Rect drop = getDropdownRect();
            auto overlay = std::make_shared<UISelectDropdownOverlay>(this, m_options, m_selectedValue, drop);
            host->showPopup(overlay, m_bounds, [this]() {
                m_isOpen = false;
                markNeedsPaint();
            });
            markNeedsPaint();
            return true;
        } else {
            // 宿主为空（纯单元测试环境）走内联
            m_isOpen = true;
            m_openProgress.setTarget(1.0f, 180.0f, EasingType::SmoothCubic);
            markNeedsPaint();
            return true;
        }
    }

    return false;
}

bool UISelect::onMouseMove(const UIMouseEvent& e) {
    if (m_isOpen && !getWindowHost()) {
        int oldIndex = m_hoveredIndex;
        m_hoveredIndex = -1;
        for (size_t i = 0; i < m_options.size(); ++i) {
            if (getOptionRect(i).contains(e.position)) {
                m_hoveredIndex = static_cast<int>(i);
                break;
            }
        }
        if (m_hoveredIndex != oldIndex) {
            markNeedsPaint();
        }
        return true;
    }
    return false;
}

void UISelect::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    // 1. 绘制触发器输入框
    const float radius = 7.0f;
    // 下沉微阴影
    ctx.drawRoundedRect(
        Rect(m_bounds.left, m_bounds.top + 1.0f, m_bounds.right, m_bounds.bottom + 1.0f),
        radius, Color(0.0f, 0.0f, 0.0f, dark ? 0.20f : 0.04f), 1.0f
    );

    Color bg = dark ? Color::fromHex(0x191724, 0.92f) : Color::fromHex(0xffffff, 0.96f);
    Color border = m_isOpen
        ? p.primary
        : (dark ? Color(1.0f, 1.0f, 1.0f, 0.10f + 0.08f * m_hoverAlpha.get())
                : (m_hoverAlpha.get() > 0.01f ? p.primary.withAlpha(0.35f) : Color(0.06f, 0.09f, 0.16f, 0.10f)));

    ctx.fillRoundedRect(m_bounds, radius, bg);

    // 顶部 1px 极细微晶反光高光
    Color sheen = dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(1.0f, 1.0f, 1.0f, 0.90f);
    ctx.drawLine(
        Point(m_bounds.left + radius * 0.5f, m_bounds.top + 0.8f),
        Point(m_bounds.right - radius * 0.5f, m_bounds.top + 0.8f),
        sheen, 1.0f
    );

    ctx.drawRoundedRect(m_bounds, radius, border, 1.0f);

    // 绘制当前选中的标签
    std::wstring displayLabel = L"请选择";
    for (const auto& opt : m_options) {
        if (opt.value == m_selectedValue) {
            displayLabel = opt.label;
            break;
        }
    }

    Rect labelRect(m_bounds.left + 10.0f, m_bounds.top, m_bounds.right - 26.0f, m_bounds.bottom);
    ctx.drawText(displayLabel, labelRect, p.textPrimary, FontToken::Base, FontWeight::Medium,
                 FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

    // 绘制下拉指示 Chevron 图标
    Rect iconRect(m_bounds.right - 22.0f, m_bounds.top + (m_bounds.height() - 14.0f) * 0.5f,
                  m_bounds.right - 8.0f, m_bounds.top + (m_bounds.height() + 14.0f) * 0.5f);
    VectorIconRenderer::drawIcon(ctx, m_isOpen ? IconType::ChevronUp : IconType::ChevronDown, iconRect, p.textMuted, 1.8f);

    // 2. 内联模式下的下拉菜单绘制（有 NativeWindowHost 时由顶层 Popup 绘制，避免裁剪与层级遮挡）
    if (!getWindowHost()) {
        float openProg = m_openProgress.get();
        if (openProg > 0.01f) {
            Rect dropRect = getDropdownRect();
            GlassmorphismRenderer::drawGlassPanel(ctx, dropRect, 8.0f, dark, 2);

            for (size_t i = 0; i < m_options.size(); ++i) {
                Rect optRect = getOptionRect(i);
                const bool isSelected = (m_options[i].value == m_selectedValue);
                const bool isHovered = (static_cast<int>(i) == m_hoveredIndex);

                if (isHovered) {
                    Color hBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.0f, 0.0f, 0.0f, 0.05f);
                    ctx.fillRoundedRect(optRect, 5.0f, hBg);
                }

                Color textColor = isSelected ? p.primary : p.textPrimary;
                Rect optTextRect(optRect.left + 8.0f, optRect.top, optRect.right - 24.0f, optRect.bottom);
                ctx.drawText(m_options[i].label, optTextRect, textColor, FontToken::Base,
                             isSelected ? FontWeight::SemiBold : FontWeight::Medium,
                             FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

                if (isSelected) {
                    Rect checkRect(optRect.right - 20.0f, optRect.top + (optRect.height() - 12.0f) * 0.5f,
                                   optRect.right - 8.0f, optRect.top + (optRect.height() + 12.0f) * 0.5f);
                    VectorIconRenderer::drawIcon(ctx, IconType::Check, checkRect, p.primary, 2.0f);
                }
            }
        }
    }
}

} // namespace tools3000::ui::native
