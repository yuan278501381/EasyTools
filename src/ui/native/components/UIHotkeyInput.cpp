#include "ui/native/components/UIHotkeyInput.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include "core/hotkey/HotkeyManager.h"
#include <windows.h>
#include <algorithm>

namespace tools3000::ui::native {

UIHotkeyInput::UIHotkeyInput(const std::string& hotkey,
                             std::function<void(const std::string&)> onChange)
    : m_hotkey(hotkey), m_onChange(std::move(onChange)) {
}

UIHotkeyInput::~UIHotkeyInput() {
    if (m_isRecording) {
        tools3000::core::HotkeyManager::instance().setPaused(false);
    }
}

void UIHotkeyInput::setHotkey(const std::string& hotkey) {
    if (m_hotkey != hotkey) {
        m_hotkey = hotkey;
        markNeedsPaint();
    }
}

Size UIHotkeyInput::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableWidth;
    (void)availableHeight;
    (void)ctx;
    m_desiredSize = Size(150.0f, 32.0f);
    return m_desiredSize;
}

void UIHotkeyInput::onBlur() {
    UIElement::onBlur();
    if (m_isRecording) {
        m_isRecording = false;
        tools3000::core::HotkeyManager::instance().setPaused(false);
        markNeedsPaint();
    }
}

bool UIHotkeyInput::onMouseDown(const UIMouseEvent& e) {
    if (e.button != MouseButton::Left || !m_enabled) return false;

    // 检查是否点击右侧清空按钮
    Rect clearRect(m_bounds.right - 24.0f, m_bounds.top + 4.0f, m_bounds.right - 4.0f, m_bounds.bottom - 4.0f);
    if (!m_hotkey.empty() && clearRect.contains(e.position)) {
        m_hotkey.clear();
        if (m_isRecording) {
            m_isRecording = false;
            tools3000::core::HotkeyManager::instance().setPaused(false);
        }
        if (m_onChange) m_onChange(m_hotkey);
        markNeedsPaint();
        return true;
    }

    if (m_bounds.contains(e.position)) {
        m_isRecording = !m_isRecording;
        tools3000::core::HotkeyManager::instance().setPaused(m_isRecording);
        markNeedsPaint();
        return true;
    }

    return false;
}

bool UIHotkeyInput::onKeyDown(const UIKeyEvent& e) {
    if (!m_isRecording || !m_enabled) return false;

    if (e.virtualKey == VK_ESCAPE) {
        m_isRecording = false;
        tools3000::core::HotkeyManager::instance().setPaused(false);
        markNeedsPaint();
        return true;
    }

    // 忽略纯修饰键
    if (e.virtualKey == VK_CONTROL || e.virtualKey == VK_SHIFT ||
        e.virtualKey == VK_MENU || e.virtualKey == VK_LWIN || e.virtualKey == VK_RWIN) {
        return true;
    }

    std::string combo;
    if (e.modifiers.ctrl) combo += "Ctrl+";
    if (e.modifiers.alt) combo += "Alt+";
    if (e.modifiers.shift) combo += "Shift+";
    if (e.modifiers.win) combo += "Win+";

    // 键名转换
    std::string keyName;
    if (e.virtualKey >= 'A' && e.virtualKey <= 'Z') {
        keyName = static_cast<char>(e.virtualKey);
    } else if (e.virtualKey >= '0' && e.virtualKey <= '9') {
        keyName = static_cast<char>(e.virtualKey);
    } else if (e.virtualKey >= VK_F1 && e.virtualKey <= VK_F24) {
        keyName = "F" + std::to_string(e.virtualKey - VK_F1 + 1);
    } else {
        switch (e.virtualKey) {
        case VK_SPACE: keyName = "Space"; break;
        case VK_RETURN: keyName = "Enter"; break;
        case VK_TAB: keyName = "Tab"; break;
        case VK_BACK: keyName = "Backspace"; break;
        case VK_DELETE: keyName = "Delete"; break;
        case VK_UP: keyName = "Up"; break;
        case VK_DOWN: keyName = "Down"; break;
        case VK_LEFT: keyName = "Left"; break;
        case VK_RIGHT: keyName = "Right"; break;
        default:
            keyName = "Key" + std::to_string(e.virtualKey);
            break;
        }
    }

    combo += keyName;
    m_hotkey = combo;
    m_isRecording = false;
    tools3000::core::HotkeyManager::instance().setPaused(false);

    if (m_onChange) {
        m_onChange(m_hotkey);
    }
    markNeedsPaint();
    return true;
}

void UIHotkeyInput::onRenderContent(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    const bool dark = theme.isEffectiveDark();

    const float radius = 6.0f;
    Color bg = dark ? Color::fromHex(0x0f0f19, 0.75f) : Color::fromHex(0xffffff, 0.90f);
    Color border = m_isRecording
        ? p.primary
        : (dark ? Color(1.0f, 1.0f, 1.0f, 0.12f) : Color(0.06f, 0.09f, 0.16f, 0.10f));

    ctx.fillRoundedRect(m_bounds, radius, bg);
    ctx.drawRoundedRect(m_bounds, radius, border, m_isRecording ? 1.5f : 1.0f);

    std::wstring displayText;
    Color textColor = p.textPrimary;
    if (m_isRecording) {
        displayText = L"请按下快捷键...";
        textColor = p.primary;
    } else if (m_hotkey.empty()) {
        displayText = L"点击录制快捷键";
        textColor = p.textMuted;
    } else {
        displayText = std::wstring(m_hotkey.begin(), m_hotkey.end());
    }

    Rect textRect(m_bounds.left + 10.0f, m_bounds.top, m_bounds.right - 24.0f, m_bounds.bottom);
    ctx.drawText(displayText, textRect, textColor, FontToken::Base, FontWeight::Medium,
                 FontFamilyType::Sans, TextAlignmentH::Left, TextAlignmentV::Center);

    if (!m_hotkey.empty() && !m_isRecording) {
        Rect clearRect(m_bounds.right - 22.0f, m_bounds.top + (m_bounds.height() - 12.0f) * 0.5f,
                       m_bounds.right - 10.0f, m_bounds.top + (m_bounds.height() + 12.0f) * 0.5f);
        VectorIconRenderer::drawIcon(ctx, IconType::X, clearRect, p.textMuted, 2.0f);
    }
}

} // namespace tools3000::ui::native
