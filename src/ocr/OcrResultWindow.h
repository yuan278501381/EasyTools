#pragma once
#include <windows.h>
#include <string>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace easy::ocr {

class OcrResultWindow {
public:
    static OcrResultWindow& instance() {
        static OcrResultWindow inst;
        return inst;
    }

    bool initialize();
    void cleanup();

    void showResult(const std::string& text);

private:
    OcrResultWindow() = default;
    ~OcrResultWindow() { cleanup(); }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool createResources();
    bool createTextResources(float scale);
    bool ensureSurface(int width, int height);
    void releaseSurface();
    void discardDeviceResources();
    void updatePlacement();
    void rebuildTextLayout();
    void updateHover(POINT point);
    void copyAll();
    void hide();
    void render();

    HWND m_hwnd = nullptr;
    HINSTANCE m_instance = nullptr;
    std::string m_text;
    std::wstring m_wideText;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_buttonFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_hintFormat;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_textLayout;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushMuted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtn;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtnHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushSuccess;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushError;

    HDC m_screenDc = nullptr;
    HDC m_memoryDc = nullptr;
    HBITMAP m_bitmap = nullptr;
    HGDIOBJ m_previousBitmap = nullptr;
    void* m_pixels = nullptr;
    int m_surfaceWidth = 0;
    int m_surfaceHeight = 0;

    float m_scrollY = 0.0f;
    float m_maxScroll = 0.0f;
    float m_scale = 1.0f;

    // UI Layout
    D2D1_RECT_F m_btnCopyRect;
    D2D1_RECT_F m_btnCloseRect;

    bool m_hoverCopy = false;
    bool m_hoverClose = false;
    bool m_trackingMouse = false;
    bool m_updatingPlacement = false;
    float m_currentAlpha = 0.0f;
    uint64_t m_showTime = 0;

    uint64_t m_copiedTime = 0;
    bool m_copySucceeded = false;
};

} // namespace easy::ocr
