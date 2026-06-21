// ─────────────────────────────────────────────────────────────────────────────
// PinWindow.cpp — 贴图窗口实现
//
// 功能:
//   - 截图后按 "钉住" 将截图贴在屏幕上
//   - 左键拖拽移动, 滚轮缩放, 双击关闭
//   - 支持透明度调节 (右键菜单)
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/PinWindow.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <string>
#include <windowsx.h>

namespace easy::capture {

using namespace Microsoft::WRL;

static constexpr const wchar_t* PIN_CLASS = L"EasyTools_PinWindow";

// 静态成员
std::vector<std::shared_ptr<PinWindow>> PinWindow::s_instances;
bool PinWindow::s_classRegistered = false;
bool PinWindow::s_allHidden = false;

// ─────────────────────────────────────────────────────────────────────────────
// 创建 / 销毁
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<PinWindow> PinWindow::create(const cv::Mat& image, int x, int y) {
    if (image.empty()) return nullptr;

    auto pin = std::shared_ptr<PinWindow>(new PinWindow());
    pin->m_origWidth = image.cols;
    pin->m_origHeight = image.rows;
    pin->m_sourceImage = image.clone();  // 保存原图，供右键"复制到剪贴板"

    if (!pin->initWindow(GetModuleHandleW(nullptr), x, y, image.cols, image.rows)) {
        LOG_ERROR("贴图窗口创建失败");
        return nullptr;
    }

    if (!pin->createRenderResources(image)) {
        LOG_ERROR("贴图窗口渲染资源创建失败");
        return nullptr;
    }

    ShowWindow(pin->m_hwnd, SW_SHOWNOACTIVATE);
    pin->render();

    s_instances.push_back(pin);
    LOG_INFO("贴图窗口已创建: {}x{} @ ({},{}), 总数={}", image.cols, image.rows, x, y, s_instances.size());

    return pin;
}

void PinWindow::close() {
    // 保活: 下面的 erase 会丢弃 s_instances 中指向自身的最后一个 shared_ptr，
    // 若不先持有自身引用，this 会在本函数执行途中被析构 (use-after-free)。
    // close() 通常从 pinWndProc (双击/右键) 内被调用，UAF 会偶发崩溃。
    std::shared_ptr<PinWindow> keepAlive;
    try {
        keepAlive = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        // 理论上不会发生 (实例总由 create() 用 shared_ptr 持有)
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    // 从全局列表移除已失效的实例
    s_instances.erase(
        std::remove_if(s_instances.begin(), s_instances.end(),
                       [](const auto& p) { return !p->isAlive(); }),
        s_instances.end()
    );
}

void PinWindow::closeAll() {
    for (auto& pin : s_instances) {
        if (pin->m_hwnd) {
            DestroyWindow(pin->m_hwnd);
            pin->m_hwnd = nullptr;
        }
    }
    s_instances.clear();
    s_allHidden = false;
    LOG_INFO("所有贴图窗口已关闭");
}

void PinWindow::toggleHideAll() {
    if (s_instances.empty()) return;
    // 智能切换：只要有任意贴图被隐藏（含被 Esc 单独隐藏的），就全部显示；否则全部隐藏。
    // 这样单独 Esc 隐藏的贴图按一次此键即可找回。
    bool anyHidden = false;
    for (auto& pin : s_instances) {
        if (pin && pin->m_hwnd && !IsWindowVisible(pin->m_hwnd)) { anyHidden = true; break; }
    }
    bool show = anyHidden;
    for (auto& pin : s_instances) {
        if (pin && pin->m_hwnd) {
            ShowWindow(pin->m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
        }
    }
    s_allHidden = !show;
    LOG_INFO("所有贴图已{}", show ? "显示" : "隐藏");
}

void PinWindow::arrangeAll() {
    if (s_instances.empty()) return;
    s_allHidden = false;

    RECT wa{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);  // 主屏工作区（排除任务栏）

    const int step = 32;        // 层叠步进
    const int perColumn = 12;   // 每列层叠数，超过换到右侧新列
    int i = 0;
    for (auto& pin : s_instances) {
        if (!pin || !pin->m_hwnd) continue;
        int col = i / perColumn;
        int row = i % perColumn;
        int px = wa.left + 40 + col * (step * 13) + row * step;
        int py = wa.top  + 40 + row * step;
        ShowWindow(pin->m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(pin->m_hwnd, HWND_TOPMOST, px, py, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ++i;
    }
    LOG_INFO("已整理 {} 张贴图", i);
}

PinWindow::~PinWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 属性
// ─────────────────────────────────────────────────────────────────────────────

void PinWindow::applyLayeredOpacity() {
    if (!m_hwnd) return;
    // 穿透态用更低不透明度作为"幽灵"视觉提示
    float eff = m_opacity * (m_clickThrough ? 0.55f : 1.0f);
    SetLayeredWindowAttributes(m_hwnd, 0, static_cast<BYTE>(eff * 255), LWA_ALPHA);
}

void PinWindow::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.1f, 1.0f);
    applyLayeredOpacity();
}

void PinWindow::setClickThrough(bool enable) {
    if (!m_hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (enable) ex |= WS_EX_TRANSPARENT;
    else        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex);
    m_clickThrough = enable;
    applyLayeredOpacity();
    LOG_INFO("贴图鼠标穿透: {}", enable ? "开启" : "关闭");
}

bool PinWindow::toggleClickThroughUnderCursor() {
    POINT pt;
    GetCursorPos(&pt);
    // 倒序遍历（后创建的视为更上层）：找到光标矩形命中的贴图并切换其穿透态
    for (auto it = s_instances.rbegin(); it != s_instances.rend(); ++it) {
        auto& pin = *it;
        if (!pin || !pin->m_hwnd) continue;
        RECT rc;
        if (GetWindowRect(pin->m_hwnd, &rc) && PtInRect(&rc, pt)) {
            pin->setClickThrough(!pin->m_clickThrough);
            return true;
        }
    }
    return false;
}

// 将 cv::Mat 以 CF_DIB 写入剪贴板（复刻 ScreenCapture 的成熟实现，供右键"复制"使用）
static bool copyImageToClipboard(const cv::Mat& image) {
    if (image.empty() || !OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = image.cols;
    bi.biHeight = -image.rows;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int rowBytes = image.cols * 3;
    int stride = (rowBytes + 3) & ~3;
    bi.biSizeImage = stride * image.rows;

    size_t totalSize = sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) { CloseClipboard(); return false; }

    auto* pMem = static_cast<uint8_t*>(GlobalLock(hGlobal));
    if (!pMem) { GlobalFree(hGlobal); CloseClipboard(); return false; }
    memcpy(pMem, &bi, sizeof(bi));

    cv::Mat cont;
    if (image.channels() == 4) cv::cvtColor(image, cont, cv::COLOR_BGRA2BGR);
    else cont = image.isContinuous() ? image : image.clone();

    for (int y = 0; y < cont.rows; ++y) {
        memcpy(pMem + sizeof(bi) + y * stride, cont.ptr(y), rowBytes);
    }

    GlobalUnlock(hGlobal);
    SetClipboardData(CF_DIB, hGlobal);
    CloseClipboard();
    return true;
}

void PinWindow::setScale(float scale) {
    m_scale = std::clamp(scale, 0.25f, 4.0f);
    if (m_hwnd) {
        int w = static_cast<int>(m_origWidth * m_scale);
        int h = static_cast<int>(m_origHeight * m_scale);
        SetWindowPos(m_hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

        if (m_renderTarget) {
            m_renderTarget->Resize(D2D1::SizeU(w, h));
        }
        render();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 剪贴板贴图（图像 / 文本 / 颜色 → 浮空贴图）
// ─────────────────────────────────────────────────────────────────────────────

// 剪贴板位图(HBITMAP) → BGR cv::Mat。用 GetDIBits 统一转 32 位, 兼容任意源格式。
static cv::Mat hbitmapToMat(HBITMAP hbm) {
    BITMAP bm{};
    if (!GetObject(hbm, sizeof(bm), &bm)) return {};
    int w = bm.bmWidth, h = std::abs(bm.bmHeight);
    if (w <= 0 || h <= 0) return {};

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    cv::Mat bgra(h, w, CV_8UC4);
    HDC dc = GetDC(nullptr);
    int got = GetDIBits(dc, hbm, 0, h, bgra.data, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (got == 0) return {};

    // GetDIBits 32 位 BI_RGB 不写 alpha, 直接丢弃 alpha 得到不透明 BGR
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

// 解析 #RGB / #RRGGBB 文本为 BGR 颜色
static bool parseHexColor(const std::wstring& in, cv::Scalar& bgr, std::wstring& label) {
    size_t a = in.find_first_not_of(L" \t\r\n");
    size_t b = in.find_last_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return false;
    std::wstring s = in.substr(a, b - a + 1);
    if (s.size() < 4 || s[0] != L'#') return false;
    std::wstring hex = s.substr(1);
    if (hex.size() != 3 && hex.size() != 6) return false;
    for (wchar_t c : hex) if (!std::iswxdigit(c)) return false;

    auto hv = [](wchar_t c) -> int {
        c = std::towlower(c);
        return (c <= L'9') ? (c - L'0') : (c - L'a' + 10);
    };
    int r, g, bl;
    if (hex.size() == 3) { r = hv(hex[0]) * 17; g = hv(hex[1]) * 17; bl = hv(hex[2]) * 17; }
    else { r = hv(hex[0]) * 16 + hv(hex[1]); g = hv(hex[2]) * 16 + hv(hex[3]); bl = hv(hex[4]) * 16 + hv(hex[5]); }
    bgr = cv::Scalar(bl, g, r);
    label = s;
    return true;
}

// 颜色 → 色卡贴图（色块 + 文字按亮度自动取黑/白以保证对比）
static cv::Mat renderColorSwatch(const cv::Scalar& bgr, const std::wstring& label) {
    const int w = 180, h = 130;
    cv::Mat img(h, w, CV_8UC3, bgr);
    double lum = 0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2];
    cv::Scalar tc = lum > 140 ? cv::Scalar(20, 20, 20) : cv::Scalar(240, 240, 240);
    std::string lbl(label.begin(), label.end());  // hex 为 ASCII，可安全窄化
    cv::putText(img, lbl, cv::Point(14, h - 18), cv::FONT_HERSHEY_SIMPLEX, 0.7, tc, 1, cv::LINE_AA);
    return img;
}

// 文本 → 卡片贴图（GDI DrawTextW 渲染，正确支持中文/Unicode）
static cv::Mat renderTextToImage(const std::wstring& text) {
    HDC screen = GetDC(nullptr);
    HDC memdc = CreateCompatibleDC(screen);

    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HGDIOBJ oldFont = SelectObject(memdc, font);

    const int pad = 16, maxW = 600;
    RECT rc{0, 0, maxW, 0};
    DrawTextW(memdc, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int w = std::min<LONG>(rc.right, maxW) + pad * 2;
    int h = rc.bottom + pad * 2;
    w = std::clamp(w, 48, maxW + pad * 2);
    h = std::clamp(h, 40, 2000);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    cv::Mat result;
    if (dib && bits) {
        HGDIOBJ oldBm = SelectObject(memdc, dib);
        RECT full{0, 0, w, h};
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 34));
        FillRect(memdc, &full, bg);
        DeleteObject(bg);

        SetBkMode(memdc, TRANSPARENT);
        SetTextColor(memdc, RGB(238, 238, 238));
        RECT tr{pad, pad, w - pad, h - pad};
        DrawTextW(memdc, text.c_str(), -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        GdiFlush();

        cv::Mat bgra(h, w, CV_8UC4, bits);
        cv::cvtColor(bgra, result, cv::COLOR_BGRA2BGR);  // 输出独立内存，DIB 释放后仍有效

        SelectObject(memdc, oldBm);
        DeleteObject(dib);
    }

    SelectObject(memdc, oldFont);
    DeleteObject(font);
    DeleteDC(memdc);
    ReleaseDC(nullptr, screen);
    return result;
}

std::shared_ptr<PinWindow> PinWindow::createFromClipboard() {
    POINT pt;
    GetCursorPos(&pt);

    cv::Mat img;
    std::wstring text;
    if (OpenClipboard(nullptr)) {
        if (IsClipboardFormatAvailable(CF_BITMAP)) {
            if (HBITMAP hbm = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP))) {
                img = hbitmapToMat(hbm);  // 句柄归剪贴板所有，期间使用、勿释放
            }
        }
        if (img.empty() && IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                    text = p;
                    GlobalUnlock(h);
                }
            }
        }
        CloseClipboard();
    }

    if (img.empty() && !text.empty()) {
        cv::Scalar c;
        std::wstring hexLabel;
        if (parseHexColor(text, c, hexLabel)) img = renderColorSwatch(c, hexLabel);
        else img = renderTextToImage(text);
    }

    if (img.empty()) {
        LOG_INFO("剪贴板贴图：剪贴板无可贴内容");
        return nullptr;
    }

    LOG_INFO("剪贴板贴图：{}x{} @ ({},{})", img.cols, img.rows, pt.x, pt.y);
    return create(img, pt.x, pt.y);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口初始化
// ─────────────────────────────────────────────────────────────────────────────

bool PinWindow::initWindow(HINSTANCE hInstance, int x, int y, int w, int h) {
    if (!s_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = pinWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_SIZEALL);
        wc.lpszClassName = PIN_CLASS;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        PIN_CLASS, L"",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    return true;
}

bool PinWindow::createRenderResources(const cv::Mat& image) {
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
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

    // 禁用 D2D 的自动 DPI 缩放
    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 转为 BGRA 并创建 D2D Bitmap
    cv::Mat bgra;
    if (image.channels() == 3) {
        cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    } else if (image.channels() == 4) {
        bgra = image;
    } else {
        return false;
    }

    D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_renderTarget->CreateBitmap(
        D2D1::SizeU(bgra.cols, bgra.rows),
        bgra.data, bgra.cols * 4,
        bitmapProps, m_bitmap.GetAddressOf()
    );

    return SUCCEEDED(hr);
}

void PinWindow::render() {
    if (!m_renderTarget || !m_bitmap) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawBitmap(m_bitmap.Get(),
                               D2D1::RectF(0, 0, size.width, size.height));

    // 边框：选中态用紫色强调 2px 内描边，否则 1px 灰色
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    if (m_focused) {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.486f, 0.227f, 0.965f, 0.95f), borderBrush.GetAddressOf());
        m_renderTarget->DrawRectangle(D2D1::RectF(1, 1, size.width - 1, size.height - 1), borderBrush.Get(), 2.0f);
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.5f), borderBrush.GetAddressOf());
        m_renderTarget->DrawRectangle(D2D1::RectF(0, 0, size.width, size.height), borderBrush.Get(), 1.0f);
    }

    m_renderTarget->EndDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK PinWindow::pinWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!self) break;
            SetForegroundWindow(hwnd);  // 选中此贴图（取得键盘焦点，使其能响应 Esc）
            self->m_isDragging = true;
            self->m_dragOffset = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        }

        case WM_SETFOCUS: {
            if (self) { self->m_focused = true; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }

        case WM_KILLFOCUS: {
            if (self) { self->m_focused = false; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;
        }

        case WM_KEYDOWN: {
            // 选中贴图后按 Esc 隐藏（非破坏性，可用 Ctrl+Alt+Shift+H 全部找回）
            if (self && wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;  // 其余按键交给 DefWindowProc
        }

        case WM_MOUSEMOVE: {
            if (self && self->m_isDragging) {
                POINT cursor;
                GetCursorPos(&cursor);
                SetWindowPos(hwnd, nullptr,
                             cursor.x - self->m_dragOffset.x,
                             cursor.y - self->m_dragOffset.y,
                             0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (self) {
                self->m_isDragging = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            // 双击关闭
            if (self) self->close();
            return 0;
        }

        case WM_MOUSEWHEEL: {
            // 滚轮缩放
            if (!self) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float newScale = self->m_scale + (delta > 0 ? 0.1f : -0.1f);
            self->setScale(newScale);
            return 0;
        }

        case WM_RBUTTONUP: {
            // 右键菜单
            if (!self) break;
            HMENU menu = CreatePopupMenu();
            bool isZh = easy::core::WinUtils::isSystemLanguageChinese();
            AppendMenuW(menu, MF_STRING, 1, isZh ? L"复制到剪贴板" : L"Copy to Clipboard");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 2, isZh ? L"透明度 100%" : L"Opacity 100%");
            AppendMenuW(menu, MF_STRING, 3, isZh ? L"透明度 75%" : L"Opacity 75%");
            AppendMenuW(menu, MF_STRING, 4, isZh ? L"透明度 50%" : L"Opacity 50%");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 7, isZh ? L"鼠标穿透  (Ctrl+Alt+Shift+X 切回)"
                                               : L"Click-through  (Ctrl+Alt+Shift+X toggles)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 9, isZh ? L"整理全部贴图" : L"Arrange All Pins");
            AppendMenuW(menu, MF_STRING, 8, isZh ? L"隐藏全部贴图  (Ctrl+Alt+Shift+H 显示)"
                                               : L"Hide All Pins  (Ctrl+Alt+Shift+H shows)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 5, isZh ? L"关闭" : L"Close");
            AppendMenuW(menu, MF_STRING, 6, isZh ? L"关闭全部贴图" : L"Close All Pins");

            POINT pt;
            GetCursorPos(&pt);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            switch (cmd) {
                case 1: copyImageToClipboard(self->m_sourceImage); break;  // 修复：原先 cmd 1 未处理
                case 2: self->setOpacity(1.0f); break;
                case 3: self->setOpacity(0.75f); break;
                case 4: self->setOpacity(0.5f); break;
                case 5: self->close(); break;
                case 6: PinWindow::closeAll(); break;
                // 菜单只负责开启穿透（开启后窗口收不到右键），关闭走全局快捷键
                case 7: self->setClickThrough(true); break;
                case 8: PinWindow::toggleHideAll(); break;  // 隐藏后靠快捷键恢复
                case 9: PinWindow::arrangeAll(); break;
            }
            return 0;
        }

        case WM_PAINT: {
            if (self) self->render();
            ValidateRect(hwnd, nullptr);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::capture
