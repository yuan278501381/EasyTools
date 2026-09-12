#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UISettingGroup.h — Tools3000 原生设置分组与模块聚合组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGGROUP_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGGROUP_H

#include "ui/native/core/UIElement.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>

namespace tools3000::ui::native {

class UISettingGroup : public UIElement {
public:
    UISettingGroup(const std::wstring& title = L"", IconType icon = IconType::None);

    void setTitle(const std::wstring& title) { m_title = title; markNeedsLayout(); }
    const std::wstring& getTitle() const { return m_title; }

    void setIcon(IconType icon) { m_icon = icon; markNeedsLayout(); }
    IconType getIcon() const { return m_icon; }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    void layout(const Rect& bounds, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_title;
    IconType m_icon = IconType::None;
    Rect m_contentRect;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGGROUP_H
