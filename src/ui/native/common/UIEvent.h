#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIEvent.h — Tools3000 原生强类型用户交互与硬件输入事件体系
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMMON_UIEVENT_H
#define TOOLS3000_UI_NATIVE_COMMON_UIEVENT_H

#include "ui/native/common/UIGeometry.h"
#include <cstdint>

namespace tools3000::ui::native {

enum class MouseEventType {
    MouseMove,
    MouseDown,
    MouseUp,
    MouseWheel,
    MouseEnter,
    MouseLeave
};

enum class MouseButton {
    None = 0,
    Left,
    Right,
    Middle
};

struct EventModifiers {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
};

struct UIMouseEvent {
    MouseEventType type = MouseEventType::MouseMove;
    Point position;
    MouseButton button = MouseButton::None;
    float wheelDelta = 0.0f;
    int clickCount = 1;
    EventModifiers modifiers;
    mutable bool handled = false;

    UIMouseEvent() = default;
    UIMouseEvent(MouseEventType t, Point pt, MouseButton btn = MouseButton::None, float delta = 0.0f, int clicks = 1)
        : type(t), position(pt), button(btn), wheelDelta(delta), clickCount(clicks) {}
};

enum class KeyEventType {
    KeyDown,
    KeyUp,
    Char
};

struct UIKeyEvent {
    KeyEventType type = KeyEventType::KeyDown;
    uint32_t virtualKey = 0;
    wchar_t charCode = 0;
    EventModifiers modifiers;
    uint32_t repeatCount = 1;
    mutable bool handled = false;

    UIKeyEvent() = default;
    UIKeyEvent(KeyEventType t, uint32_t vk, wchar_t ch = 0)
        : type(t), virtualKey(vk), charCode(ch) {}
};

enum class FocusEventType {
    GotFocus,
    LostFocus
};

struct UIFocusEvent {
    FocusEventType type = FocusEventType::GotFocus;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMMON_UIEVENT_H
