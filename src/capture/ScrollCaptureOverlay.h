#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <opencv2/core.hpp>
#include <mutex>
#include <atomic>

namespace easy::capture {

class ScrollCaptureOverlay {
public:
    static ScrollCaptureOverlay& instance() {
        static ScrollCaptureOverlay inst;
        return inst;
    }

    bool initialize();
    void show(RECT captureRect);
    void hide();
    
    // Updates the preview and triggers a flash effect
    void updatePreview(const cv::Mat& stitched, int frameCount);

private:
    ScrollCaptureOverlay() = default;
    ~ScrollCaptureOverlay() { hide(); }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    bool createResources();
    void render();

    HWND m_hwnd = nullptr;
    RECT m_captureRect = {};
    
    std::mutex m_mutex;
    cv::Mat m_preview;
    bool m_previewDirty = false;
    int m_frameCount = 0;
    float m_dpiScale = 1.0f;
    
    uint64_t m_lastFlashTime = 0;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFlash;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_d2dBitmap;
};

} // namespace easy::capture
