#include "ui/native/graphics/UIRenderContext.h"
#include <algorithm>

namespace tools3000::ui::native {

UIRenderContext::UIRenderContext(ID2D1RenderTarget* rt, IDWriteFactory* dwriteFactory, float dpiScale)
    : m_rt(rt), m_dwriteFactory(dwriteFactory), m_dpiScale(std::max(0.25f, dpiScale)) {
    if (m_rt) {
        m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), m_sharedBrush.GetAddressOf());
        // 开启次像素 ClearType 文本抗锯齿模式
        m_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
}

UIRenderContext::~UIRenderContext() {
    if (m_rt) {
        while (m_clipDepth > 0) {
            m_rt->PopAxisAlignedClip();
            m_clipDepth--;
        }
    }
}

ID2D1SolidColorBrush* UIRenderContext::getSolidBrush(const Color& color) {
    if (!m_rt) return nullptr;
    if (!m_sharedBrush) {
        m_rt->CreateSolidColorBrush(color.toD2D(), m_sharedBrush.GetAddressOf());
    } else {
        m_sharedBrush->SetColor(color.toD2D());
    }
    return m_sharedBrush.Get();
}

void UIRenderContext::fillRect(const Rect& rect, const Color& color) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        m_rt->FillRectangle(rect.toD2D(), brush);
    }
}

void UIRenderContext::fillRoundedRect(const Rect& rect, float radius, const Color& color) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        m_rt->FillRoundedRectangle(rect.toRoundedD2D(radius), brush);
    }
}

void UIRenderContext::drawRoundedRect(const Rect& rect, float radius, const Color& color, float strokeWidth) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        m_rt->DrawRoundedRectangle(rect.toRoundedD2D(radius), brush, strokeWidth);
    }
}

void UIRenderContext::drawLine(Point p1, Point p2, const Color& color, float strokeWidth) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        m_rt->DrawLine(p1.toD2D(), p2.toD2D(), brush, strokeWidth);
    }
}

void UIRenderContext::drawCircle(Point center, float radius, const Color& color, float strokeWidth) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(center.toD2D(), radius, radius);
        m_rt->DrawEllipse(ellipse, brush, strokeWidth);
    }
}

void UIRenderContext::fillCircle(Point center, float radius, const Color& color) {
    if (!m_rt || color.a <= 0.001f) return;
    auto brush = getSolidBrush(color);
    if (brush) {
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(center.toD2D(), radius, radius);
        m_rt->FillEllipse(ellipse, brush);
    }
}

void UIRenderContext::drawBitmap(ID2D1Bitmap* bitmap, const Rect& destRect, float opacity) {
    if (!m_rt || !bitmap || opacity <= 0.001f) return;
    D2D1_RECT_F d = destRect.toD2D();
    m_rt->DrawBitmap(bitmap, d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void UIRenderContext::pushClip(const Rect& rect) {
    if (!m_rt) return;
    m_rt->PushAxisAlignedClip(rect.toD2D(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_clipDepth++;
}

void UIRenderContext::popClip() {
    if (!m_rt || m_clipDepth <= 0) return;
    m_rt->PopAxisAlignedClip();
    m_clipDepth--;
}

void UIRenderContext::pushTransform(const D2D1_MATRIX_3X2_F& transform) {
    if (!m_rt) return;
    D2D1_MATRIX_3X2_F current;
    m_rt->GetTransform(&current);
    m_transformStack.push_back(current);
    m_rt->SetTransform(transform * current);
}

void UIRenderContext::popTransform() {
    if (!m_rt || m_transformStack.empty()) return;
    m_rt->SetTransform(m_transformStack.back());
    m_transformStack.pop_back();
}

float UIRenderContext::getFontSize(FontToken token) const {
    // 基础磅值严格遵循项目单一事实源排版设计令牌 (以 DIP 为基准，D2D 自动按硬件 DPI 缩放)
    float basePt = 14.0f;
    switch (token) {
    case FontToken::Xs:   basePt = 12.0f; break; // 严守 11.8px 渲染底线
    case FontToken::Sm:   basePt = 13.0f; break;
    case FontToken::Base: basePt = 14.0f; break;
    case FontToken::Md:   basePt = 15.5f; break;
    case FontToken::Lg:   basePt = 17.5f; break;
    case FontToken::Xl:   basePt = 21.0f; break;
    case FontToken::Xxl:  basePt = 26.0f; break;
    }
    return basePt;
}

IDWriteTextFormat* UIRenderContext::getOrCreateTextFormat(FontToken token, FontWeight weight, FontFamilyType family) {
    if (!m_dwriteFactory) return nullptr;

    const uint32_t key = (static_cast<uint32_t>(token) << 16) |
                         (static_cast<uint32_t>(weight) << 8) |
                         static_cast<uint32_t>(family);

    auto it = m_textFormatCache.find(key);
    if (it != m_textFormatCache.end()) {
        return it->second.Get();
    }

    const float fontSize = getFontSize(token);
    const DWRITE_FONT_WEIGHT dwriteWeight = static_cast<DWRITE_FONT_WEIGHT>(weight);

    // 字体单一事实源：方案 B 思源黑体优先，回退 Segoe UI / 微软雅黑
    const wchar_t* const sansFamilies[] = {
        L"Noto Sans SC",
        L"Source Han Sans SC",
        L"Segoe UI Variable Text",
        L"Segoe UI",
        L"PingFang SC",
        L"Microsoft YaHei UI"
    };

    const wchar_t* const monoFamilies[] = {
        L"Cascadia Code",
        L"Cascadia Mono",
        L"Consolas",
        L"Segoe UI Mono"
    };

    const wchar_t* const* candidateList = (family == FontFamilyType::Mono) ? monoFamilies : sansFamilies;
    const size_t candidateCount = (family == FontFamilyType::Mono)
        ? (sizeof(monoFamilies) / sizeof(monoFamilies[0]))
        : (sizeof(sansFamilies) / sizeof(sansFamilies[0]));

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    for (size_t i = 0; i < candidateCount; ++i) {
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            candidateList[i],
            nullptr,
            dwriteWeight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"zh-cn",
            textFormat.GetAddressOf()
        );
        if (SUCCEEDED(hr) && textFormat) {
            break;
        }
    }

    if (!textFormat) {
        // 最终系统默认回退
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            dwriteWeight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"zh-cn",
            textFormat.GetAddressOf()
        );
    }

    if (textFormat) {
        textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_textFormatCache[key] = textFormat;
        return textFormat.Get();
    }

    return nullptr;
}

void UIRenderContext::drawText(const std::wstring& text,
                              const Rect& bounds,
                              const Color& color,
                              FontToken token,
                              FontWeight weight,
                              FontFamilyType family,
                              TextAlignmentH alignH,
                              TextAlignmentV alignV,
                              bool wordWrap,
                              bool ellipsis) {
    if (!m_rt || !m_dwriteFactory || text.empty() || color.a <= 0.001f) return;

    auto format = getOrCreateTextFormat(token, weight, family);
    if (!format) return;

    format->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

    switch (alignH) {
    case TextAlignmentH::Left:
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        break;
    case TextAlignmentH::Center:
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    case TextAlignmentH::Right:
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        break;
    }

    switch (alignV) {
    case TextAlignmentV::Top:
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        break;
    case TextAlignmentV::Center:
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        break;
    case TextAlignmentV::Bottom:
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        break;
    }

    if (ellipsis) {
        Microsoft::WRL::ComPtr<IDWriteInlineObject> trimmingSign;
        if (SUCCEEDED(m_dwriteFactory->CreateEllipsisTrimmingSign(format, trimmingSign.GetAddressOf()))) {
            DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            format->SetTrimming(&trimming, trimmingSign.Get());
        }
    } else {
        DWRITE_TRIMMING noneTrimming = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
        format->SetTrimming(&noneTrimming, nullptr);
    }

    auto brush = getSolidBrush(color);
    if (brush) {
        m_rt->DrawTextW(
            text.c_str(),
            static_cast<UINT32>(text.length()),
            format,
            bounds.toD2D(),
            brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
        );
    }
}

void UIRenderContext::drawText(const std::wstring& text,
                              Point origin,
                              FontToken token,
                              FontWeight weight,
                              const Color& color,
                              FontFamilyType family) {
    drawText(text, Rect(origin.x, origin.y, origin.x + 4000.0f, origin.y + 120.0f), color, token, weight, family,
             TextAlignmentH::Left, TextAlignmentV::Top, false, false);
}

Size UIRenderContext::measureText(const std::wstring& text,
                                 FontToken token,
                                 FontWeight weight,
                                 FontFamilyType family,
                                 float maxWidth) {
    if (!m_dwriteFactory || text.empty()) return Size(0.0f, 0.0f);

    auto format = getOrCreateTextFormat(token, weight, family);
    if (!format) return Size(0.0f, 0.0f);

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        format,
        maxWidth,
        10000.0f,
        layout.GetAddressOf()
    );

    if (SUCCEEDED(hr) && layout) {
        DWRITE_TEXT_METRICS metrics;
        if (SUCCEEDED(layout->GetMetrics(&metrics))) {
            return Size(metrics.widthIncludingTrailingWhitespace, metrics.height);
        }
    }

    // 粗略兜底
    float h = getFontSize(token) * 1.4f;
    float w = text.length() * getFontSize(token) * 0.6f;
    return Size(w, h);
}

} // namespace tools3000::ui::native
