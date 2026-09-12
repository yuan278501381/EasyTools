#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIRenderContext.h — Direct2D 1.1 + DirectWrite 高性能次像素排版与绘制上下文
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_GRAPHICS_UIRENDERCONTEXT_H
#define TOOLS3000_UI_NATIVE_GRAPHICS_UIRENDERCONTEXT_H

#include "ui/native/common/UIGeometry.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace tools3000::ui::native {

enum class FontToken {
    Xs,   // 12px (严守 11.8px 渲染底线，辅助徽章/代码胶囊)
    Sm,   // 13px (次级描述、辅助文本)
    Base, // 14px (标准正文、标签、按钮文本)
    Md,   // 15.5px (卡片标题、Tab 标题)
    Lg,   // 17.5px (分组标题)
    Xl,   // 21px (页面大标题)
    Xxl   // 26px (品牌大标)
};

enum class FontWeight {
    Regular = 500,  // 映射到 500 Medium，严格恪守排版黄金底线
    Medium = 500,   // 严守 >= 500 黄金底线，杜绝笔画发虚
    SemiBold = 600, // 强调标题、活跃 Tab
    Bold = 700      // 强力强调
};

enum class FontFamilyType {
    Sans, // Noto Sans SC / Segoe UI Variable / PingFang SC / Microsoft YaHei UI
    Mono  // Cascadia Code / Cascadia Mono / Consolas
};

enum class TextAlignmentH {
    Left,
    Center,
    Right
};

enum class TextAlignmentV {
    Top,
    Center,
    Bottom
};

class UIRenderContext {
public:
    UIRenderContext(ID2D1RenderTarget* rt, IDWriteFactory* dwriteFactory, float dpiScale = 1.0f);
    ~UIRenderContext();

    ID2D1RenderTarget* target() const { return m_rt; }
    IDWriteFactory* dwrite() const { return m_dwriteFactory; }
    float dpiScale() const { return m_dpiScale; }

    /// 高分屏尺寸缩放
    float scale(float val) const { return val * m_dpiScale; }
    Thickness scale(const Thickness& t) const {
        return Thickness(scale(t.left), scale(t.top), scale(t.right), scale(t.bottom));
    }
    Rect scale(const Rect& r) const {
        return Rect(scale(r.left), scale(r.top), scale(r.right), scale(r.bottom));
    }

    // ── 基础几何绘制 ──────────────────────────────────────────────────────────
    void fillRect(const Rect& rect, const Color& color);
    void fillRoundedRect(const Rect& rect, float radius, const Color& color);
    void drawRoundedRect(const Rect& rect, float radius, const Color& color, float strokeWidth = 1.0f);
    void drawLine(Point p1, Point p2, const Color& color, float strokeWidth = 1.0f);
    void drawCircle(Point center, float radius, const Color& color, float strokeWidth = 1.0f);
    void fillCircle(Point center, float radius, const Color& color);
    void drawBitmap(ID2D1Bitmap* bitmap, const Rect& destRect, float opacity = 1.0f);

    // ── DirectWrite 次像素排版 ────────────────────────────────────────────────
    void drawText(const std::wstring& text,
                  const Rect& bounds,
                  const Color& color,
                  FontToken token = FontToken::Base,
                  FontWeight weight = FontWeight::Medium,
                  FontFamilyType family = FontFamilyType::Sans,
                  TextAlignmentH alignH = TextAlignmentH::Left,
                  TextAlignmentV alignV = TextAlignmentV::Center,
                  bool wordWrap = false,
                  bool ellipsis = true);

    void drawText(const std::wstring& text,
                  Point origin,
                  FontToken token = FontToken::Base,
                  FontWeight weight = FontWeight::Medium,
                  const Color& color = Color(1.0f, 1.0f, 1.0f, 1.0f),
                  FontFamilyType family = FontFamilyType::Sans);

    Size measureText(const std::wstring& text,
                     FontToken token = FontToken::Base,
                     FontWeight weight = FontWeight::Medium,
                     FontFamilyType family = FontFamilyType::Sans,
                     float maxWidth = 10000.0f);

    // ── 裁剪与变换栈 ──────────────────────────────────────────────────────────
    void pushClip(const Rect& rect);
    void popClip();

    void pushTransform(const D2D1_MATRIX_3X2_F& transform);
    void popTransform();

    // ── 画刷与文本格式缓存 ──────────────────────────────────────────────────
    ID2D1SolidColorBrush* getSolidBrush(const Color& color);
    IDWriteTextFormat* getOrCreateTextFormat(FontToken token, FontWeight weight, FontFamilyType family);
    float getFontSize(FontToken token) const;

private:
    ID2D1RenderTarget* m_rt = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    float m_dpiScale = 1.0f;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_sharedBrush;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>> m_textFormatCache;
    std::vector<D2D1_MATRIX_3X2_F> m_transformStack;
    int m_clipDepth = 0;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_GRAPHICS_UIRENDERCONTEXT_H
