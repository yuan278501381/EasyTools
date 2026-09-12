#include "ui/native/components/UIButton.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <algorithm>

namespace tools3000::ui::native {

UIButton::UIButton(const std::wstring& text, ButtonVariant variant, ButtonSize size)
    : m_text(text), m_variant(variant), m_size(size) {
}

void UIButton::setText(const std::wstring& text) {
    if (m_text != text) {
        m_text = text;
        markNeedsLayout();
    }
}

void UIButton::setVariant(ButtonVariant variant) {
    if (m_variant != variant) {
        m_variant = variant;
        markNeedsPaint();
    }
}

void UIButton::setButtonSize(ButtonSize size) {
    if (m_size != size) {
        m_size = size;
        markNeedsLayout();
    }
}

Size UIButton::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    if (m_isWindowControl) {
        float h = m_layoutParams.fixedHeight > 0 ? m_layoutParams.fixedHeight : 38.0f;
        float w = m_layoutParams.fixedWidth > 0 ? m_layoutParams.fixedWidth : 46.0f;
        m_desiredSize = Size(w, h);
        return m_desiredSize;
    }

    if (m_isNavItem) {
        float h = m_layoutParams.fixedHeight > 0 ? m_layoutParams.fixedHeight : 36.0f;
        float w = availableWidth > 0 ? availableWidth : 206.0f;
        m_desiredSize = Size(w, h);
        return m_desiredSize;
    }

    (void)availableWidth;
    (void)availableHeight;

    float height = 34.0f;
    float padH = 14.0f;
    FontToken font = FontToken::Base;

    switch (m_size) {
    case ButtonSize::Sm:
        height = 30.0f;
        padH = 12.0f;
        font = FontToken::Sm;
        break;
    case ButtonSize::Md:
        height = 36.0f;
        padH = 16.0f;
        font = FontToken::Base;
        break;
    case ButtonSize::Lg:
        height = 42.0f;
        padH = 20.0f;
        font = FontToken::Md;
        break;
    }

    float width = padH * 2.0f;
    if (m_icon != IconType::None) {
        width += (m_size == ButtonSize::Sm ? 14.0f : 16.0f);
        if (!m_text.empty()) width += 6.0f;
    }

    if (!m_text.empty()) {
        Size textSize = ctx.measureText(m_text, font, FontWeight::SemiBold);
        width += textSize.width;
    }

    m_desiredSize = Size(std::max(width, height), height);
    return m_desiredSize;
}

bool UIButton::update(float dt) {
    bool dirty = m_hoverAlpha.update(dt);
    dirty |= m_pressScale.update(dt);
    if (dirty) {
        markNeedsPaint();
    }
    bool active = m_hoverAlpha.isAnimating() || m_pressScale.isAnimating();
    active |= UIElement::update(dt);
    return active;
}

void UIButton::onMouseEnter() {
    UIElement::onMouseEnter();
    m_hoverAlpha.setTarget(1.0f, 150.0f, EasingType::SmoothCubic);
}

void UIButton::onMouseLeave() {
    UIElement::onMouseLeave();
    m_hoverAlpha.setTarget(0.0f, 180.0f, EasingType::SmoothCubic);
    m_pressScale.setTarget(1.0f, 180.0f, EasingType::SpringBounce);
}

bool UIButton::onMouseDown(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled) {
        UIElement::onMouseDown(e);
        if (!m_isWindowControl) {
            m_pressScale.setTarget(0.96f, 80.0f, EasingType::SmoothCubic);
        }
        return true;
    }
    return false;
}

bool UIButton::onMouseUp(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled) {
        m_pressScale.setTarget(1.0f, 220.0f, EasingType::SpringBounce);
        bool wasPressed = m_pressed;
        UIElement::onMouseUp(e);
        Rect hitArea = m_bounds.inset(Thickness(-6.0f));
        if (wasPressed && hitArea.contains(e.position) && m_onClick) {
            m_onClick();
        }
        return true;
    }
    return false;
}

void UIButton::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    // 绘制区域结合按压缩放微交互
    Rect drawRect = m_bounds;
    const float scaleFactor = m_pressScale.get();
    if (std::abs(scaleFactor - 1.0f) > 0.001f) {
        Point center = m_bounds.center();
        float w = m_bounds.width() * scaleFactor;
        float h = m_bounds.height() * scaleFactor;
        drawRect = Rect(center.x - w * 0.5f, center.y - h * 0.5f, center.x + w * 0.5f, center.y + h * 0.5f);
    }

    // 1. 窗口原生级控制按钮模式 (最小化、最大化、关闭)
    if (m_isWindowControl) {
        Color textColor = p.textPrimary;
        if (m_isCloseBtn) {
            if (m_pressed) {
                ctx.fillRect(drawRect, Color(0.768f, 0.063f, 0.122f, 1.0f));
                textColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
            } else if (m_hovered) {
                ctx.fillRect(drawRect, Color(0.91f, 0.106f, 0.137f, 1.0f)); // Win11 关闭红底 #E81123
                textColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
            }
        } else {
            if (m_pressed) {
                Color pressBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.0f, 0.0f, 0.0f, 0.08f);
                ctx.fillRect(drawRect, pressBg);
            } else if (m_hovered) {
                Color hovBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.07f) : Color(0.0f, 0.0f, 0.0f, 0.05f);
                ctx.fillRect(drawRect, hovBg);
            }
        }

        if (m_icon != IconType::None) {
            const float iconSz = 10.5f;
            Rect iconRect(drawRect.left + (drawRect.width() - iconSz) * 0.5f,
                          drawRect.top + (drawRect.height() - iconSz) * 0.5f,
                          drawRect.left + (drawRect.width() + iconSz) * 0.5f,
                          drawRect.top + (drawRect.height() + iconSz) * 0.5f);
            VectorIconRenderer::drawIcon(ctx, m_icon, iconRect, textColor, 1.2f);
        }
        return;
    }

    // 2. 侧边栏导航条目模式 (左对齐 + 激活态指示条 + primaryDim 微晶背景)
    if (m_isNavItem) {
        const float radius = 7.0f;
        Color textColor = p.textSecondary;

        if (m_isActive) {
            Color bg = p.primaryDim;
            if (bg.a <= 0.01f) {
                bg = p.primary.withAlpha(dark ? 0.16f : 0.12f);
            }
            ctx.fillRoundedRect(drawRect, radius, bg);
            Color border = p.primary.withAlpha(dark ? 0.25f : 0.20f);
            ctx.drawRoundedRect(drawRect, radius, border, 1.0f);

            // 左侧 3.5px 高精度圆角激活指示条
            const float barH = 18.0f;
            const float barW = 3.5f;
            Rect barRect(drawRect.left + 2.5f, drawRect.top + (drawRect.height() - barH) * 0.5f,
                         drawRect.left + 2.5f + barW, drawRect.top + (drawRect.height() + barH) * 0.5f);
            ctx.fillRoundedRect(barRect, barW * 0.5f, p.primary);

            textColor = p.primary;
        } else if (m_hoverAlpha.get() > 0.01f) {
            Color hovBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.06f * m_hoverAlpha.get())
                               : Color(0.0f, 0.0f, 0.0f, 0.04f * m_hoverAlpha.get());
            ctx.fillRoundedRect(drawRect, radius, hovBg);
            textColor = m_hovered ? p.textPrimary : p.textSecondary;
        }

        const float padLeft = 14.0f;
        const float iconSize = 16.0f;
        const float gap = 10.0f;

        if (m_icon != IconType::None) {
            Rect iconRect(drawRect.left + padLeft, drawRect.top + (drawRect.height() - iconSize) * 0.5f,
                          drawRect.left + padLeft + iconSize, drawRect.top + (drawRect.height() + iconSize) * 0.5f);
            VectorIconRenderer::drawIcon(ctx, m_icon, iconRect, textColor, m_isActive ? 2.0f : 1.7f);
        }

        if (!m_text.empty()) {
            Rect textRect(drawRect.left + padLeft + iconSize + gap, drawRect.top,
                          drawRect.right - 8.0f, drawRect.bottom);
            ctx.drawText(m_text, textRect, textColor, FontToken::Base,
                         m_isActive ? FontWeight::SemiBold : FontWeight::Medium,
                         FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
        }
        return;
    }

    const float radius = drawRect.height() * 0.5f;
    Color textColor = p.textPrimary;

    switch (m_variant) {
    case ButtonVariant::Primary: {
        const float r = (m_size == ButtonSize::Sm) ? 6.0f : 8.0f;
        // 双层下沉投影与主色微光
        ctx.drawRoundedRect(
            Rect(drawRect.left, drawRect.top + 1.0f, drawRect.right, drawRect.bottom + 1.0f),
            r, p.primary.withAlpha(dark ? 0.35f : 0.22f), 1.0f
        );
        ctx.drawRoundedRect(
            Rect(drawRect.left, drawRect.top + 2.5f, drawRect.right, drawRect.bottom + 2.5f),
            r + 0.5f, p.primary.withAlpha(dark ? 0.20f : 0.12f), 1.0f
        );

        Color bg = p.primary;
        if (m_hovered) {
            bg = p.primaryHover;
        }
        ctx.fillRoundedRect(drawRect, r, bg);

        // 顶部微晶高光反光线
        ctx.drawLine(
            Point(drawRect.left + r * 0.6f, drawRect.top + 0.8f),
            Point(drawRect.right - r * 0.6f, drawRect.top + 0.8f),
            Color(1.0f, 1.0f, 1.0f, 0.38f), 1.0f
        );

        // 边框微光
        Color border = dark ? Color(1.0f, 1.0f, 1.0f, 0.20f) : Color(0.0f, 0.0f, 0.0f, 0.12f);
        ctx.drawRoundedRect(drawRect, r, border, 1.0f);

        textColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
        break;
    }
    case ButtonVariant::Confirm: {
        const float r = (m_size == ButtonSize::Sm) ? 6.0f : 8.0f;
        Color bg = m_hovered ? Color(0.06f, 0.78f, 0.48f, 1.0f) : Color(0.06f, 0.72f, 0.44f, 0.95f);
        ctx.fillRoundedRect(drawRect, r, bg);
        ctx.drawLine(
            Point(drawRect.left + r * 0.6f, drawRect.top + 0.8f),
            Point(drawRect.right - r * 0.6f, drawRect.top + 0.8f),
            Color(1.0f, 1.0f, 1.0f, 0.35f), 1.0f
        );
        textColor = Color(1.0f, 1.0f, 1.0f, 1.0f);
        break;
    }
    case ButtonVariant::Danger: {
        const float r = (m_size == ButtonSize::Sm) ? 6.0f : 8.0f;
        ctx.drawRoundedRect(
            Rect(drawRect.left, drawRect.top + 1.0f, drawRect.right, drawRect.bottom + 1.0f),
            r, Color(0.94f, 0.26f, 0.26f, dark ? 0.20f : 0.08f), 1.0f
        );
        Color bg = dark
            ? Color(0.94f, 0.26f, 0.26f, m_hovered ? 0.28f : 0.14f)
            : Color(0.94f, 0.26f, 0.26f, m_hovered ? 0.16f : 0.08f);
        ctx.fillRoundedRect(drawRect, r, bg);

        Color border = Color(0.94f, 0.26f, 0.26f, m_hovered ? 0.60f : 0.30f);
        ctx.drawRoundedRect(drawRect, r, border, 1.0f);

        textColor = p.danger;
        break;
    }
    case ButtonVariant::Ghost: {
        const float r = (m_size == ButtonSize::Sm) ? 6.0f : radius;
        if (m_hoverAlpha.get() > 0.01f) {
            Color hoverBg = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f * m_hoverAlpha.get())
                                 : Color(0.0f, 0.0f, 0.0f, 0.06f * m_hoverAlpha.get());
            ctx.fillRoundedRect(drawRect, r, hoverBg);
        }
        textColor = m_hovered ? p.textPrimary : p.textSecondary;
        break;
    }
    case ButtonVariant::Secondary:
    default: {
        // 次级微晶触感按钮 (细腻垂直渐变 + 双层物理微阴影 + 顶部微晶高光)
        const float r = (m_size == ButtonSize::Sm) ? 6.0f : 8.0f;

        // 1. 双层物理触感下沉微阴影 (Dual-layer downward tactile shadow)
        ctx.drawRoundedRect(
            Rect(drawRect.left, drawRect.top + 1.0f, drawRect.right, drawRect.bottom + 1.0f),
            r, Color(0.0f, 0.0f, 0.0f, dark ? 0.26f : 0.05f), 1.0f
        );
        ctx.drawRoundedRect(
            Rect(drawRect.left, drawRect.top + 2.2f, drawRect.right, drawRect.bottom + 2.2f),
            r + 0.5f, Color(0.0f, 0.0f, 0.0f, dark ? 0.14f : 0.025f), 1.0f
        );

        // 2. 实体底板 (微晶渐变)
        auto rt = ctx.target();
        bool gradientFilled = false;
        if (rt) {
            D2D1_GRADIENT_STOP stops[2];
            if (dark) {
                stops[0].position = 0.0f;
                stops[0].color = m_hovered ? D2D1::ColorF(44.0f / 255.0f, 42.0f / 255.0f, 60.0f / 255.0f, 0.96f)
                                           : D2D1::ColorF(34.0f / 255.0f, 32.0f / 255.0f, 48.0f / 255.0f, 0.94f);
                stops[1].position = 1.0f;
                stops[1].color = m_hovered ? D2D1::ColorF(34.0f / 255.0f, 32.0f / 255.0f, 48.0f / 255.0f, 0.92f)
                                           : D2D1::ColorF(24.0f / 255.0f, 22.0f / 255.0f, 36.0f / 255.0f, 0.90f);
            } else {
                stops[0].position = 0.0f;
                stops[0].color = m_hovered ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
                                           : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
                stops[1].position = 1.0f;
                stops[1].color = m_hovered ? D2D1::ColorF(242.0f / 255.0f, 246.0f / 255.0f, 252.0f / 255.0f, 0.98f)
                                           : D2D1::ColorF(248.0f / 255.0f, 250.0f / 255.0f, 253.0f / 255.0f, 0.96f);
            }

            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopColl;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopColl.GetAddressOf())) && stopColl) {
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(drawRect.left, drawRect.top),
                    D2D1::Point2F(drawRect.left, drawRect.bottom)
                );
                Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> gradBrush;
                if (SUCCEEDED(rt->CreateLinearGradientBrush(gradProps, stopColl.Get(), gradBrush.GetAddressOf())) && gradBrush) {
                    rt->FillRoundedRectangle(drawRect.toRoundedD2D(r), gradBrush.Get());
                    gradientFilled = true;
                }
            }
        }

        if (!gradientFilled) {
            Color bg = dark
                ? Color(1.0f, 1.0f, 1.0f, 0.07f + 0.05f * m_hoverAlpha.get())
                : (m_hovered ? Color::fromHex(0xf8fafc, 1.0f) : Color::fromHex(0xffffff, 0.96f));
            ctx.fillRoundedRect(drawRect, r, bg);
        }

        // 3. 顶部 1px 极细微晶反光线 (Specular Sheen)
        Color sheen = dark
            ? Color(1.0f, 1.0f, 1.0f, 0.16f)
            : Color(1.0f, 1.0f, 1.0f, 0.95f);
        ctx.drawLine(
            Point(drawRect.left + r * 0.5f, drawRect.top + 0.8f),
            Point(drawRect.right - r * 0.5f, drawRect.top + 0.8f),
            sheen, 1.0f
        );

        // 4. 外缘细腻描边 (悬浮时微光染色)
        Color border = dark
            ? (m_hovered ? p.primary.withAlpha(0.45f) : Color(1.0f, 1.0f, 1.0f, 0.12f))
            : (m_hovered ? p.primary.withAlpha(0.45f) : Color(0.06f, 0.09f, 0.16f, 0.10f));
        ctx.drawRoundedRect(drawRect, r, border, 1.0f);

        textColor = m_hovered ? (dark ? Color(1.0f, 1.0f, 1.0f, 1.0f) : p.primary) : p.textPrimary;
        break;
    }
    }

    // 绘制内容 (图标 + 文本居中对齐)
    float contentWidth = 0.0f;
    const float iconSize = (m_size == ButtonSize::Sm ? 14.0f : 16.0f);
    FontToken font = (m_size == ButtonSize::Sm ? FontToken::Sm : FontToken::Base);

    if (m_icon != IconType::None) {
        contentWidth += iconSize;
        if (!m_text.empty()) contentWidth += 6.0f;
    }
    Size textSize(0.0f, 0.0f);
    if (!m_text.empty()) {
        textSize = ctx.measureText(m_text, font, FontWeight::SemiBold);
        contentWidth += textSize.width;
    }

    float currentX = drawRect.left + (drawRect.width() - contentWidth) * 0.5f;

    if (m_icon != IconType::None) {
        Rect iconRect(currentX, drawRect.top + (drawRect.height() - iconSize) * 0.5f,
                      currentX + iconSize, drawRect.top + (drawRect.height() + iconSize) * 0.5f);
        VectorIconRenderer::drawIcon(ctx, m_icon, iconRect, textColor, 1.8f);
        currentX += iconSize + 6.0f;
    }

    if (!m_text.empty()) {
        Rect textRect(currentX, drawRect.top, drawRect.right, drawRect.bottom);
        ctx.drawText(m_text, textRect, textColor, font, FontWeight::SemiBold,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    }
}

} // namespace tools3000::ui::native
