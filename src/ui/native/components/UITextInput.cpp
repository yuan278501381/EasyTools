#include "ui/native/components/UITextInput.h"
#include "ui/native/common/UITheme.h"
#include <windows.h>
#include <algorithm>

namespace tools3000::ui::native {

UITextInput::UITextInput(const std::wstring& value,
                         const std::wstring& placeholder,
                         std::function<void(const std::wstring&)> onChange)
    : m_value(value), m_placeholder(placeholder), m_onChange(std::move(onChange)) {
    m_cursorPos = m_value.length();
}

void UITextInput::setValue(const std::wstring& val) {
    if (m_value != val) {
        m_value = val;
        m_cursorPos = std::min(m_cursorPos, m_value.length());
        markNeedsPaint();
    }
}

Size UITextInput::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    (void)ctx;
    float w = (availableWidth > 0.0f) ? std::min(availableWidth, 240.0f) : 180.0f;
    m_desiredSize = Size(w, 32.0f);
    return m_desiredSize;
}

bool UITextInput::update(float dt) {
    if (m_focused) {
        m_caretBlinkTimer += dt;
        if (m_caretBlinkTimer >= 0.5f) {
            m_caretBlinkTimer = 0.0f;
            m_caretVisible = !m_caretVisible;
            markNeedsPaint();
        }
    }
    return UIElement::update(dt);
}

void UITextInput::onFocus() {
    UIElement::onFocus();
    m_caretVisible = true;
    m_caretBlinkTimer = 0.0f;
}

void UITextInput::onBlur() {
    UIElement::onBlur();
    m_caretVisible = false;
}

bool UITextInput::onMouseDown(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled) {
        onFocus();
        m_cursorPos = m_value.length();
        return true;
    }
    return false;
}

bool UITextInput::onKeyDown(const UIKeyEvent& e) {
    if (!m_focused || !m_enabled) return false;

    if (e.virtualKey == VK_LEFT) {
        if (m_cursorPos > 0) {
            m_cursorPos--;
            m_caretVisible = true;
            m_caretBlinkTimer = 0.0f;
            markNeedsPaint();
        }
        return true;
    } else if (e.virtualKey == VK_RIGHT) {
        if (m_cursorPos < m_value.length()) {
            m_cursorPos++;
            m_caretVisible = true;
            m_caretBlinkTimer = 0.0f;
            markNeedsPaint();
        }
        return true;
    } else if (e.virtualKey == VK_HOME) {
        m_cursorPos = 0;
        m_caretVisible = true;
        m_caretBlinkTimer = 0.0f;
        markNeedsPaint();
        return true;
    } else if (e.virtualKey == VK_END) {
        m_cursorPos = m_value.length();
        m_caretVisible = true;
        m_caretBlinkTimer = 0.0f;
        markNeedsPaint();
        return true;
    } else if (e.virtualKey == VK_BACK) {
        if (m_cursorPos > 0) {
            m_value.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
            m_caretVisible = true;
            m_caretBlinkTimer = 0.0f;
            if (m_onChange) m_onChange(m_value);
            markNeedsPaint();
        }
        return true;
    } else if (e.virtualKey == VK_DELETE) {
        if (m_cursorPos < m_value.length()) {
            m_value.erase(m_cursorPos, 1);
            m_caretVisible = true;
            m_caretBlinkTimer = 0.0f;
            if (m_onChange) m_onChange(m_value);
            markNeedsPaint();
        }
        return true;
    } else if (e.modifiers.ctrl && e.virtualKey == 'V') {
        // 粘贴文本
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pText) {
                    std::wstring pasted(pText);
                    m_value.insert(m_cursorPos, pasted);
                    m_cursorPos += pasted.length();
                    GlobalUnlock(hData);
                    if (m_onChange) m_onChange(m_value);
                    markNeedsPaint();
                }
            }
            CloseClipboard();
        }
        return true;
    }
    return false;
}

bool UITextInput::onChar(const UIKeyEvent& e) {
    if (!m_focused || !m_enabled) return false;

    if (e.charCode >= 32 && e.charCode != 127) {
        m_value.insert(m_cursorPos, 1, e.charCode);
        m_cursorPos++;
        m_caretVisible = true;
        m_caretBlinkTimer = 0.0f;
        if (m_onChange) m_onChange(m_value);
        markNeedsPaint();
        return true;
    }
    return false;
}

void UITextInput::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    const float radius = 6.0f;
    Color bg = dark ? Color::fromHex(0x0f0f19, 0.75f) : Color::fromHex(0xffffff, 0.90f);
    Color border = m_focused ? p.primary : (dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.12f));

    ctx.fillRoundedRect(m_bounds, radius, bg);
    ctx.drawRoundedRect(m_bounds, radius, border, m_focused ? 1.5f : 1.0f);

    Rect textInner(m_bounds.left + 10.0f, m_bounds.top, m_bounds.right - 10.0f, m_bounds.bottom);

    if (m_value.empty() && !m_placeholder.empty()) {
        ctx.drawText(m_placeholder, textInner, p.textMuted, FontToken::Base, FontWeight::Medium,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    } else {
        ctx.drawText(m_value, textInner, p.textPrimary, FontToken::Base, FontWeight::Medium,
                     FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);
    }

    // 绘制光标
    if (m_focused && m_caretVisible) {
        std::wstring prefix = m_value.substr(0, m_cursorPos);
        Size prefSize = ctx.measureText(prefix, FontToken::Base, FontWeight::Medium, FontFamilyType::Sans);
        float caretX = textInner.left + prefSize.width;
        float caretTop = m_bounds.top + (m_bounds.height() - 16.0f) * 0.5f;
        ctx.drawLine(Point(caretX, caretTop), Point(caretX, caretTop + 16.0f), p.primary, 1.5f);
    }
}

} // namespace tools3000::ui::native
