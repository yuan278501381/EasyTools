#include "KeycastOverlay.h"
#include "KeycastStyle.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"
#include "gesture/GestureInputPolicy.h"
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

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcex.lpszClassName,
        L"EasyTools Keycast",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create KeycastOverlay window");
        return false;
    }

    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除按键回显: error={}", GetLastError());
    }

    m_autoBypassFullscreen.store(easy::core::ConfigManager::instance().get<bool>("/keycast/autoBypassFullscreen", true));

    // Graphics resources stay lazy until the first keystroke so a disabled or
    // unused plugin does not allocate a large high-DPI backing bitmap.
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void KeycastOverlay::cleanup() {
    if (m_hwnd) {
        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    discardResources();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void KeycastOverlay::discardResources() {
    m_brushText.Reset();
    m_brushBg.Reset();
    m_brushBorder.Reset();
    m_brushBadgeBg.Reset();
    m_renderTarget.Reset();
    m_textFormat.Reset();
    if (m_memoryDC && m_oldBitmap) SelectObject(m_memoryDC, m_oldBitmap);
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) DeleteObject(m_memoryBitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
    m_memoryBitmap = nullptr;
    m_memoryDC = nullptr;
}

bool KeycastOverlay::createResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_textFormat &&
        m_brushText && m_brushBg && m_brushBorder && m_brushBadgeBg) {
        return true;
    }
    discardResources();
    if (!m_d2dFactory && FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
        return false;
    }

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget))) return false;
    m_renderTarget->SetDpi(96.0f, 96.0f);
    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // 创建常驻 32 位 DIB 内存 DC
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    m_memoryDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    m_memoryBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memoryDC || !m_memoryBitmap || !pBits) {
        discardResources();
        return false;
    }
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memoryDC, m_memoryBitmap));

    RECT bindRect = {0, 0, m_width, m_height};
    m_renderTarget->BindDC(m_memoryDC, &bindRect);

    if (!m_dwriteFactory && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory))) {
        discardResources();
        return false;
    }

    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        KeycastStyle::BaseFontSize * m_dpiScale,
        L"zh-cn",
        &m_textFormat
    ))) {
        discardResources();
        return false;
    }

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 浮动文本 Toast 提示卡片：粗圆角矩形纯白色边框 + 曜石深黑底 + 纯白高亮按键文字
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushText);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.08f, 0.11f, 0.94f), &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushBorder);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushBadgeBg);

    if (!m_brushText || !m_brushBg || !m_brushBorder || !m_brushBadgeBg) {
        discardResources();
        return false;
    }
    return true;
}

bool KeycastOverlay::updatePlacement() {
    if (!m_hwnd) return false;
    if (m_updatingPlacement) return true;
    m_updatingPlacement = true;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const float newScale = easy::core::dpi::scaleForMonitor(monitor);
    const int newWidth = easy::core::dpi::scaleMetric(KeycastStyle::BaseWidth, newScale);
    const int newHeight = easy::core::dpi::scaleMetric(KeycastStyle::BaseHeight, newScale);
    if (std::abs(newScale - m_dpiScale) >= 0.01f ||
        newWidth != m_width || newHeight != m_height) {
        m_dpiScale = newScale;
        m_width = newWidth;
        m_height = newHeight;
        discardResources();
    }

    const RECT work = easy::core::dpi::workArea(monitor);
    const int x = work.left + ((work.right - work.left) - m_width) / 2;
    const int y = std::max(work.top, work.bottom - m_height -
        easy::core::dpi::scaleMetric(KeycastStyle::BaseBottomMargin, m_dpiScale));
    const bool positioned = SetWindowPos(
        m_hwnd, HWND_TOPMOST, x, y, m_width, m_height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
    m_updatingPlacement = false;
    return positioned;
}

void KeycastOverlay::pushKey(const std::string& keyStr) {
    if (keyStr.empty()) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t now = GetTickCount64();
        if (m_rawLastKey == keyStr && (now - m_lastPushTime) < 1800) {
            m_repeatCount++;
            m_currentText = keyStr + "  ×" + std::to_string(m_repeatCount);
        } else {
            m_rawLastKey = keyStr;
            m_repeatCount = 1;
            m_currentText = keyStr;
        }

        m_lastPushTime = now;
        m_opacity = 0.15f;
        m_scale = 0.92f;
        m_animating = true;
    }
    if (m_hwnd) PostMessageW(m_hwnd, START_ANIMATION_MESSAGE, 0, 0);
}

void KeycastOverlay::render() {
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // 真正完全透明背景

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
        std::wstring wtext = easy::core::WinUtils::utf8ToWstring(textToDraw);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(
            wtext.c_str(), static_cast<UINT32>(wtext.length()),
            m_textFormat.Get(),
            10000.0f, 1000.0f,
            &layout
        );

        if (layout) {
            // The shared text format is centered for regular DrawText calls.
            // A 10,000 px measurement layout would therefore place glyphs near
            // x=5,000 and outside this window when drawn at the measured origin.
            // Measure from a leading-aligned layout, then center that measured
            // block explicitly below.
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DWRITE_TEXT_METRICS metrics;
            if (FAILED(layout->GetMetrics(&metrics))) return;

            float paddingX = 38.0f * m_dpiScale;
            float paddingY = 16.0f * m_dpiScale;
            float boxW = (std::max)(metrics.width + paddingX * 2.0f, 136.0f * m_dpiScale);
            float boxH = (std::max)(metrics.height + paddingY * 2.0f, 58.0f * m_dpiScale);

            float centerX = m_width / 2.0f;
            float centerY = m_height / 2.0f;

            // 弹性微缩放
            D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(
                currentScale, currentScale,
                D2D1::Point2F(centerX, centerY)
            );
            m_renderTarget->SetTransform(transform);

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f, centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                16.0f * m_dpiScale, 16.0f * m_dpiScale
            );

            // 1. 曜石深黑微透卡片背景
            m_brushBg->SetOpacity(0.94f * currentOpacity);
            m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());

            // 2. 粗圆角纯白色边框 (2.6px 纯白高亮粗边框)
            if (m_brushBorder) {
                m_brushBorder->SetOpacity(1.0f * currentOpacity);
                m_renderTarget->DrawRoundedRectangle(
                    &rrect, m_brushBorder.Get(), 2.6f * m_dpiScale);
            }

            // 3. 高质量纯白文字排版
            m_brushText->SetOpacity(1.0f * currentOpacity);
            m_renderTarget->DrawTextLayout(
                D2D1::Point2F(centerX - metrics.width / 2.0f, centerY - metrics.height / 2.0f),
                layout.Get(),
                m_brushText.Get()
            );

            m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        }
    }

    if (FAILED(m_renderTarget->EndDraw())) {
        discardResources();
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = {0, 0};
    SIZE size = {m_width, m_height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, hdcScreen, nullptr, &size, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK KeycastOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == START_ANIMATION_MESSAGE) {
        auto& self = KeycastOverlay::instance();
        if (!self.updatePlacement() || !self.createResources()) {
            LOG_ERROR("按键回显显示失败: 无法创建高 DPI 渲染资源");
            return 0;
        }
        self.render();  // Clear stale pixels before the layered window is shown.
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, ANIMATION_TIMER_ID, 16, nullptr);
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

                if (elapsed < 140) {
                    const float t = elapsed / 140.0f;
                    self.m_opacity = 0.2f + 0.8f * t;
                    self.m_scale = 0.92f + 0.08f * std::sin(t * 1.57079f);
                    shouldRender = true;
                } else if (elapsed < 1400) {
                    if (self.m_opacity != 1.0f || self.m_scale != 1.0f) {
                        self.m_opacity = 1.0f;
                        self.m_scale = 1.0f;
                        shouldRender = true;
                    }
                } else if (elapsed < 1750) {
                    const float t = (elapsed - 1400) / 350.0f;
                    self.m_opacity = 1.0f - t;
                    self.m_scale = 1.0f + 0.04f * t;
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

    if ((msg == WM_DPICHANGED || msg == WM_DISPLAYCHANGE) &&
        IsWindowVisible(hwnd)) {
        auto& self = KeycastOverlay::instance();
        if (self.updatePlacement() && self.createResources()) self.render();
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace easy::keycast
