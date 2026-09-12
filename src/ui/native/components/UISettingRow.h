#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UISettingRow.h — Tools3000 原生标准配置行组件 (标题 + 说明 + 交互控件)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGROW_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGROW_H

#include "ui/native/core/UIElement.h"
#include <string>

namespace tools3000::ui::native {

class UISettingRow : public UIElement {
public:
    UISettingRow(const std::wstring& label = L"",
                 const std::wstring& description = L"",
                 std::shared_ptr<UIElement> control = nullptr);

    void setLabel(const std::wstring& label) { m_label = label; markNeedsLayout(); }
    void setDescription(const std::wstring& desc) { m_description = desc; markNeedsLayout(); }
    void setControl(std::shared_ptr<UIElement> control);

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    void layout(const Rect& bounds, UIRenderContext& ctx) override;
    bool onMouseDown(const UIMouseEvent& e) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_label;
    std::wstring m_description;
    std::shared_ptr<UIElement> m_control;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UISETTINGROW_H
