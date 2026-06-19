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

namespace easy::capture {

/// 覆盖层状态
enum class OverlayState {
    Idle,           // 空闲
    Selecting,      // 正在拖拽选区
    Selected,       // 选区完成，等待确认/标注
    Marking,        // 正在使用标注工具
};

/// 选区确认回调
using SelectionCallback = std::function<void(const CaptureRegion& region, const cv::Mat& markedImage)>;

class CaptureOverlay {
public:
    static CaptureOverlay& instance();

    /// 初始化覆盖层资源
    bool initialize(HINSTANCE hInstance);

    /// 关闭
    void shutdown();

    /// 进入截图选区模式（冻结屏幕 + 显示覆盖层）
    void startSelection(const CaptureOptions& options);

    /// 取消截图
    void cancel();

    /// 设置截图完成回调
    void setCallback(SelectionCallback callback) { m_callback = std::move(callback); }

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

    /// 绘制十字准星
    void drawCrosshair(float x, float y);

    /// 检测光标下的窗口并返回其矩形
    RECT detectWindowUnderCursor(POINT cursorPos);

    /// 确认选区
    void confirmSelection();

    /// 窗口过程
    static LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 窗口
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    std::atomic<OverlayState> m_state{OverlayState::Idle};

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

    // 回调
    SelectionCallback m_callback;

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
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H
