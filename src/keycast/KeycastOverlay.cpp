#include "KeycastOverlay.h"
#include "KeycastStyle.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"
#include "gesture/GestureInputPolicy.h"
#include <algorithm>
#include <vector>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

namespace easy::keycast {

namespace {
constexpr UINT_PTR ANIMATION_TIMER_ID = 1;
constexpr UINT START_ANIMATION_MESSAGE = WM_APP + 1;
}

KeycastOverlay& KeycastOverlay::instance() {
    static KeycastOverlay inst;
    return inst;
}

bool KeycastOverlay::init() {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = wndProc;
    wcex.hInstance = GetModuleHandleW(nullptr);
    wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"EasyTools_KeycastOverlay";

    RegisterClassExW(&wcex);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcex.lpszClassName,
        L"EasyTools Keycast",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create KeycastOverlay window");
        return false;
    }

    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除按键回显: error={}", GetLastError());
    }

    auto& cfg = easy::core::ConfigManager::instance();
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_settings.enabled = cfg.get<bool>("/keycast/enabled", true);
        m_settings.autoBypassFullscreen = cfg.get<bool>("/keycast/autoBypassFullscreen", true);
        m_settings.showKeyboard = cfg.get<bool>("/keycast/showKeyboard", true);
        m_settings.onlyShortcuts = cfg.get<bool>("/keycast/onlyShortcuts", false);
        m_settings.displayDurationMs = cfg.get<int>("/keycast/displayDurationMs", 3000);
        m_settings.fontSize = cfg.get<int>("/keycast/fontSize", 20);
        m_settings.textColor = cfg.get<std::string>("/keycast/textColor", "#ffffff");
        m_settings.backgroundColor = cfg.get<std::string>("/keycast/backgroundColor", "#202020");
    }

    // Graphics resources stay lazy until the first keystroke so a disabled or
    // unused plugin does not allocate a large high-DPI backing bitmap.
    ShowWindow(m_hwnd, SW_HIDE);
    return true;
}

void KeycastOverlay::cleanup() {
    if (m_hwnd) {
        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    discardResources();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

KeycastSettings KeycastOverlay::getSettings() const {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    return m_settings;
}

void KeycastOverlay::updateSettings(const KeycastSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_settings = settings;
    }

    auto& cfg = easy::core::ConfigManager::instance();
    cfg.set("/keycast/enabled", settings.enabled);
    cfg.set("/keycast/autoBypassFullscreen", settings.autoBypassFullscreen);
    cfg.set("/keycast/showKeyboard", settings.showKeyboard);
    cfg.set("/keycast/onlyShortcuts", settings.onlyShortcuts);
    cfg.set("/general/keycastOnlyShortcuts", settings.onlyShortcuts);
    cfg.set("/keycast/displayDurationMs", settings.displayDurationMs);
    cfg.set("/keycast/fontSize", settings.fontSize);
    cfg.set("/keycast/textColor", settings.textColor);
    cfg.set("/keycast/backgroundColor", settings.backgroundColor);

    discardResources();
}

void KeycastOverlay::resetDefaults() {
    KeycastSettings def;
    updateSettings(def);
}

void KeycastOverlay::setAutoBypassFullscreen(bool enable) {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.autoBypassFullscreen = enable;
    easy::core::ConfigManager::instance().set("/keycast/autoBypassFullscreen", enable);
}

bool KeycastOverlay::autoBypassFullscreen() const {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    return m_settings.autoBypassFullscreen;
}

D2D1_COLOR_F KeycastOverlay::parseColor(const std::string& hex, float alpha) const {
    if (hex == "auto" || hex.empty()) {
        std::string accent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "blue");
        if (accent == "cyan") return D2D1::ColorF(6.0f / 255.0f, 182.0f / 255.0f, 212.0f / 255.0f, alpha);
        if (accent == "amber") return D2D1::ColorF(245.0f / 255.0f, 158.0f / 255.0f, 11.0f / 255.0f, alpha);
        if (accent == "mint") return D2D1::ColorF(16.0f / 255.0f, 185.0f / 255.0f, 129.0f / 255.0f, alpha);
        if (accent == "coral") return D2D1::ColorF(244.0f / 255.0f, 63.0f / 255.0f, 94.0f / 255.0f, alpha);
        if (accent == "violet") return D2D1::ColorF(139.0f / 255.0f, 92.0f / 255.0f, 246.0f / 255.0f, alpha);
        return D2D1::ColorF(59.0f / 255.0f, 130.0f / 255.0f, 246.0f / 255.0f, alpha); // 经典蓝
    }
    std::string cleanHex = hex;
    if (!cleanHex.empty() && cleanHex[0] == '#') cleanHex = cleanHex.substr(1);
    if (cleanHex.length() == 6) {
        unsigned int rgb = 0;
        try {
            rgb = std::stoul(cleanHex, nullptr, 16);
            float r = ((rgb >> 16) & 0xFF) / 255.0f;
            float g = ((rgb >> 8) & 0xFF) / 255.0f;
            float b = (rgb & 0xFF) / 255.0f;
            return D2D1::ColorF(r, g, b, alpha);
        } catch (...) {}
    }
    return D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha);
}

void KeycastOverlay::discardResources() {
    m_brushText.Reset();
    m_brushBg.Reset();
    m_brushBorder.Reset();
    m_brushBadgeBg.Reset();
    m_renderTarget.Reset();
    m_textFormat.Reset();
    if (m_memoryDC && m_oldBitmap) SelectObject(m_memoryDC, m_oldBitmap);
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) DeleteObject(m_memoryBitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
    m_memoryBitmap = nullptr;
    m_memoryDC = nullptr;
}

bool KeycastOverlay::createResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_textFormat &&
        m_brushText && m_brushBg && m_brushBorder && m_brushBadgeBg) {
        return true;
    }
    discardResources();
    if (!m_d2dFactory && FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
        return false;
    }

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D2D1_FEATURE_LEVEL_DEFAULT
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_renderTarget))) return false;
    m_renderTarget->SetDpi(96.0f, 96.0f);
    m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // 创建常驻 32 位 DIB 内存 DC
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    m_memoryDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    m_memoryBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memoryDC || !m_memoryBitmap || !pBits) {
        discardResources();
        return false;
    }
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memoryDC, m_memoryBitmap));

    RECT bindRect = {0, 0, m_width, m_height};
    m_renderTarget->BindDC(m_memoryDC, &bindRect);

    if (!m_dwriteFactory && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &m_dwriteFactory))) {
        discardResources();
        return false;
    }

    KeycastSettings settings = getSettings();
    float targetFontSize = (std::max)(12.0f, static_cast<float>(settings.fontSize) * 1.5f) * m_dpiScale;

    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        targetFontSize,
        L"zh-cn",
        &m_textFormat
    ))) {
        discardResources();
        return false;
    }

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // 动态文字与背景画刷
    auto textColor = parseColor(settings.textColor, 1.0f);
    auto bgColor = parseColor(settings.backgroundColor, 0.94f);
    auto borderColor = parseColor(settings.textColor, 0.85f);

    m_renderTarget->CreateSolidColorBrush(textColor, &m_brushText);
    m_renderTarget->CreateSolidColorBrush(bgColor, &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(borderColor, &m_brushBorder);
    m_renderTarget->CreateSolidColorBrush(textColor, &m_brushBadgeBg);

    if (!m_brushText || !m_brushBg || !m_brushBorder || !m_brushBadgeBg) {
        discardResources();
        return false;
    }
    return true;
}

bool KeycastOverlay::updatePlacement() {
    if (!m_hwnd) return false;
    if (m_updatingPlacement) return true;
    m_updatingPlacement = true;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const float newScale = easy::core::dpi::scaleForMonitor(monitor);
    const int newWidth = easy::core::dpi::scaleMetric(KeycastStyle::BaseWidth, newScale);
    const int newHeight = easy::core::dpi::scaleMetric(KeycastStyle::BaseHeight, newScale);
    if (std::abs(newScale - m_dpiScale) >= 0.01f ||
        newWidth != m_width || newHeight != m_height) {
        m_dpiScale = newScale;
        m_width = newWidth;
        m_height = newHeight;
        discardResources();
    }

    const RECT work = easy::core::dpi::workArea(monitor);
    const int x = work.left + ((work.right - work.left) - m_width) / 2;
    const int y = std::max(work.top, work.bottom - m_height -
        easy::core::dpi::scaleMetric(KeycastStyle::BaseBottomMargin, m_dpiScale));
    const bool positioned = SetWindowPos(
        m_hwnd, HWND_TOPMOST, x, y, m_width, m_height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER) != FALSE;
    m_updatingPlacement = false;
    return positioned;
}

void KeycastOverlay::pushKey(const std::string& keyStr) {
    if (keyStr.empty()) return;

    KeycastSettings settings = getSettings();
    if (!settings.enabled || !settings.showKeyboard) return;

    if (settings.autoBypassFullscreen) {
        HWND fg = GetForegroundWindow();
        if (fg && easy::core::WinUtils::isWindowFullscreen(fg)) {
            wchar_t classWide[256] = {0};
            GetClassNameW(fg, classWide, static_cast<int>(std::size(classWide)));
            if (easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide))) {
                return;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t now = GetTickCount64();
        if (m_rawLastKey == keyStr && (now - m_lastPushTime) < (static_cast<uint64_t>(settings.displayDurationMs) - 200)) {
            m_repeatCount++;
            m_currentText = keyStr + "  ×" + std::to_string(m_repeatCount);
        } else {
            m_rawLastKey = keyStr;
            m_repeatCount = 1;
            m_currentText = keyStr;
        }

        m_lastPushTime = now;
        m_opacity = 0.15f;
        m_scale = 0.92f;
        m_animating = true;
    }
    if (m_hwnd) PostMessageW(m_hwnd, START_ANIMATION_MESSAGE, 0, 0);
}

void KeycastOverlay::render() {
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // 完全透明背景

    std::string textToDraw;
    float currentOpacity = 0.0f;
    float currentScale = 1.0f;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        textToDraw = m_currentText;
        currentOpacity = m_opacity;
        currentScale = m_scale;
    }

    if (!textToDraw.empty() && currentOpacity > 0.01f) {
        std::wstring wtext = easy::core::WinUtils::utf8ToWstring(textToDraw);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(
            wtext.c_str(), static_cast<UINT32>(wtext.length()),
            m_textFormat.Get(),
            10000.0f, 1000.0f,
            &layout
        );

        if (layout) {
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            DWRITE_TEXT_METRICS metrics;
            if (FAILED(layout->GetMetrics(&metrics))) return;

            float paddingX = 38.0f * m_dpiScale;
            float paddingY = 16.0f * m_dpiScale;
            float boxW = (std::max)(metrics.width + paddingX * 2.0f, 136.0f * m_dpiScale);
            float boxH = (std::max)(metrics.height + paddingY * 2.0f, 58.0f * m_dpiScale);

            float centerX = m_width / 2.0f;
            float centerY = m_height / 2.0f;

            // 弹性微缩放
            D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(
                currentScale, currentScale,
                D2D1::Point2F(centerX, centerY)
            );
            m_renderTarget->SetTransform(transform);

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f, centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                16.0f * m_dpiScale, 16.0f * m_dpiScale
            );

            // 1. 微透卡片背景
            m_brushBg->SetOpacity(0.94f * currentOpacity);
            m_renderTarget->FillRoundedRectangle(&rrect, m_brushBg.Get());

            // 2. 圆角边框
            if (m_brushBorder) {
                m_brushBorder->SetOpacity(0.85f * currentOpacity);
                m_renderTarget->DrawRoundedRectangle(
                    &rrect, m_brushBorder.Get(), 2.0f * m_dpiScale);
            }

            // 3. 高质量文字排版
            m_brushText->SetOpacity(1.0f * currentOpacity);
            m_renderTarget->DrawTextLayout(
                D2D1::Point2F(centerX - metrics.width / 2.0f, centerY - metrics.height / 2.0f),
                layout.Get(),
                m_brushText.Get()
            );

            m_renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        }
    }

    if (FAILED(m_renderTarget->EndDraw())) {
        discardResources();
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = {0, 0};
    SIZE size = {m_width, m_height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, hdcScreen, nullptr, &size, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

LRESULT CALLBACK KeycastOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == START_ANIMATION_MESSAGE) {
        auto& self = KeycastOverlay::instance();
        if (!self.updatePlacement() || !self.createResources()) {
            LOG_ERROR("按键回显显示失败: 无法创建高 DPI 渲染资源");
            return 0;
        }
        self.render();
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, ANIMATION_TIMER_ID, 16, nullptr);
        return 0;
    }

    if (msg == WM_TIMER && wParam == ANIMATION_TIMER_ID) {
        auto& self = KeycastOverlay::instance();
        bool finished = false;
        bool shouldRender = false;
        {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            if (!self.m_animating) {
                finished = true;
            } else {
                const uint64_t now = GetTickCount64();
                const uint64_t elapsed = now - self.m_lastPushTime;
                const uint64_t holdTime = (std::max)(500ULL, static_cast<uint64_t>(self.getSettings().displayDurationMs));

                if (elapsed < 140) {
                    const float t = elapsed / 140.0f;
                    self.m_opacity = 0.2f + 0.8f * t;
                    self.m_scale = 0.92f + 0.08f * std::sin(t * 1.57079f);
                    shouldRender = true;
                } else if (elapsed < holdTime) {
                    if (self.m_opacity != 1.0f || self.m_scale != 1.0f) {
                        self.m_opacity = 1.0f;
                        self.m_scale = 1.0f;
                        shouldRender = true;
                    }
                } else if (elapsed < holdTime + 350) {
                    const float t = static_cast<float>(elapsed - holdTime) / 350.0f;
                    self.m_opacity = 1.0f - t;
                    self.m_scale = 1.0f + 0.04f * t;
                    shouldRender = true;
                } else {
                    self.m_opacity = 0.0f;
                    self.m_animating = false;
                    self.m_currentText.clear();
                    finished = true;
                }
            }
        }

        if (finished) {
            KillTimer(hwnd, ANIMATION_TIMER_ID);
            ShowWindow(hwnd, SW_HIDE);
            self.discardResources();
            easy::core::WinUtils::trimWorkingSet();
        } else if (shouldRender) {
            self.render();
        }
        return 0;
    }

    if ((msg == WM_DPICHANGED || msg == WM_DISPLAYCHANGE) &&
        IsWindowVisible(hwnd)) {
        auto& self = KeycastOverlay::instance();
        if (self.updatePlacement() && self.createResources()) self.render();
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace easy::keycast
