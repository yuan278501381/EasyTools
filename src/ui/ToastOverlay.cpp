#include "ui/ToastOverlay.h"
#include "ui/ToastStyle.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include "core/accessibility/OverlayAnnouncement.h"
#include "core/accessibility/OverlayUiaProvider.h"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace easy::ui {

static constexpr const wchar_t* TOAST_CLASS = L"EasyTools_ToastOverlay";
static constexpr UINT_PTR TIMER_ID = 1;
static constexpr UINT_PTR FADE_TIMER_ID = 2;
static constexpr UINT_PTR FADE_IN_TIMER_ID = 3;
static constexpr int HIDE_TIMEOUT_MS = 2000;
static constexpr int FADE_INTERVAL_MS = 16;

ToastOverlay& ToastOverlay::instance() {
    static ToastOverlay inst;
    return inst;
}

bool ToastOverlay::initialize(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = TOAST_CLASS;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TOAST_CLASS, L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除通知窗口: error={}", GetLastError());
    }

    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())))) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_d2dFactory.Reset();
        m_dwriteFactory.Reset();
        return false;
    }

    updatePlacement();
    ShowWindow(m_hwnd, SW_HIDE);
    LOG_INFO("系统通知 Overlay 初始化完成");
    return true;
}

void ToastOverlay::shutdown() {
    discardResources();
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_ID);
        KillTimer(m_hwnd, FADE_TIMER_ID);
        KillTimer(m_hwnd, FADE_IN_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void ToastOverlay::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_hwnd) {
        KillTimer(m_hwnd, TIMER_ID);
        KillTimer(m_hwnd, FADE_TIMER_ID);
        KillTimer(m_hwnd, FADE_IN_TIMER_ID);
        ShowWindow(m_hwnd, SW_HIDE);
        easy::core::accessibility::hideOverlay(m_hwnd);
        std::lock_guard lock(m_mutex);
        m_opacity = 0.0f;
        m_fadingIn = false;
        m_fadingOut = false;
    }
}

bool ToastOverlay::isEnabled() const {
    return m_enabled;
}

void ToastOverlay::showToast(const std::string& text) {
    if (!m_enabled || !m_hwnd || text.empty()) return;

    updatePlacement();

    {
        std::lock_guard lock(m_mutex);
        m_displayText = text;
        if (!m_fadingOut && m_opacity > 0.0f && !m_fadingIn) {
            // Already fully visible, just refresh timer
            m_opacity = 1.0f;
            m_animScale = 1.0f;
        } else {
            // Start fresh animation
            m_opacity = 0.0f;
            m_animScale = 0.9f;
            m_fadingIn = true;
        }
        m_fadingOut = false;
    }

    KillTimer(m_hwnd, TIMER_ID);
    KillTimer(m_hwnd, FADE_TIMER_ID);
    KillTimer(m_hwnd, FADE_IN_TIMER_ID);

    render();  // Replace retained layered-window pixels before showing again.
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    easy::core::accessibility::announceOverlay(
        m_hwnd, easy::core::WinUtils::utf8ToWstring(text));
    
    if (m_fadingIn) {
        SetTimer(m_hwnd, FADE_IN_TIMER_ID, FADE_INTERVAL_MS, nullptr);
    } else {
        render();
        SetTimer(m_hwnd, TIMER_ID, HIDE_TIMEOUT_MS, nullptr);
    }
}

bool ToastOverlay::createResources() {
    if (!m_renderTarget && m_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget)) ||
            !m_renderTarget) {
            return false;
        }
        m_renderTarget->SetDpi(96.0f, 96.0f);

        if (m_renderTarget) {
            // 背景: 圆角深色半透明
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.16f, 0.92f), &m_bgBrush);
            // 边框: 白色加粗，启动通知在浅色/深色桌面上都够清楚
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_strokeBrush);
            // 文字: 白色
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_textBrush);
        }

        if (m_dwriteFactory && !m_textFormat) {
            const HRESULT hr = m_dwriteFactory->CreateTextFormat(
                L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                ToastStyle::BaseFontSize * m_dpiScale, L"zh-cn", &m_textFormat
            );
            if (FAILED(hr) || !m_textFormat) return false;
            m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    if (!m_bgBrush || !m_strokeBrush || !m_textBrush) {
        discardResources();
        return false;
    }
    return m_renderTarget && m_textFormat;
}

void ToastOverlay::discardResources() {
    m_textBrush.Reset();
    m_strokeBrush.Reset();
    m_bgBrush.Reset();
    m_textFormat.Reset();
    m_renderTarget.Reset();
    releaseSurface();
}

void ToastOverlay::releaseSurface() {
    if (m_memoryDC && m_oldBitmap) {
        SelectObject(m_memoryDC, m_oldBitmap);
    }
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) DeleteObject(m_memoryBitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
    m_memoryBitmap = nullptr;
    m_memoryDC = nullptr;
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
}

bool ToastOverlay::ensureSurface(int width, int height) {
    if (m_memoryDC && m_memoryBitmap &&
        m_surfaceWidth == width && m_surfaceHeight == height) {
        return true;
    }
    releaseSurface();
    if (width <= 0 || height <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        return false;
    }

    m_memoryDC = memory;
    m_memoryBitmap = bitmap;
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(memory, bitmap));
    m_surfaceWidth = width;
    m_surfaceHeight = height;
    return true;
}

void ToastOverlay::updatePlacement() {
    if (!m_hwnd || m_updatingPlacement) return;
    m_updatingPlacement = true;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const float newScale = easy::core::dpi::scaleForMonitor(monitor);
    if (std::abs(newScale - m_dpiScale) >= 0.01f) {
        m_dpiScale = newScale;
        discardResources();
    }
    const RECT work = easy::core::dpi::workArea(monitor);
    const int width = easy::core::dpi::scaleMetric(ToastStyle::BaseWidth, m_dpiScale);
    const int height = easy::core::dpi::scaleMetric(ToastStyle::BaseHeight, m_dpiScale);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = ToastStyle::originYForWorkArea(
        work.top, work.bottom, height,
        easy::core::dpi::scaleMetric(ToastStyle::BaseBottomMargin, m_dpiScale));
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    m_updatingPlacement = false;
}

void ToastOverlay::render() {
    if (!m_hwnd) return;
    if (!createResources()) return;

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if (!ensureSurface(width, height)) return;

    RECT clientRc = {0, 0, width, height};
    if (FAILED(m_renderTarget->BindDC(m_memoryDC, &clientRc))) return;
    m_renderTarget->SetDpi(96.0f, 96.0f);

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    std::string text;
    float alpha;
    float scaleAnim;
    {
        std::lock_guard lock(m_mutex);
        text = m_displayText;
        alpha = m_opacity;
        scaleAnim = m_animScale;
    }

    if (!text.empty() && alpha > 0.0f) {
        std::wstring wText = easy::core::WinUtils::utf8ToWstring(text);
        
        const float cx = width / 2.0f;
        const float cy = height / 2.0f;
        const float margin = 8.0f * m_dpiScale;
        float textWidth = static_cast<float>(wText.length() * 20.0f + 60.0f) *
                          m_dpiScale;
        textWidth = std::min(textWidth, std::max(1.0f, width - 2.0f * margin));
        const float rectHeight = 60.0f * m_dpiScale;

        // 应用进场缩放动画
        D2D1::Matrix3x2F transform = D2D1::Matrix3x2F::Scale(scaleAnim, scaleAnim, D2D1::Point2F(cx, cy));
        m_renderTarget->SetTransform(transform);
        
        D2D1_RECT_F bgRect = D2D1::RectF(cx - textWidth/2, cy - rectHeight/2, cx + textWidth/2, cy + rectHeight/2);
        
        m_bgBrush->SetOpacity(alpha);
        // 绘制圆角背景
        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
            bgRect, 8.0f * m_dpiScale, 8.0f * m_dpiScale);
        m_renderTarget->FillRoundedRectangle(roundedRect, m_bgBrush.Get());
        m_strokeBrush->SetOpacity(alpha);
        m_renderTarget->DrawRoundedRectangle(
            roundedRect, m_strokeBrush.Get(), ToastStyle::StrokeWidth * m_dpiScale);

        m_textBrush->SetOpacity(alpha);
        m_renderTarget->DrawTextW(
            wText.c_str(), static_cast<UINT32>(wText.length()), m_textFormat.Get(),
            D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)),
            m_textBrush.Get()
        );
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (FAILED(hr)) {
        discardResources();
        return;
    }

    POINT ptSrc = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    HDC screen = GetDC(nullptr);
    if (screen) {
        UpdateLayeredWindow(
            m_hwnd, screen, nullptr, &size, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
}

LRESULT CALLBACK ToastOverlay::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ToastOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // 窗口过程兜底：任何 std 异常都不得逃逸到 Win32 派发层（否则 std::terminate 崩溃）。
    try {
        switch (msg) {
            case WM_GETOBJECT:
                return easy::core::accessibility::respondToOverlayUiaGetObject(
                    hwnd, wParam, lParam,
                    {L"EasyTools.Toast", L"EasyTools notification",
                     easy::core::accessibility::OverlayUiaRole::Status, true});
            case WM_TIMER: {
                if (self) {
                    if (wParam == TIMER_ID) {
                        KillTimer(hwnd, TIMER_ID);
                        self->m_fadingOut = true;
                        SetTimer(hwnd, FADE_TIMER_ID, FADE_INTERVAL_MS, nullptr);
                    } else if (wParam == FADE_TIMER_ID) {
                        // 关键修复：先在锁内更新动画状态并取快照，释放锁后再 render()。
                        // render() 内部会再次锁 m_mutex；若在持锁状态下调用，会递归锁非递归
                        // std::mutex，MSVC 抛 "resource deadlock would occur" 并崩溃。
                        bool hide = false;
                        {
                            std::lock_guard lock(self->m_mutex);
                            self->m_opacity -= 0.05f;
                            self->m_animScale += 0.01f; // fade out with slight scale up
                            if (self->m_opacity <= 0.0f) {
                                self->m_opacity = 0.0f;
                                hide = true;
                            }
                        }
                        if (hide) {
                            KillTimer(hwnd, FADE_TIMER_ID);
                            ShowWindow(hwnd, SW_HIDE);
                            easy::core::accessibility::hideOverlay(hwnd);
                        } else {
                            self->render();
                        }
                    } else if (wParam == FADE_IN_TIMER_ID) {
                        bool done = false;
                        {
                            std::lock_guard lock(self->m_mutex);
                            self->m_opacity += 0.1f;
                            self->m_animScale += 0.01f;
                            if (self->m_opacity >= 1.0f) {
                                self->m_opacity = 1.0f;
                                self->m_animScale = 1.0f;
                                self->m_fadingIn = false;
                                done = true;
                            }
                        }
                        if (done) {
                            KillTimer(hwnd, FADE_IN_TIMER_ID);
                            SetTimer(hwnd, TIMER_ID, HIDE_TIMEOUT_MS, nullptr);
                        }
                        self->render();
                    }
                }
                return 0;
            }
            case WM_DPICHANGED:
            case WM_DISPLAYCHANGE:
                if (self && IsWindowVisible(hwnd)) {
                    self->updatePlacement();
                    self->render();
                }
                return 0;
            case WM_DESTROY:
                easy::core::accessibility::disconnectOverlayUiaProvider(hwnd);
                KillTimer(hwnd, TIMER_ID);
                KillTimer(hwnd, FADE_TIMER_ID);
                KillTimer(hwnd, FADE_IN_TIMER_ID);
                return 0;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ToastOverlay 窗口过程异常: {}", e.what());
    } catch (...) {
        LOG_ERROR("ToastOverlay 窗口过程未知异常");
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::ui
