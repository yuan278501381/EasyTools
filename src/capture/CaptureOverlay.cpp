#include "capture/CaptureOverlay.h"
#include "capture/CaptureBackend.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"
#include "capture/CaptureHistory.h"
#include "capture/ShortcutHintOverlay.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/utils/DpiUtils.h"

#include <algorithm>
#include <chrono>

namespace {

float dpiScaleAt(POINT screenPoint) {
    return easy::core::dpi::scaleAtPoint(screenPoint);
}

double elapsedMilliseconds(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
}

}  // namespace

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
    ShortcutHintOverlay::instance().hide();
    const bool wasActive = m_state.state.load() != OverlayState::Idle;
    m_state.state = OverlayState::Idle;
    ReleaseCapture();
    if (m_hwnd) {
        m_renderer.releaseWindowResources();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    releaseFrozenSurface();
    m_renderer.shutdown();
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
        [this](CaptureCompletion completion){ this->confirmSelection(std::move(completion)); }
    );
    
    return true;
}

void CaptureOverlay::startSelection(const CaptureOptions& options, OverlayMode mode) {
    easy::core::TraceId::Scope scope;
    const auto totalStarted = std::chrono::steady_clock::now();

    // The virtual-desktop overlay is physically pixel-sized. Scale its HUD from
    // the monitor under the pointer before DWrite resources are created, so the
    // very first frame is already correct at 125%/150%/200%.
    POINT cursorScreen{};
    GetCursorPos(&cursorScreen);
    m_state.options = options;
    m_state.mode = mode;
    m_state.dpiScale = dpiScaleAt(cursorScreen);
    m_state.currentCursor = {
        cursorScreen.x - GetSystemMetrics(SM_XVIRTUALSCREEN),
        cursorScreen.y - GetSystemMetrics(SM_YVIRTUALSCREEN)};

    const auto windowStarted = std::chrono::steady_clock::now();
    if (!m_hwnd && (!m_hInstance || !createOverlayWindow(m_hInstance))) {
        LOG_ERROR("无法启动截图: 覆盖层窗口初始化失败");
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"截图界面初始化失败，请重试"});
        if (m_closedCallback) m_closedCallback();
        return;
    }
    easy::core::PerformanceMonitor::instance().recordLatency(
        "screenshot.window", elapsedMilliseconds(windowStarted));

    m_state.state = OverlayState::Selecting;
    m_state.dragging = false;
    m_state.isMarking = false;
    m_state.markupBaseReady = false;
    m_state.activeElement = nullptr;
    m_state.dragHandle = HitArea::None;
    m_state.isManipulating = false;
    m_state.toolbarButtons.clear();
    m_state.toolbarLayoutValid = false;
    m_state.markup.clearAll();
    m_state.loupeToastUntil = 0;
    m_state.showTimestamp = GetTickCount();
    m_state.isFadingOut = false;
    m_state.fadeOutStart = 0;

    const auto freezeStarted = std::chrono::steady_clock::now();
    if (!freezeScreen()) {
        LOG_ERROR("无法启动截图: 桌面底图捕获或上传失败");
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"无法捕获屏幕，请重试"});
        realCancel();
        return;
    }
    easy::core::PerformanceMonitor::instance().recordLatency(
        "screenshot.freeze", elapsedMilliseconds(freezeStarted));

    const auto uploadStarted = std::chrono::steady_clock::now();
    if (!m_renderer.updateScreenBitmap(m_state.frozenScreen)) {
        LOG_ERROR("无法启动截图: 桌面底图上传失败");
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"无法显示屏幕截图，请重试"});
        realCancel();
        return;
    }
    easy::core::PerformanceMonitor::instance().recordLatency(
        "screenshot.upload", elapsedMilliseconds(uploadStarted));
    
    m_renderer.invalidate();
    
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_CYVIRTUALSCREEN),
                 SWP_SHOWWINDOW);

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    RedrawWindow(m_hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetCapture(m_hwnd);
    SetFocus(m_hwnd);
    if (options.showShortcutHints) {
        ShortcutHintOverlay::instance().show(
            mode == OverlayMode::RecordRegion
                ? ShortcutHintContext::RecordSelecting
                : ShortcutHintContext::CaptureSelecting);
    }
    easy::core::PerformanceMonitor::instance().recordLatency(
        "screenshot", elapsedMilliseconds(totalStarted));
}

bool CaptureOverlay::freezeScreen() {
    releaseFrozenSurface();
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (w <= 0 || h <= 0) {
        LOG_ERROR("无效的虚拟屏幕尺寸: {}x{}", w, h);
        return false;
    }

    // 优先尝试硬件加速捕获后端 (DXGI Desktop Duplication / WGC)
    auto backend = createCaptureBackend();
    std::string backendError;
    CaptureRegion region{x, y, w, h};
    if (backend && backend->initialize(region, backendError)) {
        CaptureFrameView frame;
        if (backend->capture(frame, backendError) && frame.data && frame.width == w && frame.height == h) {
            if (frame.format == CapturePixelFormat::Bgra32) {
                m_state.frozenScreen = cv::Mat(h, w, CV_8UC4, frame.data, frame.stride).clone();
            } else {
                cv::Mat bgr(h, w, CV_8UC3, frame.data, frame.stride);
                cv::cvtColor(bgr, m_state.frozenScreen, cv::COLOR_BGR2BGRA);
            }
            backend->releaseFrame();
            backend->shutdown();
            if (!m_state.frozenScreen.empty()) {
                LOG_DEBUG("截图覆盖层使用硬件加速后端捕获成功: {}x{}", w, h);
                return true;
            }
        }
        backend->releaseFrame();
        backend->shutdown();
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
        // Keep the DIB section alive for the duration of the interaction and
        // let cv::Mat reference it directly. This removes the full-screen
        // BGRA->BGR conversion here and the BGR->BGRA conversion during D2D
        // upload. Selection/markup paths convert only the much smaller ROI.
        m_state.frozenScreen = cv::Mat(
            h, w, CV_8UC4, pixels, static_cast<size_t>(w) * 4);
        m_frozenBitmap = bitmap;
        bitmap = nullptr;
    } else {
        LOG_ERROR("截图覆盖层 BitBlt 失败, error={}", GetLastError());
        m_state.frozenScreen.release();
    }

    SelectObject(hdcMem, previous);
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return copied && !m_state.frozenScreen.empty();
}

void CaptureOverlay::releaseFrozenSurface() {
    m_state.frozenScreen.release();
    if (m_frozenBitmap) {
        DeleteObject(m_frozenBitmap);
        m_frozenBitmap = nullptr;
    }
}

void CaptureOverlay::cancel() {
    m_state.isFadingOut = true;
    m_state.fadeOutStart = GetTickCount();
    m_renderer.invalidate();
    ReleaseCapture();
}

void CaptureOverlay::setShortcutHintsEnabled(bool enabled) {
    m_state.options.showShortcutHints = enabled;
    if (!enabled) {
        ShortcutHintOverlay::instance().hide();
        return;
    }
    const auto current = m_state.state.load();
    if (current == OverlayState::Idle) return;
    ShortcutHintContext context = ShortcutHintContext::CaptureSelecting;
    if (m_state.mode == OverlayMode::RecordRegion) {
        context = ShortcutHintContext::RecordSelecting;
    } else if (current == OverlayState::Selected || current == OverlayState::Marking) {
        context = ShortcutHintContext::CaptureSelected;
    }
    ShortcutHintOverlay::instance().show(context);
}

void CaptureOverlay::realCancel() {
    ShortcutHintOverlay::instance().hide();
    const bool wasActive = m_state.state.load() != OverlayState::Idle;
    m_state.state = OverlayState::Idle;
    ReleaseCapture();

    // 不保留隐藏的全屏 D2D layered window。实测该窗口在部分 Windows/DWM
    // 组合上会在隐藏后继续消耗接近一个 CPU 核，按次重建的代价远低于常驻耗电。
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_renderer.releaseWindowResources();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    releaseFrozenSurface();
    if (wasActive && m_closedCallback) m_closedCallback();
}

void CaptureOverlay::confirmSelection(CaptureCompletion completion) {
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
        if (cb) cb(region, cropped, completion);
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
