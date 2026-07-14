#include "ui/ToastOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

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

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    float scale = easy::core::WinUtils::getDpiScale();
    int width = static_cast<int>(600 * scale);
    int height = static_cast<int>(80 * scale);
    int x = (screenW - width) / 2;
    int y = static_cast<int>(80 * scale); // 顶部留白

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TOAST_CLASS, L"",
        WS_POPUP,
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));

    LOG_INFO("按键回显 Overlay 初始化完成");
    return true;
}

void ToastOverlay::shutdown() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    discardResources();
    m_dwriteFactory = nullptr;
    m_d2dFactory = nullptr;
}

void ToastOverlay::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool ToastOverlay::isEnabled() const {
    return m_enabled;
}

void ToastOverlay::showToast(const std::string& text) {
    if (!m_enabled || !m_hwnd) return;

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
    
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    
    if (m_fadingIn) {
        SetTimer(m_hwnd, FADE_IN_TIMER_ID, FADE_INTERVAL_MS, nullptr);
    } else {
        render();
        SetTimer(m_hwnd, TIMER_ID, HIDE_TIMEOUT_MS, nullptr);
    }
}

void ToastOverlay::createResources() {
    if (!m_renderTarget && m_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget);

        if (m_renderTarget) {
            // 背景: 圆角深色半透明
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.16f, 0.9f), &m_bgBrush);
            // 边框: 主题紫
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.54f, 0.36f, 0.96f, 0.8f), &m_strokeBrush);
            // 文字: 白色
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_textBrush);
        }

        if (m_dwriteFactory && !m_textFormat) {
            float scale = easy::core::WinUtils::getDpiScale(m_hwnd);
            m_dwriteFactory->CreateTextFormat(
                L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 18.0f * scale, L"zh-cn", &m_textFormat
            );
            m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void ToastOverlay::discardResources() {
    m_renderTarget = nullptr;
    m_bgBrush = nullptr;
    m_strokeBrush = nullptr;
    m_textBrush = nullptr;
    m_textFormat = nullptr;
}

void ToastOverlay::render() {
    if (!m_hwnd) return;
    createResources();
    if (!m_renderTarget) return;

    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC hdc = GetDC(m_hwnd);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    RECT clientRc = {0, 0, width, height};
    m_renderTarget->BindDC(memDC, &clientRc);
    
    // 获取 DPI 以按比例缩放绘制
    float scale = easy::core::WinUtils::getDpiScale(m_hwnd);
    m_renderTarget->SetDpi(96.0f * scale, 96.0f * scale);

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
        
        float cx = (width / scale) / 2.0f;
        float cy = (height / scale) / 2.0f;
        float textWidth = static_cast<float>(wText.length() * 20.0f + 60.0f); 
        float rectHeight = 60.0f;

        // 应用进场缩放动画
        D2D1::Matrix3x2F transform = D2D1::Matrix3x2F::Scale(scaleAnim, scaleAnim, D2D1::Point2F(cx, cy));
        m_renderTarget->SetTransform(transform);
        
        D2D1_RECT_F bgRect = D2D1::RectF(cx - textWidth/2, cy - rectHeight/2, cx + textWidth/2, cy + rectHeight/2);
        
        m_bgBrush->SetOpacity(alpha);
        // 绘制圆角背景
        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(bgRect, 8.0f * scale, 8.0f * scale);
        m_renderTarget->FillRoundedRectangle(roundedRect, m_bgBrush.Get());
        m_renderTarget->DrawRoundedRectangle(roundedRect, m_strokeBrush.Get(), 1.5f * scale);

        m_textBrush->SetOpacity(alpha);
        m_renderTarget->DrawTextW(
            wText.c_str(), static_cast<UINT32>(wText.length()), m_textFormat.Get(),
            D2D1::RectF(0, 0, width / scale, height / scale),
            m_textBrush.Get()
        );
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        discardResources();
    }

    POINT ptSrc = {0, 0};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    
    UpdateLayeredWindow(m_hwnd, hdc, nullptr, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(m_hwnd, hdc);
}

LRESULT CALLBACK ToastOverlay::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ToastOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // 窗口过程兜底：任何 std 异常都不得逃逸到 Win32 派发层（否则 std::terminate 崩溃）。
    try {
        switch (msg) {
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
            case WM_DESTROY:
                PostQuitMessage(0);
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
