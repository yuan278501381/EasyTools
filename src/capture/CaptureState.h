#pragma once
#ifndef EASYTOOLS_CAPTURE_CAPTURE_STATE_H
#define EASYTOOLS_CAPTURE_CAPTURE_STATE_H

#include "capture/ScreenCapture.h"
#include "capture/MarkupEngine.h"

#include <windows.h>
#include <d2d1.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <opencv2/core.hpp>

namespace easy::capture {

enum class OverlayMode { Screenshot, RecordRegion };
enum class OverlayState { Idle, Selecting, Selected, Marking };
enum class CaptureCompletionAction { Default, Copy, SaveAs };
enum class ToolbarCommand { SelectTool, SelectColor, ToggleCornerRadius, Undo, Redo, Clear, ExtractText, PinWindow, ScrollCapture, Confirm, Cancel };

enum class ColorFormatType {
    HEX = 0,     // #3A86FF
    RGB,         // rgb(58, 134, 255)
    RGBA,        // rgba(58, 134, 255, 1.0)
    HEX_0x,      // 0x3A86FF
    HSL,         // hsl(217, 100%, 61%)
    HSV,         // hsv(217, 77%, 100%)
    CMYK,        // cmyk(77%, 47%, 0%, 0%)
    DEC,         // 3835647
    COUNT
};

struct CaptureCompletion {
    CaptureCompletionAction action = CaptureCompletionAction::Default;
    std::string filePath;
    ImageFormat format = ImageFormat::PNG;
};

struct ToolbarButton {
    D2D1_RECT_F rect{};
    ToolbarCommand command = ToolbarCommand::SelectTool;
    MarkupTool tool = MarkupTool::Rectangle;
    MarkupColor color = MarkupColor::Red();
    std::wstring label;
};

using SelectionCallback = std::function<void(
    const CaptureRegion& region, const cv::Mat& markedImage, const CaptureCompletion& completion)>;
using RecordSelectionCallback = std::function<void(const CaptureRegion& region)>;

class CaptureState {
public:
    std::atomic<OverlayState> state{OverlayState::Idle};
    OverlayMode mode = OverlayMode::Screenshot;
    CaptureOptions options;

    POINT dragStart{};
    POINT dragEnd{};
    POINT currentCursor{};
    bool dragging = false;
    RECT detectedWindow{};
    std::vector<RECT> detectedWindowHierarchy;
    int detectedWindowHierarchyIndex = 0;
    POINT lastMousePos{};

    cv::Mat frozenScreen;

    MarkupEngine markup;
    MarkupTool currentTool = MarkupTool::Rectangle;
    MarkupColor currentColor = MarkupColor::Red();
    bool markupBaseReady = false;
    bool isMarking = false;
    POINT markupStart{};
    POINT markupEnd{};
    std::vector<cv::Point> penPoints;
    std::vector<ToolbarButton> toolbarButtons;
    D2D1_RECT_F toolbarLayoutSelection{};
    D2D1_SIZE_F toolbarLayoutSurface{};
    float toolbarLayoutScale = 0.0f;
    OverlayMode toolbarLayoutMode = OverlayMode::Screenshot;
    bool toolbarLayoutChinese = true;
    bool toolbarLayoutValid = false;
    MarkupElement* activeElement = nullptr;
    HitArea dragHandle = HitArea::None;
    bool isManipulating = false;

    bool isAdjustingSelection = false;
    HitArea selAdjustHandle = HitArea::None;
    POINT selAdjustLast{};

    bool isAdjustingCornerRadius = false;
    float cornerDragStartRadius = 0.0f;
    POINT cornerDragStartPos{};

    int dynamicMagnifierRadius = 60;
    float dynamicMagnifierScale = 2.0f;

    DWORD loupeToastUntil = 0;
    bool isFadingOut = false;
    float dpiScale = 1.0f;
    ColorFormatType colorFormat = ColorFormatType::HEX;
    bool colorFormatHex = true;
    bool showTimestamp = false;
    float cornerRadius = 0.0f; // 选区圆角半径 (0, 8, 12, 16, 24)
    float detectedWindowCornerRadius = 10.0f; // 现代 Windows 11 窗口圆角高亮半径
    DWORD fadeOutStart = 0;

    SelectionCallback callback;
    RecordSelectionCallback recordCallback;
    std::function<void(const CaptureRegion& region, const cv::Mat& cropped)> ocrCallback;

    bool historyMode = false;
    int historyIndex = 0;
};

} // namespace easy::capture
#endif // EASYTOOLS_CAPTURE_CAPTURE_STATE_H
