#pragma once
#ifndef EASYTOOLS_CAPTURE_CAPTURERENDERER_H
#define EASYTOOLS_CAPTURE_CAPTURERENDERER_H

#include "capture/CaptureState.h"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

namespace easy::capture {

class CaptureRenderer {
public:
    bool initialize(HWND hwnd, CaptureState& state);
    void shutdown();
    /// Release HWND/render-target resources while retaining thread-safe,
    /// device-independent factories for the next capture.
    void releaseWindowResources();
    bool updateDpiScale(float scale);
    void applyThemeColors();
    
    void render(CaptureState& state);
    void invalidate() {
        m_needsRender = true;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    bool needsRender() const { return m_needsRender; }
    void clearNeedsRender() { m_needsRender = false; }
    bool updateScreenBitmap(const cv::Mat& image);
    
    void markMarkupDirty() { m_markupCacheDirty = true; m_needsRender = true; }
    void updateHistoryBitmap(CaptureState& state);

public:
    ID2D1HwndRenderTarget* getRenderTarget() const { return m_renderTarget.Get(); }
private:
    bool createRenderResources(CaptureState& state);

    void drawDimOverlay(const D2D1_RECT_F& selectionRect, CaptureState& state);
    void drawSelection(const D2D1_RECT_F& rect, CaptureState& state);
    void drawSizeInfo(const D2D1_RECT_F& rect, CaptureState& state);
    void drawToolbar(const D2D1_RECT_F& selectionRect, CaptureState& state);
    void drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough);
    void drawMarkupPreview(const D2D1_RECT_F& selectionRect, CaptureState& state);
    void drawActiveMarkupPreview(const D2D1_RECT_F& selectionRect, CaptureState& state);
    void drawDynamicMagnifier(CaptureState& state);
    void drawSelectionLoupe(float cx, float cy, CaptureState& state);
    void drawCrosshair(float x, float y);

    public:
    bool sampleScreenColor(int x, int y, int& r, int& g, int& b, CaptureState& state) const;

    HWND m_hwnd = nullptr;
    bool m_needsRender = true;
    bool m_markupCacheDirty = true;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_screenBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_markupCacheBitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_historyBitmap;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_dimBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_infoBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_infoTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_crosshairBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_windowHighlightBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_infoTextFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textInputFormat;
    float m_textScale = 0.0f;
};

} // namespace easy::capture
#endif // EASYTOOLS_CAPTURE_CAPTURERENDERER_H
