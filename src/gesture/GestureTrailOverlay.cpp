// ─────────────────────────────────────────────────────────────────────────────
// GestureTrailOverlay.cpp — 手势轨迹可视化覆盖层实现
//
// 核心原理:
//   1. 创建 WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST 的全屏窗口
//   2. 使用 Direct2D 绘制半透明轨迹线段
//   3. 线段从起点到终点做渐变透明度变化
//   4. 手势绘制过程中及手势完成后显示按键回显风格的实时动作名称
//   5. 窗口始终 click-through（WS_EX_TRANSPARENT），不影响用户操作
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

    // 预热 D2D 与 DWrite 核心工厂库（消除首次 DLL/COM 加载延迟），
    // 30MB 全屏 DIB 位图与 RenderTarget 在实际手势发生时按需生成并在退场时自动回收
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (SUCCEEDED(hr)) {
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        );
    }

    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;

    LOG_INFO("手势轨迹覆盖层初始化成功");
    return true;
}

void GestureTrailOverlay::setStyle(const TrailStyle& style) {
    m_style = style;
    m_textScale = 0.0f;
    if (m_dwriteFactory) updateTextFormat(m_dpiScale);
    if (m_renderTarget) {
        const float r = ((m_style.lineColor >> 16) & 0xFF) / 255.0f;
        const float g = ((m_style.lineColor >> 8) & 0xFF) / 255.0f;
        const float b = (m_style.lineColor & 0xFF) / 255.0f;
        ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(m_renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(r, g, b, 1.0f), brush.GetAddressOf()))) {
            m_lineBrush = std::move(brush);
        }
    }
}

void GestureTrailOverlay::shutdown() {
    releaseD2DResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_helperOwnerHwnd) {
        DestroyWindow(m_helperOwnerHwnd);
        m_helperOwnerHwnd = nullptr;
    }
    m_visible = false;
    LOG_DEBUG("手势轨迹覆盖层已关闭");
}

void GestureTrailOverlay::clearCanvas() {
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    m_renderTarget->EndDraw();

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = { 0, 0 };
    SIZE sz = { m_width, m_height };
    POINT ptDst = { m_originX, m_originY };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

// ─────────────────────────────────────────────────────────────────────────────
// 轨迹操作
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::beginTrail() {
    // 纯内存重置（< 1 微秒），严禁在钩子回调中做重型 D2D 重建或 UpdateLayeredWindow
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_pathCache.clear();
    }

    m_fading = false;
    m_fadeAlpha = 1.0f;
}

void GestureTrailOverlay::addPoint(float x, float y) {
    bool hasVisibleTrail = false;
    {
        std::lock_guard lock(m_trailMutex);
        if (!m_points.empty()) {
            float dx = x - m_points.back().x;
            float dy = y - m_points.back().y;
            const float minimumDelta = 4.0f * m_dpiScale;
            if (dx * dx + dy * dy < minimumDelta * minimumDelta) return;
        }
        m_points.push_back({x, y, GetTickCount()});
        hasVisibleTrail = m_points.size() >= 2;
    }
    if (hasVisibleTrail && m_hwnd) {
        if (!m_renderTarget) {
            POINT cursor{};
            GetCursorPos(&cursor);
            m_dpiScale = easy::core::dpi::scaleAtPoint(cursor);
            createD2DResources();
        }
        if (!m_visible.exchange(true)) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        if (!m_renderTimerActive.exchange(true)) {
            SetTimer(m_hwnd, RENDER_TIMER_ID, RENDER_INTERVAL_MS, nullptr);
        }
    }
}

void GestureTrailOverlay::setLiveAction(const std::string& actionText) {
    std::lock_guard lock(m_trailMutex);
    m_resultText = actionText;
}

void GestureTrailOverlay::endTrail(const std::string& resultText) {
    {
        std::lock_guard lock(m_trailMutex);
        if (!resultText.empty()) {
            m_resultText = resultText;
        }
    }
    startFadeOut();
}

void GestureTrailOverlay::hide() {
    if (m_hwnd) {
        KillTimer(m_hwnd, RENDER_TIMER_ID);
        KillTimer(m_hwnd, FADE_TIMER_ID);
        m_renderTimerActive.store(false);
        clearCanvas();
        ShowWindow(m_hwnd, SW_HIDE);
    }
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_pathCache.clear();
    }
    m_visible = false;
    m_fading = false;
    m_fadeAlpha = 1.0f;
    releaseD2DResources();
    easy::core::WinUtils::trimWorkingSet();
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

    // 创建隐藏的 Helper Owner 窗口，使覆盖层成为 Owned Window，杜绝 Windows Shell 派发顶层窗口通知导致任务栏图标跳动
    m_helperOwnerHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS,
        L"",
        WS_POPUP,
        screenX, screenY, screenW, screenH,
        m_helperOwnerHwnd, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败, error={}", GetLastError());
        return false;
    }

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除手势轨迹: error={}", GetLastError());
    }

    LOG_DEBUG("轨迹覆盖层窗口已创建, screen={}x{} @ ({},{})", screenW, screenH, screenX, screenY);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct2D 资源管理
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createD2DResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_lineBrush && m_textBorderBrush) return true;
    releaseD2DResources();
    const auto fail = [this]() {
        releaseD2DResources();
        return false;
    };
    HRESULT hr;

    // D2D 工厂
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return fail();

    // DWrite 工厂
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );
    if (FAILED(hr)) return fail();

    if (!updateTextFormat(m_dpiScale)) return fail();

    // 渲染目标
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_d2dFactory->CreateDCRenderTarget(&rtProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return fail();

    // 创建内存 DC 与 DIB
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return fail();
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
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memoryDC || !m_memoryBitmap || !pBits) return fail();
    m_oldBitmap = (HBITMAP)SelectObject(m_memoryDC, m_memoryBitmap);
    
    RECT memRect = { 0, 0, m_width, m_height };
    if (FAILED(m_renderTarget->BindDC(m_memoryDC, &memRect))) return fail();

    // 禁用 D2D 的自动 DPI 缩放，使逻辑坐标 1:1 映射到物理像素 (因为输入坐标已是物理像素)
    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 画笔
    float r = ((m_style.lineColor >> 16) & 0xFF) / 255.0f;
    float g = ((m_style.lineColor >> 8) & 0xFF) / 255.0f;
    float b = (m_style.lineColor & 0xFF) / 255.0f;

    const auto accent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "violet");
    const auto accentRgb = easy::core::getAccentColorRGB(accent);

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0f), m_lineBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.12f, 0.88f), m_textBgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(accentRgb.r, accentRgb.g, accentRgb.b, 0.70f), m_textBorderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f), m_textBrush.GetAddressOf());

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

    if (FAILED(hr) || !m_lineBrush || !m_textBgBrush || !m_textBorderBrush || !m_textBrush) return fail();
    return true;
}

bool GestureTrailOverlay::updateTextFormat(float dpiScale) {
    dpiScale = std::clamp(dpiScale, 1.0f, 5.0f);
    if (m_textFormat && std::abs(m_textScale - dpiScale) < 0.01f) return true;
    if (!m_dwriteFactory) return false;
    ComPtr<IDWriteTextFormat> format;
    const HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_style.resultFontSize * dpiScale,
        L"zh-CN",
        format.GetAddressOf()
    );
    if (FAILED(hr) || !format) return false;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat = std::move(format);
    m_textScale = dpiScale;
    return true;
}

void GestureTrailOverlay::releaseD2DResources() {
    m_pathCache.clear();
    m_textBrush.Reset();
    m_textBorderBrush.Reset();
    m_textBgBrush.Reset();
    m_lineBrush.Reset();
    m_textFormat.Reset();
    m_textScale = 0.0f;
    m_strokeStyle.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    
    if (m_memoryDC && m_oldBitmap) {
        SelectObject(m_memoryDC, m_oldBitmap);
    }
    m_oldBitmap = nullptr;
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
        size_t numSegments = m_points.size() - 1;
        if (m_pathCache.size() < numSegments) {
            m_pathCache.resize(numSegments);
        }

        // 仅重建缺少或需要更新的最后两段（因为新点的加入会影响前一段的结束点）
        for (size_t i = 0; i < numSegments; ++i) {
            bool isLast = (i == numSegments - 1);
            bool isSecondLast = (i + 1 == numSegments - 1);
            
            if (m_pathCache[i] == nullptr || isLast || isSecondLast) {
                if (FAILED(m_d2dFactory->CreatePathGeometry(
                        m_pathCache[i].ReleaseAndGetAddressOf())) || !m_pathCache[i]) {
                    continue;
                }
                
                ComPtr<ID2D1GeometrySink> sink;
                if (FAILED(m_pathCache[i]->Open(&sink)) || !sink) {
                    m_pathCache[i].Reset();
                    continue;
                }
                
                auto getPt = [&](size_t idx) -> D2D1_POINT_2F {
                    return D2D1::Point2F(m_points[idx].x - m_originX, m_points[idx].y - m_originY);
                };
                
                D2D1_POINT_2F p0 = getPt(i);
                D2D1_POINT_2F p1 = getPt(i + 1);
                
                D2D1_POINT_2F startPt, ctrlPt, endPt;
                if (i == 0) {
                    startPt = p0;
                } else {
                    startPt = D2D1::Point2F((p0.x + p1.x) / 2.0f, (p0.y + p1.y) / 2.0f);
                }
                
                ctrlPt = p1;
                
                if (isLast) {
                    endPt = p1;
                } else {
                    D2D1_POINT_2F p2 = getPt(i + 2);
                    endPt = D2D1::Point2F((p1.x + p2.x) / 2.0f, (p1.y + p2.y) / 2.0f);
                }

                sink->BeginFigure(startPt, D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(ctrlPt, endPt));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (FAILED(sink->Close())) m_pathCache[i].Reset();
            }
        }

        // 绘制所有贝塞尔段（渐变透明度）
        for (size_t i = 0; i < numSegments; ++i) {
            float progress = static_cast<float>(i) / static_cast<float>(numSegments);
            float alpha = m_style.startOpacity - progress * (m_style.startOpacity - m_style.endOpacity);
            alpha *= m_fadeAlpha;

            m_lineBrush->SetOpacity(alpha);

            if (m_pathCache[i]) {
                m_renderTarget->DrawGeometry(
                    m_pathCache[i].Get(), m_lineBrush.Get(),
                    m_style.lineWidth * m_dpiScale, m_strokeStyle.Get());
            }
        }

        // 绘制轨迹头部发光点
        if (!m_fading) {
            const auto& lastPt = m_points.back();
            m_lineBrush->SetOpacity(0.8f);
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F(lastPt.x - m_originX, lastPt.y - m_originY),
                    5.0f * m_dpiScale, 5.0f * m_dpiScale),
                m_lineBrush.Get()
            );
        }
    }

    // 绘制识别结果与实时动作名称文字（按键回显风格浮动卡片）
    if (!m_resultText.empty() && !m_points.empty()) {
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        HMONITOR hMon = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);
        const RECT work = easy::core::dpi::workArea(hMon);
        const float resultScale = easy::core::dpi::scaleForMonitor(hMon);
        const bool hasTextFormat = updateTextFormat(resultScale);

        float centerX = static_cast<float>(work.left + work.right) / 2.0f - m_originX;
        float centerY = work.top + static_cast<float>(work.bottom - work.top) * 0.82f - m_originY;

        const std::wstring wText = hasTextFormat
            ? easy::core::WinUtils::utf8ToWstring(m_resultText) : std::wstring{};

        if (hasTextFormat && !wText.empty() && m_dwriteFactory) {
            ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wText.c_str(), static_cast<UINT32>(wText.length()),
                m_textFormat.Get(),
                10000.0f, 1000.0f,
                layout.GetAddressOf()
            );

            float boxW = 120.0f * resultScale;
            float boxH = 48.0f * resultScale;
            if (layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    float paddingX = 28.0f * resultScale;
                    float paddingY = 14.0f * resultScale;
                    boxW = (std::max)(metrics.width + paddingX * 2.0f, 90.0f * resultScale);
                    boxH = (std::max)(metrics.height + paddingY * 2.0f, 44.0f * resultScale);
                }
            }

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                            centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                12.0f * resultScale, 12.0f * resultScale
            );

            // 1. 深色半透明卡片背景 (Keycast 风格)
            m_textBgBrush->SetOpacity(0.88f * m_fadeAlpha);
            m_renderTarget->FillRoundedRectangle(&rrect, m_textBgBrush.Get());

            // 2. 优雅紫调微发光描边
            if (m_textBorderBrush) {
                m_textBorderBrush->SetOpacity(0.60f * m_fadeAlpha);
                m_renderTarget->DrawRoundedRectangle(
                    &rrect, m_textBorderBrush.Get(), 1.5f * resultScale);
            }

            // 3. 高清居中文本渲染
            m_textBrush->SetOpacity(m_fadeAlpha);
            m_renderTarget->DrawText(
                wText.c_str(),
                static_cast<UINT32>(wText.size()),
                m_textFormat.Get(),
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                            centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                m_textBrush.Get());
        }
    }

    if (FAILED(m_renderTarget->EndDraw())) {
        LOG_WARN("手势轨迹 Direct2D 帧提交失败");
        return;
    }
    
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
        m_renderTimerActive.store(false);
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
                if (!self->m_visible.exchange(true)) {
                    ShowWindow(self->m_hwnd, SW_SHOWNOACTIVATE);
                }
            } else if (wParam == FADE_TIMER_ID) {
                // 淡出动画
                DWORD elapsed = GetTickCount() - self->m_fadeStartTick;
                float progress = static_cast<float>(elapsed) / self->m_style.fadeOutMs;

                if (progress >= 1.0f) {
                    self->hide();
                } else {
                    self->m_fadeAlpha = 1.0f - progress;
                    self->render();
                    if (!self->m_visible.exchange(true)) {
                        ShowWindow(self->m_hwnd, SW_SHOWNOACTIVATE);
                    }
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
                    HDC hdcScreen = GetDC(nullptr);
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = w;
                    bmi.bmiHeader.biHeight = -h;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    
                    void* pBits = nullptr;
                    HBITMAP replacement = hdcScreen ? CreateDIBSection(
                        hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0) : nullptr;
                    if (hdcScreen) ReleaseDC(nullptr, hdcScreen);

                    if (replacement && pBits) {
                        HBITMAP previousSurface = static_cast<HBITMAP>(
                            SelectObject(self->m_memoryDC, replacement));
                        if (previousSurface && previousSurface != HGDI_ERROR) {
                            self->m_memoryBitmap = replacement;
                            DeleteObject(previousSurface);
                        } else {
                            DeleteObject(replacement);
                            replacement = nullptr;
                        }
                    }

                    if (replacement && self->m_renderTarget) {
                        RECT memRect = {0, 0, w, h};
                        if (FAILED(self->m_renderTarget->BindDC(
                                self->m_memoryDC, &memRect))) {
                            self->releaseD2DResources();
                        }
                    } else if (!replacement) {
                        self->releaseD2DResources();
                    }
                    self->m_pathCache.clear();
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
