#include "ScrollCaptureOverlay.h"
#include "core/logger/Logger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")

namespace easy::capture {

bool ScrollCaptureOverlay::initialize() {
    if (m_hwnd) return true;

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"EasyTools_ScrollCaptureOverlay";

    RegisterClassExW(&wcex);

    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcex.lpszClassName,
        L"Scroll Capture Overlay",
        WS_POPUP,
        screenX, screenY, screenW, screenH,
        nullptr, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) return false;

    // VERY IMPORTANT: Exclude from screen capture so it doesn't pollute the scroll capture frames!
    SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);

    if (!createResources()) return false;
    
    return true;
}

void ScrollCaptureOverlay::hide() {
    if (m_hwnd) {
        KillTimer(m_hwnd, 1);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_brushFlash.Reset();
    m_brushBorder.Reset();
    m_brushBg.Reset();
    m_d2dBitmap.Reset();
    m_renderTarget.Reset();
    m_d2dFactory.Reset();
}

bool ScrollCaptureOverlay::createResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) return false;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget))) return false;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushFlash);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.8f, 0.2f, 1.0f), &m_brushBorder);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.8f), &m_brushBg);

    return true;
}

void ScrollCaptureOverlay::show(RECT captureRect) {
    if (!initialize()) return;
    m_captureRect = captureRect;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stitched = cv::Mat();
        m_d2dBitmap.Reset();
        m_frameCount = 0;
        m_lastFlashTime = GetTickCount64();
    }

    UpdateLayeredWindow(m_hwnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, ULW_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNA);
    
    SetTimer(m_hwnd, 1, 16, nullptr);
    render();
}

void ScrollCaptureOverlay::updatePreview(const cv::Mat& stitched, int frameCount) {
    if (!m_hwnd) return;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!stitched.empty()) {
            cv::cvtColor(stitched, m_stitched, cv::COLOR_BGR2BGRA);
        }
        m_frameCount = frameCount;
        m_lastFlashTime = GetTickCount64();
        m_d2dBitmap.Reset(); // forces recreate in render
    }
}

void ScrollCaptureOverlay::render() {
    if (!m_hwnd || !m_renderTarget) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if (width <= 0 || height <= 0) return;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMemory = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; 
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hbmpOld = (HBITMAP)SelectObject(hdcMemory, hbmp);

    RECT bindRect = {0, 0, width, height};
    m_renderTarget->BindDC(hdcMemory, &bindRect);

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    uint64_t elapsed = GetTickCount64() - m_lastFlashTime;
    float flashOpacity = 0.0f;
    if (elapsed < 300) {
        flashOpacity = 1.0f - (elapsed / 300.0f);
    }

    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    D2D1_RECT_F cRect = D2D1::RectF(
        (float)(m_captureRect.left - screenX),
        (float)(m_captureRect.top - screenY),
        (float)(m_captureRect.right - screenX),
        (float)(m_captureRect.bottom - screenY)
    );

    // Draw border
    m_renderTarget->DrawRectangle(&cRect, m_brushBorder.Get(), 3.0f);

    // Draw Flash
    if (flashOpacity > 0.01f) {
        m_brushFlash->SetOpacity(flashOpacity * 0.4f);
        m_renderTarget->FillRectangle(&cRect, m_brushFlash.Get());
    }

    // Draw thumbnail preview on the right side of the capture rect
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stitched.empty()) {
        if (!m_d2dBitmap) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            m_renderTarget->CreateBitmap(
                D2D1::SizeU(m_stitched.cols, m_stitched.rows),
                m_stitched.data,
                m_stitched.step,
                &props,
                &m_d2dBitmap
            );
        }

        if (m_d2dBitmap) {
            float thumbWidth = 200.0f;
            float scale = thumbWidth / m_stitched.cols;
            float thumbHeight = m_stitched.rows * scale;
            
            float thumbX = cRect.right + 20.0f;
            if (thumbX + thumbWidth > width) {
                thumbX = cRect.left - thumbWidth - 20.0f; // try left side
            }
            float thumbY = cRect.top;

            D2D1_RECT_F tRect = D2D1::RectF(thumbX, thumbY, thumbX + thumbWidth, thumbY + thumbHeight);
            
            // Draw background for thumbnail
            D2D1_ROUNDED_RECT rRect = D2D1::RoundedRect(
                D2D1::RectF(tRect.left - 5, tRect.top - 5, tRect.right + 5, tRect.bottom + 5), 8.0f, 8.0f
            );
            m_renderTarget->FillRoundedRectangle(&rRect, m_brushBg.Get());
            
            // Draw the stitched image
            m_renderTarget->DrawBitmap(m_d2dBitmap.Get(), &tRect);
            
            // Draw thumbnail border
            m_renderTarget->DrawRoundedRectangle(&rRect, m_brushBorder.Get(), 2.0f);
        }
    }

    m_renderTarget->EndDraw();

    POINT ptSrc = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, hdcScreen, nullptr, &size, hdcMemory, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(hdcMemory, hbmpOld);
    DeleteObject(hbmp);
    DeleteDC(hdcMemory);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK ScrollCaptureOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& self = instance();
    if (msg == WM_TIMER && wParam == 1) {
        self.render();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace easy::capture
