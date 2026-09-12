#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UITextInput.h — Tools3000 原生次像素文本输入框组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UITEXTINPUT_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UITEXTINPUT_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <functional>

namespace tools3000::ui::native {

class UITextInput : public UIElement {
public:
    UITextInput(const std::wstring& value = L"",
                const std::wstring& placeholder = L"",
                std::function<void(const std::wstring&)> onChange = nullptr);

    void setValue(const std::wstring& val);
    const std::wstring& getValue() const { return m_value; }

    void setPlaceholder(const std::wstring& ph) { m_placeholder = ph; markNeedsPaint(); }
    void setOnChange(std::function<void(const std::wstring&)> cb) { m_onChange = std::move(cb); }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onKeyDown(const UIKeyEvent& e) override;
    bool onChar(const UIKeyEvent& e) override;
    void onFocus() override;
    void onBlur() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::wstring m_value;
    std::wstring m_placeholder;
    std::function<void(const std::wstring&)> m_onChange;

    size_t m_cursorPos = 0;
    float m_caretBlinkTimer = 0.0f;
    bool m_caretVisible = true;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UITEXTINPUT_H
