#include "PinnedWindow.h"
#include <vector>
#include <mutex>
#include <algorithm>
#include <windowsx.h>

#pragma comment(lib, "d2d1.lib")

static std::vector<PinnedWindow*> g_pinnedWindows;
static std::mutex g_pinnedMutex;
static const wchar_t* PINNED_WINDOW_CLASS = L"EasyTools_PinnedWindow";
static bool g_classRegistered = false;

void PinnedWindow::registerClass(HINSTANCE hInstance) {
    if (g_classRegistered) return;
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | 0x00020000; // CS_DROPSHADOW
    wcex.lpfnWndProc = PinnedWindow::wndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = PINNED_WINDOW_CLASS;
    RegisterClassExW(&wcex);
    g_classRegistered = true;
}

void PinnedWindow::closeAllWindows() {
    std::lock_guard<std::mutex> lock(g_pinnedMutex);
    for (auto* pw : g_pinnedWindows) {
        if (pw->m_hwnd) DestroyWindow(pw->m_hwnd);
    }
}

PinnedWindow::PinnedWindow(const cv::Mat& image, int x, int y) {
    // Clone image to ensure continuous memory
    m_image = image.clone();
    m_origWidth = m_image.cols;
    m_origHeight = m_image.rows;

    registerClass(GetModuleHandle(nullptr));

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        PINNED_WINDOW_CLASS, L"PinnedImage",
        WS_POPUP,
        x, y, m_origWidth, m_origHeight,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (m_hwnd) {
        SetLayeredWindowAttributes(m_hwnd, 0, m_alpha, LWA_ALPHA);
        
        std::lock_guard<std::mutex> lock(g_pinnedMutex);
        g_pinnedWindows.push_back(this);
    }
}

PinnedWindow::~PinnedWindow() {
    std::lock_guard<std::mutex> lock(g_pinnedMutex);
    auto it = std::find(g_pinnedWindows.begin(), g_pinnedWindows.end(), this);
    if (it != g_pinnedWindows.end()) {
        g_pinnedWindows.erase(it);
    }
}

void PinnedWindow::show() {
    if (!m_hwnd) return;
    initD2D();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    render();
}

void PinnedWindow::destroy() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool PinnedWindow::initD2D() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    m_d2dFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(m_hwnd, size),
        &m_renderTarget
    );

    if (!m_renderTarget) return false;

    // Convert cv::Mat (BGR) to BGRA for D2D
    cv::Mat bgra;
    if (m_image.channels() == 3) {
        cv::cvtColor(m_image, bgra, cv::COLOR_BGR2BGRA);
    } else {
        bgra = m_image;
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    m_renderTarget->CreateBitmap(
        D2D1::SizeU(bgra.cols, bgra.rows),
        bgra.data,
        bgra.step[0],
        props,
        &m_d2dBitmap
    );

    return true;
}

void PinnedWindow::render() {
    if (!m_renderTarget) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (m_d2dBitmap) {
        auto size = m_renderTarget->GetSize();
        m_renderTarget->DrawBitmap(
            m_d2dBitmap.Get(),
            D2D1::RectF(0, 0, size.width, size.height)
        );
    }

    // Draw a subtle border
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.5f), &borderBrush);
    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawRectangle(D2D1::RectF(0, 0, size.width, size.height), borderBrush.Get(), 1.0f);

    m_renderTarget->EndDraw();
}

void PinnedWindow::updateScale(float deltaScale, POINT cursorPt) {
    float oldScale = m_scale;
    m_scale = std::clamp(m_scale + deltaScale, 0.1f, 10.0f);
    
    if (m_scale == oldScale) return;

    // Calculate new size
    int newW = static_cast<int>(m_origWidth * m_scale);
    int newH = static_cast<int>(m_origHeight * m_scale);

    // Keep the point under cursor at the same screen position
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    
    float ratioX = static_cast<float>(cursorPt.x - rc.left) / (rc.right - rc.left);
    float ratioY = static_cast<float>(cursorPt.y - rc.top) / (rc.bottom - rc.top);

    int newX = cursorPt.x - static_cast<int>(newW * ratioX);
    int newY = cursorPt.y - static_cast<int>(newH * ratioY);

    SetWindowPos(m_hwnd, nullptr, newX, newY, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_renderTarget) {
        m_renderTarget->Resize(D2D1::SizeU(newW, newH));
        render();
    }
}

void PinnedWindow::updateAlpha(int deltaAlpha) {
    m_alpha = std::clamp(m_alpha + deltaAlpha, 25, 255);
    SetLayeredWindowAttributes(m_hwnd, 0, m_alpha, LWA_ALPHA);
}

LRESULT CALLBACK PinnedWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PinnedWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<PinnedWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<PinnedWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->handleMessage(msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT PinnedWindow::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            // Easy dragging
            ReleaseCapture();
            SendMessage(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;

        case WM_RBUTTONUP:
        case WM_LBUTTONDBLCLK:
            // Right click or double click to close
            destroy();
            return 0;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            
            if (ctrl) {
                // Adjust opacity
                updateAlpha(delta > 0 ? 15 : -15);
            } else {
                // Adjust scale
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                updateScale(delta > 0 ? 0.1f : -0.1f, pt);
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                destroy();
                return 0;
            }
            break;

        case WM_DPICHANGED: {
            RECT* prcNewWindow = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(m_hwnd,
                nullptr,
                prcNewWindow->left,
                prcNewWindow->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            render();
            EndPaint(m_hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            // Do not quit app on pinned window close
            // Self delete is tricky here if we allocated it dynamically.
            // Let's just delete this when window is destroyed.
            delete this;
            return 0;
    }
    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}
