#include "OcrResultWindow.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include <algorithm>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

namespace easy::ocr {

bool OcrResultWindow::initialize() {
    if (m_hwnd) return true;

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"EasyTools_OcrResult";

    RegisterClassExW(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    int w = 600;
    int h = 400;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        wcex.lpszClassName,
        L"OCR Result",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) return false;

    if (!createResources()) return false;
    
    return true;
}

void OcrResultWindow::cleanup() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_textLayout.Reset();
    m_textFormat.Reset();
    m_brushBg.Reset();
    m_brushText.Reset();
    m_brushBtn.Reset();
    m_brushBtnHover.Reset();
    m_dwriteFactory.Reset();
    m_renderTarget.Reset();
    m_d2dFactory.Reset();
}

bool OcrResultWindow::createResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) return false;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget))) return false;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory))) return false;

    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        18.0f, L"zh-cn", &m_textFormat
    ))) return false;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.85f), &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f), &m_brushText);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.5f, 0.9f, 0.8f), &m_brushBtn);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.6f, 1.0f, 1.0f), &m_brushBtnHover);

    return true;
}

void OcrResultWindow::showResult(const std::string& text) {
    if (!m_hwnd && !initialize()) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_text = text;
    m_scrollY = 0;
    m_maxScroll = 0;
    m_copiedTime = 0;

    std::wstring wtext = easy::core::WinUtils::utf8ToWstring(m_text);
    m_textLayout.Reset();
    
    if (!wtext.empty()) {
        m_dwriteFactory->CreateTextLayout(
            wtext.c_str(), static_cast<UINT32>(wtext.length()),
            m_textFormat.Get(),
            560.0f, 10000.0f,
            &m_textLayout
        );
        
        DWRITE_TEXT_METRICS metrics;
        m_textLayout->GetMetrics(&metrics);
        m_maxScroll = (std::max)(0.0f, metrics.height - 300.0f);
    }

    UpdateLayeredWindow(m_hwnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, ULW_ALPHA);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    
    // Auto timer to refresh if copying
    SetTimer(m_hwnd, 1, 16, nullptr);
    render();
}

void OcrResultWindow::render() {
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

    std::lock_guard<std::mutex> lock(m_mutex);

    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
        D2D1::RectF(0, 0, (float)width, (float)height), 12.0f, 12.0f
    );
    m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());
    
    m_renderTarget->DrawRoundedRectangle(&rrect, m_brushText.Get(), 1.0f); // subtle border
    
    if (m_textLayout) {
        // clipping for text
        m_renderTarget->PushAxisAlignedClip(D2D1::RectF(20, 20, width - 20.0f, height - 60.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_renderTarget->DrawTextLayout(
            D2D1::Point2F(20.0f, 20.0f - m_scrollY),
            m_textLayout.Get(),
            m_brushText.Get()
        );
        m_renderTarget->PopAxisAlignedClip();
    }

    // Buttons
    m_btnCopyRect = D2D1::RectF(width - 240.0f, height - 50.0f, width - 130.0f, height - 15.0f);
    m_btnCloseRect = D2D1::RectF(width - 120.0f, height - 50.0f, width - 20.0f, height - 15.0f);

    D2D1_ROUNDED_RECT rCopy = D2D1::RoundedRect(m_btnCopyRect, 6.0f, 6.0f);
    m_renderTarget->FillRoundedRectangle(&rCopy, m_hoverCopy ? m_brushBtnHover.Get() : m_brushBtn.Get());
    
    D2D1_ROUNDED_RECT rClose = D2D1::RoundedRect(m_btnCloseRect, 6.0f, 6.0f);
    m_renderTarget->FillRoundedRectangle(&rClose, m_hoverClose ? m_brushBtnHover.Get() : m_brushBtn.Get());

    // Button Texts
    Microsoft::WRL::ComPtr<IDWriteTextLayout> tlCopy;
    std::wstring copyStr = (GetTickCount64() - m_copiedTime < 2000) ? L"Ѹ" : L"һ";
    m_dwriteFactory->CreateTextLayout(copyStr.c_str(), (UINT32)copyStr.length(), m_textFormat.Get(), 110, 35, &tlCopy);
    tlCopy->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_renderTarget->DrawTextLayout(D2D1::Point2F(m_btnCopyRect.left, m_btnCopyRect.top + 5), tlCopy.Get(), m_brushText.Get());

    Microsoft::WRL::ComPtr<IDWriteTextLayout> tlClose;
    std::wstring closeStr = L"ر";
    m_dwriteFactory->CreateTextLayout(closeStr.c_str(), (UINT32)closeStr.length(), m_textFormat.Get(), 100, 35, &tlClose);
    tlClose->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_renderTarget->DrawTextLayout(D2D1::Point2F(m_btnCloseRect.left, m_btnCloseRect.top + 5), tlClose.Get(), m_brushText.Get());

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

LRESULT CALLBACK OcrResultWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& self = instance();
    
    switch (msg) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        
        bool hoverCopy = (x >= self.m_btnCopyRect.left && x <= self.m_btnCopyRect.right && 
                          y >= self.m_btnCopyRect.top && y <= self.m_btnCopyRect.bottom);
        bool hoverClose = (x >= self.m_btnCloseRect.left && x <= self.m_btnCloseRect.right && 
                           y >= self.m_btnCloseRect.top && y <= self.m_btnCloseRect.bottom);
                           
        if (hoverCopy != self.m_hoverCopy || hoverClose != self.m_hoverClose) {
            self.m_hoverCopy = hoverCopy;
            self.m_hoverClose = hoverClose;
            self.render();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (self.m_hoverCopy) {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            easy::core::WinUtils::copyToClipboard(self.m_text);
            self.m_copiedTime = GetTickCount64();
            self.render();
        } else if (self.m_hoverClose) {
            ShowWindow(hwnd, SW_HIDE);
            KillTimer(hwnd, 1);
        } else {
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); // Allow drag
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        self.m_scrollY -= (zDelta / 120.0f) * 30.0f; // Scroll speed
        self.m_scrollY = (std::max)(0.0f, (std::min)(self.m_scrollY, self.m_maxScroll));
        self.render();
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(hwnd, SW_HIDE);
            KillTimer(hwnd, 1);
        }
        return 0;
    case WM_TIMER:
        if (wParam == 1 && self.m_copiedTime > 0) {
            if (GetTickCount64() - self.m_copiedTime > 2000) {
                self.m_copiedTime = 0;
                self.render();
            }
        }
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace easy::ocr
