#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UILabel.h — Tools3000 原生次像素排版文本标签组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UILABEL_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UILABEL_H

#include "ui/native/core/UIElement.h"

namespace tools3000::ui::native {

enum class TextColorRole {
    Default,    // 跟随主题 palette().textPrimary
    Primary,    // 跟随主题 palette().textPrimary
    Secondary,  // 跟随主题 palette().textSecondary
    Muted,      // 跟随主题 palette().textMuted
    Disabled,   // 跟随主题 palette().textDisabled
    Accent,     // 跟随主题 palette().primary
    Custom      // 显式固定 m_color
};

class UILabel : public UIElement {
public:
    UILabel(const std::wstring& text = L"",
            FontToken font = FontToken::Base,
            FontWeight weight = FontWeight::Medium,
            FontFamilyType family = FontFamilyType::Sans);

    void setText(const std::wstring& text);
    const std::wstring& getText() const { return m_text; }

    void setColor(const Color& color);
    void setColorRole(TextColorRole role);
    TextColorRole getColorRole() const { return m_colorRole; }

    void setFontToken(FontToken token);
    void setFontWeight(FontWeight weight);
    void setAlignment(TextAlignmentH alignH, TextAlignmentV alignV = TextAlignmentV::Center);
    void setWordWrap(bool wrap);
    void setEllipsis(bool ellipsis);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_text;
    FontToken m_fontToken = FontToken::Base;
    FontWeight m_fontWeight = FontWeight::Medium;
    FontFamilyType m_fontFamily = FontFamilyType::Sans;
    TextAlignmentH m_alignH = TextAlignmentH::Left;
    TextAlignmentV m_alignV = TextAlignmentV::Center;
    Color m_color;
    TextColorRole m_colorRole = TextColorRole::Default;
    bool m_wordWrap = false;
    bool m_ellipsis = true;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UILABEL_H
