#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UISeparator.h — Tools3000 原生 1px 微晶双层刻痕分隔线组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UISEPARATOR_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UISEPARATOR_H

#include "ui/native/core/UIElement.h"

namespace tools3000::ui::native {

class UISeparator : public UIElement {
public:
    UISeparator(bool vertical = false);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    bool m_vertical = false;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UISEPARATOR_H
