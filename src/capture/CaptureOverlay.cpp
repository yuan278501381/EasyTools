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
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <algorithm>
#include <format>

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

void CaptureOverlay::startSelection(const CaptureOptions& options) {
    easy::core::TraceId::Scope scope;
    m_options = options;
    m_state = OverlayState::Selecting;
    m_dragging = false;
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

void CaptureOverlay::drawToolbar([[maybe_unused]] const D2D1_RECT_F& selectionRect) {
    // TODO: 绘制标注工具按钮（矩形/箭头/椭圆/画笔/高亮/马赛克/文本/序号/放大镜/撤销/保存）
    // 工具栏位于选区底部
    float toolbarY = selectionRect.bottom + 8;
    float toolbarX = selectionRect.left;
    float toolbarW = 500.0f;
    float toolbarH = 36.0f;

    if (toolbarY + toolbarH > m_renderTarget->GetSize().height) {
        toolbarY = selectionRect.top - toolbarH - 8;
    }

    D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(
        D2D1::RectF(toolbarX, toolbarY, toolbarX + toolbarW, toolbarY + toolbarH),
        6.0f, 6.0f
    );

    m_renderTarget->FillRoundedRectangle(bg, m_infoBgBrush.Get());

    // 工具图标占位文字
    std::wstring toolNames = L"▭  ↗  ○  ✏  ▬  ▦  T  ①  🔍  ↩  💾  ✓";
    m_renderTarget->DrawText(
        toolNames.c_str(), static_cast<UINT32>(toolNames.size()),
        m_infoTextFormat.Get(),
        D2D1::RectF(toolbarX + 12, toolbarY + 2, toolbarX + toolbarW - 12, toolbarY + toolbarH),
        m_infoTextBrush.Get()
    );
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

    CaptureRegion region{x1, y1, w, h};

    // 从冻结的屏幕裁剪选区
    cv::Rect roiRect(x1, y1, w, h);
    roiRect &= cv::Rect(0, 0, m_frozenScreen.cols, m_frozenScreen.rows);

    cv::Mat cropped;
    if (roiRect.area() > 0) {
        m_frozenScreen(roiRect).copyTo(cropped);
        cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
    }

    // 应用标注
    if (m_markup.elementCount() > 0) {
        m_markup.setBaseImage(cropped);
        cropped = m_markup.getCompositeImage();
    }

    cancel();  // 关闭覆盖层

    if (m_callback) {
        m_callback(region, cropped);
    }

    LOG_INFO("截图选区确认: ({},{}) {}x{}", x1, y1, w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK CaptureOverlay::overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CaptureOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            self->m_dragStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            self->m_dragEnd = self->m_dragStart;
            self->m_dragging = true;
            self->m_state = OverlayState::Selecting;
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!self) break;
            self->m_currentCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (self->m_dragging) {
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
            if (!self || !self->m_dragging) break;
            self->m_dragEnd = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            self->m_dragging = false;

            int w = std::abs(self->m_dragEnd.x - self->m_dragStart.x);
            int h = std::abs(self->m_dragEnd.y - self->m_dragStart.y);

            if (w > 3 && h > 3) {
                self->m_state = OverlayState::Selected;
            } else {
                // 拖拽太小，视为点击——吸附到检测的窗口
                if (self->m_detectedWindow.right > self->m_detectedWindow.left &&
                    self->m_detectedWindow.bottom > self->m_detectedWindow.top) {
                    self->m_dragStart = {static_cast<LONG>(self->m_detectedWindow.left),
                                         static_cast<LONG>(self->m_detectedWindow.top)};
                    self->m_dragEnd = {static_cast<LONG>(self->m_detectedWindow.right),
                                       static_cast<LONG>(self->m_detectedWindow.bottom)};
                    self->m_state = OverlayState::Selected;
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
