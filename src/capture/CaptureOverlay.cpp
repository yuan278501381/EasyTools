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

#include <algorithm>
#include <array>
#include <format>
#include <windowsx.h>

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
    m_markup.clearAll();
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
        SetCapture(m_hwnd);

        SetTimer(m_hwnd, RENDER_TIMER_ID, 16, nullptr);
    }

    LOG_INFO("截图选区模式已启动");
}

void CaptureOverlay::cancel() {
    KillTimer(m_hwnd, RENDER_TIMER_ID);
    ReleaseCapture();
    ShowWindow(m_hwnd, SW_HIDE);
    m_state = OverlayState::Idle;
    m_isMarking = false;
    m_markupBaseReady = false;
    m_toolbarButtons.clear();
    m_penPoints.clear();
    m_frozenScreen.release();
    m_screenBitmap.Reset();
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

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

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
        13.0f, L"zh-CN", m_infoTextFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;
    m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_infoTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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
    // 截取全屏
    CaptureRegion fullScreen;
    fullScreen.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    fullScreen.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    fullScreen.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    fullScreen.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

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

    D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(
        D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH),
        4.0f, 4.0f
    );

    m_renderTarget->FillRoundedRectangle(bgRect, m_infoBgBrush.Get());
    m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                             m_infoTextFormat.Get(),
                             D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH),
                             m_infoTextBrush.Get());
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
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(bgRect, 7.0f, 7.0f), m_infoBgBrush.Get());

    ComPtr<ID2D1SolidColorBrush> buttonBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<ID2D1SolidColorBrush> dangerBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), buttonBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 0.95f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.25f, 0.25f, 0.85f), dangerBrush.GetAddressOf());

    for (const auto& button : m_toolbarButtons) {
        bool isTool = button.command == ToolbarCommand::SelectTool;
        bool isActiveTool = isTool && button.tool == m_currentTool;
        bool isDanger = button.command == ToolbarCommand::Cancel || button.command == ToolbarCommand::Clear;

        auto rounded = D2D1::RoundedRect(button.rect, 5.0f, 5.0f);
        auto* fillBrush = isActiveTool ? activeBrush.Get() : isDanger ? dangerBrush.Get() : buttonBrush.Get();
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
}

void CaptureOverlay::drawMarkupPreview(const D2D1_RECT_F& selectionRect) {
    if (!m_markupBaseReady || m_markup.elementCount() == 0 || !m_renderTarget) return;

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
    if (SUCCEEDED(hr) && bitmap) {
        m_renderTarget->DrawBitmap(bitmap.Get(), selectionRect);
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

void CaptureOverlay::drawCrosshair(float x, float y) {
    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, size.height), m_crosshairBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(size.width, y), m_crosshairBrush.Get(), 1.0f);
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

    auto size = m_renderTarget->GetSize();
    constexpr float buttonSize = 30.0f;
    constexpr float gap = 4.0f;
    constexpr float toolbarHeight = 42.0f;
    constexpr float padding = 8.0f;
    bool isRecord = (m_mode == OverlayMode::RecordRegion);
    int commandCount = isRecord ? 2 : 6;
    const int buttonCount = isRecord ? 2 : static_cast<int>(tools.size()) + commandCount;
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
        button.rect = D2D1::RectF(x, toolbarY + 6.0f, x + width, toolbarY + 6.0f + buttonSize);
        m_toolbarButtons.push_back(std::move(button));
        x += width + gap;
    };

    if (isRecord) {
        addButton(ToolbarCommand::Confirm, MarkupTool::Rectangle, L"🔴 录制", 60.0f);
        addButton(ToolbarCommand::Cancel, MarkupTool::Rectangle, L"✖ 取消", 60.0f);
    } else {
        for (const auto& tool : tools) {
            addButton(ToolbarCommand::SelectTool, tool.tool, tool.label, buttonSize);
        }
        addButton(ToolbarCommand::Undo, MarkupTool::Rectangle, L"↩", buttonSize);
        addButton(ToolbarCommand::Redo, MarkupTool::Rectangle, L"↪", buttonSize);
        addButton(ToolbarCommand::Clear, MarkupTool::Rectangle, L"🗑", buttonSize);
        addButton(ToolbarCommand::ExtractText, MarkupTool::Rectangle, L"文", buttonSize);
        addButton(ToolbarCommand::PinWindow, MarkupTool::Rectangle, L"📌", buttonSize);
        addButton(ToolbarCommand::ScrollCapture, MarkupTool::Rectangle, L"长", buttonSize);
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
    switch (button.command) {
        case ToolbarCommand::SelectTool:
            m_currentTool = button.tool;
            m_state = OverlayState::Selected;
            m_isMarking = false;
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
        m_markup.addText(local, "Text", m_currentColor, 18.0f);
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
            m_markup.drawHighlight(start, end, MarkupColor::Yellow());
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

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            if (self->m_state == OverlayState::Selected || self->m_state == OverlayState::Marking) {
                if (auto* button = self->hitTestToolbar(point)) {
                    self->executeToolbarCommand(*button);
                } else {
                    self->beginMarkup(point);
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

            if (self->m_isMarking) {
                self->updateMarkup(self->m_currentCursor);
            } else if (self->m_dragging) {
                self->m_dragEnd = self->m_currentCursor;
            } else if (self->m_state == OverlayState::Selecting && !self->m_dragging) {
                // 未拖拽时检测光标下的窗口
                POINT screenPt = self->m_currentCursor;
                int offX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int offY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                screenPt.x += offX;
                screenPt.y += offY;
                self->m_detectedWindow = self->detectWindowUnderCursor(screenPt);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (self && self->m_isMarking) {
                self->finishMarkup({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }

            if (!self || !self->m_dragging) break;
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
            // 双击确认
            if (self && self->m_state == OverlayState::Selected) {
                self->confirmSelection();
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (!self) break;
            if (wParam == VK_ESCAPE) {
                self->cancel();
            } else if (wParam == VK_RETURN) {
                if (self->m_state == OverlayState::Selected || 
                    self->m_state == OverlayState::Marking) {
                    self->confirmSelection();
                }
            } else if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') {
                self->m_markup.undo();
            } else if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') {
                self->m_markup.redo();
            } else if (wParam == VK_DELETE) {
                self->m_markup.clearAll();
            }
            return 0;
        }

        case WM_TIMER: {
            if (self && wParam == RENDER_TIMER_ID) {
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
