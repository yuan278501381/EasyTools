#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UISelect.h — Tools3000 原生微晶下拉选择框组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UISELECT_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UISELECT_H

#include "ui/native/core/UIElement.h"
#include "ui/native/common/UIAnimation.h"
#include <string>
#include <vector>
#include <functional>

namespace tools3000::ui::native {

struct SelectOption {
    std::string value;
    std::wstring label;
};

class UISelect : public UIElement {
public:
    UISelect(const std::vector<SelectOption>& options = {},
             const std::string& selectedValue = "",
             std::function<void(const std::string&)> onSelect = nullptr);

    void setOptions(const std::vector<SelectOption>& options);
    void setSelectedValue(const std::string& val);
    const std::string& getSelectedValue() const { return m_selectedValue; }

    void setOnSelect(std::function<void(const std::string&)> cb) { m_onSelect = std::move(cb); }
    const std::function<void(const std::string&)>& getOnSelect() const { return m_onSelect; }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    void onMouseEnter() override;
    void onMouseLeave() override;
    void onBlur() override;

    std::shared_ptr<UIElement> hitTest(Point pt) override;

    Rect getDropdownRect() const;
    Rect getOptionRect(size_t index) const;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:

private:
    std::vector<SelectOption> m_options;
    std::string m_selectedValue;
    std::function<void(const std::string&)> m_onSelect;

    bool m_isOpen = false;
    int m_hoveredIndex = -1;
    AnimatedFloat m_openProgress{0.0f};
    AnimatedFloat m_hoverAlpha{0.0f};
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UISELECT_H
