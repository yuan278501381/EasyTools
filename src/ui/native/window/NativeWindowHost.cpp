#include "ui/native/window/NativeWindowHost.h"
#include "ui/native/components/UIButton.h"
#include "ui/native/components/UIScrollView.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/KeyboardPipeline.h"
#include "core/utils/WinUtils.h"
#include "core/utils/DpiUtils.h"
#include "core/logger/Logger.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>

namespace tools3000::ui::native {

using namespace Microsoft::WRL;

static const wchar_t* NATIVE_WINDOW_CLASS_NAME = L"Tools3000_NativeWindowHost";

NativeWindowHost::NativeWindowHost() {
    m_lastFrameTime = std::chrono::steady_clock::now();
}

NativeWindowHost::~NativeWindowHost() {
    destroy();
}

bool NativeWindowHost::create(HINSTANCE hInstance, const NativeWindowConfig& config) {
    m_hInstance = hInstance ? hInstance : GetModuleHandleW(nullptr);
    m_config = config;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = staticWndProc;
    wc.hInstance = m_hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D 自行全量绘制
    const std::wstring clsName = config.className.empty() ? std::wstring(NATIVE_WINDOW_CLASS_NAME) : config.className;
    wc.lpszClassName = clsName.c_str();

    RegisterClassExW(&wc);

    // 默认初始尺寸与居中计算
    int posX = CW_USEDEFAULT;
    int posY = CW_USEDEFAULT;
    int initialW = config.width;
    int initialH = config.height;

    if (config.centerOnScreen) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        posX = std::max(0, (screenW - initialW) / 2);
        posY = std::max(0, (screenH - initialH) / 2);
    }

    DWORD style = config.isPopup
        ? (WS_POPUP | (config.resizable ? WS_THICKFRAME : 0) | WS_CLIPCHILDREN)
        : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);
    DWORD exStyle = config.isToolWindow
        ? (WS_EX_TOOLWINDOW | (config.alwaysOnTop ? WS_EX_TOPMOST : 0))
        : (WS_EX_APPWINDOW | (config.alwaysOnTop ? WS_EX_TOPMOST : 0));

    m_hwnd = CreateWindowExW(
        exStyle,
        clsName.c_str(),
        config.title.c_str(),
        style,
        posX, posY, initialW, initialH,
        nullptr, nullptr, m_hInstance, this
    );

    if (!m_hwnd) return false;

    // 扩展 DWM 客户区帧以启用 360 度原生系统投影与沉浸式自绘无缝标题栏
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // 强制触发非客户区重算以彻底剥离系统自带原生标题栏与粗糙边框
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // 启用 Per-Monitor 高分屏缩放
    m_dpiScale = tools3000::core::dpi::scaleForDpi(tools3000::core::dpi::effectiveDpiForWindow(m_hwnd));

    if (!initDirect2D()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    applyRoundedCorners();
    return true;
}

bool NativeWindowHost::initDirect2D() {
    D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
    options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    UINT width = std::max(1L, rc.right - rc.left);
    UINT height = std::max(1L, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f * m_dpiScale,
        96.0f * m_dpiScale
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd,
        D2D1::SizeU(width, height),
        D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
    return SUCCEEDED(hr);
}

void NativeWindowHost::resizeD2D(UINT width, UINT height) {
    if (m_renderTarget) {
        m_renderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
        m_renderTarget->Resize(D2D1::SizeU(std::max(1u, width), std::max(1u, height)));
        m_needsLayout = true;
        m_needsPaint = true;
    }
}

void NativeWindowHost::showPopup(std::shared_ptr<UIElement> popup, const Rect& anchorRect, std::function<void()> onDismiss) {
    if (m_activePopup && m_activePopup != popup) {
        closePopup();
    }
    m_activePopup = popup;
    m_activePopupAnchor = anchorRect;
    m_onPopupDismiss = std::move(onDismiss);
    if (m_activePopup) {
        m_activePopup->setWindowHost(this);
    }
    requestPaint();
}

void NativeWindowHost::closePopup() {
    if (m_activePopup) {
        auto dismiss = std::move(m_onPopupDismiss);
        m_activePopup->setWindowHost(nullptr);
        m_activePopup = nullptr;
        m_activePopupAnchor = Rect();
        m_onPopupDismiss = nullptr;
        if (dismiss) {
            dismiss();
        }
        requestPaint();
    }
}

void NativeWindowHost::show(int cmdShow) {
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, cmdShow);
    UpdateWindow(m_hwnd);
    wakeAnimationTimer();
}

void NativeWindowHost::hide() {
    if (!m_hwnd) return;
    closePopup();
    ShowWindow(m_hwnd, SW_HIDE);
    if (m_isTimerActive) {
        KillTimer(m_hwnd, 1);
        m_isTimerActive = false;
    }

    // 遵循架构规范：在窗口隐藏冷路径安全调用 trimWorkingSet 释放物理内存
    tools3000::core::WinUtils::trimWorkingSet();
}

void NativeWindowHost::destroy() {
    if (m_hwnd) {
        closePopup();
        if (m_isTimerActive) {
            KillTimer(m_hwnd, 1);
            m_isTimerActive = false;
        }
        HWND h = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(h);

        // 窗口销毁退场冷路径修剪物理内存
        tools3000::core::WinUtils::trimWorkingSet();
    }
}

void NativeWindowHost::setRootElement(std::shared_ptr<UIElement> root) {
    m_rootElement = root;
    if (m_rootElement) {
        m_rootElement->setWindowHost(this);
    }
    requestLayout();
}

void NativeWindowHost::requestLayout() {
    m_needsLayout = true;
    m_needsPaint = true;
    wakeAnimationTimer();
    if (m_hwnd && IsWindow(m_hwnd)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void NativeWindowHost::requestPaint() {
    m_needsPaint = true;
    wakeAnimationTimer();
    if (m_hwnd && IsWindow(m_hwnd)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void NativeWindowHost::wakeAnimationTimer() {
    if (m_hwnd && !m_isTimerActive) {
        // 120 FPS / 60 FPS 流畅物理动画时钟 (8ms)
        SetTimer(m_hwnd, 1, 8, nullptr);
        m_isTimerActive = true;
        m_lastFrameTime = std::chrono::steady_clock::now();
    }
}

bool NativeWindowHost::isMaximized() const {
    return m_hwnd && IsZoomed(m_hwnd);
}

void NativeWindowHost::minimize() {
    if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);
}

void NativeWindowHost::toggleMaximize() {
    if (!m_hwnd) return;
    if (IsZoomed(m_hwnd)) {
        ShowWindow(m_hwnd, SW_RESTORE);
    } else {
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    }
}

void NativeWindowHost::close() {
    hide();
}

void NativeWindowHost::setFocusedElement(std::shared_ptr<UIElement> element) {
    if (m_focusedElement != element) {
        if (m_focusedElement) {
            m_focusedElement->onBlur();
        }
        m_focusedElement = element;
        if (m_focusedElement) {
            m_focusedElement->onFocus();
        }
        requestPaint();
    }
}

void NativeWindowHost::applyRoundedCorners() {
    if (!m_hwnd) return;
    if (IsZoomed(m_hwnd)) {
        SetWindowRgn(m_hwnd, nullptr, TRUE);
        return;
    }
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    int radius = static_cast<int>(12.0f * m_dpiScale);
    tools3000::core::WinUtils::applyUniversalRoundedCorners(m_hwnd, w, h, radius);
}

EventModifiers NativeWindowHost::getKeyboardModifiers() const {
    EventModifiers mods;
    mods.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mods.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    mods.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mods.win = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0;
    return mods;
}

bool NativeWindowHost::updateAnimations() {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    // 限制单帧时间步长，防止休眠唤醒后数值暴冲
    dt = std::clamp(dt, 0.001f, 0.05f);

    bool active = false;
    if (m_rootElement) {
        active |= m_rootElement->update(dt);
    }
    if (m_activePopup) {
        active |= m_activePopup->update(dt);
    }
    return active;
}

std::shared_ptr<UIScrollView> NativeWindowHost::findScrollViewAt(const std::shared_ptr<UIElement>& root, Point pt) {
    if (!root || !root->isVisible() || !root->isEnabled() || !root->bounds().contains(pt)) {
        return nullptr;
    }
    for (auto it = root->getChildren().rbegin(); it != root->getChildren().rend(); ++it) {
        auto res = findScrollViewAt(*it, pt);
        if (res) return res;
    }
    return std::dynamic_pointer_cast<UIScrollView>(root);
}

void NativeWindowHost::paint() {
    if (!m_renderTarget) return;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const float physicalW = static_cast<float>(rc.right - rc.left);
    const float physicalH = static_cast<float>(rc.bottom - rc.top);
    if (physicalW <= 0.0f || physicalH <= 0.0f) return;

    // 转换为 Direct2D 逻辑 DIP (设备无关像素)
    const float width = physicalW / m_dpiScale;
    const float height = physicalH / m_dpiScale;

    UIRenderContext ctx(m_renderTarget.Get(), m_dwriteFactory.Get(), m_dpiScale);

    if (m_needsLayout && m_rootElement) {
        m_rootElement->measure(width, height, ctx);
        m_rootElement->layout(Rect(0.0f, 0.0f, width, height), ctx);
        m_needsLayout = false;
    }

    m_renderTarget->BeginDraw();

    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();

    // 世界级微晶背景环境光晕与底色全屏清除
    GlassmorphismRenderer::drawWindowBackground(ctx, Rect(0.0f, 0.0f, width, height), theme.isEffectiveDark(), p.primary);

    if (m_rootElement && m_rootElement->isVisible()) {
        m_rootElement->render(ctx);
    }

    // 绘制顶层活动弹出层 (不受任何父级滚动或局部裁剪影响)
    if (m_activePopup && m_activePopup->isVisible()) {
        m_activePopup->render(ctx);
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        m_renderTarget.Reset();
        initDirect2D();
    }

    m_needsPaint = false;
}

LRESULT CALLBACK NativeWindowHost::staticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NativeWindowHost* self = nullptr;
    if (uMsg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<NativeWindowHost*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<NativeWindowHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->handleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT NativeWindowHost::handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // 拦截系统默认按键抢占焦点
    if (tools3000::ui::KeyboardPipeline::filterWindowMessage(m_hwnd, uMsg, wParam, lParam)) {
        return 0;
    }

    switch (uMsg) {
    case WM_NCCALCSIZE: {
        if (wParam) {
            // 当 wParam 为 TRUE 时，lParam 指向 NCCALCSIZE_PARAMS
            // 返回 0 即将整个窗口物理矩形作为客户区，彻底剥离 Win32 原生浅蓝/白底标题栏与非客户区边框
            if (IsZoomed(m_hwnd)) {
                auto params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{ sizeof(mi) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    params->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        } else {
            if (IsZoomed(m_hwnd)) {
                auto rc = reinterpret_cast<RECT*>(lParam);
                HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{ sizeof(mi) };
                if (GetMonitorInfoW(hMon, &mi)) {
                    *rc = mi.rcWork;
                }
            }
            return 0;
        }
    }

    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(hMon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        mmi->ptMinTrackSize.x = static_cast<LONG>(m_config.minWidth * m_dpiScale);
        mmi->ptMinTrackSize.y = static_cast<LONG>(m_config.minHeight * m_dpiScale);
        return 0;
    }

    case WM_NCACTIVATE:
        // 彻底禁止 Windows Shell 在窗口激活态切换时自绘原生浅蓝/白底经典标题栏
        return TRUE;

    case WM_NCPAINT:
        // 彻底禁止 Windows Shell 自行绘制任何原生边框与标题栏
        return 0;

    case WM_NCLBUTTONDBLCLK: {
        if (wParam == HTCAPTION && m_config.resizable) {
            toggleMaximize();
            return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == 1) {
            bool hasRunningAnimations = updateAnimations();
            paint();
            // 只有当所有弹簧/渐变动画均已结束且无需重绘与重排时，才暂停计时器节省 CPU
            if (!m_needsPaint && !m_needsLayout && !hasRunningAnimations) {
                KillTimer(m_hwnd, 1);
                m_isTimerActive = false;
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        paint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        resizeD2D(w, h);
        applyRoundedCorners();
        if (m_onSizeChanged) {
            m_onSizeChanged(static_cast<int>(w), static_cast<int>(h), IsZoomed(m_hwnd) != FALSE);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        m_dpiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
        auto rc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_renderTarget) {
            m_renderTarget->SetDpi(96.0f * m_dpiScale, 96.0f * m_dpiScale);
        }
        m_needsLayout = true;
        m_needsPaint = true;
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;
        GetWindowRect(m_hwnd, &rc);

        const int border = static_cast<int>(8.0f * m_dpiScale);
        if (!IsZoomed(m_hwnd) && m_config.resizable) {
            if (pt.y < rc.top + border && pt.x < rc.left + border) return HTTOPLEFT;
            if (pt.y < rc.top + border && pt.x >= rc.right - border) return HTTOPRIGHT;
            if (pt.y >= rc.bottom - border && pt.x < rc.left + border) return HTBOTTOMLEFT;
            if (pt.y >= rc.bottom - border && pt.x >= rc.right - border) return HTBOTTOMRIGHT;
            if (pt.y < rc.top + border) return HTTOP;
            if (pt.y >= rc.bottom - border) return HTBOTTOM;
            if (pt.x < rc.left + border) return HTLEFT;
            if (pt.x >= rc.right - border) return HTRIGHT;
        }

        // 顶部沉浸式标题栏区域拖拽（严格跟随 DPI 缩放并动态支持内部按钮控件响应）
        if (m_config.seamlessTitlebar) {
            const int titleBarH = static_cast<int>(38.0f * m_dpiScale);
            if (pt.y < rc.top + titleBarH) {
                Point logicPt(static_cast<float>(pt.x - rc.left) / m_dpiScale,
                              static_cast<float>(pt.y - rc.top) / m_dpiScale);
                std::shared_ptr<UIElement> hit = nullptr;
                if (m_rootElement) {
                    hit = m_rootElement->hitTest(logicPt);
                }
                if (hit && hit != m_rootElement) {
                    auto btn = std::dynamic_pointer_cast<UIButton>(hit);
                    if (btn) {
                        return HTCLIENT;
                    }
                }
                return HTCAPTION;
            }
        }

        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        Point pt(static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale,
                 static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale);
        UIMouseEvent me(MouseEventType::MouseMove, pt);
        me.modifiers = getKeyboardModifiers();

        if (m_capturedElement) {
            m_capturedElement->onMouseMove(me);
            return 0;
        }

        std::shared_ptr<UIElement> hit = nullptr;
        if (m_activePopup && m_activePopup->isVisible()) {
            hit = m_activePopup->hitTest(pt);
        }
        if (!hit && m_rootElement) {
            hit = m_rootElement->hitTest(pt);
        }

        if (hit != m_hoveredElement) {
            if (m_hoveredElement) {
                m_hoveredElement->onMouseLeave();
            }
            m_hoveredElement = hit;
            if (m_hoveredElement) {
                m_hoveredElement->onMouseEnter();
            }
        }

        if (m_hoveredElement) {
            m_hoveredElement->onMouseMove(me);
        }

        // 跟踪鼠标离开窗口
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (m_hoveredElement) {
            m_hoveredElement->onMouseLeave();
            m_hoveredElement = nullptr;
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        Point pt(static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale,
                 static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale);
        MouseButton btn = (uMsg == WM_LBUTTONDOWN) ? MouseButton::Left : MouseButton::Right;
        UIMouseEvent me(MouseEventType::MouseDown, pt, btn);
        me.modifiers = getKeyboardModifiers();

        // 1. 如果当前有弹出层
        if (m_activePopup && m_activePopup->isVisible()) {
            auto popupHit = m_activePopup->hitTest(pt);
            if (popupHit) {
                if (popupHit != m_focusedElement) {
                    if (m_focusedElement) m_focusedElement->onBlur();
                    m_focusedElement = popupHit;
                    m_focusedElement->onFocus();
                }
                m_capturedElement = popupHit;
                SetCapture(m_hwnd);
                popupHit->onMouseDown(me);
                return 0;
            } else {
                closePopup();
                return 0; // 轻量级收起 (Light Dismiss) 吞掉此次点击，避免误触发底层控件
            }
        }

        auto hit = m_rootElement ? m_rootElement->hitTest(pt) : nullptr;
        if (hit != m_focusedElement) {
            if (m_focusedElement) {
                m_focusedElement->onBlur();
            }
            m_focusedElement = hit;
            if (m_focusedElement) {
                m_focusedElement->onFocus();
            }
        }

        if (hit) {
            m_capturedElement = hit;
            hit->onMouseDown(me);
        }
        SetCapture(m_hwnd);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        Point pt(static_cast<float>(GET_X_LPARAM(lParam)) / m_dpiScale,
                 static_cast<float>(GET_Y_LPARAM(lParam)) / m_dpiScale);
        MouseButton btn = (uMsg == WM_LBUTTONUP) ? MouseButton::Left : MouseButton::Right;
        UIMouseEvent me(MouseEventType::MouseUp, pt, btn);
        me.modifiers = getKeyboardModifiers();

        if (m_capturedElement) {
            auto captured = m_capturedElement;
            m_capturedElement = nullptr;
            ReleaseCapture();
            captured->onMouseUp(me);
        } else if (m_hoveredElement) {
            m_hoveredElement->onMouseUp(me);
            ReleaseCapture();
        } else {
            ReleaseCapture();
        }

        // 释放后重新更新悬停元素
        std::shared_ptr<UIElement> newHit = nullptr;
        if (m_activePopup && m_activePopup->isVisible()) {
            newHit = m_activePopup->hitTest(pt);
        }
        if (!newHit && m_rootElement) {
            newHit = m_rootElement->hitTest(pt);
        }
        if (newHit != m_hoveredElement) {
            if (m_hoveredElement) m_hoveredElement->onMouseLeave();
            m_hoveredElement = newHit;
            if (m_hoveredElement) m_hoveredElement->onMouseEnter();
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (m_capturedElement) {
            UIMouseEvent cancelEvent(MouseEventType::MouseUp, Point(-1.0f, -1.0f), MouseButton::Left);
            m_capturedElement->onMouseUp(cancelEvent);
            m_capturedElement = nullptr;
        }
        return 0;

    case WM_MOUSEWHEEL: {
        POINT screenPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(m_hwnd, &screenPt);
        Point pt(static_cast<float>(screenPt.x) / m_dpiScale, static_cast<float>(screenPt.y) / m_dpiScale);
        float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam));

        UIMouseEvent me(MouseEventType::MouseWheel, pt, MouseButton::None, delta);
        me.modifiers = getKeyboardModifiers();

        if (m_activePopup && m_activePopup->isVisible()) {
            auto popupHit = m_activePopup->hitTest(pt);
            auto curr = popupHit;
            while (curr) {
                if (curr->onMouseWheel(me)) return 0;
                curr = curr->getParent();
            }
            closePopup();
        }

        auto hit = m_rootElement ? m_rootElement->hitTest(pt) : nullptr;
        auto curr = hit;
        while (curr) {
            if (curr->onMouseWheel(me)) return 0;
            curr = curr->getParent();
        }

        auto scroll = findScrollViewAt(m_rootElement, pt);
        if (scroll) {
            scroll->onMouseWheel(me);
            return 0;
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (m_activePopup) {
                closePopup();
                return 0;
            }
            if (m_config.isPopup) {
                hide();
                return 0;
            }
        }
        UIKeyEvent ke(KeyEventType::KeyDown, static_cast<uint32_t>(wParam));
        ke.modifiers = getKeyboardModifiers();
        bool handled = false;
        if (m_focusedElement) {
            handled = m_focusedElement->onKeyDown(ke);
        }
        if (!handled && m_rootElement && m_rootElement != m_focusedElement) {
            m_rootElement->onKeyDown(ke);
        }
        return 0;
    }

    case WM_KEYUP: {
        UIKeyEvent ke(KeyEventType::KeyUp, static_cast<uint32_t>(wParam));
        ke.modifiers = getKeyboardModifiers();
        bool handled = false;
        if (m_focusedElement) {
            handled = m_focusedElement->onKeyUp(ke);
        }
        if (!handled && m_rootElement && m_rootElement != m_focusedElement) {
            m_rootElement->onKeyUp(ke);
        }
        return 0;
    }

    case WM_CHAR: {
        UIKeyEvent ke(KeyEventType::Char, 0, static_cast<wchar_t>(wParam));
        ke.modifiers = getKeyboardModifiers();
        bool handled = false;
        if (m_focusedElement) {
            handled = m_focusedElement->onChar(ke);
        }
        if (!handled && m_rootElement && m_rootElement != m_focusedElement) {
            m_rootElement->onChar(ke);
        }
        return 0;
    }

    case WM_CLOSE:
        hide();
        return 0;

    default:
        break;
    }

    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

} // namespace tools3000::ui::native
