#include "capture/FloatingGlassBar.h"
#include "core/utils/ThemeUtils.h"
#include <algorithm>

namespace tools3000::capture {

using namespace Microsoft::WRL;

void FloatingGlassBar::drawGlassPanel(
    ID2D1RenderTarget* rt,
    const D2D1_RECT_F& rect,
    float radius,
    bool isDark) {
    if (!rt) return;

    // 1. 双层物理分层高斯微阴影 (Dual-Layer Ambient & Key Shadows)
    // ── 层 1: Key Shadow (近地紧致物理轮廓层: offsetY=1.5px, blur=3px)
    struct ShadowStep {
        float dx;
        float dy;
        float expand;
        float alpha;
    };

    static const ShadowStep keySteps[] = {
        { 0.0f, 0.8f, 0.6f, 0.030f },
        { 0.0f, 1.4f, 1.4f, 0.025f },
        { 0.0f, 2.0f, 2.4f, 0.015f },
    };

    // ── 层 2: Ambient Shadow (环境光柔和悬浮层: offsetY=6.0px, blur=18px)
    static const ShadowStep ambientSteps[] = {
        { 0.0f, 3.0f, 3.5f,  0.016f },
        { 0.0f, 4.5f, 6.5f,  0.013f },
        { 0.0f, 6.0f, 10.5f, 0.010f },
        { 0.0f, 7.5f, 15.0f, 0.007f },
    };

    const float shadowMul = isDark ? 2.4f : 1.0f;

    auto renderShadow = [&](const ShadowStep* steps, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, steps[i].alpha * shadowMul),
                b.GetAddressOf());
            if (b) {
                float exp = steps[i].expand;
                auto sRect = D2D1::RectF(
                    rect.left - exp + steps[i].dx,
                    rect.top - exp + steps[i].dy,
                    rect.right + exp + steps[i].dx,
                    rect.bottom + exp + steps[i].dy
                );
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(sRect, radius + exp, radius + exp),
                    b.Get());
            }
        }
    };

    renderShadow(ambientSteps, sizeof(ambientSteps) / sizeof(ambientSteps[0]));
    renderShadow(keySteps, sizeof(keySteps) / sizeof(keySteps[0]));

    auto rr = D2D1::RoundedRect(rect, radius, radius);

    // 2. 双层微晶亚克力悬浮质感主体 (Refined Glassmorphism Body)
    // 浅色模式: 纯净陶瓷微晶 (rgba(255,255,255,0.96) -> rgba(248,250,252,0.92))
    // 深色模式: 黑曜石磨砂半透明质感 (rgba(28,33,45,0.92) -> rgba(20,24,33,0.88))
    ComPtr<ID2D1GradientStopCollection> stopCollection;
    D2D1_GRADIENT_STOP stops[2];
    if (isDark) {
        stops[0].position = 0.0f;
        stops[0].color = D2D1::ColorF(28.0f / 255.0f, 33.0f / 255.0f, 45.0f / 255.0f, 0.92f);
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(20.0f / 255.0f, 24.0f / 255.0f, 33.0f / 255.0f, 0.88f);
    } else {
        stops[0].position = 0.0f;
        stops[0].color = D2D1::ColorF(255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 0.96f);
        stops[1].position = 1.0f;
        stops[1].color = D2D1::ColorF(248.0f / 255.0f, 250.0f / 255.0f, 252.0f / 255.0f, 0.92f);
    }

    bool filledBody = false;
    if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stopCollection.GetAddressOf())) && stopCollection) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(rect.left, rect.top),
            D2D1::Point2F(rect.left, rect.bottom)
        );
        ComPtr<ID2D1LinearGradientBrush> gradBrush;
        if (SUCCEEDED(rt->CreateLinearGradientBrush(gradProps, stopCollection.Get(), gradBrush.GetAddressOf())) && gradBrush) {
            rt->FillRoundedRectangle(rr, gradBrush.Get());
            filledBody = true;
        }
    }

    if (!filledBody) {
        ComPtr<ID2D1SolidColorBrush> fallbackBrush;
        rt->CreateSolidColorBrush(
            isDark ? D2D1::ColorF(20.0f / 255.0f, 24.0f / 255.0f, 33.0f / 255.0f, 0.88f)
                   : D2D1::ColorF(250.0f / 255.0f, 251.0f / 255.0f, 253.0f / 255.0f, 0.94f),
            fallbackBrush.GetAddressOf());
        if (fallbackBrush) rt->FillRoundedRectangle(rr, fallbackBrush.Get());
    }

    // 3. 顶部 1px 极细微晶内发光与高光棱线 (Specular Edge Sheen)
    ComPtr<ID2D1SolidColorBrush> sheen;
    rt->CreateSolidColorBrush(
        isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f)
               : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.75f),
        sheen.GetAddressOf());
    if (sheen) {
        float sheenInset = std::max(radius * 0.7f, 4.0f);
        rt->DrawLine(
            D2D1::Point2F(rect.left + sheenInset, rect.top + 0.8f),
            D2D1::Point2F(rect.right - sheenInset, rect.top + 0.8f),
            sheen.Get(), 1.0f);
    }

    // 4. 外侧极细微透明描边 (Subtle Outer Border, 告别生硬粗边框)
    ComPtr<ID2D1SolidColorBrush> border;
    rt->CreateSolidColorBrush(
        isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f)
               : D2D1::ColorF(15.0f / 255.0f, 23.0f / 255.0f, 42.0f / 255.0f, 0.08f),
        border.GetAddressOf());
    if (border) {
        rt->DrawRoundedRectangle(rr, border.Get(), 1.0f);
    }
}

void FloatingGlassBar::drawButtonPill(
    ID2D1RenderTarget* rt,
    const D2D1_RECT_F& rect,
    float radius,
    bool isHovered,
    bool isActive,
    bool isDanger,
    bool isConfirm,
    float alpha,
    bool isDark) {
    if (!rt || alpha <= 0.01f) return;

    auto rr = D2D1::RoundedRect(rect, radius, radius);

    if (isConfirm) {
        // 确认 / 完成药丸 (翡翠绿高亮药丸，与 CleanShot X / macOS 一致)
        ComPtr<ID2D1SolidColorBrush> confirmBrush;
        float a = (isHovered ? 1.0f : 0.92f) * alpha;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.72f, 0.44f, a), confirmBrush.GetAddressOf());
        if (confirmBrush) rt->FillRoundedRectangle(rr, confirmBrush.Get());
        return;
    }

    if (isDanger) {
        // 危险 / 取消药丸 (柔和微晶红)
        ComPtr<ID2D1SolidColorBrush> dangerBrush;
        float a = (isHovered ? (isDark ? 0.32f : 0.18f) : (isDark ? 0.18f : 0.08f)) * alpha;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.26f, 0.26f, a), dangerBrush.GetAddressOf());
        if (dangerBrush) rt->FillRoundedRectangle(rr, dangerBrush.Get());
        return;
    }

    auto& cfg = tools3000::core::ConfigManager::instance();
    const std::string accent = cfg.get<std::string>("/general/accentColor", "blue");
    const tools3000::core::AccentColorRGB themeRgb = tools3000::core::getAccentColorRGB(accent);

    if (isActive) {
        // 活跃状态药丸 (系统主题动态强调色微晶胶囊)
        ComPtr<ID2D1SolidColorBrush> activeBg, activeBorder;
        float bgAlpha = (isDark ? 0.25f : 0.15f) * alpha;
        rt->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, bgAlpha), activeBg.GetAddressOf());
        rt->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.85f * alpha), activeBorder.GetAddressOf());
        if (activeBg) rt->FillRoundedRectangle(rr, activeBg.Get());
        if (activeBorder) rt->DrawRoundedRectangle(rr, activeBorder.Get(), 1.0f);
        return;
    }

    if (isHovered) {
        // 悬浮微晶交互药丸
        ComPtr<ID2D1SolidColorBrush> hoverBrush;
        float hAlpha = (isDark ? 0.12f : 0.06f) * alpha;
        rt->CreateSolidColorBrush(isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, hAlpha) : D2D1::ColorF(0.0f, 0.0f, 0.0f, hAlpha), hoverBrush.GetAddressOf());
        if (hoverBrush) rt->FillRoundedRectangle(rr, hoverBrush.Get());
    }
}

void FloatingGlassBar::drawSeparator(
    ID2D1RenderTarget* rt,
    float x,
    float top,
    float bottom,
    float alpha,
    bool isDark) {
    if (!rt || alpha <= 0.01f) return;

    // 1px 双层微刻痕效果 (Shadow incision + Specular highlight)
    ComPtr<ID2D1SolidColorBrush> shadowLine, highlightLine;
    if (isDark) {
        rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f * alpha), shadowLine.GetAddressOf());
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f * alpha), highlightLine.GetAddressOf());
    } else {
        rt->CreateSolidColorBrush(D2D1::ColorF(15.0f / 255.0f, 23.0f / 255.0f, 42.0f / 255.0f, 0.08f * alpha), shadowLine.GetAddressOf());
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.70f * alpha), highlightLine.GetAddressOf());
    }

    if (shadowLine) {
        rt->DrawLine(D2D1::Point2F(x, top), D2D1::Point2F(x, bottom), shadowLine.Get(), 1.0f);
    }
    if (highlightLine) {
        rt->DrawLine(D2D1::Point2F(x + 1.0f, top), D2D1::Point2F(x + 1.0f, bottom), highlightLine.Get(), 1.0f);
    }
}

} // namespace tools3000::capture
