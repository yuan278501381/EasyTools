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
#include "core/utils/WinUtils.h"

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

    // 渲染第一帧（全透明），确保 UpdateLayeredWindow 更新了画面，防止出现图层闪烁
    render();

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
    // 距离过滤: 跳过与上一点过近的采样, 否则手势起点处的密集点会叠成一团("一坨")
    if (!m_points.empty()) {
        float dx = x - m_points.back().x;
        float dy = y - m_points.back().y;
        if (dx * dx + dy * dy < 16.0f) return;  // < 4px 忽略
    }
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

    // 覆盖整个虚拟屏幕（多显示器支持，使用物理边界防止高DPI错位）
    RECT bounds = easy::core::WinUtils::getVirtualScreenPhysicalBounds();
    int screenX = bounds.left;
    int screenY = bounds.top;
    int screenW = bounds.right - bounds.left;
    int screenH = bounds.bottom - bounds.top;
    m_originX = screenX;
    m_originY = screenY;
    m_width = screenW;
    m_height = screenH;

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

    // 移除 COLORKEY，改用 UpdateLayeredWindow 逐像素透明渲染
    // SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

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
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_d2dFactory->CreateDCRenderTarget(&rtProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    // 创建内存 DC 与 DIB
    HDC hdcScreen = GetDC(nullptr);
    m_memoryDC = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pBits = nullptr;
    m_memoryBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    m_oldBitmap = (HBITMAP)SelectObject(m_memoryDC, m_memoryBitmap);
    
    RECT memRect = { 0, 0, m_width, m_height };
    m_renderTarget->BindDC(m_memoryDC, &memRect);
    
    ReleaseDC(nullptr, hdcScreen);

    // 禁用 D2D 的自动 DPI 缩放，使逻辑坐标 1:1 映射到物理像素 (因为输入坐标已是物理像素)
    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 画笔
    float r = ((m_style.lineColor >> 16) & 0xFF) / 255.0f;
    float g = ((m_style.lineColor >> 8) & 0xFF) / 255.0f;
    float b = (m_style.lineColor & 0xFF) / 255.0f;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0f), m_lineBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f), m_textBgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), m_textBrush.GetAddressOf());

    // 笔触样式 (使线段更平滑，具有圆头)
    D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND,
        10.0f,
        D2D1_DASH_STYLE_SOLID,
        0.0f
    );
    hr = m_d2dFactory->CreateStrokeStyle(strokeProps, nullptr, 0, m_strokeStyle.GetAddressOf());

    return true;
}

void GestureTrailOverlay::releaseD2DResources() {
    m_textBrush.Reset();
    m_textBgBrush.Reset();
    m_lineBrush.Reset();
    m_textFormat.Reset();
    m_strokeStyle.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    
    if (m_memoryDC && m_oldBitmap) {
        SelectObject(m_memoryDC, m_oldBitmap);
    }
    if (m_memoryBitmap) {
        DeleteObject(m_memoryBitmap);
        m_memoryBitmap = nullptr;
    }
    if (m_memoryDC) {
        DeleteDC(m_memoryDC);
        m_memoryDC = nullptr;
    }
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

            D2D1_POINT_2F p0 = D2D1::Point2F(m_points[i - 1].x - m_originX, m_points[i - 1].y - m_originY);
            D2D1_POINT_2F p1 = D2D1::Point2F(m_points[i].x - m_originX, m_points[i].y - m_originY);

            m_renderTarget->DrawLine(p0, p1, m_lineBrush.Get(), m_style.lineWidth, m_strokeStyle.Get());
        }

        // 绘制轨迹头部发光点
        if (!m_fading) {
            const auto& lastPt = m_points.back();
            m_lineBrush->SetOpacity(0.8f);
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(lastPt.x - m_originX, lastPt.y - m_originY), 5.0f, 5.0f),
                m_lineBrush.Get()
            );
        }
    }

    // 绘制识别结果文字
    if (!m_resultText.empty() && !m_points.empty()) {
        // 获取当前鼠标所在的显示器
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        HMONITOR hMon = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);
        
        // 在屏幕中下方（合十位置）显示
        float centerX = static_cast<float>(mi.rcMonitor.left + mi.rcMonitor.right) / 2.0f;
        float centerY = mi.rcMonitor.top + static_cast<float>(mi.rcMonitor.bottom - mi.rcMonitor.top) * 0.85f;
        
        centerX -= m_originX;  // 转为覆盖层(客户区)坐标
        centerY -= m_originY;

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
    
    // 使用 UpdateLayeredWindow 一次性提交画面（逐像素Alpha混合）
    if (m_memoryDC) {
        HDC hdcScreen = GetDC(nullptr);
        POINT ptSrc = {0, 0};
        POINT ptWin = {m_originX, m_originY};
        SIZE size = {m_width, m_height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        
        UpdateLayeredWindow(m_hwnd, hdcScreen, &ptWin, &size, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, hdcScreen);
    }
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
                self->m_originX = x;
                self->m_originY = y;
                self->m_width = w;
                self->m_height = h;
                MoveWindow(self->m_hwnd, x, y, w, h, FALSE);

                // 重建内存位图并重新绑定 DC
                if (self->m_memoryDC) {
                    if (self->m_oldBitmap) SelectObject(self->m_memoryDC, self->m_oldBitmap);
                    if (self->m_memoryBitmap) DeleteObject(self->m_memoryBitmap);
                    
                    HDC hdcScreen = GetDC(nullptr);
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = w;
                    bmi.bmiHeader.biHeight = -h;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    
                    void* pBits = nullptr;
                    self->m_memoryBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
                    self->m_oldBitmap = (HBITMAP)SelectObject(self->m_memoryDC, self->m_memoryBitmap);
                    ReleaseDC(nullptr, hdcScreen);
                    
                    if (self->m_renderTarget) {
                        RECT memRect = {0, 0, w, h};
                        self->m_renderTarget->BindDC(self->m_memoryDC, &memRect);
                    }
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
