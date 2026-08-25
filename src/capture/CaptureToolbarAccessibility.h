#pragma once

#include "capture/CaptureState.h"

namespace easy::capture {

// The painted toolbar uses compact glyphs. Keep the spoken/UIA contract in a
// small header-only mapping so it cannot drift from the command dispatcher or
// require UI/window construction in unit tests.
inline std::wstring toolbarButtonAccessibleName(const ToolbarButton& button) {
    switch (button.command) {
        case ToolbarCommand::SelectTool:
            switch (button.tool) {
                case MarkupTool::Rectangle: return L"矩形标注";
                case MarkupTool::Arrow: return L"箭头标注";
                case MarkupTool::Ellipse: return L"椭圆标注";
                case MarkupTool::Pen: return L"画笔标注";
                case MarkupTool::Highlight: return L"高亮标注";
                case MarkupTool::Mosaic: return L"马赛克标注";
                case MarkupTool::Text: return L"文字标注";
                case MarkupTool::Number: return L"编号标注";
                case MarkupTool::Magnifier: return L"放大镜标注";
                case MarkupTool::Spotlight: return L"聚光灯标注";
                case MarkupTool::Watermark: return L"水印标注";
                case MarkupTool::Inpaint: return L"修复标注";
            }
            return L"标注工具";
        case ToolbarCommand::SelectColor: return L"标注颜色";
        case ToolbarCommand::Undo: return L"撤销";
        case ToolbarCommand::Redo: return L"重做";
        case ToolbarCommand::Clear: return L"清空标注";
        case ToolbarCommand::ToggleCornerRadius: return L"调节选区圆角";
        case ToolbarCommand::ExtractText: return L"提取文本";
        case ToolbarCommand::PinWindow: return L"置顶到屏幕";
        case ToolbarCommand::ScrollCapture: return L"开始长截图";
        case ToolbarCommand::Confirm: return L"确认截图";
        case ToolbarCommand::Cancel: return L"取消截图";
    }
    return L"截图工具栏操作";
}

inline std::wstring toolbarButtonKeyboardShortcut(const ToolbarButton& button) {
    switch (button.command) {
        case ToolbarCommand::ToggleCornerRadius: return L"[ / ]";
        case ToolbarCommand::Undo: return L"Ctrl+Z";
        case ToolbarCommand::Redo: return L"Ctrl+Y";
        case ToolbarCommand::Confirm: return L"Enter";
        case ToolbarCommand::Cancel: return L"Escape";
        default: return L"";
    }
}

inline bool isToolbarButtonSelected(const ToolbarButton& button, const CaptureState& state) noexcept {
    if (button.command == ToolbarCommand::SelectTool) return button.tool == state.currentTool;
    if (button.command == ToolbarCommand::SelectColor) {
        return button.color.r == state.currentColor.r && button.color.g == state.currentColor.g &&
               button.color.b == state.currentColor.b && button.color.a == state.currentColor.a;
    }
    return false;
}

inline bool isToolbarButtonEnabled(const ToolbarButton& button, const CaptureState& state) noexcept {
    const auto overlayState = state.state.load(std::memory_order_acquire);
    if (overlayState != OverlayState::Selected && overlayState != OverlayState::Marking) return false;
    switch (button.command) {
        case ToolbarCommand::Undo: return state.markup.canUndo();
        case ToolbarCommand::Redo: return state.markup.canRedo();
        case ToolbarCommand::Clear: return state.markup.elementCount() > 0;
        case ToolbarCommand::ExtractText: return static_cast<bool>(state.ocrCallback);
        default: return true;
    }
}

}  // namespace easy::capture
