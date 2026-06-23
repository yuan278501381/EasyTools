#pragma once
#include <windows.h>
#include <string>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <mutex>

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
    void render();

    HWND m_hwnd = nullptr;
    std::mutex m_mutex;
    std::string m_text;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_textLayout;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtn;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBtnHover;
    
    float m_scrollY = 0.0f;
    float m_maxScroll = 0.0f;
    
    // UI Layout
    D2D1_RECT_F m_btnCopyRect;
    D2D1_RECT_F m_btnCloseRect;
    
    bool m_hoverCopy = false;
    bool m_hoverClose = false;
    
    uint64_t m_copiedTime = 0;
};

} // namespace easy::ocr
