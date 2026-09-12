#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIBadge.h — Tools3000 原生微晶胶囊徽章组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UIBADGE_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UIBADGE_H

#include "ui/native/core/UIElement.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <string>

namespace tools3000::ui::native {

class UIBadge : public UIElement {
public:
    UIBadge(const std::wstring& text = L"", BadgeVariant variant = BadgeVariant::Primary);

    void setText(const std::wstring& text);
    const std::wstring& getText() const { return m_text; }

    void setVariant(BadgeVariant variant);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_text;
    BadgeVariant m_variant = BadgeVariant::Primary;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UIBADGE_H
