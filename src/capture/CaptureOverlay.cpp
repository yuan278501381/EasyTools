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
    m_hInstance = hInstance;
    // 覆盖层是整个虚拟桌面大小。启动时创建一个隐藏的 layered D2D 窗口，
    // 在部分显卡/驱动上即使不可见也会持续触发表面合成，占满一个 CPU 核。
    // 这里只保存模块句柄，真正截图时再创建，用完立即释放。
    return m_hInstance != nullptr;
}

void CaptureOverlay::shutdown() {
    const bool wasActive = m_state.state.load() != OverlayState::Idle;
    m_state.state = OverlayState::Idle;
    ReleaseCapture();
    if (m_hwnd) {
        m_renderer.shutdown();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_state.frozenScreen.release();
    if (wasActive && m_closedCallback) m_closedCallback();
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
    
    if (!m_renderer.initialize(m_hwnd, m_state)) {
        LOG_ERROR("截图覆盖层 Direct2D 初始化失败");
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }
    
    m_input.initialize(m_hwnd, m_state, m_renderer, 
        [this](){ this->realCancel(); },
        [this](){ this->confirmSelection(); }
    );
    
    return true;
}

void CaptureOverlay::startSelection(const CaptureOptions& options, OverlayMode mode) {
    easy::core::TraceId::Scope scope;
    if (!m_hwnd && (!m_hInstance || !createOverlayWindow(m_hInstance))) {
        LOG_ERROR("无法启动截图: 覆盖层窗口初始化失败");
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"截图界面初始化失败，请重试"});
        if (m_closedCallback) m_closedCallback();
        return;
    }

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

    if (!freezeScreen() || !m_renderer.updateScreenBitmap(m_state.frozenScreen)) {
        LOG_ERROR("无法启动截图: 桌面底图捕获或上传失败");
        m_state.state = OverlayState::Idle;
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"无法捕获屏幕，请重试"});
        if (m_closedCallback) m_closedCallback();
        return;
    }
    
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

bool CaptureOverlay::freezeScreen() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (w <= 0 || h <= 0) {
        LOG_ERROR("无效的虚拟屏幕尺寸: {}x{}", w, h);
        return false;
    }

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) {
        LOG_ERROR("GetDC(nullptr) 失败, error={}", GetLastError());
        return false;
    }
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        LOG_ERROR("CreateCompatibleDC 失败, error={}", GetLastError());
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = w;
    bitmapInfo.bmiHeader.biHeight = -h;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        hdcScreen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        LOG_ERROR("CreateDIBSection 失败, error={}", GetLastError());
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    HGDIOBJ previous = SelectObject(hdcMem, bitmap);
    const BOOL copied = BitBlt(
        hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY | CAPTUREBLT);
    if (copied) {
        const cv::Mat bgra(h, w, CV_8UC4, pixels, static_cast<size_t>(w) * 4);
        cv::cvtColor(bgra, m_state.frozenScreen, cv::COLOR_BGRA2BGR);
    } else {
        LOG_ERROR("截图覆盖层 BitBlt 失败, error={}", GetLastError());
        m_state.frozenScreen.release();
    }

    SelectObject(hdcMem, previous);
    DeleteObject(bitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return copied && !m_state.frozenScreen.empty();
}

void CaptureOverlay::cancel() {
    m_state.isFadingOut = true;
    m_state.fadeOutStart = GetTickCount();
    m_renderer.invalidate();
    ReleaseCapture();
}

void CaptureOverlay::realCancel() {
    const bool wasActive = m_state.state.load() != OverlayState::Idle;
    m_state.state = OverlayState::Idle;
    ReleaseCapture();

    // 不保留隐藏的全屏 D2D layered window。实测该窗口在部分 Windows/DWM
    // 组合上会在隐藏后继续消耗接近一个 CPU 核，按次重建的代价远低于常驻耗电。
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_renderer.shutdown();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_state.frozenScreen.release();
    if (wasActive && m_closedCallback) m_closedCallback();
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
        realCancel();
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

    // 必须在开始录屏或交付截图前同步隐藏覆盖层；否则覆盖层会进入首帧并持续遮挡桌面。
    realCancel();

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
