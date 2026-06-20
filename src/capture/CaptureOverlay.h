#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CaptureOverlay — 截图区域选择覆盖层
//
// 职责:
//   1. 全屏半透明覆盖，按住鼠标拖拽选择截图区域
//   2. 鼠标悬停时自动检测窗口边界并高亮
//   3. 选区完成后显示标注工具栏
//   4. 实时显示选区尺寸和坐标
//   5. 支持 ESC 取消、Enter/双击确认
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H
#define EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H

#include "capture/ScreenCapture.h"
#include "capture/MarkupEngine.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <opencv2/core.hpp>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>

namespace easy::capture {

/// 覆盖层模式
enum class OverlayMode {
    Screenshot,     // 截图模式
    RecordRegion,   // 录屏选区模式
};

/// 覆盖层状态
enum class OverlayState {
    Idle,           // 空闲
    Selecting,      // 正在拖拽选区
    Selected,       // 选区完成，等待确认/标注
    Marking,        // 正在使用标注工具
};

enum class ToolbarCommand {
    SelectTool,
    SelectColor,    // 选择标注颜色
    Undo,
    Redo,
    Clear,
    ExtractText,    // 提取文字 (OCR)
    PinWindow,      // 贴图
    ScrollCapture,  // 长截图
    Confirm,
    Cancel,
};

struct ToolbarButton {
    D2D1_RECT_F rect{};
    ToolbarCommand command = ToolbarCommand::SelectTool;
    MarkupTool tool = MarkupTool::Rectangle;
    MarkupColor color = MarkupColor::Red();  // command == SelectColor 时使用
    std::wstring label;
};

/// 选区确认回调
using SelectionCallback = std::function<void(const CaptureRegion& region, const cv::Mat& markedImage)>;
using RecordSelectionCallback = std::function<void(const CaptureRegion& region)>;

class CaptureOverlay {
public:
    static CaptureOverlay& instance();

    /// 初始化覆盖层资源
    bool initialize(HINSTANCE hInstance);

    /// 关闭
    void shutdown();

    /// 进入选区模式（冻结屏幕 + 显示覆盖层）
    void startSelection(const CaptureOptions& options, OverlayMode mode = OverlayMode::Screenshot);

    /// 取消截图
    void cancel();

    /// 设置截图完成回调
    void setCallback(SelectionCallback callback) { m_callback = std::move(callback); }

    /// 设置录屏选区完成回调
    void setRecordCallback(RecordSelectionCallback callback) { m_recordCallback = std::move(callback); }

    /// 设置 OCR 回调
    void setOcrCallback(std::function<void(const CaptureRegion& region, const cv::Mat& cropped)> callback) { m_ocrCallback = std::move(callback); }

    /// 当前状态
    OverlayState state() const { return m_state.load(); }

private:
    CaptureOverlay() = default;
    CaptureOverlay(const CaptureOverlay&) = delete;
    CaptureOverlay& operator=(const CaptureOverlay&) = delete;

    /// 创建覆盖层窗口
    bool createOverlayWindow(HINSTANCE hInstance);

    /// 创建 D2D 资源
    bool createRenderResources();

    /// 释放渲染资源
    void releaseRenderResources();

    /// 冻结当前屏幕（全屏截图作为背景）
    void freezeScreen();

    /// 渲染帧
    void render();

    /// 绘制半透明遮罩（选区外区域变暗）
    void drawDimOverlay(const D2D1_RECT_F& selectionRect);

    /// 绘制选区边框和控制点
    void drawSelection(const D2D1_RECT_F& rect);

    /// 绘制尺寸标注信息
    void drawSizeInfo(const D2D1_RECT_F& rect);

    /// 绘制标注工具栏
    void drawToolbar(const D2D1_RECT_F& selectionRect);

    /// 绘制已完成标注预览
    void drawMarkupPreview(const D2D1_RECT_F& selectionRect);

    /// 绘制当前正在拖拽的标注预览
    void drawActiveMarkupPreview(const D2D1_RECT_F& selectionRect);

    /// 绘制十字准星
    void drawCrosshair(float x, float y);

    /// 检测光标下的窗口并返回其矩形
    RECT detectWindowUnderCursor(POINT cursorPos);

    /// 确认选区
    void confirmSelection();

    /// 当前选区矩形
    D2D1_RECT_F currentSelectionRect() const;

    /// 准备标注底图
    void prepareMarkupBase();

    /// 构建并命中测试工具栏按钮
    void rebuildToolbarButtons(const D2D1_RECT_F& selectionRect);
    ToolbarButton* hitTestToolbar(POINT point);
    void executeToolbarCommand(const ToolbarButton& button);

    /// 标注交互
    bool isPointInSelection(POINT point) const;
    cv::Point toMarkupPoint(POINT point) const;
    void beginMarkup(POINT point);
    void updateMarkup(POINT point);
    void finishMarkup(POINT point);

    /// 文本输入 (Text 工具): WM_CHAR 累积 → 回车/切换工具时提交
    void onTextChar(wchar_t ch);
    void commitTextEditing();
    void drawTextEditing(const D2D1_RECT_F& selectionRect);

    /// 窗口过程
    static LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 窗口
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    std::atomic<OverlayState> m_state{OverlayState::Idle};
    OverlayMode m_mode = OverlayMode::Screenshot;

    // 选区
    POINT m_dragStart{};
    POINT m_dragEnd{};
    POINT m_currentCursor{};
    bool m_dragging = false;
    RECT m_detectedWindow{};    // 光标悬停窗口的矩形

    // 冻结的屏幕图像
    cv::Mat m_frozenScreen;
    CaptureOptions m_options;

    // 标注
    MarkupEngine m_markup;
    MarkupTool m_currentTool = MarkupTool::Rectangle;
    MarkupColor m_currentColor = MarkupColor::Red();
    bool m_markupBaseReady = false;
    bool m_isMarking = false;
    POINT m_markupStart{};
    POINT m_markupEnd{};
    std::vector<cv::Point> m_penPoints;
    std::vector<ToolbarButton> m_toolbarButtons;

    // 二次编辑与拖拽
    uint32_t m_selectedElementId = 0;
    int m_resizingHandle = -1; // -1:移动, 0-7:缩放控制点
    bool m_isManipulating = false;
    POINT m_lastMousePos{};

    // 文本输入状态
    bool m_editingText = false;
    cv::Point m_textAnchor{};      // 文本在标注坐标系中的锚点
    POINT m_textScreenPos{};       // 文本在覆盖层坐标系中的位置 (用于绘制)
    std::wstring m_textBuffer;

    // 回调
    SelectionCallback m_callback;
    RecordSelectionCallback m_recordCallback;
    std::function<void(const CaptureRegion& region, const cv::Mat& cropped)> m_ocrCallback;

    // D2D 渲染
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_screenBitmap;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dimBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_infoBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_infoTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_crosshairBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_windowHighlightBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_infoTextFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textInputFormat;  // 文本工具: 左对齐大字号
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H
