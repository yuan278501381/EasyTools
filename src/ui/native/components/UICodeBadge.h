#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UICodeBadge.h — Tools3000 原生等宽微晶代码/类名胶囊组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UICODEBADGE_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UICODEBADGE_H

#include "ui/native/core/UIElement.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include <string>

namespace tools3000::ui::native {

class UICodeBadge : public UIElement {
public:
    UICodeBadge(const std::wstring& code = L"");

    void setCode(const std::wstring& code);
    const std::wstring& getCode() const { return m_code; }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_code;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UICODEBADGE_H
