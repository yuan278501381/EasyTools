// ─────────────────────────────────────────────────────────────────────────────
// RecordingIndicator.cpp — 录制状态指示器实现
//
// 外观:
//   ┌──────────────────────────────────────────┐
//   │ 🔴  REC  03:45  1800帧   ⏸  ⏹          │
//   └──────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/RecordingIndicator.h"
#include "capture/ScreenRecorder.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"

#include <format>
#include <algorithm>
#include <cmath>
#include <windowsx.h>

namespace easy::capture {

using namespace Microsoft::WRL;

static constexpr const wchar_t* INDICATOR_CLASS = L"EasyTools_RecordingIndicator";
static constexpr UINT_PTR RENDER_TIMER_ID = 3001;
static constexpr int INDICATOR_WIDTH = 400;
static constexpr int INDICATOR_HEIGHT = 36;
static constexpr int BTN_SIZE = 28;

static float indicatorDpiScale(HMONITOR monitor) {
    return easy::core::dpi::scaleForMonitor(monitor);
}

static int scaledIndicatorMetric(int value, float scale) {
    return easy::core::dpi::scaleMetric(value, scale);
}

RecordingIndicator& RecordingIndicator::instance() {
    static RecordingIndicator inst;
    return inst;
}

bool RecordingIndicator::initialize(HINSTANCE hInstance) {
    // 与截图覆盖层一样按需创建，避免隐藏的 layered D2D 窗口参与桌面合成。
    m_hInstance = hInstance;
    return m_hInstance != nullptr;
}

void RecordingIndicator::shutdown() {
    if (m_hwnd) KillTimer(m_hwnd, RENDER_TIMER_ID);
    releaseRenderResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void RecordingIndicator::show() {
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    m_dpiScale = indicatorDpiScale(monitor);

    if (!m_hwnd) {
        if (!m_hInstance || !createWindow(m_hInstance) || !createRenderResources()) {
            LOG_ERROR("录制指示器按需初始化失败");
            releaseRenderResources();
            if (m_hwnd) {
                DestroyWindow(m_hwnd);
                m_hwnd = nullptr;
            }
            return;
        }
        LOG_DEBUG("录制指示器按需初始化成功");
    }

    // Follow the monitor where region selection finished, including its work
    // area and effective DPI. This also avoids forcing the bar onto display 1.
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    const int width = scaledIndicatorMetric(INDICATOR_WIDTH, m_dpiScale);
    const int height = scaledIndicatorMetric(INDICATOR_HEIGHT, m_dpiScale);
    const int x = monitorInfo.rcWork.left +
                  ((monitorInfo.rcWork.right - monitorInfo.rcWork.left) - width) / 2;
    const int y = monitorInfo.rcWork.top + scaledIndicatorMetric(8, m_dpiScale);
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    m_blinkTick = GetTickCount();
    SetTimer(m_hwnd, RENDER_TIMER_ID, 100, nullptr);  // 10 Hz 电平刷新；闪烁仍按 500 ms 计算
    LOG_INFO("录制指示器已显示");
}

void RecordingIndicator::hide() {
    if (m_hwnd) {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        ShowWindow(m_hwnd, SW_HIDE);
        releaseRenderResources();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_paused = false;
    m_countingDown = false;
    m_countdownRemaining = 0;
    m_systemAudioPeak = 0.0f;
    m_microphonePeak = 0.0f;
    m_storageWarning = false;
    m_estimatedRemainingSec = -1;
    m_performanceLimited = false;
    m_effectiveFps = 0.0;
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
        0, 0, scaledIndicatorMetric(INDICATOR_WIDTH, m_dpiScale),
        scaledIndicatorMetric(INDICATOR_HEIGHT, m_dpiScale),
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(m_hwnd, 0, 230, LWA_ALPHA);
    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除录制悬浮条: error={}", GetLastError());
    }

    // 按钮区域
    m_pauseBtn = {INDICATOR_WIDTH - BTN_SIZE * 2 - 16, 4, INDICATOR_WIDTH - BTN_SIZE - 12, 4 + BTN_SIZE};
    m_stopBtn = {INDICATOR_WIDTH - BTN_SIZE - 8, 4, INDICATOR_WIDTH - 4, 4 + BTN_SIZE};
    m_systemAudioBtn = {216, 4, 244, 32};
    m_microphoneBtn = {248, 4, 276, 32};

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
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.85f, 0.55f, 1.0f), m_audioBrush.GetAddressOf());

    return true;
}

void RecordingIndicator::releaseRenderResources() {
    m_audioBrush.Reset();
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
    m_renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(m_dpiScale, m_dpiScale));

    // 圆角背景
    auto physicalSize = m_renderTarget->GetSize();
    auto size = D2D1::SizeF(physicalSize.width / m_dpiScale,
                            physicalSize.height / m_dpiScale);
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
    std::wstring recText = m_countingDown ? L"READY" : (m_paused ? L"PAUSED" : L"REC");
    m_renderTarget->DrawText(recText.c_str(), static_cast<UINT32>(recText.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(32, 0, 90, size.height),
                             m_textBrush.Get());

    // 时间
    auto timeStr = m_countingDown
        ? std::to_wstring(std::max(1, m_countdownRemaining))
        : formatDuration(m_duration);
    m_renderTarget->DrawText(timeStr.c_str(), static_cast<UINT32>(timeStr.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(90, 0, 160, size.height),
                             m_textBrush.Get());

    // 帧数
    auto frameStr = m_countingDown ? std::wstring{}
        : (m_storageWarning && m_estimatedRemainingSec >= 0
            ? std::format(L"~{}m", std::max<std::int64_t>(1, m_estimatedRemainingSec / 60))
            : m_performanceLimited
                ? std::format(L"{:.0f}fps", m_effectiveFps)
                : std::format(L"{}帧", m_frames));
    m_renderTarget->DrawText(frameStr.c_str(), static_cast<UINT32>(frameStr.size()),
                             m_textFormat.Get(),
                             D2D1::RectF(160, 0, 220, size.height),
                             m_textBrush.Get());

    // 系统声/麦克风双通道实时电平。静音时保留暗色轨道，让用户仍能确认音轨已启用。
    const auto drawMeter = [&](const RECT& rect, const wchar_t* label,
                               float peak, bool active, bool muted) {
        if (!active) return;
        const auto bounds = D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                         static_cast<float>(rect.right), static_cast<float>(rect.bottom)), 4, 4);
        m_renderTarget->FillRoundedRectangle(bounds, muted ? m_redDotBrush.Get() : m_btnBrush.Get());
        m_renderTarget->DrawText(label, 1, m_btnTextFormat.Get(),
            D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                         static_cast<float>(rect.right - 6), static_cast<float>(rect.bottom)),
            m_textBrush.Get());
        const float normalized = std::clamp(peak, 0.0f, 1.0f);
        if (!muted && normalized > 0.005f) {
            const auto fill = D2D1::RoundedRect(
                D2D1::RectF(static_cast<float>(rect.right - 5),
                             static_cast<float>(rect.bottom - 3) - normalized * 22,
                             static_cast<float>(rect.right - 2),
                             static_cast<float>(rect.bottom - 3)), 2, 2);
            m_renderTarget->FillRoundedRectangle(fill, m_audioBrush.Get());
        }
    };
    if (!m_countingDown) {
        drawMeter(m_systemAudioBtn, L"S", m_systemAudioPeak,
                  m_systemAudioActive, m_systemAudioMuted);
        drawMeter(m_microphoneBtn, L"M", m_microphonePeak,
                  m_microphoneActive, m_microphoneMuted);
    }

    // 倒计时只能取消，开始写帧后才显示暂停按钮。
    if (!m_countingDown) {
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
    }

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
            POINT hitPt = {
                static_cast<LONG>(pt.x / self->m_dpiScale),
                static_cast<LONG>(pt.y / self->m_dpiScale)};

            // 检查是否点击了按钮
            if (!self->m_countingDown && PtInRect(&self->m_pauseBtn, hitPt)) {
                if (self->m_onPause) self->m_onPause();
                return 0;
            }
            if (PtInRect(&self->m_stopBtn, hitPt)) {
                if (self->m_onStop) self->m_onStop();
                return 0;
            }
            if (self->m_systemAudioActive && PtInRect(&self->m_systemAudioBtn, hitPt)) {
                if (self->m_onSystemAudioMute) self->m_onSystemAudioMute();
                return 0;
            }
            if (self->m_microphoneActive && PtInRect(&self->m_microphoneBtn, hitPt)) {
                if (self->m_onMicrophoneMute) self->m_onMicrophoneMute();
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
                const auto stats = ScreenRecorder::instance().stats();
                self->m_duration = stats.durationSec;
                self->m_frames = stats.frameCount;
                self->m_systemAudioPeak = stats.systemAudioPeak;
                self->m_microphonePeak = stats.microphonePeak;
                self->m_systemAudioActive = stats.systemAudioActive;
                self->m_microphoneActive = stats.microphoneActive;
                self->m_systemAudioMuted = stats.systemAudioMuted;
                self->m_microphoneMuted = stats.microphoneMuted;
                self->m_countingDown = ScreenRecorder::instance().state() == RecordState::Countdown;
                self->m_countdownRemaining = stats.countdownRemaining;
                self->m_storageWarning = stats.storageWarning;
                self->m_estimatedRemainingSec = stats.estimatedRemainingSec;
                self->m_performanceLimited = stats.performanceLimited;
                self->m_effectiveFps = stats.effectiveFps;
                self->render();  // 刷新闪烁
            }
            return 0;
        }

        case WM_DPICHANGED: {
            if (!self) break;
            self->m_dpiScale = std::clamp(
                static_cast<float>(LOWORD(wParam)) / 96.0f, 1.0f, 5.0f);
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            const int width = scaledIndicatorMetric(INDICATOR_WIDTH, self->m_dpiScale);
            const int height = scaledIndicatorMetric(INDICATOR_HEIGHT, self->m_dpiScale);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (self->m_renderTarget) {
                self->m_renderTarget->Resize(D2D1::SizeU(width, height));
                self->render();
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::capture
