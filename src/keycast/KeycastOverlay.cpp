#include "KeycastOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include <algorithm>
#include <vector>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

namespace easy::keycast {

namespace {
constexpr UINT_PTR ANIMATION_TIMER_ID = 1;
constexpr UINT START_ANIMATION_MESSAGE = WM_APP + 1;
constexpr int OVERLAY_WIDTH = 800;
constexpr int OVERLAY_HEIGHT = 180;
}

KeycastOverlay& KeycastOverlay::instance() {
    static KeycastOverlay inst;
    return inst;
}

bool KeycastOverlay::init() {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"EasyTools_KeycastOverlay";

    RegisterClassExW(&wcex);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    const int x = (screenW - OVERLAY_WIDTH) / 2;
    const int y = std::max(0, screenH - OVERLAY_HEIGHT - 48);
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcex.lpszClassName,
        L"EasyTools Keycast",
        WS_POPUP,
        x, y, OVERLAY_WIDTH, OVERLAY_HEIGHT,
        nullptr, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create KeycastOverlay window");
        return false;
    }

    if (!createResources()) {
        LOG_ERROR("Failed to create Keycast D2D resources");
        return false;
    }

    // 空闲时完全隐藏且不设定时器；只有实际显示按键的 1.8 秒内才渲染。
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void KeycastOverlay::cleanup() {
    if (m_hwnd) {
        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_brushText.Reset();
    m_brushBg.Reset();
    m_brushBorder.Reset();
    m_textFormat.Reset();
    m_dwriteFactory.Reset();
    m_renderTarget.Reset();
    m_d2dFactory.Reset();
}

bool KeycastOverlay::createResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) return false;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget))) return false;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory))) return false;

    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        48.0f,
        L"zh-cn",
        &m_textFormat
    ))) return false;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushText);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.74f), &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), &m_brushBorder);

    return true;
}

void KeycastOverlay::pushKey(const std::string& keyStr) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t now = GetTickCount64();
        if (m_rawLastKey == keyStr && (now - m_lastPushTime) < 2000) {
            m_repeatCount++;
            m_currentText = keyStr + " x" + std::to_string(m_repeatCount);
        } else {
            m_rawLastKey = keyStr;
            m_repeatCount = 1;
            m_currentText = keyStr;
        }

        m_lastPushTime = now;
        m_opacity = 0.0f;
        m_scale = 0.8f;
        m_animating = true;
    }
    PostMessageW(m_hwnd, START_ANIMATION_MESSAGE, 0, 0);
}

void KeycastOverlay::render() {
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

    std::string textToDraw;
    float currentOpacity = 0.0f;
    float currentScale = 1.0f;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        textToDraw = m_currentText;
        currentOpacity = m_opacity;
        currentScale = m_scale;
    }

    if (!textToDraw.empty() && currentOpacity > 0.01f) {
        // Measure text
        std::wstring wtext = easy::core::WinUtils::utf8ToWstring(textToDraw);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(
            wtext.c_str(), static_cast<UINT32>(wtext.length()),
            m_textFormat.Get(),
            10000.0f, 1000.0f,
            &layout
        );

        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);

        float paddingX = 40.0f;
        float paddingY = 20.0f;
        float boxW = metrics.width + paddingX * 2;
        float boxH = metrics.height + paddingY * 2;

        float centerX = width / 2.0f;
        float centerY = height / 2.0f;

        // Set transform for elastic scale
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(
            currentScale, currentScale,
            D2D1::Point2F(centerX, centerY)
        );
        m_renderTarget->SetTransform(transform);

        D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
            D2D1::RectF(centerX - boxW/2, centerY - boxH/2, centerX + boxW/2, centerY + boxH/2),
            16.0f, 16.0f
        );

        m_brushBg->SetOpacity(currentOpacity);
        m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());
        if (m_brushBorder) {
            m_brushBorder->SetOpacity(currentOpacity);
            m_renderTarget->DrawRoundedRectangle(&rrect, m_brushBorder.Get(), 1.0f);
        }

        m_brushBorder->SetOpacity(currentOpacity);
        m_renderTarget->DrawRoundedRectangle(&rrect, m_brushBorder.Get(), 2.0f);

        m_brushText->SetOpacity(currentOpacity);
        m_renderTarget->DrawTextLayout(
            D2D1::Point2F(centerX - metrics.width/2, centerY - metrics.height/2),
            layout.Get(),
            m_brushText.Get()
        );

        m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
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

LRESULT CALLBACK KeycastOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == START_ANIMATION_MESSAGE) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, ANIMATION_TIMER_ID, 16, nullptr);
        KeycastOverlay::instance().render();
        return 0;
    }
    if (msg == WM_TIMER && wParam == ANIMATION_TIMER_ID) {
        auto& self = KeycastOverlay::instance();
        bool finished = false;
        bool shouldRender = false;
        {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            if (!self.m_animating) {
                finished = true;
            } else {
                const uint64_t now = GetTickCount64();
                const uint64_t elapsed = now - self.m_lastPushTime;

                if (elapsed < 150) {
                    const float t = elapsed / 150.0f;
                    self.m_opacity = t;
                    const float p = 0.3f;
                    self.m_scale = powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * 3.14159f) / p) + 1.0f;
                    shouldRender = true;
                } else if (elapsed < 1500) {
                    self.m_opacity = 1.0f;
                    self.m_scale = 1.0f;
                    shouldRender = true;
                } else if (elapsed < 1800) {
                    const float t = (elapsed - 1500) / 300.0f;
                    self.m_opacity = 1.0f - t;
                    self.m_scale = 1.0f + (0.1f * t);
                    shouldRender = true;
                } else {
                    self.m_opacity = 0.0f;
                    self.m_animating = false;
                    self.m_currentText.clear();
                    finished = true;
                }
            }
        }
        if (finished) {
            KillTimer(hwnd, ANIMATION_TIMER_ID);
            ShowWindow(hwnd, SW_HIDE);
        } else if (shouldRender) {
            self.render();
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace easy::keycast
