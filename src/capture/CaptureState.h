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
enum class ToolbarCommand {
    SelectTool,
    SelectColor,
    ToggleCornerRadius,
    Undo,
    Redo,
    Clear,
    ExtractText,
    PinWindow,
    ScrollCapture,
    Confirm,
    Cancel,
    // 二级属性栏专属命令
    ToggleFill,              // 切换填充模式
    CycleStrokeWidth,        // 循环切换线宽 (2, 4, 6, 8, 12, 16)
    CycleElementCornerRadius,// 循环切换标注圆角 (0, 8, 14, 20)
    ToggleLineStyleDropdown,  // 展开/收起线条样式下拉菜单
    SelectLineStyle,         // 选择线条样式 (实线/虚线/点线/点划线)
    ToggleArrowStyleDropdown,// 展开/收起箭头样式下拉菜单
    SelectArrowStyle,        // 选择箭头样式 (标准/细线/双向)
    ToggleShapeDropdown,     // 展开/收起形状下拉菜单
    TogglePenDropdown,       // 展开/收起画笔下拉菜单
    ToggleArrowDropdown,     // 展开/收起箭头主下拉菜单
    ToggleMosaicDropdown,    // 展开/收起马赛克下拉菜单
    SelectMosaicType,        // 选择马赛克类型 (像素/高斯模糊)
    // 选区侧边浮动菜单专属命令 (PixPin 标杆交互)
    SideToggleCornerRadius,  // 侧边展开/收起选区圆角滑块面板
    SideInvertSelection,     // 侧边反选/选区翻转
    SideResetSelection,      // 侧边重置选区直角
};

enum class SubmenuType {
    None = 0,
    LineStyle,
    ArrowStyle,
    ShapeType,
    PenType,
    ArrowType,
    MosaicType,
};

enum class SliderPopupType {
    None = 0,
    StrokeWidth,            // 线宽无级滑块 (1~30px)
    CornerRadius,           // 标注圆角无级滑块 (0~60px)
    SelectionCornerRadius,  // 选区圆角无级滑块 (0~120px) (PixPin 标杆)
    TextFontSize,           // 字号无级滑块 (12~72px)
    MosaicBlockSize,        // 马赛克颗粒度无级滑块 (2~40px)
};

struct SliderPopupState {
    SliderPopupType type = SliderPopupType::None;
    D2D1_RECT_F popupRect{};
    D2D1_RECT_F trackRect{};
    D2D1_RECT_F thumbRect{};
    std::vector<std::pair<int, D2D1_RECT_F>> presetButtons;
    int minValue = 1;
    int maxValue = 30;
    int currentValue = 4;
    bool isDragging = false;
    float dragAnchorX = 0.0f;
};

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
    int intParam = 0;
    LineStyle lineStyleParam = LineStyle::Solid;
    ArrowStyle arrowStyleParam = ArrowStyle::Standard;
    bool boolParam = false;
    bool hasDropdown = false;
    bool isSecondary = false;
    bool isSeparatorBefore = false;
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
    LineStyle currentLineStyle = LineStyle::Solid;
    int currentStrokeWidth = 4;
    bool currentFillMode = false;
    float currentElementCornerRadius = 0.0f;
    ArrowStyle currentArrowStyle = ArrowStyle::Standard;

    bool markupBaseReady = false;
    bool isMarking = false;
    POINT markupStart{};
    POINT markupEnd{};
    std::vector<cv::Point> penPoints;
    std::vector<ToolbarButton> toolbarButtons;
    std::vector<ToolbarButton> secondaryToolbarButtons;
    std::vector<ToolbarButton> selectionSideButtons;
    SubmenuType openSubmenu = SubmenuType::None;
    D2D1_RECT_F openSubmenuRect{};
    std::vector<ToolbarButton> submenuButtons;
    SliderPopupState sliderPopup{};
    D2D1_RECT_F primaryToolbarRect{};
    D2D1_RECT_F secondaryToolbarRect{};
    D2D1_RECT_F selectionSideRect{};
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

    enum class SizeUnit { Pixel = 0, DeviceIndependentPixel = 1 };
    SizeUnit sizeUnit = SizeUnit::Pixel;
    bool showPositionInHud = true;
    bool showUnitInHud = true;
    bool isSizeHudHovered = false;
    bool isSizeMenuOpen = false;
    D2D1_RECT_F sizeHudRect{};
    D2D1_RECT_F sizeMenuRect{};

    // 智能二维码探测与快速动作胶囊
    std::string detectedQrText;
    D2D1_RECT_F qrChipRect{};
    bool isQrChipHovered = false;

    SelectionCallback callback;
    RecordSelectionCallback recordCallback;
    std::function<void(const CaptureRegion& region, const cv::Mat& cropped)> ocrCallback;

    bool historyMode = false;
    int historyIndex = 0;
};

} // namespace easy::capture
#endif // EASYTOOLS_CAPTURE_CAPTURE_STATE_H
