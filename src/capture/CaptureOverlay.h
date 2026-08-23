#pragma once
#ifndef EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H
#define EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H

#include "capture/ScreenCapture.h"
#include "capture/CaptureState.h"
#include "capture/CaptureRenderer.h"
#include "capture/CaptureInput.h"
#include <windows.h>
#include <memory>

namespace easy::capture {

class CaptureOverlay {
public:
    static CaptureOverlay& instance();

    bool initialize(HINSTANCE hInstance);
    void shutdown();
    void startSelection(const CaptureOptions& options, OverlayMode mode = OverlayMode::Screenshot);
    /// 启动已有贴图的二次标注编辑
    void startEditPinned(const cv::Mat& image, const CaptureRegion& region,
                         std::function<void(const cv::Mat& newImage)> onFinished);
    void cancel();
    void reloadThemeColors() { m_renderer.applyThemeColors(); m_renderer.invalidate(); }

    void setCallback(SelectionCallback callback) { m_state.callback = std::move(callback); }
    void setClosedCallback(std::function<void()> callback) { m_closedCallback = std::move(callback); }
    void setRecordCallback(RecordSelectionCallback callback) { m_state.recordCallback = std::move(callback); }
    void setOcrCallback(std::function<void(const CaptureRegion& region, const cv::Mat& cropped)> callback) { m_state.ocrCallback = std::move(callback); }

    OverlayState state() const { return m_state.state.load(); }
    OverlayMode mode() const { return m_state.mode; }
    void setShortcutHintsEnabled(bool enabled);

private:
    CaptureOverlay() = default;
    ~CaptureOverlay() = default;
    CaptureOverlay(const CaptureOverlay&) = delete;
    CaptureOverlay& operator=(const CaptureOverlay&) = delete;

    bool createOverlayWindow(HINSTANCE hInstance);
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;

    CaptureState m_state;
    CaptureRenderer m_renderer;
    CaptureInput m_input;
    std::function<void()> m_closedCallback;
    HBITMAP m_frozenBitmap = nullptr;

    void realCancel();
    void confirmSelection(CaptureCompletion completion = {});
    bool freezeScreen();
    void releaseFrozenSurface();
};

} // namespace easy::capture
#endif // EASYTOOLS_CAPTURE_CAPTUREOVERLAY_H
