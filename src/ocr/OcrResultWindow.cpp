#include "OcrResultWindow.h"

#include "OcrResultStyle.h"
#include "core/events/MainThreadDispatcher.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

#include <windowsx.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

namespace easy::ocr {
namespace {

constexpr wchar_t WindowClass[] = L"EasyTools_OcrResult";
constexpr UINT FadeTimerId = 1;
constexpr UINT CopyStateTimerId = 2;
constexpr UINT FadeIntervalMs = 16;
constexpr UINT FadeDurationMs = 150;
constexpr UINT CopyStateDurationMs = 2000;

bool contains(const D2D1_RECT_F& rect, POINT point) noexcept {
    return point.x >= rect.left && point.x <= rect.right &&
           point.y >= rect.top && point.y <= rect.bottom;
}

}  // namespace

bool OcrResultWindow::initialize() {
    if (m_hwnd && IsWindow(m_hwnd)) return true;

    m_instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = wndProc;
    windowClass.hInstance = m_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_ERROR("OCR result window class registration failed, error={}", GetLastError());
        return false;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WindowClass, L"OCR Result", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, m_instance, this);
    if (!m_hwnd) {
        LOG_ERROR("OCR result window creation failed, error={}", GetLastError());
        return false;
    }

    // Do not let our own OCR result or keyboard guide leak into screenshots or
    // recordings. Unsupported Windows builds simply ignore this best effort.
    SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);
    if (!createResources()) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }
    return true;
}

void OcrResultWindow::cleanup() {
    if (m_hwnd) {
        KillTimer(m_hwnd, FadeTimerId);
        KillTimer(m_hwnd, CopyStateTimerId);
        ShowWindow(m_hwnd, SW_HIDE);
        SetFocus(nullptr);
    }
    discardDeviceResources();
    if (m_hwnd) {
        const HWND window = m_hwnd;
        m_hwnd = nullptr;
        DestroyWindow(window);
    }
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    m_text.clear();
    m_wideText.clear();
}

bool OcrResultWindow::createResources() {
    if (!m_d2dFactory && FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
        return false;
    }
    if (!m_dwriteFactory && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory))) {
        return false;
    }
    if (!m_renderTarget) {
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
            D2D1_FEATURE_LEVEL_DEFAULT);
        if (FAILED(m_d2dFactory->CreateDCRenderTarget(
                &properties, &m_renderTarget))) {
            return false;
        }
    }
    return createTextResources(m_scale);
}

bool OcrResultWindow::createTextResources(float scale) {
    if (!m_dwriteFactory) return false;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> hintFormat;
    const auto createFormat = [&](float size, DWRITE_FONT_WEIGHT weight,
                                  IDWriteTextFormat** output) {
        return m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size * scale,
            easy::core::WinUtils::isSystemLanguageChinese() ? L"zh-CN" : L"en-US",
            output);
    };
    if (FAILED(createFormat(OcrResultStyle::BaseBodyFont,
                            DWRITE_FONT_WEIGHT_NORMAL, &textFormat)) ||
        FAILED(createFormat(OcrResultStyle::BaseTitleFont,
                            DWRITE_FONT_WEIGHT_SEMI_BOLD, &titleFormat)) ||
        FAILED(createFormat(OcrResultStyle::BaseButtonFont,
                            DWRITE_FONT_WEIGHT_SEMI_BOLD, &buttonFormat)) ||
        FAILED(createFormat(OcrResultStyle::BaseHintFont,
                            DWRITE_FONT_WEIGHT_NORMAL, &hintFormat))) {
        return false;
    }
    textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    buttonFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    buttonFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textLayout.Reset();
    m_textFormat = std::move(textFormat);
    m_titleFormat = std::move(titleFormat);
    m_buttonFormat = std::move(buttonFormat);
    m_hintFormat = std::move(hintFormat);
    return true;
}

void OcrResultWindow::discardDeviceResources() {
    m_brushBg.Reset();
    m_brushText.Reset();
    m_brushMuted.Reset();
    m_brushBorder.Reset();
    m_brushBtn.Reset();
    m_brushBtnHover.Reset();
    m_brushSuccess.Reset();
    m_brushError.Reset();
    m_textLayout.Reset();
    m_textFormat.Reset();
    m_titleFormat.Reset();
    m_buttonFormat.Reset();
    m_hintFormat.Reset();
    m_renderTarget.Reset();
    releaseSurface();
}

void OcrResultWindow::releaseSurface() {
    if (m_renderTarget) {
        RECT emptyRect{0, 0, 0, 0};
        m_renderTarget->BindDC(nullptr, &emptyRect);
    }
    if (m_memoryDc && m_previousBitmap) {
        SelectObject(m_memoryDc, m_previousBitmap);
    }
    m_previousBitmap = nullptr;
    if (m_bitmap) DeleteObject(m_bitmap);
    if (m_memoryDc) DeleteDC(m_memoryDc);
    if (m_screenDc) ReleaseDC(nullptr, m_screenDc);
    m_bitmap = nullptr;
    m_memoryDc = nullptr;
    m_screenDc = nullptr;
    m_pixels = nullptr;
    m_surfaceWidth = 0;
    m_surfaceHeight = 0;
}

bool OcrResultWindow::ensureSurface(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (m_bitmap && m_surfaceWidth == width && m_surfaceHeight == height) return true;

    releaseSurface();
    m_screenDc = GetDC(nullptr);
    m_memoryDc = m_screenDc ? CreateCompatibleDC(m_screenDc) : nullptr;
    if (!m_screenDc || !m_memoryDc) {
        releaseSurface();
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    m_bitmap = CreateDIBSection(m_screenDc, &bitmapInfo, DIB_RGB_COLORS,
                                &m_pixels, nullptr, 0);
    if (!m_bitmap || !m_pixels) {
        releaseSurface();
        return false;
    }
    m_previousBitmap = SelectObject(m_memoryDc, m_bitmap);
    if (!m_previousBitmap || m_previousBitmap == HGDI_ERROR) {
        m_previousBitmap = nullptr;
        releaseSurface();
        return false;
    }
    m_surfaceWidth = width;
    m_surfaceHeight = height;
    return true;
}

void OcrResultWindow::updatePlacement() {
    if (!m_hwnd || m_updatingPlacement) return;
    m_updatingPlacement = true;

    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const RECT workArea = easy::core::dpi::workArea(monitor);
    const unsigned dpi = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = easy::core::dpi::scaleForDpi(dpi);
    SIZE size = OcrResultStyle::windowSizeForDpi(dpi);
    const int margin = easy::core::dpi::scaleMetric(
        OcrResultStyle::BaseScreenMargin, scale);
    const int availableWidth = (std::max)(
        1, static_cast<int>(workArea.right - workArea.left) - margin * 2);
    const int availableHeight = (std::max)(
        1, static_cast<int>(workArea.bottom - workArea.top) - margin * 2);
    size.cx = (std::min)(size.cx, static_cast<LONG>(availableWidth));
    size.cy = (std::min)(size.cy, static_cast<LONG>(availableHeight));
    const int x = workArea.left + (workArea.right - workArea.left - size.cx) / 2;
    const int y = workArea.top + (workArea.bottom - workArea.top - size.cy) / 2;

    const bool dpiChanged = std::abs(m_scale - scale) > 0.001f;
    m_scale = scale;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (dpiChanged) createTextResources(scale);
    rebuildTextLayout();
    m_updatingPlacement = false;
}

void OcrResultWindow::rebuildTextLayout() {
    m_textLayout.Reset();
    m_maxScroll = 0.0f;
    if (!m_textFormat || !m_dwriteFactory || m_wideText.empty() || !m_hwnd) return;

    RECT client{};
    GetClientRect(m_hwnd, &client);
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    const float padding = OcrResultStyle::BasePadding * m_scale;
    const float header = OcrResultStyle::BaseHeaderHeight * m_scale;
    const float footer = OcrResultStyle::BaseFooterHeight * m_scale;
    const float contentWidth = (std::max)(1.0f, width - padding * 2.0f);
    const float viewportHeight = (std::max)(1.0f, height - header - footer);

    if (FAILED(m_dwriteFactory->CreateTextLayout(
            m_wideText.c_str(), static_cast<UINT32>(m_wideText.size()),
            m_textFormat.Get(), contentWidth, 100000.0f, &m_textLayout))) {
        return;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(m_textLayout->GetMetrics(&metrics))) {
        m_maxScroll = (std::max)(0.0f, metrics.height - viewportHeight);
        m_scrollY = std::clamp(m_scrollY, 0.0f, m_maxScroll);
    }
}

void OcrResultWindow::showResult(const std::string& text) {
    auto& dispatcher = easy::core::MainThreadDispatcher::instance();
    if (m_hwnd && GetWindowThreadProcessId(m_hwnd, nullptr) != GetCurrentThreadId()) {
        dispatcher.post([this, text]() { showResult(text); });
        return;
    }
    if (!m_hwnd && dispatcher.isInitialized() && !dispatcher.isOwnerThread()) {
        dispatcher.post([this, text]() { showResult(text); });
        return;
    }

    if ((!m_hwnd || !IsWindow(m_hwnd)) && !initialize()) return;

    m_text = text;
    m_wideText = easy::core::WinUtils::utf8ToWstring(text);
    m_scrollY = 0.0f;
    m_maxScroll = 0.0f;
    m_copiedTime = 0;
    m_copySucceeded = false;
    m_hoverCopy = false;
    m_hoverClose = false;
    m_showTime = GetTickCount64();
    m_currentAlpha = 0.0f;
    KillTimer(m_hwnd, CopyStateTimerId);

    updatePlacement();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    SetTimer(m_hwnd, FadeTimerId, FadeIntervalMs, nullptr);
    render();
}

void OcrResultWindow::copyAll() {
    if (m_text.empty()) return;
    m_copySucceeded = easy::core::WinUtils::copyToClipboard(m_text);
    m_copiedTime = GetTickCount64();
    SetTimer(m_hwnd, CopyStateTimerId, CopyStateDurationMs, nullptr);
    render();
}

void OcrResultWindow::hide() {
    if (!m_hwnd) return;
    KillTimer(m_hwnd, FadeTimerId);
    KillTimer(m_hwnd, CopyStateTimerId);
    ShowWindow(m_hwnd, SW_HIDE);
    releaseSurface();
    m_textLayout.Reset();
    m_text.clear();
    m_wideText.clear();
    easy::core::WinUtils::trimWorkingSet();
}

void OcrResultWindow::updateHover(POINT point) {
    const bool hoverCopy = contains(m_btnCopyRect, point);
    const bool hoverClose = contains(m_btnCloseRect, point);
    if (hoverCopy == m_hoverCopy && hoverClose == m_hoverClose) return;
    m_hoverCopy = hoverCopy;
    m_hoverClose = hoverClose;
    SetCursor(LoadCursorW(nullptr, (hoverCopy || hoverClose) ? IDC_HAND : IDC_ARROW));
    render();
}

void OcrResultWindow::render() {
    if (!m_hwnd || !IsWindowVisible(m_hwnd)) return;
    if (!m_renderTarget && !createResources()) return;

    RECT client{};
    GetClientRect(m_hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (!ensureSurface(width, height)) return;
    std::memset(m_pixels, 0, static_cast<size_t>(width) * height * 4);

    RECT bindRect{0, 0, width, height};
    if (FAILED(m_renderTarget->BindDC(m_memoryDc, &bindRect))) return;
    if (!m_brushBg) {
        const auto brush = [&](D2D1_COLOR_F color,
                               Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>& output) {
            return SUCCEEDED(m_renderTarget->CreateSolidColorBrush(color, &output));
        };
        if (!brush(D2D1::ColorF(0x11131A, 1.0f), m_brushBg) ||
            !brush(D2D1::ColorF(0xF4F5F8, 1.0f), m_brushText) ||
            !brush(D2D1::ColorF(0xA8ACB8, 0.92f), m_brushMuted) ||
            !brush(D2D1::ColorF(0x7C6CF2, 0.62f), m_brushBorder) ||
            !brush(D2D1::ColorF(0xFFFFFF, 0.09f), m_brushBtn) ||
            !brush(D2D1::ColorF(0xFFFFFF, 0.16f), m_brushBtnHover) ||
            !brush(D2D1::ColorF(0x34D399, 0.88f), m_brushSuccess) ||
            !brush(D2D1::ColorF(0xF87171, 0.88f), m_brushError)) {
            m_brushBg.Reset();
            m_brushText.Reset();
            m_brushMuted.Reset();
            m_brushBorder.Reset();
            m_brushBtn.Reset();
            m_brushBtnHover.Reset();
            m_brushSuccess.Reset();
            m_brushError.Reset();
            return;
        }
    }

    const float scale = m_scale;
    const float padding = OcrResultStyle::BasePadding * scale;
    const float header = OcrResultStyle::BaseHeaderHeight * scale;
    const float footer = OcrResultStyle::BaseFooterHeight * scale;
    const float radius = OcrResultStyle::BaseCornerRadius * scale;
    const float border = OcrResultStyle::BaseBorderWidth * scale;
    const float buttonHeight = OcrResultStyle::BaseButtonHeight * scale;
    const float copyWidth = OcrResultStyle::BaseButtonWidth * scale;
    const float closeWidth = OcrResultStyle::BaseCloseButtonWidth * scale;
    const float buttonGap = OcrResultStyle::BaseButtonGap * scale;

    m_btnCloseRect = D2D1::RectF(
        width - padding - closeWidth, height - padding - buttonHeight,
        width - padding, height - padding);
    m_btnCopyRect = D2D1::RectF(
        m_btnCloseRect.left - buttonGap - copyWidth,
        m_btnCloseRect.top, m_btnCloseRect.left - buttonGap,
        m_btnCloseRect.bottom);

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0.0f));
    const auto card = D2D1::RoundedRect(
        D2D1::RectF(border / 2.0f, border / 2.0f,
                    width - border / 2.0f, height - border / 2.0f),
        radius, radius);
    m_renderTarget->FillRoundedRectangle(&card, m_brushBg.Get());
    m_renderTarget->DrawRoundedRectangle(&card, m_brushBorder.Get(), border);

    const bool chinese = easy::core::WinUtils::isSystemLanguageChinese();
    const wchar_t* title = chinese ? L"文字识别结果" : L"Recognized text";
    m_renderTarget->DrawTextW(
        title, static_cast<UINT32>(wcslen(title)), m_titleFormat.Get(),
        D2D1::RectF(padding, padding * 0.72f, width - padding, header),
        m_brushText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    if (m_textLayout) {
        const D2D1_RECT_F viewport = D2D1::RectF(
            padding, header, width - padding, height - footer);
        m_renderTarget->PushAxisAlignedClip(
            viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_renderTarget->DrawTextLayout(
            D2D1::Point2F(padding, header - m_scrollY),
            m_textLayout.Get(), m_brushText.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
        m_renderTarget->PopAxisAlignedClip();
    }

    const float buttonRadius = 7.0f * scale;
    ID2D1SolidColorBrush* copyBrush = m_hoverCopy ? m_brushBtnHover.Get() : m_brushBtn.Get();
    std::wstring copyLabel = chinese ? L"复制全文" : L"Copy all";
    if (m_copiedTime) {
        if (m_copySucceeded) {
            copyBrush = m_brushSuccess.Get();
            copyLabel = chinese ? L"已复制" : L"Copied";
        } else {
            copyBrush = m_brushError.Get();
            copyLabel = chinese ? L"复制失败" : L"Copy failed";
        }
    }
    const auto copyButton = D2D1::RoundedRect(
        m_btnCopyRect, buttonRadius, buttonRadius);
    const auto closeButton = D2D1::RoundedRect(
        m_btnCloseRect, buttonRadius, buttonRadius);
    m_renderTarget->FillRoundedRectangle(&copyButton, copyBrush);
    m_renderTarget->FillRoundedRectangle(
        &closeButton, m_hoverClose ? m_brushBtnHover.Get() : m_brushBtn.Get());
    m_renderTarget->DrawTextW(
        copyLabel.c_str(), static_cast<UINT32>(copyLabel.size()),
        m_buttonFormat.Get(), m_btnCopyRect, m_brushText.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    const std::wstring closeLabel = chinese ? L"关闭" : L"Close";
    m_renderTarget->DrawTextW(
        closeLabel.c_str(), static_cast<UINT32>(closeLabel.size()),
        m_buttonFormat.Get(), m_btnCloseRect, m_brushText.Get(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // Shortcuts live inside the result window footer instead of spawning a
    // second overlay. This keeps the guide discoverable without stealing more
    // screen space or interrupting the OCR reading flow.
    const wchar_t* hint = chinese
        ? L"Ctrl+C  复制全文   ·   滚轮 / PgUp PgDn  滚动   ·   Esc  关闭"
        : L"Ctrl+C  Copy all   ·   Wheel / PgUp PgDn  Scroll   ·   Esc  Close";
    m_renderTarget->DrawTextW(
        hint, static_cast<UINT32>(wcslen(hint)), m_hintFormat.Get(),
        D2D1::RectF(padding, height - padding - buttonHeight,
                    m_btnCopyRect.left - buttonGap, height - padding),
        m_brushMuted.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const HRESULT endResult = m_renderTarget->EndDraw();
    if (endResult == D2DERR_RECREATE_TARGET) {
        discardDeviceResources();
        if (createResources()) {
            rebuildTextLayout();
            render();
        }
        return;
    }
    if (FAILED(endResult)) return;

    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0,
                        static_cast<BYTE>(std::clamp(m_currentAlpha, 0.0f, 255.0f)),
                        AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, m_screenDc, nullptr, &size, m_memoryDc,
                        &source, 0, &blend, ULW_ALPHA);
}

LRESULT CALLBACK OcrResultWindow::wndProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    OcrResultWindow* self = reinterpret_cast<OcrResultWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OcrResultWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            const float dragBottom = OcrResultStyle::BaseHeaderHeight * self->m_scale;
            return point.y < dragBottom ? HTCAPTION : HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            if (!self->m_trackingMouse) {
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
                self->m_trackingMouse = TrackMouseEvent(&tracking) != FALSE;
            }
            self->updateHover({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        }
        case WM_MOUSELEAVE:
            self->m_trackingMouse = false;
            self->m_hoverCopy = false;
            self->m_hoverClose = false;
            self->render();
            return 0;
        case WM_LBUTTONUP: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (contains(self->m_btnCopyRect, point)) self->copyAll();
            else if (contains(self->m_btnCloseRect, point)) self->hide();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float amount = OcrResultStyle::BaseLineScroll * self->m_scale;
            self->m_scrollY = std::clamp(
                self->m_scrollY - (static_cast<float>(delta) / WHEEL_DELTA) * amount,
                0.0f, self->m_maxScroll);
            self->render();
            return 0;
        }
        case WM_KEYDOWN: {
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wParam == VK_ESCAPE) {
                self->hide();
            } else if (control && wParam == 'C') {
                self->copyAll();
            } else {
                const float viewport = (std::max)(
                    1.0f, static_cast<float>(self->m_surfaceHeight) -
                                  (OcrResultStyle::BaseHeaderHeight +
                                   OcrResultStyle::BaseFooterHeight) * self->m_scale);
                float next = self->m_scrollY;
                if (wParam == VK_PRIOR) next -= viewport * 0.9f;
                else if (wParam == VK_NEXT) next += viewport * 0.9f;
                else if (wParam == VK_HOME) next = 0.0f;
                else if (wParam == VK_END) next = self->m_maxScroll;
                else return 0;
                self->m_scrollY = std::clamp(next, 0.0f, self->m_maxScroll);
                self->render();
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == FadeTimerId) {
                const float elapsed = static_cast<float>(
                    GetTickCount64() - self->m_showTime);
                const float progress = std::clamp(
                    elapsed / static_cast<float>(FadeDurationMs), 0.0f, 1.0f);
                self->m_currentAlpha = 255.0f *
                    (1.0f - std::pow(1.0f - progress, 3.0f));
                self->render();
                if (progress >= 1.0f) KillTimer(hwnd, FadeTimerId);
            } else if (wParam == CopyStateTimerId) {
                KillTimer(hwnd, CopyStateTimerId);
                self->m_copiedTime = 0;
                self->render();
            }
            return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
            if (!self->m_updatingPlacement && IsWindowVisible(hwnd)) {
                self->updatePlacement();
                self->render();
            }
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, FadeTimerId);
            KillTimer(hwnd, CopyStateTimerId);
            self->releaseSurface();
            if (self->m_hwnd == hwnd) self->m_hwnd = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace easy::ocr
