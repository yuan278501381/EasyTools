// ─────────────────────────────────────────────────────────────────────────────
// RecordingIndicator.cpp — 录制状态指示器实现
//
// 外观:
//   ┌──────────────────────────────────────────┐
//   │ 🔴  REC  03:45  1800帧   ⏸  ⏹          │
//   └──────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/RecordingIndicator.h"
#include "core/logger/Logger.h"

#include <format>
#include <windowsx.h>

namespace easy::capture {

using namespace Microsoft::WRL;

static constexpr const wchar_t* INDICATOR_CLASS = L"EasyTools_RecordingIndicator";
static constexpr UINT_PTR RENDER_TIMER_ID = 3001;
static constexpr int INDICATOR_WIDTH = 320;
static constexpr int INDICATOR_HEIGHT = 36;
static constexpr int BTN_SIZE = 28;

RecordingIndicator& RecordingIndicator::instance() {
    static RecordingIndicator inst;
    return inst;
}

bool RecordingIndicator::initialize(HINSTANCE hInstance) {
    if (!createWindow(hInstance)) return false;
    if (!createRenderResources()) return false;
    LOG_DEBUG("录制指示器初始化成功");
    return true;
}

void RecordingIndicator::shutdown() {
    releaseRenderResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void RecordingIndicator::show() {
    if (!m_hwnd) return;

    // 屏幕顶部居中
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int x = (screenW - INDICATOR_WIDTH) / 2;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, 8, INDICATOR_WIDTH, INDICATOR_HEIGHT,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    m_blinkTick = GetTickCount();
    SetTimer(m_hwnd, RENDER_TIMER_ID, 500, nullptr);  // 500ms 闪烁
    LOG_INFO("录制指示器已显示");
}

void RecordingIndicator::hide() {
    if (m_hwnd) {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        ShowWindow(m_hwnd, SW_HIDE);
    }
    LOG_DEBUG("录制指示器已隐藏");
}

void RecordingIndicator::update(double durationSec, int frameCount) {
    m_duration = durationSec;
    m_frames = frameCount;
    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        render();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口
// ─────────────────────────────────────────────────────────────────────────────

bool RecordingIndicator::createWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = indicatorWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.lpszClassName = INDICATOR_CLASS;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        INDICATOR_CLASS, L"",
        WS_POPUP,
        0, 0, INDICATOR_WIDTH, INDICATOR_HEIGHT,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(m_hwnd, 0, 230, LWA_ALPHA);

    // 按钮区域
    m_pauseBtn = {INDICATOR_WIDTH - BTN_SIZE * 2 - 16, 4, INDICATOR_WIDTH - BTN_SIZE - 12, 4 + BTN_SIZE};
    m_stopBtn = {INDICATOR_WIDTH - BTN_SIZE - 8, 4, INDICATOR_WIDTH - 4, 4 + BTN_SIZE};

    return true;
}

bool RecordingIndicator::createRenderResources() {
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    m_dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
                                       DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN",
                                       m_textFormat.GetAddressOf());

    m_dwriteFactory->CreateTextFormat(L"Segoe UI Symbol", nullptr,
                                       DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-CN",
                                       m_btnTextFormat.GetAddressOf());

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    auto rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    auto hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(rc.right, rc.bottom), D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    // 禁用 D2D 的自动 DPI 缩放
    m_renderTarget->SetDpi(96.0f, 96.0f);

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.15f, 1.0f), m_bgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), m_textBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.2f, 0.2f, 1.0f), m_redDotBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.15f), m_btnBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.3f), m_btnHoverBrush.GetAddressOf());

    return true;
}

void RecordingIndicator::releaseRenderResources() {
    m_btnHoverBrush.Reset();
    m_btnBrush.Reset();
    m_redDotBrush.Reset();
    m_textBrush.Reset();
    m_bgBrush.Reset();
    m_btnTextFormat.Reset();
    m_textFormat.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

void RecordingIndicator::render() {
    if (!m_renderTarget) return;

    m_renderTarget->BeginDraw();

    // 圆角背景
    auto size = m_renderTarget->GetSize();
    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(0, 0, size.width, size.height), 8.0f, 8.0f
    );
    m_renderTarget->FillRoundedRectangle(bg, m_bgBrush.Get());

    // 红色录制圆点（闪烁）
    bool showDot = !m_paused || ((GetTickCount() - m_blinkTick) / 500 % 2 == 0);
    if (showDot) {
        m_renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(20, size.height / 2), 5, 5),
            m_redDotBrush.Get()
        );
    }

    // REC 文字
    std::wstring recText = m_paused ? L"PAUSED" : L"REC";
    m_renderTarget->DrawText(recText.c_str(), static_cast<UINT32>(recText.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(32, 0, 90, size.height),
                             m_textBrush.Get());

    // 时间
    auto timeStr = formatDuration(m_duration);
    m_renderTarget->DrawText(timeStr.c_str(), static_cast<UINT32>(timeStr.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(90, 0, 160, size.height),
                             m_textBrush.Get());

    // 帧数
    auto frameStr = std::format(L"{}帧", m_frames);
    m_renderTarget->DrawText(frameStr.c_str(), static_cast<UINT32>(frameStr.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(160, 0, 230, size.height),
                             m_textBrush.Get());

    // 暂停按钮
    D2D1_ROUNDED_RECT pauseRect = D2D1::RoundedRect(
        D2D1::RectF(static_cast<float>(m_pauseBtn.left), static_cast<float>(m_pauseBtn.top),
                     static_cast<float>(m_pauseBtn.right), static_cast<float>(m_pauseBtn.bottom)),
        4.0f, 4.0f
    );
    m_renderTarget->FillRoundedRectangle(pauseRect, m_btnBrush.Get());
    std::wstring pauseIcon = m_paused ? L"▶" : L"⏸";
    m_renderTarget->DrawText(pauseIcon.c_str(), static_cast<UINT32>(pauseIcon.size()),
                             m_btnTextFormat.Get(),
                             D2D1::RectF(static_cast<float>(m_pauseBtn.left), static_cast<float>(m_pauseBtn.top),
                                         static_cast<float>(m_pauseBtn.right), static_cast<float>(m_pauseBtn.bottom)),
                             m_textBrush.Get());

    // 停止按钮
    D2D1_ROUNDED_RECT stopRect = D2D1::RoundedRect(
        D2D1::RectF(static_cast<float>(m_stopBtn.left), static_cast<float>(m_stopBtn.top),
                     static_cast<float>(m_stopBtn.right), static_cast<float>(m_stopBtn.bottom)),
        4.0f, 4.0f
    );
    m_renderTarget->FillRoundedRectangle(stopRect, m_redDotBrush.Get());
    m_renderTarget->DrawText(L"⏹", 1, m_btnTextFormat.Get(),
                             D2D1::RectF(static_cast<float>(m_stopBtn.left), static_cast<float>(m_stopBtn.top),
                                         static_cast<float>(m_stopBtn.right), static_cast<float>(m_stopBtn.bottom)),
                             m_textBrush.Get());

    m_renderTarget->EndDraw();
}

std::wstring RecordingIndicator::formatDuration(double seconds) const {
    int totalSec = static_cast<int>(seconds);
    int min = totalSec / 60;
    int sec = totalSec % 60;
    return std::format(L"{:02d}:{:02d}", min, sec);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK RecordingIndicator::indicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<RecordingIndicator*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // 检查是否点击了按钮
            if (PtInRect(&self->m_pauseBtn, pt)) {
                if (self->m_onPause) self->m_onPause();
                return 0;
            }
            if (PtInRect(&self->m_stopBtn, pt)) {
                if (self->m_onStop) self->m_onStop();
                return 0;
            }

            // 拖拽移动
            self->m_isDragging = true;
            self->m_dragOffset = pt;
            SetCapture(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (self && self->m_isDragging) {
                POINT cursor;
                GetCursorPos(&cursor);
                SetWindowPos(hwnd, nullptr,
                             cursor.x - self->m_dragOffset.x,
                             cursor.y - self->m_dragOffset.y,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (self) {
                self->m_isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_TIMER: {
            if (self && wParam == RENDER_TIMER_ID) {
                self->render();  // 刷新闪烁
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::capture
