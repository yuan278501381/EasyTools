#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UICard.h — Tools3000 原生微晶亚克力玻璃卡片容器组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UICARD_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UICARD_H

#include "ui/native/core/UIElement.h"
#include <string>

namespace tools3000::ui::native {

class UICard : public UIElement {
public:
    UICard(const std::wstring& title = L"", const std::wstring& subtitle = L"");

    void setTitle(const std::wstring& title) { m_title = title; markNeedsLayout(); }
    const std::wstring& getTitle() const { return m_title; }

    void setSubtitle(const std::wstring& subtitle) { m_subtitle = subtitle; markNeedsLayout(); }
    const std::wstring& getSubtitle() const { return m_subtitle; }

    void setHeaderAction(std::shared_ptr<UIElement> action);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    void layout(const Rect& bounds, UIRenderContext& ctx) override;

    void onMouseEnter() override;
    void onMouseLeave() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_title;
    std::wstring m_subtitle;
    std::shared_ptr<UIElement> m_headerAction;
    Rect m_bodyRect;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UICARD_H
