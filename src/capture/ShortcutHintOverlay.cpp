#include "capture/ShortcutHintOverlay.h"
#include "capture/ShortcutHintStyle.h"

#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace easy::capture {

namespace {
constexpr const wchar_t* WindowClass = L"EasyTools_ShortcutHintOverlay";

bool useChineseLabels() {
    const auto language = easy::core::ConfigManager::instance().get<std::string>(
        "/general/language", "auto");
    return language == "zh-CN" ||
           (language == "auto" && easy::core::WinUtils::isSystemLanguageChinese());
}

std::wstring configuredShortcut(const char* name, const wchar_t* fallback) {
    for (const auto& entry : easy::core::HotkeyManager::instance().getAllHotkeys()) {
        if (entry.name == name) {
            const auto text = entry.def.toString();
            return text.empty() ? std::wstring{} : easy::core::WinUtils::utf8ToWstring(text);
        }
    }
    return fallback;
}

bool highContrastEnabled() {
    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    return SystemParametersInfoW(
               SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0) &&
           (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

D2D1_COLOR_F systemColor(int colorIndex, float alpha = 1.0f) {
    const COLORREF color = GetSysColor(colorIndex);
    return D2D1::ColorF(
        GetRValue(color) / 255.0f,
        GetGValue(color) / 255.0f,
        GetBValue(color) / 255.0f,
        alpha);
}
}  // namespace

ShortcutHintOverlay& ShortcutHintOverlay::instance() {
    static ShortcutHintOverlay overlay;
    return overlay;
}

bool ShortcutHintOverlay::isVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

std::vector<ShortcutHintItem>
ShortcutHintOverlay::itemsFor(ShortcutHintContext context) const {
    const bool zh = useChineseLabels();
    switch (context) {
        case ShortcutHintContext::CaptureSelecting:
            return zh
                ? std::vector<ShortcutHintItem>{{L"拖拽", L"选择区域"}, {L"单击", L"选择窗口"},
                                                {L"Ctrl+A", L"全屏"}, {L"Esc", L"退出"}}
                : std::vector<ShortcutHintItem>{{L"Drag", L"Select area"}, {L"Click", L"Select window"},
                                                {L"Ctrl+A", L"Full screen"}, {L"Esc", L"Exit"}};
        case ShortcutHintContext::CaptureSelected:
            return zh
                ? std::vector<ShortcutHintItem>{{L"Enter", L"完成"}, {L"Ctrl+C", L"复制"},
                                                {L"Ctrl+Z", L"撤销"}, {L"R / A / T", L"标注工具"},
                                                {L"Esc", L"取消"}}
                : std::vector<ShortcutHintItem>{{L"Enter", L"Finish"}, {L"Ctrl+C", L"Copy"},
                                                {L"Ctrl+Z", L"Undo"}, {L"R / A / T", L"Markup tools"},
                                                {L"Esc", L"Cancel"}};
        case ShortcutHintContext::RecordSelecting:
            return zh
                ? std::vector<ShortcutHintItem>{{L"拖拽", L"选择录制区域"}, {L"Enter", L"开始录制"},
                                                {L"Ctrl+A", L"全屏"}, {L"Esc", L"取消"}}
                : std::vector<ShortcutHintItem>{{L"Drag", L"Select recording area"}, {L"Enter", L"Start"},
                                                {L"Ctrl+A", L"Full screen"}, {L"Esc", L"Cancel"}};
        case ShortcutHintContext::ScrollCapture:
            return zh
                ? std::vector<ShortcutHintItem>{{L"自动滚动", L"保持页面静止"}, {L"Esc", L"完成并保存"}}
                : std::vector<ShortcutHintItem>{{L"Auto scroll", L"Keep content still"}, {L"Esc", L"Finish and save"}};
        case ShortcutHintContext::Recording:
        case ShortcutHintContext::RecordingPaused: {
            std::vector<ShortcutHintItem> items;
            if (const auto pause = configuredShortcut("Record Pause", L"Ctrl+Shift+P"); !pause.empty()) {
                items.push_back({pause, zh
                    ? (context == ShortcutHintContext::RecordingPaused ? L"继续录制" : L"暂停录制")
                    : (context == ShortcutHintContext::RecordingPaused ? L"Resume" : L"Pause")});
            }
            if (const auto stop = configuredShortcut("Record", L"Ctrl+Shift+R"); !stop.empty()) {
                items.push_back({stop, zh ? L"停止并保存" : L"Stop and save"});
            }
            if (const auto mute = configuredShortcut("Mute Microphone", L""); !mute.empty()) {
                items.push_back({mute, zh ? L"麦克风静音" : L"Mute microphone"});
            }
            return items;
        }
    }
    return {};
}

bool ShortcutHintOverlay::createWindow() {
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ShortcutHintOverlay::wndProc), &m_module)) {
        return false;
    }
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = wndProc;
    windowClass.hInstance = m_module;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        m_module = nullptr;
        return false;
    }

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        WindowClass, L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, windowClass.hInstance, this);
    if (!m_hwnd) return false;
    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("无法从录屏中排除快捷键提示层: error={}", GetLastError());
    }
    return true;
}

bool ShortcutHintOverlay::createResources(float scale) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 m_d2dFactory.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.ReleaseAndGetAddressOf())))) return false;

    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(m_d2dFactory->CreateDCRenderTarget(
            &properties, m_renderTarget.ReleaseAndGetAddressOf()))) return false;

    const float keySize = ShortcutHintStyle::BaseKeyFont * scale;
    const float labelSize = ShortcutHintStyle::BaseLabelFont * scale;
    if (FAILED(m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, keySize, L"zh-CN", m_keyFormat.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, labelSize, L"zh-CN", m_labelFormat.ReleaseAndGetAddressOf()))) return false;
    m_keyFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_keyFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_labelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_labelFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (highContrastEnabled()) {
        m_renderTarget->CreateSolidColorBrush(systemColor(COLOR_WINDOW), &m_panelBrush);
        m_renderTarget->CreateSolidColorBrush(systemColor(COLOR_WINDOWTEXT), &m_borderBrush);
        m_renderTarget->CreateSolidColorBrush(systemColor(COLOR_HIGHLIGHT), &m_keyBrush);
        m_renderTarget->CreateSolidColorBrush(systemColor(COLOR_HIGHLIGHTTEXT), &m_keyTextBrush);
        m_renderTarget->CreateSolidColorBrush(systemColor(COLOR_WINDOWTEXT), &m_labelBrush);
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.06f, 0.08f, 0.78f), &m_panelBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), &m_borderBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.11f), &m_keyBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.90f), &m_keyTextBrush);
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.68f), &m_labelBrush);
    }
    return m_panelBrush && m_keyBrush && m_keyTextBrush && m_labelBrush;
}

float ShortcutHintOverlay::measureText(const std::wstring& text, IDWriteTextFormat* format) const {
    if (!m_dwriteFactory || !format || text.empty()) return 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    if (FAILED(m_dwriteFactory->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), format,
            2048.0f, 128.0f, &textLayout))) return 0.0f;
    DWRITE_TEXT_METRICS metrics{};
    return SUCCEEDED(textLayout->GetMetrics(&metrics)) ? std::ceil(metrics.widthIncludingTrailingWhitespace) : 0.0f;
}

bool ShortcutHintOverlay::layout(const std::vector<ShortcutHintItem>& items,
                                 int workWidth, float scale, int& width, int& height) {
    m_items.clear();
    if (items.empty()) return false;
    const float padX = ShortcutHintStyle::BaseHorizontalPadding * scale;
    const float padY = ShortcutHintStyle::BaseVerticalPadding * scale;
    const float keyPad = ShortcutHintStyle::BaseKeyHorizontalPadding * scale;
    const float keyHeight = ShortcutHintStyle::BaseKeyHeight * scale;
    const float itemGap = ShortcutHintStyle::BaseItemGap * scale;
    const float labelGap = ShortcutHintStyle::BaseLabelGap * scale;
    const float rowGap = ShortcutHintStyle::BaseRowGap * scale;
    const float maxRowWidth = std::min(
        workWidth - 2.0f * ShortcutHintStyle::BaseScreenMargin * scale,
        std::max(420.0f * scale, workWidth * 0.74f));

    float x = padX;
    float y = padY;
    float widest = 0.0f;
    int rows = 1;
    for (const auto& item : items) {
        const float keyWidth = std::max(28.0f * scale,
            measureText(item.key, m_keyFormat.Get()) + keyPad * 2.0f);
        const float labelWidth = measureText(item.label, m_labelFormat.Get());
        const float itemWidth = keyWidth + labelGap + labelWidth;
        if (x > padX && x + itemWidth + padX > maxRowWidth) {
            widest = std::max(widest, x - itemGap + padX);
            x = padX;
            y += keyHeight + rowGap;
            ++rows;
        }
        m_items.push_back({item, x, y, keyWidth, labelWidth});
        x += itemWidth + itemGap;
    }
    widest = std::max(widest, x - itemGap + padX);
    width = static_cast<int>(std::ceil(widest));
    height = static_cast<int>(std::ceil(padY * 2.0f + rows * keyHeight + (rows - 1) * rowGap));
    return width > 0 && height > 0;
}

void ShortcutHintOverlay::show(ShortcutHintContext context, POINT anchor) {
    if (!easy::core::ConfigManager::instance().get<bool>(
            "/capture/showShortcutHints", true)) {
        hide();
        return;
    }
    if (m_hasContext && m_context == context && isVisible()) return;
    auto items = itemsFor(context);
    if (items.empty()) {
        hide();
        return;
    }
    if (anchor.x == LONG_MIN || anchor.y == LONG_MIN) {
        if (!GetCursorPos(&anchor)) anchor = {0, 0};
    }
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const auto monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
    m_workArea = monitorInfo.rcWork;

    const UINT dpiX = easy::core::dpi::effectiveDpiForMonitor(monitor);
    const float scale = ShortcutHintStyle::scaleForDpi(dpiX);
    if (!m_hwnd && !createWindow()) return;
    if (!m_renderTarget || std::abs(scale - m_scale) > 0.01f) {
        discardResources();
        m_scale = scale;
        if (!createResources(scale)) {
            hide();
            return;
        }
    }
    if (!layout(items, m_workArea.right - m_workArea.left, scale, m_width, m_height)) {
        hide();
        return;
    }

    const int margin = static_cast<int>(ShortcutHintStyle::BaseScreenMargin * scale);
    const int x = m_workArea.left + margin;
    const int y = m_workArea.bottom - m_height - margin;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, m_width, m_height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    render();
    m_context = context;
    m_hasContext = true;
}

void ShortcutHintOverlay::render() {
    if (!m_hwnd || !m_renderTarget || m_width <= 0 || m_height <= 0) return;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = m_width;
    bitmapInfo.bmiHeader.biHeight = -m_height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!memory || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return;
    }
    const auto previous = SelectObject(memory, bitmap);
    RECT bounds{0, 0, m_width, m_height};
    m_renderTarget->BindDC(memory, &bounds);
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    const auto panel = D2D1::RoundedRect(
        D2D1::RectF(0.5f, 0.5f, static_cast<float>(m_width) - 0.5f,
                    static_cast<float>(m_height) - 0.5f),
        ShortcutHintStyle::BaseCornerRadius * m_scale,
        ShortcutHintStyle::BaseCornerRadius * m_scale);
    m_renderTarget->FillRoundedRectangle(panel, m_panelBrush.Get());
    m_renderTarget->DrawRoundedRectangle(panel, m_borderBrush.Get(), 1.0f);

    const float keyHeight = ShortcutHintStyle::BaseKeyHeight * m_scale;
    const float labelGap = ShortcutHintStyle::BaseLabelGap * m_scale;
    for (const auto& positioned : m_items) {
        const auto keyRect = D2D1::RoundedRect(
            D2D1::RectF(positioned.x, positioned.y,
                        positioned.x + positioned.keyWidth, positioned.y + keyHeight),
            ShortcutHintStyle::BaseKeyCornerRadius * m_scale,
            ShortcutHintStyle::BaseKeyCornerRadius * m_scale);
        m_renderTarget->FillRoundedRectangle(keyRect, m_keyBrush.Get());
        m_renderTarget->DrawTextW(
            positioned.item.key.c_str(), static_cast<UINT32>(positioned.item.key.size()),
            m_keyFormat.Get(), keyRect.rect, m_keyTextBrush.Get());
        const auto labelRect = D2D1::RectF(
            positioned.x + positioned.keyWidth + labelGap, positioned.y,
            positioned.x + positioned.keyWidth + labelGap + positioned.labelWidth + 2.0f * m_scale,
            positioned.y + keyHeight);
        m_renderTarget->DrawTextW(
            positioned.item.label.c_str(), static_cast<UINT32>(positioned.item.label.size()),
            m_labelFormat.Get(), labelRect, m_labelBrush.Get());
    }
    const HRESULT drawResult = m_renderTarget->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) discardResources();

    POINT source{0, 0};
    SIZE size{m_width, m_height};
    const auto margin = static_cast<LONG>(ShortcutHintStyle::BaseScreenMargin * m_scale);
    POINT destination{m_workArea.left + margin,
                      m_workArea.bottom - m_height - margin};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, screen, &destination, &size, memory,
                        &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void ShortcutHintOverlay::discardResources() {
    m_labelBrush.Reset();
    m_keyTextBrush.Reset();
    m_keyBrush.Reset();
    m_borderBrush.Reset();
    m_panelBrush.Reset();
    m_labelFormat.Reset();
    m_keyFormat.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void ShortcutHintOverlay::hide() {
    m_hasContext = false;
    m_items.clear();
    discardResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_module) {
        UnregisterClassW(WindowClass, m_module);
        m_module = nullptr;
    }
}

LRESULT CALLBACK ShortcutHintOverlay::wndProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* overlay = reinterpret_cast<ShortcutHintOverlay*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        overlay = static_cast<ShortcutHintOverlay*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    }
    if (overlay && (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED ||
                    message == WM_SYSCOLORCHANGE)) {
        overlay->discardResources();
        if (overlay->createResources(overlay->m_scale)) overlay->render();
        return 0;
    }
    if (overlay && overlay->m_hasContext &&
        (message == WM_DPICHANGED || message == WM_DISPLAYCHANGE)) {
        POINT anchor{
            (overlay->m_workArea.left + overlay->m_workArea.right) / 2,
            (overlay->m_workArea.top + overlay->m_workArea.bottom) / 2};
        if (message == WM_DPICHANGED && lParam) {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            anchor = {(suggested->left + suggested->right) / 2,
                      (suggested->top + suggested->bottom) / 2};
        }
        const auto context = overlay->m_context;
        overlay->m_hasContext = false;
        overlay->show(context, anchor);
        return 0;
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace easy::capture
