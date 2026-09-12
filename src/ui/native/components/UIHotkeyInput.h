#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIHotkeyInput.h — Tools3000 原生全局快捷键录制组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UIHOTKEYINPUT_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UIHOTKEYINPUT_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <functional>

namespace tools3000::ui::native {

class UIHotkeyInput : public UIElement {
public:
    UIHotkeyInput(const std::string& hotkey = "",
                  std::function<void(const std::string&)> onChange = nullptr);
    ~UIHotkeyInput() override;

    void setHotkey(const std::string& hotkey);
    const std::string& getHotkey() const { return m_hotkey; }

    void setOnChange(std::function<void(const std::string&)> cb) { m_onChange = std::move(cb); }

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;

    bool onMouseDown(const UIMouseEvent& e) override;
    bool onKeyDown(const UIKeyEvent& e) override;
    void onBlur() override;

protected:
    void onRenderContent(UIRenderContext& ctx) override;

private:
    std::string m_hotkey;
    std::function<void(const std::string&)> m_onChange;
    bool m_isRecording = false;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UIHOTKEYINPUT_H
