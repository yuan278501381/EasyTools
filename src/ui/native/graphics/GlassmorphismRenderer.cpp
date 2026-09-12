#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <algorithm>

namespace tools3000::ui::native {

using namespace Microsoft::WRL;

void GlassmorphismRenderer::drawElevationShadow(
    UIRenderContext& ctx,
    const Rect& rect,
    float radius,
    int elevation,
    bool isDark
) {
    auto rt = ctx.target();
    if (!rt || elevation <= 0) return;

    struct ShadowStep {
        float dx;
        float dy;
        float expand;
        float alpha;
    };

    // 纯物理自然向下投射的多层微晶柔和投影（消除卡片顶部和两侧脏光圈台阶）
    static const ShadowStep lowSteps[] = {
        { 0.0f, 1.2f, 0.0f, 0.035f },
        { 0.0f, 3.0f, 0.5f, 0.024f },
        { 0.0f, 6.0f, 1.0f, 0.014f },
    };

    static const ShadowStep highSteps[] = {
        { 0.0f, 1.5f, 0.0f, 0.045f },
        { 0.0f, 4.0f, 0.8f, 0.032f },
        { 0.0f, 8.0f, 1.8f, 0.020f },
        { 0.0f, 15.0f, 3.0f, 0.010f },
    };

    const ShadowStep* steps = (elevation >= 2) ? highSteps : lowSteps;
    const size_t count = (elevation >= 2) ? (sizeof(highSteps) / sizeof(highSteps[0])) : (sizeof(lowSteps) / sizeof(lowSteps[0]));
    const float shadowMul = isDark ? 2.2f : 1.0f;

    for (size_t i = 0; i < count; ++i) {
        const float exp = steps[i].expand;
        const float dy = steps[i].dy;
        // 关键物理避让：阴影顶边缘严格贴合或微低于实体顶边缘，彻底杜绝向上渗漏形成脏灰边框
        float sTop = rect.top + dy * 0.4f;
        auto sRect = D2D1::RectF(
            rect.left - exp + steps[i].dx,
            sTop,
            rect.right + exp + steps[i].dx,
            rect.bottom + dy + exp
        );
        Color c(0.0f, 0.0f, 0.0f, steps[i].alpha * shadowMul);
        auto brush = ctx.getSolidBrush(c);
        if (brush) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(sRect, radius + exp, radius + exp), brush);
        }
    }
}

void GlassmorphismRenderer::drawGlassPanel(
    UIRenderContext& ctx,
    const Rect& rect,
    float radius,
    bool isDark,
    int elevation
) {
    auto rt = ctx.target();
    if (!rt) return;

    // 1. 物理自然下沉阴影
    if (elevation > 0) {
        drawElevationShadow(ctx, rect, radius, elevation, isDark);
    }

    auto rr = rect.toRoundedD2D(radius);

    // 优先铺底 100% 不透明纯色底板，彻底杜绝任何图层混合穿透与背景漏光
    Color baseBg = isDark ? Color::fromHex(0x12151d, 1.0f) : Color::fromHex(0xf8fafc, 1.0f);
    if (auto baseBrush = ctx.getSolidBrush(baseBg)) {
        rt->FillRoundedRectangle(rr, baseBrush);
    }

    // 2. 亚克力微晶双层渐变主体
    ComPtr<ID2D1GradientStopCollection> stopCollection;
    D2D1_GRADIENT_STOP stops[2];
    if (isDark) {
        stops[0].position = 0.0f;
        stops[0].color = D2D1::ColorF(28.0f / 255.0f, 33.0f / 255.0f, 45.0f / 255.0f, 1.0f);
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(18.0f / 255.0f, 22.0f / 255.0f, 32.0f / 255.0f, 1.0f);
    } else {
        stops[0].position = 0.0f;
        stops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(246.0f / 255.0f, 248.0f / 255.0f, 252.0f / 255.0f, 1.0f);
    }

    bool gradientFilled = false;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopCollection.GetAddressOf())) && stopCollection) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(rect.left, rect.top),
            D2D1::Point2F(rect.left, rect.bottom)
        );
        ComPtr<ID2D1LinearGradientBrush> gradBrush;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(gradProps, stopCollection.Get(), gradBrush.GetAddressOf())) && gradBrush) {
            rt->FillRoundedRectangle(rr, gradBrush.Get());
            gradientFilled = true;
        }
    }

    if (!gradientFilled) {
        Color fallback = isDark ? Color::fromHex(0x191724, 0.96f) : Color::fromHex(0xffffff, 0.98f);
        ctx.fillRoundedRect(rect, radius, fallback);
    }

    // 3. 顶部 1px 极细微晶高光内反射线条 (Specular Edge Sheen)
    const float sheenInset = std::max(radius * 0.6f, 6.0f);
    Color sheenColor = isDark ? Color(1.0f, 1.0f, 1.0f, 0.18f) : Color(1.0f, 1.0f, 1.0f, 0.95f);
    ctx.drawLine(
        Point(rect.left + sheenInset, rect.top + 0.8f),
        Point(rect.right - sheenInset, rect.top + 0.8f),
        sheenColor, 1.0f
    );

    // 4. 外侧微透明细腻描边
    Color borderColor = isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.08f);
    ctx.drawRoundedRect(rect, radius, borderColor, 1.0f);
}

void GlassmorphismRenderer::drawSidebar(
    UIRenderContext& ctx,
    const Rect& bounds,
    bool isDark
) {
    auto rt = ctx.target();
    if (!rt) return;
    if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) return;

    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    // 1. 亚克力微晶双层垂直渐变底衬 (Ceramic / Obsidian Glass Gradient)
    ComPtr<ID2D1GradientStopCollection> stopCollection;
    D2D1_GRADIENT_STOP stops[2];
    if (isDark) {
        stops[0].position = 0.0f;
        stops[0].color = p.sidebarBg.toD2D(); // 0x191724 (Obsidian Glass)
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(18.0f / 255.0f, 17.0f / 255.0f, 26.0f / 255.0f, 0.94f); // 0x12111a
    } else {
        stops[0].position = 0.0f;
        stops[0].color = p.sidebarBg.toD2D(); // 纯净陶瓷白 #fcfdfd
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(238.0f / 255.0f, 242.0f / 255.0f, 248.0f / 255.0f, 0.94f); // 柔和微晶底衬 #eef2f8
    }

    bool gradientFilled = false;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopCollection.GetAddressOf())) && stopCollection) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(bounds.left, bounds.top),
            D2D1::Point2F(bounds.left, bounds.bottom)
        );
        ComPtr<ID2D1LinearGradientBrush> gradBrush;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(gradProps, stopCollection.Get(), gradBrush.GetAddressOf())) && gradBrush) {
            rt->FillRectangle(bounds.toD2D(), gradBrush.Get());
            gradientFilled = true;
        }
    }

    if (!gradientFilled) {
        ctx.fillRect(bounds, p.sidebarBg);
    }

    // 2. 右侧微晶双层微刻痕反光线 (Specular Edge Sheen + Outer Border)
    Color rightSheen = isDark ? Color(1.0f, 1.0f, 1.0f, 0.03f) : Color(1.0f, 1.0f, 1.0f, 0.85f);
    ctx.drawLine(Point(bounds.right - 2.0f, bounds.top), Point(bounds.right - 2.0f, bounds.bottom), rightSheen, 1.0f);
    ctx.drawLine(Point(bounds.right - 1.0f, bounds.top), Point(bounds.right - 1.0f, bounds.bottom), p.sidebarBorder, 1.0f);
}

void GlassmorphismRenderer::drawWindowBackground(
    UIRenderContext& ctx,
    const Rect& bounds,
    bool isDark,
    const Color& primaryAccent
) {
    auto rt = ctx.target();
    if (!rt) return;

    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    // 1. 基础全屏底色清除（必须确保完全不透明 1.0f，杜绝 DWM 穿透导致白屏/全透明）
    Color clearColor = p.bgBase.withAlpha(1.0f);
    rt->Clear(clearColor.toD2D());

    // 2. 左上方主题色环境光晕 (Ambient Radial Glow)
    {
        D2D1_GRADIENT_STOP stops[2];
        stops[0].position = 0.0f;
        stops[0].color = primaryAccent.withAlpha(isDark ? 0.07f : 0.04f).toD2D();
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(0, 0, 0, 0.0f);

        ComPtr<ID2D1GradientStopCollection> stopColl;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopColl.GetAddressOf())) && stopColl) {
            float radX = std::max(300.0f, bounds.width() * 0.55f);
            float radY = std::max(220.0f, bounds.height() * 0.45f);
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rgp = D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(bounds.left, bounds.top),
                D2D1::Point2F(0, 0),
                radX, radY
            );
            ComPtr<ID2D1RadialGradientBrush> radialBrush;
            if (SUCCEEDED(rt->CreateRadialGradientBrush(rgp, stopColl.Get(), radialBrush.GetAddressOf())) && radialBrush) {
                rt->FillRectangle(bounds.toD2D(), radialBrush.Get());
            }
        }
    }

    // 3. 右下方冷蓝微晶次级环境光
    {
        D2D1_GRADIENT_STOP stops[2];
        stops[0].position = 0.0f;
        stops[0].color = p.info.withAlpha(isDark ? 0.05f : 0.025f).toD2D();
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(0, 0, 0, 0.0f);

        ComPtr<ID2D1GradientStopCollection> stopColl;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopColl.GetAddressOf())) && stopColl) {
            float radX = std::max(350.0f, bounds.width() * 0.5f);
            float radY = std::max(250.0f, bounds.height() * 0.4f);
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rgp = D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(bounds.right, bounds.bottom),
                D2D1::Point2F(0, 0),
                radX, radY
            );
            ComPtr<ID2D1RadialGradientBrush> radialBrush;
            if (SUCCEEDED(rt->CreateRadialGradientBrush(rgp, stopColl.Get(), radialBrush.GetAddressOf())) && radialBrush) {
                rt->FillRectangle(bounds.toD2D(), radialBrush.Get());
            }
        }
    }
}

void GlassmorphismRenderer::drawCard(
    UIRenderContext& ctx,
    const Rect& rect,
    float radius,
    bool isHovered,
    bool isDark
) {
    auto rt = ctx.target();
    if (!rt) return;

    // 1. 物理环境光与关键光微阴影 (Elevation 1~2)
    drawElevationShadow(ctx, rect, radius, isHovered ? 2 : 1, isDark);

    auto rr = rect.toRoundedD2D(radius);

    // 2. 微晶双层渐变底板
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    ComPtr<ID2D1GradientStopCollection> stopCollection;
    D2D1_GRADIENT_STOP stops[2];
    if (isDark) {
        stops[0].position = 0.0f;
        stops[0].color = isHovered ? D2D1::ColorF(36.0f / 255.0f, 33.0f / 255.0f, 52.0f / 255.0f, 0.96f)
                                   : D2D1::ColorF(28.0f / 255.0f, 26.0f / 255.0f, 42.0f / 255.0f, 0.94f);
        stops[1].position = 1.0f;
        stops[1].color = isHovered ? D2D1::ColorF(28.0f / 255.0f, 25.0f / 255.0f, 40.0f / 255.0f, 0.92f)
                                   : D2D1::ColorF(22.0f / 255.0f, 20.0f / 255.0f, 34.0f / 255.0f, 0.90f);
    } else {
        stops[0].position = 0.0f;
        stops[0].color = isHovered ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
                                   : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        stops[1].position = 1.0f;
        stops[1].color = isHovered ? D2D1::ColorF(248.0f / 255.0f, 250.0f / 255.0f, 253.0f / 255.0f, 0.98f)
                                   : D2D1::ColorF(252.0f / 255.0f, 253.0f / 255.0f, 255.0f / 255.0f, 0.97f);
    }

    bool gradientFilled = false;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopCollection.GetAddressOf())) && stopCollection) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(rect.left, rect.top),
            D2D1::Point2F(rect.left, rect.bottom)
        );
        ComPtr<ID2D1LinearGradientBrush> gradBrush;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(gradProps, stopCollection.Get(), gradBrush.GetAddressOf())) && gradBrush) {
            rt->FillRoundedRectangle(rr, gradBrush.Get());
            gradientFilled = true;
        }
    }

    if (!gradientFilled) {
        Color cardBg = isDark
            ? (isHovered ? Color::fromHex(0x242135, 0.96f) : Color::fromHex(0x1c1a2a, 0.94f))
            : (isHovered ? Color::fromHex(0xffffff, 1.0f) : Color::fromHex(0xfcfafe, 0.98f));
        ctx.fillRoundedRect(rect, radius, cardBg);
    }

    // 3. 顶部 1px 极细微晶高光内反射线条 (Specular Edge Sheen)
    const float sheenInset = std::max(radius * 0.6f, 6.0f);
    Color sheenColor = isDark
        ? Color(1.0f, 1.0f, 1.0f, isHovered ? 0.18f : 0.10f)
        : Color(1.0f, 1.0f, 1.0f, isHovered ? 0.98f : 0.92f);
    ctx.drawLine(
        Point(rect.left + sheenInset, rect.top + 0.8f),
        Point(rect.right - sheenInset, rect.top + 0.8f),
        sheenColor, 1.0f
    );

    // 4. 外侧边框 (悬浮时微光染色)
    Color border = isDark
        ? (isHovered ? p.primary.withAlpha(0.35f) : Color(1.0f, 1.0f, 1.0f, 0.08f))
        : (isHovered ? p.primary.withAlpha(0.35f) : Color(0.06f, 0.09f, 0.16f, 0.07f));
    ctx.drawRoundedRect(rect, radius, border, 1.0f);
}

void GlassmorphismRenderer::drawButtonPill(
    UIRenderContext& ctx,
    const Rect& rect,
    float radius,
    bool isHovered,
    bool isPressed,
    bool isDanger,
    bool isConfirm,
    const Color& themeAccent,
    float alpha,
    bool isDark
) {
    if (alpha <= 0.01f) return;

    if (isConfirm) {
        Color confirmBg = isHovered ? Color(0.06f, 0.78f, 0.48f, alpha) : Color(0.06f, 0.72f, 0.44f, 0.92f * alpha);
        ctx.fillRoundedRect(rect, radius, confirmBg);
        return;
    }

    if (isDanger) {
        float bgAlpha = (isHovered ? (isDark ? 0.32f : 0.20f) : (isDark ? 0.18f : 0.10f)) * alpha;
        Color dangerBg(0.94f, 0.26f, 0.26f, bgAlpha);
        ctx.fillRoundedRect(rect, radius, dangerBg);
        Color border(0.94f, 0.26f, 0.26f, (isHovered ? 0.8f : 0.4f) * alpha);
        ctx.drawRoundedRect(rect, radius, border, 1.0f);
        return;
    }

    if (themeAccent.a > 0.01f) {
        // 主题色激活胶囊
        float bgAlpha = (isDark ? (isHovered ? 0.32f : 0.22f) : (isHovered ? 0.22f : 0.14f)) * alpha;
        Color bg = themeAccent.withAlpha(bgAlpha);
        Color border = themeAccent.withAlpha((isHovered ? 0.95f : 0.75f) * alpha);
        ctx.fillRoundedRect(rect, radius, bg);
        ctx.drawRoundedRect(rect, radius, border, 1.0f);
        return;
    }

    // 默认胶囊微交互
    if (isPressed) {
        Color pressedBg = isDark ? Color(1.0f, 1.0f, 1.0f, 0.18f * alpha) : Color(0.0f, 0.0f, 0.0f, 0.12f * alpha);
        ctx.fillRoundedRect(rect, radius, pressedBg);
    } else if (isHovered) {
        Color hoverBg = isDark ? Color(1.0f, 1.0f, 1.0f, 0.10f * alpha) : Color(0.0f, 0.0f, 0.0f, 0.06f * alpha);
        ctx.fillRoundedRect(rect, radius, hoverBg);
    }
}

void GlassmorphismRenderer::drawSeparator(
    UIRenderContext& ctx,
    Point p1,
    Point p2,
    bool isDark
) {
    (void)isDark;
    const auto& p = UITheme::instance().palette();
    Color shadow = p.separatorShadow;
    Color highlight = p.separatorHighlight;

    ctx.drawLine(p1, p2, shadow, 1.0f);

    // 水平或垂直微移 1px 呈现物理刻痕高光
    if (std::abs(p1.y - p2.y) < 0.01f) {
        ctx.drawLine(Point(p1.x, p1.y + 1.0f), Point(p2.x, p2.y + 1.0f), highlight, 1.0f);
    } else {
        ctx.drawLine(Point(p1.x + 1.0f, p1.y), Point(p2.x + 1.0f, p2.y), highlight, 1.0f);
    }
}

void GlassmorphismRenderer::drawBadge(
    UIRenderContext& ctx,
    const Rect& rect,
    const std::wstring& text,
    BadgeVariant variant,
    bool isDark
) {
    Color bg, border, textColor;
    switch (variant) {
    case BadgeVariant::Primary: {
        const auto& p = UITheme::instance().palette();
        bg = p.primaryDim;
        border = p.primary.withAlpha(isDark ? 0.32f : 0.22f);
        textColor = p.primary;
        break;
    }
    case BadgeVariant::Success:
        bg = isDark ? Color(0.06f, 0.72f, 0.44f, 0.18f) : Color(0.06f, 0.72f, 0.44f, 0.10f);
        border = Color(0.06f, 0.72f, 0.44f, isDark ? 0.35f : 0.25f);
        textColor = isDark ? Color(0.18f, 0.88f, 0.56f, 1.0f) : Color(0.04f, 0.62f, 0.38f, 1.0f);
        break;
    case BadgeVariant::Warning:
        bg = isDark ? Color(0.98f, 0.75f, 0.14f, 0.18f) : Color(0.98f, 0.75f, 0.14f, 0.10f);
        border = Color(0.98f, 0.75f, 0.14f, isDark ? 0.35f : 0.25f);
        textColor = isDark ? Color(1.0f, 0.82f, 0.24f, 1.0f) : Color(0.85f, 0.55f, 0.06f, 1.0f);
        break;
    case BadgeVariant::Danger:
        bg = isDark ? Color(0.97f, 0.44f, 0.44f, 0.18f) : Color(0.97f, 0.44f, 0.44f, 0.10f);
        border = Color(0.97f, 0.44f, 0.44f, isDark ? 0.35f : 0.25f);
        textColor = isDark ? Color(1.0f, 0.52f, 0.52f, 1.0f) : Color(0.88f, 0.24f, 0.24f, 1.0f);
        break;
    case BadgeVariant::Muted:
    default:
        bg = isDark ? Color(1.0f, 1.0f, 1.0f, 0.06f) : Color(0.0f, 0.0f, 0.0f, 0.04f);
        border = isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.0f, 0.0f, 0.0f, 0.08f);
        textColor = isDark ? Color::fromHex(0xc4c4d6) : Color::fromHex(0x64748b);
        break;
    }

    const float radius = rect.height() * 0.5f;
    ctx.fillRoundedRect(rect, radius, bg);
    ctx.drawRoundedRect(rect, radius, border, 1.0f);

    // 顶部微晶极细高光内反射线
    ctx.drawLine(
        Point(rect.left + radius * 0.6f, rect.top + 0.8f),
        Point(rect.right - radius * 0.6f, rect.top + 0.8f),
        isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(1.0f, 1.0f, 1.0f, 0.80f),
        1.0f
    );

    // 微晶状态指示发光圆点 (Status Indicator Dot)
    const float dotRadius = 2.4f;
    const float dotX = rect.left + 10.0f;
    const float dotY = rect.center().y;
    ctx.fillCircle(Point(dotX, dotY), dotRadius + 1.2f, textColor.withAlpha(0.22f));
    ctx.fillCircle(Point(dotX, dotY), dotRadius, textColor);

    // 文本紧随状态圆点居中微调排版
    Rect textRect(rect.left + 16.0f, rect.top, rect.right - 8.0f, rect.bottom);
    ctx.drawText(
        text,
        textRect,
        textColor,
        FontToken::Xs,
        FontWeight::SemiBold,
        FontFamilyType::Sans,
        TextAlignmentH::Center,
        TextAlignmentV::Center
    );
}

void GlassmorphismRenderer::drawCodeBadge(
    UIRenderContext& ctx,
    const Rect& rect,
    const std::wstring& text,
    bool isDark
) {
    Color bg = isDark ? Color::fromHex(0x0f0f19, 0.75f) : Color::fromHex(0xf1f5f9, 0.85f);
    Color border = isDark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.10f);
    Color textColor = isDark ? Color::fromHex(0xa78bfa) : Color::fromHex(0x7c3aed);

    const float radius = 5.0f;
    ctx.fillRoundedRect(rect, radius, bg);
    ctx.drawRoundedRect(rect, radius, border, 1.0f);

    ctx.drawText(
        text,
        rect.inset(Thickness(6.0f, 1.0f, 6.0f, 1.0f)),
        textColor,
        FontToken::Xs,
        FontWeight::Medium,
        FontFamilyType::Mono,
        TextAlignmentH::Center,
        TextAlignmentV::Center
    );
}

} // namespace tools3000::ui::native
