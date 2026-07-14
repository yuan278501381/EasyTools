#include "capture/CaptureOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"
#include "capture/CaptureHistory.h"

namespace easy::capture {

CaptureOverlay& CaptureOverlay::instance() {
    static CaptureOverlay inst;
    return inst;
}

bool CaptureOverlay::initialize(HINSTANCE hInstance) {
    if (m_hwnd) return true;
    m_hInstance = hInstance;
    return createOverlayWindow(hInstance);
}

void CaptureOverlay::shutdown() {
    realCancel();
    if (m_hwnd) {
        m_renderer.shutdown();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool CaptureOverlay::createOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = staticWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = L"EasyTools_CaptureOverlay";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName, nullptr,
        WS_POPUP,
        0, 0, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) {
        LOG_ERROR("截图覆盖层窗口创建失败");
        return false;
    }

    // 默认全透明，事件穿透（直到开始截图）
    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);
    
    m_renderer.initialize(m_hwnd, m_state);
    
    m_input.initialize(m_hwnd, m_state, m_renderer, 
        [this](){ this->realCancel(); },
        [this](){ this->confirmSelection(); }
    );
    
    return true;
}

void CaptureOverlay::startSelection(const CaptureOptions& options, OverlayMode mode) {
    easy::core::TraceId::Scope scope;
    m_state.options = options;
    m_state.mode = mode;
    m_state.state = OverlayState::Selecting;
    m_state.dragging = false;
    m_state.isMarking = false;
    m_state.markupBaseReady = false;
    m_state.activeElement = nullptr;
    m_state.dragHandle = HitArea::None;
    m_state.isManipulating = false;
    m_state.toolbarButtons.clear();
    m_state.markup.clearAll();
    m_state.loupeToastUntil = 0;
    m_state.showTimestamp = GetTickCount();
    m_state.isFadingOut = false;
    m_state.fadeOutStart = 0;

    freezeScreen();
    
    m_renderer.invalidate();
    
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_CYVIRTUALSCREEN),
                 SWP_SHOWWINDOW);

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    SetCapture(m_hwnd);
    SetFocus(m_hwnd);
}

void CaptureOverlay::freezeScreen() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;

    m_state.frozenScreen.create(h, w, CV_8UC4);
    GetDIBits(hdcMem, hBitmap, 0, h, m_state.frozenScreen.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    cv::cvtColor(m_state.frozenScreen, m_state.frozenScreen, cv::COLOR_BGRA2BGR);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}

void CaptureOverlay::cancel() {
    m_state.isFadingOut = true;
    m_state.fadeOutStart = GetTickCount();
    m_renderer.invalidate();
    ReleaseCapture();
}

void CaptureOverlay::realCancel() {
    m_state.state = OverlayState::Idle;
    ShowWindow(m_hwnd, SW_HIDE);
    SetLayeredWindowAttributes(m_hwnd, 0, 0, LWA_ALPHA);
    ReleaseCapture();
}

void CaptureOverlay::confirmSelection() {
    easy::core::TraceId::Scope scope;
    if (m_state.activeElement) {
        m_state.activeElement->isActive = false;
        m_state.activeElement->isEditing = false;
        m_state.activeElement = nullptr;
    }

    int x1 = std::min(m_state.dragStart.x, m_state.dragEnd.x);
    int y1 = std::min(m_state.dragStart.y, m_state.dragEnd.y);
    int x2 = std::max(m_state.dragStart.x, m_state.dragEnd.x);
    int y2 = std::max(m_state.dragStart.y, m_state.dragEnd.y);
    int w = x2 - x1;
    int h = y2 - y1;

    if (w <= 0 || h <= 0) {
        cancel();
        return;
    }

    cv::Mat cropped;
    if (m_state.markup.elementCount() > 0) {
        cropped = m_state.markup.getCompositeImage();
    } else {
        cv::Rect roiRect(x1, y1, w, h);
        roiRect &= cv::Rect(0, 0, m_state.frozenScreen.cols, m_state.frozenScreen.rows);

        if (roiRect.area() > 0) {
            m_state.frozenScreen(roiRect).copyTo(cropped);
            if (cropped.channels() == 4) {
                cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
            }
        }
    }

    int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    CaptureRegion region{x1 + offsetX, y1 + offsetY, w, h};

    auto cb = m_state.callback;
    auto rcb = m_state.recordCallback;
    auto mode = m_state.mode;

    cancel();

    if (mode == OverlayMode::RecordRegion) {
        if (rcb) rcb(region);
    } else {
        if (cb) cb(region, cropped);
    }
}

LRESULT CALLBACK CaptureOverlay::staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CaptureOverlay* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<CaptureOverlay*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CaptureOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    
    if (self) {
        if (msg == WM_PAINT) {
            self->m_renderer.render(self->m_state);
            ValidateRect(hwnd, nullptr);
            return 0;
        }
        LRESULT res = self->m_input.handleMessage(hwnd, msg, wParam, lParam);
        if (res != 0) return res;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::capture
