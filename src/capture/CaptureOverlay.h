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

    /// 请求重绘（脏标记驱动，避免空转全屏重绘）
    void invalidate() { m_needsRender = true; }

    /// 标记标注内容已变更（合成缓存失效 + 请求重绘）
    void markMarkupDirty() { m_markupCacheDirty = true; m_needsRender = true; }

    /// 绘制半透明遮罩（选区外区域变暗）
    void drawDimOverlay(const D2D1_RECT_F& selectionRect);

    /// 绘制选区边框和控制点
    void drawSelection(const D2D1_RECT_F& rect);

    /// 绘制尺寸标注信息
    void drawSizeInfo(const D2D1_RECT_F& rect);

    /// 绘制标注工具栏
    void drawToolbar(const D2D1_RECT_F& selectionRect);

    /// 绘制磨砂玻璃面板（深色蒙版 + 顶部高光 + 边框）。
    /// seeThrough=true: 圆角裁剪透出未暗化的背后画面（图层，较重，用于工具栏等主面板）；
    /// false: 直接半透明叠加在当前帧上（无图层，廉价，用于尺寸/提示等小标签）。
    void drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough = true);

    /// 绘制已完成标注预览
    void drawMarkupPreview(const D2D1_RECT_F& selectionRect);

    /// 绘制当前正在拖拽的标注预览
    void drawActiveMarkupPreview(const D2D1_RECT_F& selectionRect);

    /// 绘制实时的动态放大镜预览
    void drawDynamicMagnifier();

    /// 选区阶段的像素级取色放大镜（放大窗 + 坐标 + RGB/HEX）
    void drawSelectionLoupe(float cx, float cy);

    /// 读取冻结屏幕在 (x,y) 处的像素颜色（覆盖层坐标 == 冻结图坐标）
    bool sampleScreenColor(int x, int y, int& r, int& g, int& b) const;

    /// 绘制十字准星
    void drawCrosshair(float x, float y);

    /// 检测光标下的窗口并返回其矩形
    RECT detectWindowUnderCursor(POINT cursorPos);

    /// 确认选区
    void confirmSelection();

    /// 当前选区矩形
    D2D1_RECT_F currentSelectionRect() const;

    /// 准备标注底图（首次，elementCount==0 时）
    void prepareMarkupBase();

    /// 按当前选区重裁标注底图，但保留已有标注（用于带标注的选区二次调整）
    void rebuildMarkupBase();

    /// 构建并命中测试工具栏按钮
    void rebuildToolbarButtons(const D2D1_RECT_F& selectionRect);
    ToolbarButton* hitTestToolbar(POINT point);
    void executeToolbarCommand(const ToolbarButton& button);

    /// 选区二次调整：命中选区控制点/边框（仅在尚无标注时启用）
    HitArea hitTestSelectionBox(POINT point) const;
    /// 应用选区缩放/移动
    void adjustSelection(HitArea handle, int dx, int dy);
    /// 切换当前标注工具（快捷键/工具栏共用）
    void setCurrentTool(MarkupTool tool);
    /// 根据光标位置更新鼠标指针外观
    void updateHoverCursor(POINT point);

    /// 标注交互
    bool isPointInSelection(POINT point) const;
    cv::Point toMarkupPoint(POINT point) const;
    void beginMarkup(POINT point);
    void updateMarkup(POINT point);
    void finishMarkup(POINT point);



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
    MarkupElement* m_activeElement = nullptr;
    HitArea m_dragHandle = HitArea::None;
    bool m_isManipulating = false;
    POINT m_lastMousePos{};

    // 选区二次调整（缩放/移动）
    bool m_isAdjustingSelection = false;
    HitArea m_selAdjustHandle = HitArea::None;
    POINT m_selAdjustLast{};

    // 渲染脏标记（按需重绘，避免空转）
    bool m_needsRender = true;
    // 标注合成缓存（仅在标注变化时重建 D2D 位图）
    bool m_markupCacheDirty = true;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_markupCacheBitmap;

    // 动态放大镜
    int m_dynamicMagnifierRadius = 60;
    float m_dynamicMagnifierScale = 2.0f;

    // 取色放大镜：复制成功提示的截止 tick（GetTickCount，0 表示无提示）
    DWORD m_loupeToastUntil = 0;
    DWORD m_showTimestamp = 0;
    bool m_isFadingOut = false;
    DWORD m_fadeOutStart = 0;
    void realCancel();

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
