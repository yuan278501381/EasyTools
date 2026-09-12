#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UINumberInput.h — Tools3000 原生数字微调步进输入组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UINUMBERINPUT_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UINUMBERINPUT_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <functional>

namespace tools3000::ui::native {

class UINumberInput : public UIElement {
public:
    UINumberInput(int value = 0, int minVal = 0, int maxVal = 1000, int step = 1,
                  const std::wstring& unit = L"",
                  std::function<void(int)> onChange = nullptr);

    void setValue(int val);
    int getValue() const { return m_value; }

    void setRange(int minVal, int maxVal) { m_minVal = minVal; m_maxVal = maxVal; }
    void setStep(int step) { m_step = step; }
    void setUnit(const std::wstring& unit) { m_unit = unit; markNeedsLayout(); }
    void setOnChange(std::function<void(int)> cb) { m_onChange = std::move(cb); }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onMouseWheel(const UIMouseEvent& e) override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    Rect getUpButtonRect() const;
    Rect getDownButtonRect() const;

private:
    int m_value = 0;
    int m_minVal = 0;
    int m_maxVal = 1000;
    int m_step = 1;
    std::wstring m_unit;
    std::function<void(int)> m_onChange;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UINUMBERINPUT_H
