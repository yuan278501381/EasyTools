// ─────────────────────────────────────────────────────────────────────────────
// GestureTrailOverlay.cpp — 手势轨迹可视化覆盖层实现
//
// 核心原理:
//   1. 创建 WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST 的全屏窗口
//   2. 使用 Direct2D 绘制半透明轨迹线段
//   3. 线段从起点到终点做渐变透明度变化
//   4. 手势完成后显示识别结果，之后自动淡出
//   5. 窗口始终 click-through（WS_EX_TRANSPARENT），不影响用户操作
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <cmath>

using namespace Microsoft::WRL;

namespace easy::gesture {

static constexpr const wchar_t* OVERLAY_CLASS = L"EasyTools_GestureOverlay";
static constexpr UINT_PTR FADE_TIMER_ID = 1001;
static constexpr UINT_PTR RENDER_TIMER_ID = 1002;
static constexpr UINT RENDER_INTERVAL_MS = 16;  // ~60 FPS

GestureTrailOverlay& GestureTrailOverlay::instance() {
    static GestureTrailOverlay inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化 / 关闭
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::initialize(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;

    if (!createOverlayWindow(hInstance)) {
        LOG_ERROR("创建手势轨迹覆盖层窗口失败");
        return false;
    }

    if (!createD2DResources()) {
        LOG_ERROR("创建 Direct2D 资源失败");
        return false;
    }

    LOG_INFO("手势轨迹覆盖层初始化成功");
    return true;
}

void GestureTrailOverlay::shutdown() {
    releaseD2DResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_visible = false;
    LOG_DEBUG("手势轨迹覆盖层已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 轨迹操作
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::beginTrail() {
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
    }

    m_fading = false;
    m_fadeAlpha = 1.0f;

    // 显示窗口
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        m_visible = true;

        // 启动渲染定时器
        SetTimer(m_hwnd, RENDER_TIMER_ID, RENDER_INTERVAL_MS, nullptr);
    }
}

void GestureTrailOverlay::addPoint(float x, float y) {
    std::lock_guard lock(m_trailMutex);
    m_points.push_back({x, y, GetTickCount()});
}

void GestureTrailOverlay::endTrail(const std::string& resultText) {
    {
        std::lock_guard lock(m_trailMutex);
        m_resultText = resultText;
    }

    // 最后渲染一帧（带结果文字）
    render();

    // 启动淡出
    startFadeOut();
}

void GestureTrailOverlay::hide() {
    if (m_hwnd) {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        KillTimer(m_hwnd, FADE_TIMER_ID);
        ShowWindow(m_hwnd, SW_HIDE);
    }
    m_visible = false;
    m_fading = false;
    m_fadeAlpha = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = overlayWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // 透明
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);

    // 覆盖整个虚拟屏幕（多显示器支持）
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS,
        L"",
        WS_POPUP,
        screenX, screenY, screenW, screenH,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败, error={}", GetLastError());
        return false;
    }

    // 设置 Layered 窗口的透明色键（让黑色背景透明）
    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    LOG_DEBUG("轨迹覆盖层窗口已创建, screen={}x{} @ ({},{})", screenW, screenH, screenX, screenY);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct2D 资源管理
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createD2DResources() {
    HRESULT hr;

    // D2D 工厂
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    // DWrite 工厂
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    // 文本格式
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_style.resultFontSize,
        L"zh-CN",
        m_textFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 渲染目标
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd,
        D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    // 画笔
    float r = ((m_style.lineColor >> 16) & 0xFF) / 255.0f;
    float g = ((m_style.lineColor >> 8) & 0xFF) / 255.0f;
    float b = (m_style.lineColor & 0xFF) / 255.0f;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0f), m_lineBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f), m_textBgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), m_textBrush.GetAddressOf());

    return true;
}

void GestureTrailOverlay::releaseD2DResources() {
    m_textBrush.Reset();
    m_textBgBrush.Reset();
    m_lineBrush.Reset();
    m_textFormat.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::render() {
    if (!m_renderTarget || !m_lineBrush) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // 完全透明背景

    std::lock_guard lock(m_trailMutex);

    if (m_points.size() >= 2) {
        // 绘制轨迹线段（渐变透明度）
        for (size_t i = 1; i < m_points.size(); ++i) {
            float progress = static_cast<float>(i) / static_cast<float>(m_points.size());
            float alpha = m_style.startOpacity - progress * (m_style.startOpacity - m_style.endOpacity);
            alpha *= m_fadeAlpha;  // 淡出时整体乘以 fade alpha

            m_lineBrush->SetOpacity(alpha);

            D2D1_POINT_2F p0 = D2D1::Point2F(m_points[i - 1].x, m_points[i - 1].y);
            D2D1_POINT_2F p1 = D2D1::Point2F(m_points[i].x, m_points[i].y);

            m_renderTarget->DrawLine(p0, p1, m_lineBrush.Get(), m_style.lineWidth);
        }

        // 绘制轨迹头部发光点
        if (!m_fading) {
            const auto& lastPt = m_points.back();
            m_lineBrush->SetOpacity(0.8f);
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(lastPt.x, lastPt.y), 5.0f, 5.0f),
                m_lineBrush.Get()
            );
        }
    }

    // 绘制识别结果文字
    if (!m_resultText.empty() && !m_points.empty()) {
        // 在轨迹中心位置显示
        float centerX = 0, centerY = 0;
        for (const auto& pt : m_points) {
            centerX += pt.x;
            centerY += pt.y;
        }
        centerX /= static_cast<float>(m_points.size());
        centerY /= static_cast<float>(m_points.size());

        // 背景圆角矩形
        float textW = m_style.resultFontSize * static_cast<float>(m_resultText.size()) * 0.6f + 24.0f;
        float textH = m_style.resultFontSize + 16.0f;

        D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(
            D2D1::RectF(centerX - textW / 2, centerY - textH / 2, centerX + textW / 2, centerY + textH / 2),
            6.0f, 6.0f
        );

        m_textBgBrush->SetOpacity(0.75f * m_fadeAlpha);
        m_renderTarget->FillRoundedRectangle(bgRect, m_textBgBrush.Get());

        // 文字
        std::wstring wText(m_resultText.begin(), m_resultText.end());
        m_textBrush->SetOpacity(m_fadeAlpha);
        m_renderTarget->DrawText(
            wText.c_str(),
            static_cast<UINT32>(wText.size()),
            m_textFormat.Get(),
            D2D1::RectF(centerX - textW / 2, centerY - textH / 2, centerX + textW / 2, centerY + textH / 2),
            m_textBrush.Get()
        );
    }

    m_renderTarget->EndDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
// 淡出动画
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::startFadeOut() {
    m_fading = true;
    m_fadeStartTick = GetTickCount();

    // 停止渲染定时器，启用淡出定时器
    if (m_hwnd) {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        SetTimer(m_hwnd, FADE_TIMER_ID, RENDER_INTERVAL_MS, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK GestureTrailOverlay::overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<GestureTrailOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_TIMER: {
            if (!self) break;

            if (wParam == RENDER_TIMER_ID) {
                // 实时渲染轨迹
                self->render();
            } else if (wParam == FADE_TIMER_ID) {
                // 淡出动画
                DWORD elapsed = GetTickCount() - self->m_fadeStartTick;
                float progress = static_cast<float>(elapsed) / self->m_style.fadeOutMs;

                if (progress >= 1.0f) {
                    self->hide();
                } else {
                    self->m_fadeAlpha = 1.0f - progress;
                    self->render();
                }
            }
            return 0;
        }

        case WM_DISPLAYCHANGE: {
            // 显示器配置变化，重新调整窗口大小
            if (self && self->m_hwnd) {
                int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
                int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                MoveWindow(self->m_hwnd, x, y, w, h, FALSE);

                // 重建渲染目标
                if (self->m_renderTarget) {
                    self->m_renderTarget->Resize(D2D1::SizeU(w, h));
                }
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::gesture
