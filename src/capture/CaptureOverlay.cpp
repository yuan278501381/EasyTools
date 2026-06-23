// ─────────────────────────────────────────────────────────────────────────────
// CaptureOverlay.cpp — 截图区域选择覆盖层实现
//
// 交互流程:
//   1. 按截图快捷键 → 全屏截图冻结画面 → 显示覆盖层
//   2. 鼠标悬停 → 自动检测窗口边界并高亮
//   3. 按住左键拖拽 → 选择截图区域（实时显示尺寸）
//   4. 松开左键 → 进入标注模式（显示工具栏）
//   5. 按 Enter/双击确认 → 裁剪 + 标注 → 输出到剪贴板/文件
//   6. 按 ESC 取消
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/CaptureOverlay.h"
#include "capture/PinWindow.h"
#include "capture/ScrollCapture.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <ShellScalingApi.h>
#include <windowsx.h>
#include <imm.h>

namespace easy::capture {

using namespace Microsoft::WRL;

static constexpr const wchar_t* OVERLAY_CLASS = L"EasyTools_CaptureOverlay";
static constexpr UINT_PTR RENDER_TIMER_ID = 2001;

CaptureOverlay& CaptureOverlay::instance() {
    static CaptureOverlay inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化 / 关闭
// ─────────────────────────────────────────────────────────────────────────────

bool CaptureOverlay::initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    if (!createOverlayWindow(hInstance)) {
        LOG_ERROR("创建截图覆盖层窗口失败");
        return false;
    }

    m_dpiScale = GetDpiForWindow(m_hwnd) / 96.0f;
    if (!createRenderResources()) {
        LOG_ERROR("创建截图覆盖层渲染资源失败");
        return false;
    }

    LOG_INFO("截图覆盖层初始化成功");
    return true;
}

void CaptureOverlay::shutdown() {
    releaseRenderResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    LOG_DEBUG("截图覆盖层已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 截图流程
// ─────────────────────────────────────────────────────────────────────────────

void CaptureOverlay::startSelection(const CaptureOptions& options, OverlayMode mode) {
    easy::core::TraceId::Scope scope;
    m_options = options;
    m_mode = mode;
    m_state = OverlayState::Selecting;
    m_dragging = false;
    m_isMarking = false;
    m_markupBaseReady = false;
    m_activeElement = nullptr;
    m_dragHandle = HitArea::None;
    m_isManipulating = false;
    m_isAdjustingSelection = false;
    m_selAdjustHandle = HitArea::None;
    m_currentColor = MarkupColor::Red();
    m_markup.clearAll();
    m_markupCacheBitmap.Reset();
    m_markupCacheDirty = true;
    m_needsRender = true;
    m_loupeToastUntil = 0;
    m_showTimestamp = GetTickCount();
    m_isFadingOut = false;
    m_fadeOutStart = 0;
    m_toolbarButtons.clear();
    m_penPoints.clear();
    m_dragStart = {};
    m_dragEnd = {};

    // 冻结屏幕
    freezeScreen();

    // 显示覆盖层
    if (m_hwnd) {
        // 重新定位覆盖整个虚拟屏幕
        int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // 调整渲染目标大小
        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(w, h));
        }

        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
        SetCapture(m_hwnd);

        SetTimer(m_hwnd, RENDER_TIMER_ID, 16, nullptr);
    }

    LOG_INFO("截图选区模式已启动");
}

void CaptureOverlay::cancel() {
    if (!m_isFadingOut && m_hwnd && IsWindowVisible(m_hwnd)) {
        m_isFadingOut = true;
        m_fadeOutStart = GetTickCount();
        m_needsRender = true;
        LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT | WS_EX_LAYERED);
        SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
        SetTimer(m_hwnd, RENDER_TIMER_ID, 16, nullptr);
    } else {
        realCancel();
    }
}

void CaptureOverlay::realCancel() {
    KillTimer(m_hwnd, RENDER_TIMER_ID);
    ReleaseCapture();
    ShowWindow(m_hwnd, SW_HIDE);
    
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~(WS_EX_TRANSPARENT | WS_EX_LAYERED));

    m_state = OverlayState::Idle;
    m_isFadingOut = false;
    m_isMarking = false;
    m_markupBaseReady = false;
    m_activeElement = nullptr;
    m_isAdjustingSelection = false;
    m_toolbarButtons.clear();
    m_penPoints.clear();
    m_frozenScreen.release();
    m_screenBitmap.Reset();
    m_markupCacheBitmap.Reset();
    m_markupCacheDirty = true;
    m_loupeToastUntil = 0;
    LOG_DEBUG("截图已取消");
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool CaptureOverlay::createOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = overlayWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);  // 十字光标
    wc.hbrBackground = nullptr;
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);

    RECT bounds = easy::core::WinUtils::getVirtualScreenPhysicalBounds();
    int x = bounds.left;
    int y = bounds.top;
    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS, L"",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("CreateWindowExW 失败: error={}", GetLastError());
        return false;
    }
    
    // Disable IME for this window to allow WASD hotkeys
    ImmAssociateContext(m_hwnd, NULL);

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// D2D 资源
// ─────────────────────────────────────────────────────────────────────────────

bool CaptureOverlay::createRenderResources() {
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f), L"zh-CN", m_infoTextFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;
    m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_infoTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 文本工具的输入预览: 左对齐、较大字号
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        18.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f), L"zh-CN", m_textInputFormat.GetAddressOf()
    );
    if (SUCCEEDED(hr) && m_textInputFormat) {
        m_textInputFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_textInputFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    auto rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    auto hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // 禁用 D2D 的自动 DPI 缩放
    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 画笔
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.5f), m_dimBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 1.0f), m_borderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.8f), m_infoBgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), m_infoTextBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 0.6f), m_crosshairBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 0.3f), m_windowHighlightBrush.GetAddressOf());

    return true;
}

void CaptureOverlay::releaseRenderResources() {
    m_windowHighlightBrush.Reset();
    m_crosshairBrush.Reset();
    m_infoTextBrush.Reset();
    m_infoBgBrush.Reset();
    m_borderBrush.Reset();
    m_dimBrush.Reset();
    m_screenBitmap.Reset();
    m_infoTextFormat.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 冻结屏幕
// ─────────────────────────────────────────────────────────────────────────────

void CaptureOverlay::freezeScreen() {
    // 截取全屏，使用物理边界以防止高分屏下截图不全或偏移
    RECT bounds = easy::core::WinUtils::getVirtualScreenPhysicalBounds();
    CaptureRegion fullScreen;
    fullScreen.x = bounds.left;
    fullScreen.y = bounds.top;
    fullScreen.width = bounds.right - bounds.left;
    fullScreen.height = bounds.bottom - bounds.top;

    // 使用 BitBlt 截屏
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, fullScreen.width, fullScreen.height);
    SelectObject(memDC, hBitmap);
    BitBlt(memDC, 0, 0, fullScreen.width, fullScreen.height,
           screenDC, fullScreen.x, fullScreen.y, SRCCOPY);

    // 转为 BGRA cv::Mat
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = fullScreen.width;
    bi.biHeight = -fullScreen.height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    m_frozenScreen = cv::Mat(fullScreen.height, fullScreen.width, CV_8UC4);
    GetDIBits(memDC, hBitmap, 0, fullScreen.height, m_frozenScreen.data,
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    // 创建 D2D Bitmap
    if (m_renderTarget && !m_frozenScreen.empty()) {
        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        m_screenBitmap.Reset();
        m_renderTarget->CreateBitmap(
            D2D1::SizeU(fullScreen.width, fullScreen.height),
            m_frozenScreen.data,
            fullScreen.width * 4,
            bitmapProps,
            m_screenBitmap.GetAddressOf()
        );
    }

    LOG_DEBUG("屏幕已冻结: {}x{}", fullScreen.width, fullScreen.height);
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

void CaptureOverlay::render() {
    if (!m_renderTarget) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    // 绘制冻结的屏幕
    if (m_screenBitmap) {
        auto size = m_renderTarget->GetSize();
        m_renderTarget->DrawBitmap(m_screenBitmap.Get(),
                                   D2D1::RectF(0, 0, size.width, size.height));
    }

    auto state = m_state.load();

    if (state == OverlayState::Selecting && m_dragging) {
        // 正在拖拽选区
        float x1 = static_cast<float>(std::min(m_dragStart.x, m_dragEnd.x));
        float y1 = static_cast<float>(std::min(m_dragStart.y, m_dragEnd.y));
        float x2 = static_cast<float>(std::max(m_dragStart.x, m_dragEnd.x));
        float y2 = static_cast<float>(std::max(m_dragStart.y, m_dragEnd.y));

        D2D1_RECT_F selRect = D2D1::RectF(x1, y1, x2, y2);
        drawDimOverlay(selRect);
        drawSelection(selRect);
        drawSizeInfo(selRect);
        // 拖拽中也显示取色放大镜，便于像素级对齐选区边缘
        drawSelectionLoupe(static_cast<float>(m_currentCursor.x),
                           static_cast<float>(m_currentCursor.y));
    } else if (state == OverlayState::Selected || state == OverlayState::Marking) {
        // 选区已确认
        float x1 = static_cast<float>(std::min(m_dragStart.x, m_dragEnd.x));
        float y1 = static_cast<float>(std::min(m_dragStart.y, m_dragEnd.y));
        float x2 = static_cast<float>(std::max(m_dragStart.x, m_dragEnd.x));
        float y2 = static_cast<float>(std::max(m_dragStart.y, m_dragEnd.y));

        D2D1_RECT_F selRect = D2D1::RectF(x1, y1, x2, y2);
        drawDimOverlay(selRect);
        drawMarkupPreview(selRect);
        drawActiveMarkupPreview(selRect);

        if (m_currentTool == MarkupTool::Magnifier && !m_isMarking && !m_isManipulating) {
            drawDynamicMagnifier();
        }

        drawSelection(selRect);
        drawSizeInfo(selRect);
        drawToolbar(selRect);
    } else {
        // 空闲/选区前 — 全屏变暗 + 十字准星 + 窗口高亮
        auto size = m_renderTarget->GetSize();
        m_renderTarget->FillRectangle(
            D2D1::RectF(0, 0, size.width, size.height), m_dimBrush.Get()
        );

        // 检测光标下的窗口并高亮其边界
        if (m_detectedWindow.right > m_detectedWindow.left && 
            m_detectedWindow.bottom > m_detectedWindow.top) {
            D2D1_RECT_F winRect = D2D1::RectF(
                static_cast<float>(m_detectedWindow.left),
                static_cast<float>(m_detectedWindow.top),
                static_cast<float>(m_detectedWindow.right),
                static_cast<float>(m_detectedWindow.bottom)
            );
            // 窗口区域显示原始截图（去掉变暗效果）
            if (m_screenBitmap) {
                m_renderTarget->DrawBitmap(m_screenBitmap.Get(), winRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &winRect);
            }
            // 紫色边框高亮
            m_renderTarget->DrawRectangle(winRect, m_borderBrush.Get(), 2.0f);
            // 显示窗口尺寸
            drawSizeInfo(winRect);
        }

        drawCrosshair(static_cast<float>(m_currentCursor.x),
                      static_cast<float>(m_currentCursor.y));
        // 预选悬停时显示像素级取色放大镜（坐标 + RGB/HEX，按 C 复制）
        drawSelectionLoupe(static_cast<float>(m_currentCursor.x),
                           static_cast<float>(m_currentCursor.y));
    }

    m_renderTarget->EndDraw();
}

void CaptureOverlay::drawDimOverlay(const D2D1_RECT_F& selRect) {
    auto size = m_renderTarget->GetSize();

    // 上
    m_renderTarget->FillRectangle(D2D1::RectF(0, 0, size.width, selRect.top), m_dimBrush.Get());
    // 下
    m_renderTarget->FillRectangle(D2D1::RectF(0, selRect.bottom, size.width, size.height), m_dimBrush.Get());
    // 左
    m_renderTarget->FillRectangle(D2D1::RectF(0, selRect.top, selRect.left, selRect.bottom), m_dimBrush.Get());
    // 右
    m_renderTarget->FillRectangle(D2D1::RectF(selRect.right, selRect.top, size.width, selRect.bottom), m_dimBrush.Get());
}

void CaptureOverlay::drawSelection(const D2D1_RECT_F& rect) {
    // 选区边框（紫色）
    m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f);

    // 八个控制点
    float controlSize = 6.0f;
    float cx = (rect.left + rect.right) / 2;
    float cy = (rect.top + rect.bottom) / 2;

    D2D1_POINT_2F controls[] = {
        {rect.left, rect.top}, {cx, rect.top}, {rect.right, rect.top},
        {rect.left, cy}, {rect.right, cy},
        {rect.left, rect.bottom}, {cx, rect.bottom}, {rect.right, rect.bottom}
    };

    for (auto& pt : controls) {
        m_renderTarget->FillRectangle(
            D2D1::RectF(pt.x - controlSize / 2, pt.y - controlSize / 2,
                        pt.x + controlSize / 2, pt.y + controlSize / 2),
            m_borderBrush.Get()
        );
    }
}

void CaptureOverlay::drawSizeInfo(const D2D1_RECT_F& rect) {
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);

    auto info = std::format(L"{}×{}", w, h);

    // 尺寸标签背景
    float labelW = 100.0f;
    float labelH = 24.0f;
    float labelX = rect.left;
    float labelY = rect.top - labelH - 4;
    if (labelY < 0) labelY = rect.bottom + 4;

    // 统一玻璃风：尺寸标签也用磨砂面板（小标签走廉价无图层模式）
    drawGlassPanel(D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH), 5.0f, false);
    m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                             m_infoTextFormat.Get(),
                             D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH),
                             m_infoTextBrush.Get());
}

// 工具栏按钮 → 人类可读提示（名称 + 快捷键），解决生僻字形不易辨识的问题
static std::wstring tooltipForButton(const ToolbarButton& b, bool zh) {
    switch (b.command) {
        case ToolbarCommand::SelectTool:
            switch (b.tool) {
                case MarkupTool::Rectangle: return zh ? L"矩形  R"    : L"Rectangle  R";
                case MarkupTool::Arrow:     return zh ? L"箭头  A"    : L"Arrow  A";
                case MarkupTool::Ellipse:   return zh ? L"椭圆  O"    : L"Ellipse  O";
                case MarkupTool::Pen:       return zh ? L"画笔  P"    : L"Pen  P";
                case MarkupTool::Highlight: return zh ? L"高亮  H"    : L"Highlight  H";
                case MarkupTool::Mosaic:    return zh ? L"马赛克  M"  : L"Mosaic  M";
                case MarkupTool::Text:      return zh ? L"文字  T"    : L"Text  T";
                case MarkupTool::Number:    return zh ? L"序号  N"    : L"Number  N";
                case MarkupTool::Magnifier: return zh ? L"放大镜  G"  : L"Magnifier  G";
                default: return L"";
            }
        case ToolbarCommand::SelectColor: return zh ? L"颜色  1-5" : L"Color  1-5";
        case ToolbarCommand::Undo:        return zh ? L"撤销  Ctrl+Z" : L"Undo  Ctrl+Z";
        case ToolbarCommand::Redo:        return zh ? L"重做  Ctrl+Y" : L"Redo  Ctrl+Y";
        case ToolbarCommand::Clear:       return zh ? L"清空全部" : L"Clear all";
        case ToolbarCommand::ExtractText: return zh ? L"提取文字 (OCR)" : L"Extract text (OCR)";
        case ToolbarCommand::PinWindow:   return zh ? L"贴图置顶" : L"Pin to screen";
        case ToolbarCommand::ScrollCapture: return zh ? L"滚动长截图" : L"Scrolling capture";
        case ToolbarCommand::Cancel:      return zh ? L"取消  Esc" : L"Cancel  Esc";
        case ToolbarCommand::Confirm:     return zh ? L"完成  Enter" : L"Done  Enter";
        default: return L"";
    }
}

void CaptureOverlay::drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough) {
    if (!m_renderTarget) return;
    auto rr = D2D1::RoundedRect(rect, radius, radius);

    ComPtr<ID2D1SolidColorBrush> tint, sheen, border;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.74f), tint.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), sheen.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), border.GetAddressOf());

    // 透视玻璃：圆角裁剪 → 透出未暗化的背后画面 → 深色蒙版（图层，较重）。
    bool glass = false;
    if (seeThrough && m_d2dFactory && m_screenBitmap) {
        ComPtr<ID2D1RoundedRectangleGeometry> geo;
        if (SUCCEEDED(m_d2dFactory->CreateRoundedRectangleGeometry(rr, geo.GetAddressOf())) && geo) {
            ComPtr<ID2D1Layer> layer;
            if (SUCCEEDED(m_renderTarget->CreateLayer(layer.GetAddressOf())) && layer) {
                m_renderTarget->PushLayer(
                    D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get(),
                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                        1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE),
                    layer.Get());
                m_renderTarget->DrawBitmap(m_screenBitmap.Get(), rect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &rect);
                if (tint) m_renderTarget->FillRectangle(rect, tint.Get());
                m_renderTarget->PopLayer();
                glass = true;
            }
        }
    }
    if (!glass) {
        // 廉价磨砂：半透明深色直接叠加在当前帧上（透出已暗化的背后画面），无图层开销。
        if (tint) m_renderTarget->FillRoundedRectangle(rr, tint.Get());
        else m_renderTarget->FillRoundedRectangle(rr, m_infoBgBrush.Get());
    }

    // 顶部高光（玻璃边沿反光）+ 细边框
    if (sheen) m_renderTarget->DrawLine(
        D2D1::Point2F(rect.left + radius, rect.top + 1.0f),
        D2D1::Point2F(rect.right - radius, rect.top + 1.0f), sheen.Get(), 1.2f);
    if (border) m_renderTarget->DrawRoundedRectangle(rr, border.Get(), 1.0f);
}

void CaptureOverlay::drawToolbar(const D2D1_RECT_F& selectionRect) {
    rebuildToolbarButtons(selectionRect);
    if (m_toolbarButtons.empty()) return;

    auto bgRect = D2D1::RectF(
        m_toolbarButtons.front().rect.left - 8.0f,
        m_toolbarButtons.front().rect.top - 6.0f,
        m_toolbarButtons.back().rect.right + 8.0f,
        m_toolbarButtons.back().rect.bottom + 6.0f
    );
    // 磨砂玻璃 HUD 面板（替代原扁平深色条）
    drawGlassPanel(bgRect, 9.0f);

    // 分组分隔线（工具 | 颜色 | 命令），提升辨识度
    if (m_mode != OverlayMode::RecordRegion && m_toolbarButtons.size() >= 22) {
        ComPtr<ID2D1SolidColorBrush> sep;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.14f), sep.GetAddressOf());
        float y0 = m_toolbarButtons.front().rect.top + 3.0f;
        float y1 = m_toolbarButtons.front().rect.bottom - 3.0f;
        auto drawSep = [&](size_t l, size_t r) {
            if (sep && r < m_toolbarButtons.size()) {
                float mx = (m_toolbarButtons[l].rect.right + m_toolbarButtons[r].rect.left) * 0.5f;
                m_renderTarget->DrawLine(D2D1::Point2F(mx, y0), D2D1::Point2F(mx, y1), sep.Get(), 1.0f);
            }
        };
        drawSep(8, 9);    // 工具 | 颜色
        drawSep(13, 14);  // 颜色 | 命令
    }

    ComPtr<ID2D1SolidColorBrush> buttonBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<ID2D1SolidColorBrush> dangerBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), buttonBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 0.95f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.25f, 0.25f, 0.85f), dangerBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.20f), hoverBrush.GetAddressOf());

    for (const auto& button : m_toolbarButtons) {
        bool isTool = button.command == ToolbarCommand::SelectTool;
        bool isActiveTool = isTool && button.tool == m_currentTool;
        bool isColor = button.command == ToolbarCommand::SelectColor;
        bool isDanger = button.command == ToolbarCommand::Cancel || button.command == ToolbarCommand::Clear;
        bool isHovered = m_currentCursor.x >= button.rect.left && m_currentCursor.x <= button.rect.right &&
                         m_currentCursor.y >= button.rect.top  && m_currentCursor.y <= button.rect.bottom;

        auto rounded = D2D1::RoundedRect(button.rect, 5.0f, 5.0f);

        if (isColor) {
            // 颜色色板: 用色块填充, 当前色加白色描边, 悬停加淡描边
            ComPtr<ID2D1SolidColorBrush> swatch;
            m_renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(button.color.r / 255.0f, button.color.g / 255.0f,
                             button.color.b / 255.0f, 1.0f),
                swatch.GetAddressOf());
            if (swatch) m_renderTarget->FillRoundedRectangle(rounded, swatch.Get());
            bool isActiveColor = button.color.r == m_currentColor.r &&
                                 button.color.g == m_currentColor.g &&
                                 button.color.b == m_currentColor.b;
            if (isActiveColor) {
                m_renderTarget->DrawRoundedRectangle(rounded, m_infoTextBrush.Get(), 2.0f);
            } else if (isHovered && hoverBrush) {
                m_renderTarget->DrawRoundedRectangle(rounded, hoverBrush.Get(), 1.5f);
            }
            continue;  // 色板不画文字
        }

        auto* fillBrush = isActiveTool ? activeBrush.Get()
                        : isDanger ? dangerBrush.Get()
                        : isHovered ? hoverBrush.Get()
                        : buttonBrush.Get();
        m_renderTarget->FillRoundedRectangle(rounded, fillBrush);

        if (isActiveTool) {
            m_renderTarget->DrawRoundedRectangle(rounded, m_infoTextBrush.Get(), 1.2f);
        }

        m_renderTarget->DrawText(
            button.label.c_str(), static_cast<UINT32>(button.label.size()),
            m_infoTextFormat.Get(),
            button.rect,
            m_infoTextBrush.Get()
        );
    }

    // 悬停浮层提示：把生僻字形翻译成「名称 + 快捷键」
    bool isZh = easy::core::WinUtils::isSystemLanguageChinese();
    for (const auto& button : m_toolbarButtons) {
        if (m_currentCursor.x < button.rect.left || m_currentCursor.x > button.rect.right ||
            m_currentCursor.y < button.rect.top  || m_currentCursor.y > button.rect.bottom) {
            continue;
        }
        std::wstring tip = tooltipForButton(button, isZh);
        if (tip.empty()) break;

        float tw = 16.0f + static_cast<float>(tip.size()) * 12.0f;  // 粗略估宽，足够容纳中英文
        float th = 22.0f;
        auto sz = m_renderTarget->GetSize();
        float bcx = (button.rect.left + button.rect.right) * 0.5f;
        float tx = std::clamp(bcx - tw * 0.5f, 4.0f, std::max(4.0f, sz.width - tw - 4.0f));
        float ty = button.rect.top - th - 6.0f;
        if (ty < 4.0f) ty = button.rect.bottom + 6.0f;

        drawGlassPanel(D2D1::RectF(tx, ty, tx + tw, ty + th), 5.0f, false);
        m_renderTarget->DrawText(tip.c_str(), static_cast<UINT32>(tip.size()),
                                 m_infoTextFormat.Get(),
                                 D2D1::RectF(tx, ty, tx + tw, ty + th),
                                 m_infoTextBrush.Get());
        break;
    }
}

void CaptureOverlay::drawMarkupPreview(const D2D1_RECT_F& selectionRect) {
    if (!m_markupBaseReady || m_markup.elementCount() == 0 || !m_renderTarget) return;

    // 仅在标注内容变化时才重新合成并重建 D2D 位图，否则复用缓存。
    // 这条路径原本每帧（16ms）都 clone 整图 + 重绘全部标注 + 新建位图，是主要性能浪费点。
    if (m_markupCacheDirty || !m_markupCacheBitmap) {
        cv::Mat composite = m_markup.getCompositeImage();
        if (composite.empty()) return;

        cv::Mat bgra;
        if (composite.channels() == 3) {
            cv::cvtColor(composite, bgra, cv::COLOR_BGR2BGRA);
        } else if (composite.channels() == 4) {
            bgra = composite;
        } else {
            return;
        }

        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        ComPtr<ID2D1Bitmap> bitmap;
        HRESULT hr = m_renderTarget->CreateBitmap(
            D2D1::SizeU(bgra.cols, bgra.rows),
            bgra.data,
            bgra.cols * 4,
            bitmapProps,
            bitmap.GetAddressOf()
        );
        if (FAILED(hr) || !bitmap) return;
        m_markupCacheBitmap = bitmap;
        m_markupCacheDirty = false;
    }

    if (m_markupCacheBitmap) {
        m_renderTarget->DrawBitmap(m_markupCacheBitmap.Get(), selectionRect);
    }
}

void CaptureOverlay::drawActiveMarkupPreview(const D2D1_RECT_F& selectionRect) {
    if (!m_isMarking || !m_renderTarget) return;

    float x1 = static_cast<float>(m_markupStart.x);
    float y1 = static_cast<float>(m_markupStart.y);
    float x2 = static_cast<float>(m_markupEnd.x);
    float y2 = static_cast<float>(m_markupEnd.y);
    auto rect = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

    switch (m_currentTool) {
        case MarkupTool::Rectangle:
        case MarkupTool::Mosaic:
            m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f);
            break;

        case MarkupTool::Highlight: {
            ComPtr<ID2D1SolidColorBrush> highlightBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.78f, 0.18f, 0.28f), highlightBrush.GetAddressOf());
            m_renderTarget->FillRectangle(rect, highlightBrush.Get());
            m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 1.0f);
            break;
        }

        case MarkupTool::Arrow:
            m_renderTarget->DrawLine(
                D2D1::Point2F(x1, y1),
                D2D1::Point2F(x2, y2),
                m_borderBrush.Get(),
                2.0f
            );
            break;

        case MarkupTool::Ellipse:
            m_renderTarget->DrawEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F((x1 + x2) / 2.0f, (y1 + y2) / 2.0f),
                    std::abs(x2 - x1) / 2.0f,
                    std::abs(y2 - y1) / 2.0f
                ),
                m_borderBrush.Get(),
                2.0f
            );
            break;

        case MarkupTool::Pen:
            if (m_penPoints.size() >= 2) {
                for (size_t i = 1; i < m_penPoints.size(); ++i) {
                    m_renderTarget->DrawLine(
                        D2D1::Point2F(selectionRect.left + static_cast<float>(m_penPoints[i - 1].x),
                                      selectionRect.top + static_cast<float>(m_penPoints[i - 1].y)),
                        D2D1::Point2F(selectionRect.left + static_cast<float>(m_penPoints[i].x),
                                      selectionRect.top + static_cast<float>(m_penPoints[i].y)),
                        m_borderBrush.Get(),
                        2.0f
                    );
                }
            }
            break;

        default:
            break;
    }
}

void CaptureOverlay::drawDynamicMagnifier() {
    if (!m_renderTarget || !m_screenBitmap) return;

    float r = static_cast<float>(m_dynamicMagnifierRadius);
    float scale = m_dynamicMagnifierScale;

    // 获取光标在覆盖层中的位置 (屏幕坐标 -> 覆盖层坐标)
    float cx = static_cast<float>(m_currentCursor.x);
    float cy = static_cast<float>(m_currentCursor.y);

    float srcR = r / scale;
    D2D1_RECT_F srcRect = D2D1::RectF(cx - srcR, cy - srcR, cx + srcR, cy + srcR);
    D2D1_RECT_F dstRect = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);

    // 创建圆形裁剪
    ComPtr<ID2D1EllipseGeometry> ellipse;
    m_d2dFactory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), ellipse.GetAddressOf());

    if (ellipse) {
        ComPtr<ID2D1Layer> layer;
        m_renderTarget->CreateLayer(layer.GetAddressOf());
        m_renderTarget->PushLayer(D2D1::LayerParameters(
            D2D1::InfiniteRect(), ellipse.Get(),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(),
            1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE), layer.Get());

        m_renderTarget->DrawBitmap(m_screenBitmap.Get(), dstRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, srcRect);

        m_renderTarget->PopLayer();

        // 边框
        m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_borderBrush.Get(), 2.0f);
        
        // 十字准星
        m_renderTarget->DrawLine(D2D1::Point2F(cx - 5, cy), D2D1::Point2F(cx + 5, cy), m_borderBrush.Get(), 1.0f);
        m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 5), D2D1::Point2F(cx, cy + 5), m_borderBrush.Get(), 1.0f);
    }
}

void CaptureOverlay::drawCrosshair(float x, float y) {
    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, size.height), m_crosshairBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(size.width, y), m_crosshairBrush.Get(), 1.0f);
}

bool CaptureOverlay::sampleScreenColor(int x, int y, int& r, int& g, int& b) const {
    if (m_frozenScreen.empty()) return false;
    if (x < 0 || y < 0 || x >= m_frozenScreen.cols || y >= m_frozenScreen.rows) return false;
    // 冻结图为 BGRA（freezeScreen 写入 CV_8UC4）
    const cv::Vec4b& px = m_frozenScreen.at<cv::Vec4b>(y, x);
    b = px[0]; g = px[1]; r = px[2];
    return true;
}

void CaptureOverlay::drawSelectionLoupe(float cx, float cy) {
    if (!m_renderTarget || !m_screenBitmap) return;

    int px = static_cast<int>(cx);
    int py = static_cast<int>(cy);
    int r = 0, g = 0, b = 0;
    bool hasColor = sampleScreenColor(px, py, r, g, b);

    float scale = (m_dpiScale > 0) ? m_dpiScale : 1.0f;
    constexpr float zoom = 9.0f; 
    
    int gridCountX = 17;
    int gridCountY = 7;
    float loupeBoxW = gridCountX * zoom * scale;
    float loupeBoxH = gridCountY * zoom * scale;
    float panelH = 96.0f * scale; 
    const float totalH = loupeBoxH + panelH;
    const float pad = 12.0f * scale;

    auto size = m_renderTarget->GetSize();

    float lx = cx + 24.0f * scale;
    float ly = cy + 24.0f * scale;

    if (lx + loupeBoxW + pad > size.width) lx = cx - 24.0f * scale - loupeBoxW;
    if (ly + totalH + pad > size.height) ly = cy - 24.0f * scale - totalH;
    lx = std::clamp(lx, pad, std::max(pad, size.width - loupeBoxW - pad));
    ly = std::clamp(ly, pad, std::max(pad, size.height - totalH - pad));

    D2D1_RECT_F dst = D2D1::RectF(lx, ly, lx + loupeBoxW, ly + loupeBoxH);
    
    float srcHalfX = gridCountX / 2.0f; 
    float srcHalfY = gridCountY / 2.0f; 
    D2D1_RECT_F src = D2D1::RectF(cx - srcHalfX, cy - srcHalfY, cx + srcHalfX, cy + srcHalfY);
    
    m_renderTarget->DrawBitmap(m_screenBitmap.Get(), dst, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);

    ComPtr<ID2D1SolidColorBrush> gridBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.4f), gridBrush.GetAddressOf());
    if (gridBrush) {
        for (int i = 0; i <= gridCountY; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx, ly + lineOffset), D2D1::Point2F(lx + loupeBoxW, ly + lineOffset), gridBrush.Get(), 1.0f);
        }
        for (int i = 0; i <= gridCountX; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx + lineOffset, ly), D2D1::Point2F(lx + lineOffset, ly + loupeBoxH), gridBrush.Get(), 1.0f);
        }
    }

    ComPtr<ID2D1SolidColorBrush> blueCrosshair;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.7f, 1.0f, 0.6f), blueCrosshair.GetAddressOf());
    
    float pixelSz = zoom * scale;
    float mcx = lx + (gridCountX / 2) * pixelSz; 
    float mcy = ly + (gridCountY / 2) * pixelSz; 

    if (blueCrosshair) {
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, ly, mcx + pixelSz, mcy), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, mcy + pixelSz, mcx + pixelSz, ly + loupeBoxH), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(lx, mcy, mcx, mcy + pixelSz), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx + pixelSz, mcy, lx + loupeBoxW, mcy + pixelSz), blueCrosshair.Get());
    }

    ComPtr<ID2D1SolidColorBrush> blackBrush, whiteBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0,0,0, 1.0f), blackBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1,1,1, 1.0f), whiteBrush.GetAddressOf());
    if (blackBrush && whiteBrush) {
        D2D1_RECT_F centerCell = D2D1::RectF(mcx, mcy, mcx + pixelSz, mcy + pixelSz);
        m_renderTarget->DrawRectangle(centerCell, blackBrush.Get(), 1.5f);
    }
    
    m_renderTarget->DrawRectangle(dst, blackBrush.Get(), 1.0f);

    D2D1_RECT_F panel = D2D1::RectF(lx, ly + loupeBoxH, lx + loupeBoxW, ly + totalH);
    ComPtr<ID2D1SolidColorBrush> panelBg;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.95f), panelBg.GetAddressOf());
    if (panelBg) m_renderTarget->FillRectangle(panel, panelBg.Get());

    if (hasColor && m_infoTextFormat && m_dwriteFactory) {
        std::wstring text1 = std::format(L"({} , {})", px, py);
        std::wstring text2 = m_colorFormatHex ? 
                             std::format(L"#{:02X}{:02X}{:02X}", r, g, b) : 
                             std::format(L"rgb({}, {}, {})", r, g, b);
        std::wstring text3 = L"Shift: 切换颜色格式";
        
        bool toast = (m_loupeToastUntil != 0 && GetTickCount() < m_loupeToastUntil);
        std::wstring text4 = toast ? L"已复制!" : L"C: 复制颜色值";

        float tx = lx;
        float tw = loupeBoxW;
        float currY = ly + loupeBoxH + 8.0f * scale;
        
        m_renderTarget->DrawTextW(text1.c_str(), (UINT32)text1.size(), m_infoTextFormat.Get(),
            D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), m_infoTextBrush.Get());
        currY += 22.0f * scale;

        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(), 1000.0f, 24.0f * scale, layout.GetAddressOf());
        float textW = 0;
        if (layout) {
            DWRITE_TEXT_METRICS metrics;
            layout->GetMetrics(&metrics);
            textW = metrics.width;
        }

        float boxSz = 12.0f * scale;
        float gap = 6.0f * scale;
        float totalBlockW = boxSz + gap + textW;
        float startX = tx + (tw - totalBlockW) / 2.0f;

        ComPtr<ID2D1SolidColorBrush> colorBox;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f), colorBox.GetAddressOf());
        if (colorBox) {
            float boxY = currY + (20.0f * scale - boxSz) / 2.0f; // center vertically with text
            m_renderTarget->FillRectangle(D2D1::RectF(startX, boxY, startX + boxSz, boxY + boxSz), colorBox.Get());
            m_renderTarget->DrawRectangle(D2D1::RectF(startX, boxY, startX + boxSz, boxY + boxSz), whiteBrush.Get(), 1.0f * scale);
            
            m_renderTarget->DrawTextW(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(),
                D2D1::RectF(startX + boxSz + gap, currY, startX + totalBlockW, currY + 24.0f * scale), m_infoTextBrush.Get());
        }

        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        currY += 22.0f * scale;

        ComPtr<ID2D1SolidColorBrush> hintBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.8f, 0.8f, 1.0f), hintBrush.GetAddressOf());
        m_renderTarget->DrawTextW(text3.c_str(), (UINT32)text3.size(), m_infoTextFormat.Get(),
            D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), hintBrush.Get());
        currY += 20.0f * scale;

        m_renderTarget->DrawTextW(text4.c_str(), (UINT32)text4.size(), m_infoTextFormat.Get(),
            D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), hintBrush.Get());
    }

    m_renderTarget->DrawRectangle(D2D1::RectF(lx, ly, lx + loupeBoxW, ly + totalH), blackBrush.Get(), 1.0f);
}

RECT CaptureOverlay::detectWindowUnderCursor(POINT cursorPos) {
    HWND hwnd = WindowFromPoint(cursorPos);
    RECT rc{};
    if (hwnd) {
        GetWindowRect(hwnd, &rc);
        // 转为虚拟屏幕坐标
        int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rc.left -= offsetX;
        rc.top -= offsetY;
        rc.right -= offsetX;
        rc.bottom -= offsetY;
    }
    return rc;
}

D2D1_RECT_F CaptureOverlay::currentSelectionRect() const {
    float x1 = static_cast<float>(std::min(m_dragStart.x, m_dragEnd.x));
    float y1 = static_cast<float>(std::min(m_dragStart.y, m_dragEnd.y));
    float x2 = static_cast<float>(std::max(m_dragStart.x, m_dragEnd.x));
    float y2 = static_cast<float>(std::max(m_dragStart.y, m_dragEnd.y));
    return D2D1::RectF(x1, y1, x2, y2);
}

// ─────────────────────────────────────────────────────────────────────────────
// 选区二次调整 / 工具切换 / 光标反馈
// ─────────────────────────────────────────────────────────────────────────────

HitArea CaptureOverlay::hitTestSelectionBox(POINT point) const {
    auto r = currentSelectionRect();
    if (r.right - r.left < 1.0f || r.bottom - r.top < 1.0f) return HitArea::None;

    const float hw = 9.0f;   // 控制点命中半径
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    struct H { float x, y; HitArea area; };
    const H handles[8] = {
        {r.left,  r.top,    HitArea::LT}, {cx,      r.top,    HitArea::T},
        {r.right, r.top,    HitArea::RT}, {r.right, cy,       HitArea::R},
        {r.right, r.bottom, HitArea::RB}, {cx,      r.bottom, HitArea::B},
        {r.left,  r.bottom, HitArea::LB}, {r.left,  cy,       HitArea::L},
    };
    float px = static_cast<float>(point.x);
    float py = static_cast<float>(point.y);
    for (const auto& h : handles) {
        if (std::abs(px - h.x) <= hw && std::abs(py - h.y) <= hw) return h.area;
    }

    // 边框带（用于整体移动）：靠近任意一条边即可拖动整块选区
    const float band = 6.0f;
    bool insideX = px >= r.left - band && px <= r.right + band;
    bool insideY = py >= r.top - band && py <= r.bottom + band;
    bool nearLeft   = std::abs(px - r.left)   <= band && insideY;
    bool nearRight  = std::abs(px - r.right)  <= band && insideY;
    bool nearTop    = std::abs(py - r.top)    <= band && insideX;
    bool nearBottom = std::abs(py - r.bottom) <= band && insideX;
    if (nearLeft || nearRight || nearTop || nearBottom) return HitArea::Body;
    return HitArea::None;
}

void CaptureOverlay::adjustSelection(HitArea handle, int dx, int dy) {
    // 记录调整前的选区左上角，用于已有标注的坐标重映射
    int oldLeft = std::min(m_dragStart.x, m_dragEnd.x);
    int oldTop  = std::min(m_dragStart.y, m_dragEnd.y);

    float l = static_cast<float>(std::min(m_dragStart.x, m_dragEnd.x));
    float t = static_cast<float>(std::min(m_dragStart.y, m_dragEnd.y));
    float r = static_cast<float>(std::max(m_dragStart.x, m_dragEnd.x));
    float b = static_cast<float>(std::max(m_dragStart.y, m_dragEnd.y));

    if (handle == HitArea::Body) {
        l += dx; r += dx; t += dy; b += dy;
    } else {
        switch (handle) {
            case HitArea::LT: l += dx; t += dy; break;
            case HitArea::T:  t += dy; break;
            case HitArea::RT: r += dx; t += dy; break;
            case HitArea::R:  r += dx; break;
            case HitArea::RB: r += dx; b += dy; break;
            case HitArea::B:  b += dy; break;
            case HitArea::LB: l += dx; b += dy; break;
            case HitArea::L:  l += dx; break;
            default: break;
        }
    }

    float maxW = 1.0e6f, maxH = 1.0e6f;
    if (m_renderTarget) {
        auto s = m_renderTarget->GetSize();
        maxW = s.width; maxH = s.height;
    }

    if (handle == HitArea::Body) {
        // 移动：整体夹取到屏幕内，保持尺寸不变
        float w = r - l, h = b - t;
        if (l < 0)     { l = 0;        r = w; }
        if (t < 0)     { t = 0;        b = h; }
        if (r > maxW)  { r = maxW;     l = maxW - w; }
        if (b > maxH)  { b = maxH;     t = maxH - h; }
    } else {
        // 缩放：夹取边界并保证最小尺寸
        l = std::clamp(l, 0.0f, maxW); r = std::clamp(r, 0.0f, maxW);
        t = std::clamp(t, 0.0f, maxH); b = std::clamp(b, 0.0f, maxH);
        const float minSize = 8.0f;
        if (r - l < minSize) {
            if (handle == HitArea::L || handle == HitArea::LT || handle == HitArea::LB) l = r - minSize;
            else r = l + minSize;
        }
        if (b - t < minSize) {
            if (handle == HitArea::T || handle == HitArea::LT || handle == HitArea::RT) t = b - minSize;
            else b = t + minSize;
        }
    }

    m_dragStart = { static_cast<LONG>(l), static_cast<LONG>(t) };
    m_dragEnd   = { static_cast<LONG>(r), static_cast<LONG>(b) };

    int newLeft = std::min(m_dragStart.x, m_dragEnd.x);
    int newTop  = std::min(m_dragStart.y, m_dragEnd.y);

    if (m_markup.hasAnyMarkup()) {
        // 已有标注（含撤销栈）：按左上角位移反向平移，使其"钉"在原屏幕内容上；并按新选区重裁底图
        m_markup.translateAll(oldLeft - newLeft, oldTop - newTop);
        rebuildMarkupBase();
        markMarkupDirty();
    } else {
        // 无任何标注：底图在下次标注时按新选区再裁（避免每帧重裁）
        m_markupBaseReady = false;
    }
    m_needsRender = true;
}

void CaptureOverlay::rebuildMarkupBase() {
    if (m_frozenScreen.empty()) return;
    auto rect = currentSelectionRect();
    int x = static_cast<int>(rect.left);
    int y = static_cast<int>(rect.top);
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);
    if (w <= 0 || h <= 0) return;

    cv::Rect roi(x, y, w, h);
    roi &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);
    if (roi.area() <= 0) return;

    cv::Mat cropped;
    m_frozenScreen(roi).copyTo(cropped);
    if (cropped.channels() == 4) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
    }
    m_markup.updateBaseImage(cropped);  // 保留标注，仅替换底图
    m_markupBaseReady = true;
}

void CaptureOverlay::setCurrentTool(MarkupTool tool) {
    if (m_activeElement) {
        m_activeElement->isActive = false;
        m_activeElement->isEditing = false;
        m_activeElement = nullptr;
    }
    m_currentTool = tool;
    m_isMarking = false;
    if (m_state == OverlayState::Marking) m_state = OverlayState::Selected;
    markMarkupDirty();
}

static HCURSOR cursorForArea(HitArea area) {
    switch (area) {
        case HitArea::LT: case HitArea::RB: return LoadCursor(nullptr, IDC_SIZENWSE);
        case HitArea::RT: case HitArea::LB: return LoadCursor(nullptr, IDC_SIZENESW);
        case HitArea::T:  case HitArea::B:  return LoadCursor(nullptr, IDC_SIZENS);
        case HitArea::L:  case HitArea::R:  return LoadCursor(nullptr, IDC_SIZEWE);
        case HitArea::Body: return LoadCursor(nullptr, IDC_SIZEALL);
        default: return nullptr;
    }
}

void CaptureOverlay::updateHoverCursor(POINT point) {
    HCURSOR cur = nullptr;
    auto st = m_state.load();
    if (st == OverlayState::Selected || st == OverlayState::Marking) {
        if (hitTestToolbar(point)) {
            cur = LoadCursor(nullptr, IDC_ARROW);
        } else {
            // 选区控制点/边框优先（与点击命中顺序一致），其次才是选中元素的手柄
            HitArea sel = hitTestSelectionBox(point);
            if (sel != HitArea::None) {
                cur = cursorForArea(sel);
            } else {
                if (m_activeElement) {
                    cur = cursorForArea(m_activeElement->hitTestEx(toMarkupPoint(point)));
                }
                if (!cur) {
                    HitResult hit = m_markup.getElementAtEx(toMarkupPoint(point));
                    if (hit.element) {
                        cur = cursorForArea(hit.area);
                    }
                }
            }
        }
    }
    if (!cur) cur = LoadCursor(nullptr, IDC_CROSS);
    SetCursor(cur);
}

void CaptureOverlay::prepareMarkupBase() {
    if (m_markupBaseReady || m_frozenScreen.empty()) return;

    auto rect = currentSelectionRect();
    int x = static_cast<int>(rect.left);
    int y = static_cast<int>(rect.top);
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);
    if (w <= 0 || h <= 0) return;

    cv::Rect roiRect(x, y, w, h);
    roiRect &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);
    if (roiRect.area() <= 0) return;

    cv::Mat cropped;
    m_frozenScreen(roiRect).copyTo(cropped);
    if (cropped.channels() == 4) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
    }

    m_markup.setBaseImage(cropped);
    m_markupBaseReady = true;
}

void CaptureOverlay::rebuildToolbarButtons(const D2D1_RECT_F& selectionRect) {
    m_toolbarButtons.clear();
    if (!m_renderTarget) return;

    struct ToolSpec {
        MarkupTool tool;
        const wchar_t* label;
    };

    static constexpr std::array<ToolSpec, 9> tools{{
        {MarkupTool::Rectangle, L"□"},
        {MarkupTool::Arrow, L"↗"},
        {MarkupTool::Ellipse, L"○"},
        {MarkupTool::Pen, L"✎"},
        {MarkupTool::Highlight, L"▰"},
        {MarkupTool::Mosaic, L"▦"},
        {MarkupTool::Text, L"T"},
        {MarkupTool::Number, L"①"},
        {MarkupTool::Magnifier, L"⌕"},
    }};

    static const std::array<MarkupColor, 5> colors{{
        MarkupColor::Red(), MarkupColor::Yellow(), MarkupColor::Green(),
        MarkupColor::Blue(), MarkupColor::White(),
    }};

    auto size = m_renderTarget->GetSize();
    const float buttonSize = 30.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f);
    const float gap = 4.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f);
    const float toolbarHeight = 42.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f);
    const float padding = 8.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f);
    const float btnGapY = 6.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f);
    bool isRecord = (m_mode == OverlayMode::RecordRegion);
    // 非录屏: 9 工具 + 5 颜色 + 8 命令(撤销/重做/清除/OCR/贴图/长截图/取消/确认)
    const int buttonCount = isRecord ? 2
        : static_cast<int>(tools.size()) + static_cast<int>(colors.size()) + 8;
    const float toolbarWidth = padding * 2.0f + buttonCount * buttonSize + (buttonCount - 1) * gap + (isRecord ? 20.0f : 0.0f);
    float maxX = std::max(8.0f, size.width - toolbarWidth - 8.0f);
    float toolbarX = std::clamp(selectionRect.left, 8.0f, maxX);
    float toolbarY = selectionRect.bottom + 8.0f;
    if (toolbarY + toolbarHeight > size.height - 8.0f) {
        toolbarY = selectionRect.top - toolbarHeight - 8.0f;
    }
    toolbarY = std::max(8.0f, toolbarY);

    float x = toolbarX + padding;
    auto addButton = [&](ToolbarCommand command, MarkupTool tool, std::wstring label, float width) {
        ToolbarButton button;
        button.command = command;
        button.tool = tool;
        button.label = std::move(label);
        button.rect = D2D1::RectF(x, toolbarY + btnGapY, x + width, toolbarY + btnGapY + buttonSize);
        m_toolbarButtons.push_back(std::move(button));
        x += width + gap;
    };

    bool isZh = easy::core::WinUtils::isSystemLanguageChinese();

    if (isRecord) {
        addButton(ToolbarCommand::Confirm, MarkupTool::Rectangle, isZh ? L"🔴 录制" : L"🔴 Rec", 65.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f));
        addButton(ToolbarCommand::Cancel, MarkupTool::Rectangle, isZh ? L"✖ 取消" : L"✖ Cancel", 75.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f));
    } else {
        for (const auto& tool : tools) {
            addButton(ToolbarCommand::SelectTool, tool.tool, tool.label, buttonSize);
        }
        // 颜色色板
        for (const auto& c : colors) {
            ToolbarButton button;
            button.command = ToolbarCommand::SelectColor;
            button.color = c;
            button.rect = D2D1::RectF(x, toolbarY + btnGapY, x + buttonSize, toolbarY + btnGapY + buttonSize);
            m_toolbarButtons.push_back(std::move(button));
            x += buttonSize + gap;
        }
        addButton(ToolbarCommand::Undo, MarkupTool::Rectangle, L"↩", buttonSize);
        addButton(ToolbarCommand::Redo, MarkupTool::Rectangle, L"↪", buttonSize);
        addButton(ToolbarCommand::Clear, MarkupTool::Rectangle, L"🗑", buttonSize);
        addButton(ToolbarCommand::ExtractText, MarkupTool::Rectangle, isZh ? L"文" : L"T", buttonSize);
        addButton(ToolbarCommand::PinWindow, MarkupTool::Rectangle, L"📌", buttonSize);
        addButton(ToolbarCommand::ScrollCapture, MarkupTool::Rectangle, isZh ? L"长" : L"⇊", buttonSize);
        addButton(ToolbarCommand::Cancel, MarkupTool::Rectangle, L"✖", buttonSize);
        addButton(ToolbarCommand::Confirm, MarkupTool::Rectangle, L"✓", buttonSize);
    }
}

ToolbarButton* CaptureOverlay::hitTestToolbar(POINT point) {
    rebuildToolbarButtons(currentSelectionRect());
    for (auto& button : m_toolbarButtons) {
        if (point.x >= button.rect.left && point.x <= button.rect.right &&
            point.y >= button.rect.top && point.y <= button.rect.bottom) {
            return &button;
        }
    }
    return nullptr;
}

void CaptureOverlay::executeToolbarCommand(const ToolbarButton& button) {
    if (m_activeElement) {
        m_activeElement->isActive = false;
        m_activeElement->isEditing = false;
        m_activeElement = nullptr;
    }

    switch (button.command) {
        case ToolbarCommand::SelectTool:
            m_currentTool = button.tool;
            m_state = OverlayState::Selected;
            m_isMarking = false;
            break;

        case ToolbarCommand::SelectColor:
            m_currentColor = button.color;
            break;

        case ToolbarCommand::Undo:
            m_markup.undo();
            break;

        case ToolbarCommand::Redo:
            m_markup.redo();
            break;

        case ToolbarCommand::Clear:
            m_markup.clearAll();
            prepareMarkupBase();
            break;

        case ToolbarCommand::ExtractText:
            if (m_ocrCallback) {
                int x1 = std::min(m_dragStart.x, m_dragEnd.x);
                int y1 = std::min(m_dragStart.y, m_dragEnd.y);
                int w = std::abs(m_dragEnd.x - m_dragStart.x);
                int h = std::abs(m_dragEnd.y - m_dragStart.y);
                
                cv::Mat cropped;
                if (m_markup.elementCount() > 0) cropped = m_markup.getCompositeImage();
                else {
                    cv::Rect roi(x1, y1, w, h);
                    roi &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);
                    if (roi.area() > 0) m_frozenScreen(roi).copyTo(cropped);
                }

                int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                CaptureRegion region{x1 + offsetX, y1 + offsetY, w, h};
                auto ocrCb = m_ocrCallback;
                cancel(); 
                ocrCb(region, cropped);
            }
            break;

        case ToolbarCommand::PinWindow: {
            int x1 = std::min(m_dragStart.x, m_dragEnd.x);
            int y1 = std::min(m_dragStart.y, m_dragEnd.y);
            int w = std::abs(m_dragEnd.x - m_dragStart.x);
            int h = std::abs(m_dragEnd.y - m_dragStart.y);
            
            cv::Mat cropped;
            if (m_markup.elementCount() > 0) cropped = m_markup.getCompositeImage();
            else {
                cv::Rect roi(x1, y1, w, h);
                roi &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);
                if (roi.area() > 0) m_frozenScreen(roi).copyTo(cropped);
            }

            int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            
            // 先关闭截图覆盖层
            cancel();

            // 贴图在原位置
            PinWindow::create(cropped, x1 + offsetX, y1 + offsetY);
            break;
        }

        case ToolbarCommand::ScrollCapture: {
            int x1 = std::min(m_dragStart.x, m_dragEnd.x);
            int y1 = std::min(m_dragStart.y, m_dragEnd.y);
            int w = std::abs(m_dragEnd.x - m_dragStart.x);
            int h = std::abs(m_dragEnd.y - m_dragStart.y);

            int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            
            RECT capRect = {x1 + offsetX, y1 + offsetY, x1 + offsetX + w, y1 + offsetY + h};
            
            // 关闭覆盖层
            cancel();

            // 启动长截图 (TODO: 可以把结果通过回调传出，或者这里暂时让 ScrollCapture 把自己跑完并在内部提示)
            ScrollCaptureOptions opts;
            opts.captureRect = capRect;
            opts.mode = ScrollMode::Auto;
            ScrollCapture::instance().start(opts);
            break;
        }

        case ToolbarCommand::Confirm:
            confirmSelection();
            break;

        case ToolbarCommand::Cancel:
            cancel();
            break;
    }
}

bool CaptureOverlay::isPointInSelection(POINT point) const {
    auto rect = currentSelectionRect();
    return point.x >= rect.left && point.x <= rect.right &&
           point.y >= rect.top && point.y <= rect.bottom;
}

cv::Point CaptureOverlay::toMarkupPoint(POINT point) const {
    auto rect = currentSelectionRect();
    int width = std::max(1, static_cast<int>(rect.right - rect.left));
    int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    int x = std::clamp(static_cast<int>(point.x - rect.left), 0, width - 1);
    int y = std::clamp(static_cast<int>(point.y - rect.top), 0, height - 1);
    return {x, y};
}

void CaptureOverlay::beginMarkup(POINT point) {
    if (!isPointInSelection(point)) return;
    prepareMarkupBase();
    if (!m_markupBaseReady) return;

    cv::Point local = toMarkupPoint(point);
    if (m_currentTool == MarkupTool::Number) {
        m_markup.addNumberMark(local, m_currentColor);
        return;
    }
    if (m_currentTool == MarkupTool::Magnifier) {
        m_markup.addMagnifier(local);
        return;
    }
    if (m_currentTool == MarkupTool::Text) {
        if (m_activeElement) {
            m_activeElement->isActive = false;
            m_activeElement->isEditing = false;
        }
        m_markup.addText(local, "", m_currentColor, 18.0f * (m_dpiScale > 0 ? m_dpiScale : 1.0f));
        m_activeElement = m_markup.getElementAtEx(local).element;
        if (m_activeElement) {
            m_activeElement->isActive = true;
            m_activeElement->isEditing = true;
        }
        m_state = OverlayState::Selected;
        return;
    }

    m_markupStart = point;
    m_markupEnd = point;
    m_penPoints.clear();
    if (m_currentTool == MarkupTool::Pen) {
        m_penPoints.push_back(local);
    }
    m_isMarking = true;
    m_state = OverlayState::Marking;
}

void CaptureOverlay::updateMarkup(POINT point) {
    if (!m_isMarking) return;

    m_markupEnd = point;
    if (m_currentTool == MarkupTool::Pen) {
        cv::Point local = toMarkupPoint(point);
        if (m_penPoints.empty() || m_penPoints.back() != local) {
            m_penPoints.push_back(local);
        }
    }
}

void CaptureOverlay::finishMarkup(POINT point) {
    if (!m_isMarking) return;

    m_markupEnd = point;
    cv::Point start = toMarkupPoint(m_markupStart);
    cv::Point end = toMarkupPoint(m_markupEnd);
    int dx = std::abs(end.x - start.x);
    int dy = std::abs(end.y - start.y);

    if (m_currentTool != MarkupTool::Pen && dx < 3 && dy < 3) {
        m_isMarking = false;
        m_state = OverlayState::Selected;
        return;
    }

    switch (m_currentTool) {
        case MarkupTool::Rectangle:
            m_markup.drawRectangle(start, end, m_currentColor);
            break;

        case MarkupTool::Arrow:
            m_markup.drawArrow(start, end, m_currentColor);
            break;

        case MarkupTool::Ellipse:
            m_markup.drawEllipse(start, end, m_currentColor);
            break;

        case MarkupTool::Pen:
            m_markup.drawPenStroke(m_penPoints, m_currentColor);
            break;

        case MarkupTool::Highlight:
            m_markup.drawHighlight(start, end, m_currentColor);
            break;

        case MarkupTool::Mosaic:
            m_markup.applyMosaic(start, end);
            break;

        default:
            break;
    }

    m_isMarking = false;
    m_state = OverlayState::Selected;
}

void CaptureOverlay::confirmSelection() {
    easy::core::TraceId::Scope scope;

    if (m_activeElement) {
        m_activeElement->isActive = false;
        m_activeElement->isEditing = false;
        m_activeElement = nullptr;
    }

    int x1 = std::min(m_dragStart.x, m_dragEnd.x);
    int y1 = std::min(m_dragStart.y, m_dragEnd.y);
    int x2 = std::max(m_dragStart.x, m_dragEnd.x);
    int y2 = std::max(m_dragStart.y, m_dragEnd.y);
    int w = x2 - x1;
    int h = y2 - y1;

    if (w <= 0 || h <= 0) {
        cancel();
        return;
    }

    cv::Mat cropped;
    if (m_markup.elementCount() > 0) {
        cropped = m_markup.getCompositeImage();
    } else {
        // 从冻结的屏幕裁剪选区
        cv::Rect roiRect(x1, y1, w, h);
        roiRect &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);

        if (roiRect.area() > 0) {
            m_frozenScreen(roiRect).copyTo(cropped);
            if (cropped.channels() == 4) {
                cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
            }
        }
    }

    int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    CaptureRegion region{x1 + offsetX, y1 + offsetY, w, h};

    // 保存回调指针，因为 cancel() 会清理部分状态
    auto cb = m_callback;
    auto rcb = m_recordCallback;
    auto mode = m_mode;

    cancel();  // 关闭覆盖层

    if (mode == OverlayMode::RecordRegion) {
        if (rcb) {
            rcb(region);
        }
        LOG_INFO("录屏选区确认: ({},{}) {}x{}", x1, y1, w, h);
    } else {
        if (cb) {
            cb(region, cropped);
        }
        LOG_INFO("截图选区确认: ({},{}) {}x{}", x1, y1, w, h);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK CaptureOverlay::overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CaptureOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // 窗口过程兜底：渲染/标注路径含 OpenCV 分配、std::format 等可抛操作，异常绝不能逃逸到
    // Win32 派发层（否则 std::terminate 崩溃）。
    try {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            self->markMarkupDirty();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // 文字编辑失焦提交
            if (self->m_activeElement && self->m_activeElement->tool == MarkupTool::Text && self->m_activeElement->isEditing) {
                self->m_activeElement->isEditing = false;
                self->m_activeElement->isActive = false;
                self->m_activeElement = nullptr;
                self->m_needsRender = true;
                return 0; // 拦截点击，转为渲染态
            }

            if (self->m_state == OverlayState::Selected || self->m_state == OverlayState::Marking) {
                HitArea selHit = HitArea::None;
                if (auto* button = self->hitTestToolbar(point)) {
                    self->executeToolbarCommand(*button);
                } else if ((selHit = self->hitTestSelectionBox(point)) != HitArea::None) {
                    // 拖拽控制点缩放选区 / 拖拽边框移动选区（已有标注时会自动重映射坐标）
                    self->m_isAdjustingSelection = true;
                    self->m_selAdjustHandle = selHit;
                    self->m_selAdjustLast = point;
                } else if (self->isPointInSelection(point)) {
                    cv::Point local = self->toMarkupPoint(point);
                    HitResult hit = self->m_markup.getElementAtEx(local);

                    if (self->m_activeElement && self->m_activeElement->tool == MarkupTool::Text && self->m_activeElement->isEditing) {
                        if (!hit.element || hit.element != self->m_activeElement) {
                            self->m_activeElement->isEditing = false;
                        }
                    }

                    if (hit.element) {
                        if (self->m_activeElement && self->m_activeElement != hit.element) {
                            self->m_activeElement->isActive = false;
                            self->m_activeElement->isEditing = false;
                        }
                        self->m_activeElement = hit.element;
                        self->m_activeElement->isActive = true;

                        if (hit.area == HitArea::Body) {
                            self->m_dragHandle = HitArea::None;
                            self->m_isManipulating = true;
                            self->m_lastMousePos = point;
                        } else {
                            self->m_dragHandle = hit.area;
                            self->m_isManipulating = true;
                            self->m_lastMousePos = point;
                        }
                    } else {
                        if (self->m_activeElement) {
                            self->m_activeElement->isActive = false;
                            self->m_activeElement->isEditing = false;
                            self->m_activeElement = nullptr;
                        }
                        self->beginMarkup(point);
                    }
                }
                return 0;
            }

            self->m_dragStart = point;
            self->m_dragEnd = self->m_dragStart;
            self->m_dragging = true;
            self->m_state = OverlayState::Selecting;
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!self) break;
            self->m_currentCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // 获取当前鼠标所在显示器的 DPI 缩放比例
            HMONITOR hMon = MonitorFromPoint({self->m_currentCursor.x, self->m_currentCursor.y}, MONITOR_DEFAULTTONEAREST);
            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            self->m_dpiScale = dpiX / 96.0f;

            if (self->m_isAdjustingSelection) {
                int dx = self->m_currentCursor.x - self->m_selAdjustLast.x;
                int dy = self->m_currentCursor.y - self->m_selAdjustLast.y;
                self->adjustSelection(self->m_selAdjustHandle, dx, dy);
                self->m_selAdjustLast = self->m_currentCursor;
                self->invalidate();
            } else if (self->m_isManipulating && self->m_activeElement) {
                int dx = self->m_currentCursor.x - self->m_lastMousePos.x;
                int dy = self->m_currentCursor.y - self->m_lastMousePos.y;
                if (self->m_dragHandle == HitArea::None) {
                    self->m_activeElement->moveBy(dx, dy);
                } else {
                    self->m_activeElement->resize(dx, dy, self->m_dragHandle);
                }
                self->m_lastMousePos = self->m_currentCursor;
                self->markMarkupDirty();   // 元素几何变了，合成缓存失效
            } else if (self->m_isMarking) {
                self->updateMarkup(self->m_currentCursor);
                self->invalidate();        // 拖拽预览走 D2D，合成图不变
            } else if (self->m_dragging) {
                self->m_dragEnd = self->m_currentCursor;
                self->invalidate();
            } else {
                if (self->m_state == OverlayState::Selecting && !self->m_dragging) {
                    // 未拖拽时检测光标下的窗口
                    POINT screenPt = self->m_currentCursor;
                    int offX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                    int offY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                    screenPt.x += offX;
                    screenPt.y += offY;
                    self->m_detectedWindow = self->detectWindowUnderCursor(screenPt);
                }
                self->invalidate();        // 十字准星/动态放大镜跟随光标
            }

            self->updateHoverCursor(self->m_currentCursor);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!self) break;
            self->markMarkupDirty();

            if (self->m_isAdjustingSelection) {
                self->m_isAdjustingSelection = false;
                self->m_selAdjustHandle = HitArea::None;
                return 0;
            }

            if (self->m_isManipulating) {
                self->m_isManipulating = false;
                return 0;
            }

            if (self->m_isMarking) {
                self->finishMarkup({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }

            if (!self->m_dragging) break;
            self->m_dragEnd = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            self->m_dragging = false;

            int w = std::abs(self->m_dragEnd.x - self->m_dragStart.x);
            int h = std::abs(self->m_dragEnd.y - self->m_dragStart.y);

            if (w > 3 && h > 3) {
                self->m_state = OverlayState::Selected;
                self->prepareMarkupBase();
            } else {
                // 拖拽太小，视为点击——吸附到检测的窗口
                if (self->m_detectedWindow.right > self->m_detectedWindow.left &&
                    self->m_detectedWindow.bottom > self->m_detectedWindow.top) {
                    self->m_dragStart = {static_cast<LONG>(self->m_detectedWindow.left),
                                         static_cast<LONG>(self->m_detectedWindow.top)};
                    self->m_dragEnd = {static_cast<LONG>(self->m_detectedWindow.right),
                                       static_cast<LONG>(self->m_detectedWindow.bottom)};
                    self->m_state = OverlayState::Selected;
                    self->prepareMarkupBase();
                } else {
                    self->cancel();
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            if (self && self->m_state == OverlayState::Selected) {
                self->markMarkupDirty();
                if (self->m_activeElement && self->m_activeElement->tool == MarkupTool::Text) {
                    self->m_activeElement->isEditing = true;
                    return 0;
                }
                self->confirmSelection();
            }
            return 0;
        }

        case WM_CHAR: {
            if (self && self->m_activeElement && self->m_activeElement->tool == MarkupTool::Text && self->m_activeElement->isEditing) {
                self->markMarkupDirty();
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch == 0x08) { // Backspace
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(self->m_activeElement->text);
                    if (!wstr.empty()) {
                        wstr.pop_back();
                        self->m_activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                        self->m_activeElement->textRenderSize = cv::Size(0,0);
                    }
                } else if (ch == 0x0D || ch == 0x0A) { // Enter
                    self->m_activeElement->isEditing = false;
                } else if (ch >= 0x20) {
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(self->m_activeElement->text);
                    wstr.push_back(ch);
                    self->m_activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                    self->m_activeElement->textRenderSize = cv::Size(0,0);
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (!self) break;
            self->markMarkupDirty();
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (self->m_activeElement && self->m_activeElement->tool == MarkupTool::Magnifier) {
                self->m_activeElement->magnifierScale += (zDelta > 0) ? 0.2f : -0.2f;
                self->m_activeElement->magnifierScale = std::clamp(self->m_activeElement->magnifierScale, 1.0f, 8.0f);
            } else if (self->m_currentTool == MarkupTool::Magnifier && !self->m_isMarking && !self->m_isManipulating) {
                self->m_dynamicMagnifierScale += (zDelta > 0) ? 0.2f : -0.2f;
                self->m_dynamicMagnifierScale = std::clamp(self->m_dynamicMagnifierScale, 1.0f, 8.0f);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (!self) break;
            self->markMarkupDirty();

            bool editingText = self->m_activeElement &&
                               self->m_activeElement->tool == MarkupTool::Text &&
                               self->m_activeElement->isEditing;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool hasSelection = (self->m_state == OverlayState::Selected ||
                                 self->m_state == OverlayState::Marking);

            // 世界级细节：方向键微调
            if (!editingText) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                
                // 如果已经有选区了，方向键用来微调选区边界
                if (hasSelection && (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT)) {
                    int dx = 0, dy = 0;
                    if (wParam == VK_UP) dy = -1;
                    if (wParam == VK_DOWN) dy = 1;
                    if (wParam == VK_LEFT) dx = -1;
                    if (wParam == VK_RIGHT) dx = 1;
                    
                    if (shift) {
                        // 缩放选区（右下角）
                        self->m_dragEnd.x += dx;
                        self->m_dragEnd.y += dy;
                    } else {
                        // 移动整个选区
                        self->m_dragStart.x += dx;
                        self->m_dragStart.y += dy;
                        self->m_dragEnd.x += dx;
                        self->m_dragEnd.y += dy;
                    }
                    self->prepareMarkupBase();
                    self->m_needsRender = true;
                    return 0;
                }
                
                // 否则（或按 WASD 时），控制鼠标光标进行像素级移动（取色/找点）
                POINT pt;
                GetCursorPos(&pt);
                bool moved = false;
                if (wParam == 'W' || (!hasSelection && wParam == VK_UP)) { pt.y -= 1; moved = true; }
                if (wParam == 'S' || (!hasSelection && wParam == VK_DOWN)) { pt.y += 1; moved = true; }
                if (wParam == 'A' || (!hasSelection && wParam == VK_LEFT)) { pt.x -= 1; moved = true; }
                if (wParam == 'D' || (!hasSelection && wParam == VK_RIGHT)) { pt.x += 1; moved = true; }
                if (moved) {
                    SetCursorPos(pt.x, pt.y);
                    self->m_needsRender = true;
                    return 0;
                }
            }

            // ESC 分级退出：编辑文字→退出编辑；选中元素→取消选中；否则→关闭截图
            if (wParam == VK_ESCAPE) {
                if (editingText) {
                    self->m_activeElement->isEditing = false;
                } else if (self->m_activeElement) {
                    self->m_activeElement->isActive = false;
                    self->m_activeElement = nullptr;
                } else {
                    self->cancel();
                }
                return 0;
            }

            // 正在输入文字：其余按键交给 WM_CHAR
            if (editingText) return 0;

            // 取色：选区前/拖拽中按 C 复制光标处像素的 HEX 颜色
            if (wParam == 'C' && self->m_state == OverlayState::Selecting) {
                int cr, cg, cb;
                if (self->sampleScreenColor(self->m_currentCursor.x, self->m_currentCursor.y, cr, cg, cb)) {
                    easy::core::WinUtils::copyToClipboard(std::format("#{:02X}{:02X}{:02X}", cr, cg, cb));
                    self->m_loupeToastUntil = GetTickCount() + 1200;
                    self->invalidate();
                }
                return 0;
            }

            // 全局命令
            switch (wParam) {
                case VK_RETURN:
                    if (hasSelection) self->confirmSelection();
                    return 0;
                case 'Z':
                    if (ctrl) { self->m_activeElement = nullptr; self->m_markup.undo(); }
                    return 0;
                case 'Y':
                    if (ctrl) { self->m_activeElement = nullptr; self->m_markup.redo(); }
                    return 0;
                case VK_DELETE:
                    // 只删除当前选中的标注，避免一键误清空全部
                    if (self->m_activeElement) {
                        self->m_markup.removeElement(self->m_activeElement->id);
                        self->m_activeElement = nullptr;
                    }
                    return 0;
                default: break;
            }

            if (!hasSelection) return 0;

            // 方向键微调（需用到 Ctrl 作加速，故先于 Ctrl 拦截处理）
            if (wParam == VK_LEFT || wParam == VK_RIGHT ||
                wParam == VK_UP   || wParam == VK_DOWN) {
                int step = ctrl ? 10 : 1;
                int dx = (wParam == VK_LEFT ? -step : wParam == VK_RIGHT ? step : 0);
                int dy = (wParam == VK_UP   ? -step : wParam == VK_DOWN  ? step : 0);
                if (self->m_activeElement) {
                    self->m_activeElement->moveBy(dx, dy);   // 微调选中元素
                } else {
                    self->adjustSelection(HitArea::Body, dx, dy);  // 微调整块选区（含标注重映射）
                }
                return 0;
            }

            // 其余 Ctrl 组合键不触发工具/颜色快捷键
            if (ctrl) return 0;

            // 颜色快捷键 1-5（与工具栏色板顺序一致）
            switch (wParam) {
                case '1': self->m_currentColor = MarkupColor::Red();    return 0;
                case '2': self->m_currentColor = MarkupColor::Yellow(); return 0;
                case '3': self->m_currentColor = MarkupColor::Green();  return 0;
                case '4': self->m_currentColor = MarkupColor::Blue();   return 0;
                case '5': self->m_currentColor = MarkupColor::White();  return 0;
                default: break;
            }

            // 工具快捷键
            switch (wParam) {
                case 'R': self->setCurrentTool(MarkupTool::Rectangle); return 0;
                case 'A': self->setCurrentTool(MarkupTool::Arrow);     return 0;
                case 'O': case 'E': self->setCurrentTool(MarkupTool::Ellipse); return 0;
                case 'P': self->setCurrentTool(MarkupTool::Pen);       return 0;
                case 'H': self->setCurrentTool(MarkupTool::Highlight); return 0;
                case 'M': self->setCurrentTool(MarkupTool::Mosaic);    return 0;
                case 'T': self->setCurrentTool(MarkupTool::Text);      return 0;
                case 'N': self->setCurrentTool(MarkupTool::Number);    return 0;
                case 'G': self->setCurrentTool(MarkupTool::Magnifier); return 0;
                default: break;
            }
            return 0;
        }

        case WM_TIMER: {
            if (self && wParam == RENDER_TIMER_ID) {
                if (self->m_isFadingOut) {
                    DWORD elapsed = GetTickCount() - self->m_fadeOutStart;
                    if (elapsed >= 150) {
                        self->realCancel();
                    } else {
                        float alpha = 1.0f - (elapsed / 150.0f);
                        SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha * 255), LWA_ALPHA);
                    }
                    return 0;
                }
                
                // 取色放大镜：复制成功提示的存续期间持续重绘，到期后再渲染一帧将其清除
                if (self->m_loupeToastUntil != 0) {
                    self->m_needsRender = true;
                    if (GetTickCount() >= self->m_loupeToastUntil) self->m_loupeToastUntil = 0;
                }
                // 文字编辑期间需持续重算以保留闪烁光标；其余情况按脏标记重绘。
                bool editingText = self->m_activeElement &&
                                   self->m_activeElement->tool == MarkupTool::Text &&
                                   self->m_activeElement->isEditing;
                if (editingText) {
                    self->m_markupCacheDirty = true;
                    self->m_needsRender = true;
                }
                if (self->m_needsRender) {
                    self->m_needsRender = false;
                    self->render();
                }
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    } catch (const std::exception& e) {
        LOG_ERROR("CaptureOverlay 窗口过程异常: {}", e.what());
    } catch (...) {
        LOG_ERROR("CaptureOverlay 窗口过程未知异常");
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::capture
