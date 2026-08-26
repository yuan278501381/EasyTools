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
#include <sstream>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

namespace easy::keycast {

namespace {
constexpr UINT_PTR ANIMATION_TIMER_ID = 1;
constexpr UINT START_ANIMATION_MESSAGE = WM_APP + 1;

std::vector<std::string> splitTokens(const std::string& str) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (tokenStream >> token) {
        if (token != "+") {
            tokens.push_back(token);
        }
    }
    return tokens;
}

inline bool isModifierKey(const std::string& token) {
    return token == "Ctrl" || token == "Alt" || token == "Shift" || token == "Win";
}
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

    m_helperOwnerHwnd = easy::core::WinUtils::createOverlayHelperOwner(wcex.hInstance, L"EasyTools_KeycastHelperOwner");

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcex.lpszClassName,
        L"EasyTools Keycast",
        WS_POPUP,
        0, 0, 1, 1,
        m_helperOwnerHwnd, nullptr, wcex.hInstance, nullptr
    );

    if (!m_hwnd) {
        LOG_ERROR("Failed to create KeycastOverlay window");
        if (m_helperOwnerHwnd) {
            DestroyWindow(m_helperOwnerHwnd);
            m_helperOwnerHwnd = nullptr;
        }
        return false;
    }

    easy::core::WinUtils::applyTaskbarSafeOverlayStyle(m_hwnd, false);
    SetWindowDisplayAffinity(m_hwnd, WDA_NONE);

    auto& cfg = easy::core::ConfigManager::instance();
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_settings.enabled = cfg.get<bool>("/keycast/enabled", true);
        m_settings.autoBypassFullscreen = cfg.get<bool>("/keycast/autoBypassFullscreen", true);
        m_settings.showKeyboard = cfg.get<bool>("/keycast/showKeyboard", true);
        m_settings.filterMode = cfg.get<std::string>("/keycast/filterMode", "smart_shortcuts");
        m_settings.position = cfg.get<std::string>("/keycast/position", "top_left");
        m_settings.mergeRecentKeys = cfg.get<bool>("/keycast/mergeRecentKeys", true);
        m_settings.mergeTimeoutMs = cfg.get<int>("/keycast/mergeTimeoutMs", 1200);
        m_settings.displayDurationMs = cfg.get<int>("/keycast/displayDurationMs", 2500);
        m_settings.fontSize = cfg.get<int>("/keycast/fontSize", 18);
        m_settings.textColor = cfg.get<std::string>("/keycast/textColor", "#ffffff");
        m_settings.backgroundColor = cfg.get<std::string>("/keycast/backgroundColor", "#1c1c22");
    }

    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return true;
}

void KeycastOverlay::cleanup() {
    if (m_hwnd) {
        KillTimer(m_hwnd, ANIMATION_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_helperOwnerHwnd) {
        DestroyWindow(m_helperOwnerHwnd);
        m_helperOwnerHwnd = nullptr;
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
    cfg.set("/keycast/filterMode", settings.filterMode);
    cfg.set("/keycast/position", settings.position);
    cfg.set("/keycast/mergeRecentKeys", settings.mergeRecentKeys);
    cfg.set("/keycast/mergeTimeoutMs", settings.mergeTimeoutMs);
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
    m_brushKeycapBg.Reset();
    m_brushKeycapBorder.Reset();
    m_brushPlusText.Reset();
    m_renderTarget.Reset();
    m_textFormat.Reset();
    m_keycapTextFormat.Reset();
    if (m_memoryDC && m_oldBitmap) SelectObject(m_memoryDC, m_oldBitmap);
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) DeleteObject(m_memoryBitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
    m_memoryBitmap = nullptr;
    m_memoryDC = nullptr;
}

bool KeycastOverlay::createResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_textFormat && m_keycapTextFormat &&
        m_brushText && m_brushBg && m_brushBorder && m_brushKeycapBg) {
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
    float targetFontSize = (std::max)(12.0f, static_cast<float>(settings.fontSize) * 1.08f) * m_dpiScale;

    // 主按键与普通字符字体
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        targetFontSize,
        L"zh-cn",
        &m_textFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            targetFontSize, L"zh-cn", &m_textFormat);
    }
    if (m_textFormat) {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 内部修饰键微凸晶体键帽字体
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        targetFontSize,
        L"zh-cn",
        &m_keycapTextFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            targetFontSize, L"zh-cn", &m_keycapTextFormat);
    }
    if (m_keycapTextFormat) {
        m_keycapTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_keycapTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 动态画刷体系 (双层微透体系: 深黑外胶囊 + 修饰键透亮微凸底座)
    auto textColor = parseColor(settings.textColor, 1.0f);
    auto bgColor = parseColor(settings.backgroundColor, 0.90f);
    auto borderColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
    auto keycapBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f);       // 修饰键独立背景色
    auto keycapBorder = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.24f);   // 修饰键微高光边框
    auto plusText = D2D1::ColorF(0.85f, 0.85f, 0.88f, 0.75f);

    m_renderTarget->CreateSolidColorBrush(textColor, &m_brushText);
    m_renderTarget->CreateSolidColorBrush(bgColor, &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(borderColor, &m_brushBorder);
    m_renderTarget->CreateSolidColorBrush(keycapBg, &m_brushKeycapBg);
    m_renderTarget->CreateSolidColorBrush(keycapBorder, &m_brushKeycapBorder);
    m_renderTarget->CreateSolidColorBrush(plusText, &m_brushPlusText);

    if (!m_brushText || !m_brushBg || !m_brushBorder || !m_brushKeycapBg || !m_brushKeycapBorder || !m_brushPlusText) {
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
    const int newWidth = easy::core::dpi::scaleMetric(860, newScale);
    const int newHeight = easy::core::dpi::scaleMetric(180, newScale);
    if (std::abs(newScale - m_dpiScale) >= 0.01f ||
        newWidth != m_width || newHeight != m_height) {
        m_dpiScale = newScale;
        m_width = newWidth;
        m_height = newHeight;
        discardResources();
    }

    const RECT work = easy::core::dpi::workArea(monitor);
    const int marginX = easy::core::dpi::scaleMetric(32, m_dpiScale);
    const int marginY = easy::core::dpi::scaleMetric(42, m_dpiScale);

    KeycastSettings settings = getSettings();
    int x = work.left + marginX;
    int y = work.bottom - m_height - marginY;

    if (settings.position == "bottom_center") {
        x = work.left + ((work.right - work.left) - m_width) / 2;
        y = work.bottom - m_height - marginY;
    } else if (settings.position == "bottom_right") {
        x = work.right - m_width - marginX;
        y = work.bottom - m_height - marginY;
    } else if (settings.position == "top_left") {
        x = work.left + marginX;
        y = work.top + marginY;
    } else if (settings.position == "top_right") {
        x = work.right - m_width - marginX;
        y = work.top + marginY;
    }

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
            std::wstring cls = classWide;
            // 排除 EasyTools 自身所有组件、Windows 自带截图(Win+Shift+S / SnippingTool)以及主流截图与录屏工具全屏遮罩
            const bool isAllowedOverlay =
                (cls.rfind(L"EasyTools_", 0) == 0) ||
                (cls.find(L"ScreenClip") != std::wstring::npos) ||
                (cls.find(L"Snipping") != std::wstring::npos) ||
                (cls.find(L"ScreenSketch") != std::wstring::npos) ||
                (cls.find(L"Qt") != std::wstring::npos) ||
                (cls == L"Windows.UI.Core.CoreWindow") ||
                (cls == L"Snipaste") ||
                (cls == L"WeChatMainWndForPC") ||
                (cls == L"TXGuiFoundation");

            if (!isAllowedOverlay) {
                if (easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide))) {
                    return;
                }
            }
        }
    }

    LOG_DEBUG("KeycastOverlay pushKey: {}", keyStr);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t now = GetTickCount64();

        KeycastItem item;
        item.rawKey = keyStr;
        item.tokens = splitTokens(keyStr);
        item.pushTime = now;
        item.offsetX = 22.0f; // 从右向左阻尼推入初始距离
        item.opacity = 0.20f;

        const uint64_t mergeLimit = static_cast<uint64_t>((std::max)(300, settings.mergeTimeoutMs));
        const bool isRecent = (now - m_lastGlobalPushTime) < mergeLimit;

        if (settings.mergeRecentKeys && isRecent && !m_rows.empty()) {
            // 同一操作时间段：追加到当前排末尾
            auto& currentRow = m_rows.back();
            if (!currentRow.items.empty() && currentRow.items.back().rawKey == keyStr) {
                currentRow.items.back().repeatCount++;
                currentRow.items.back().pushTime = now;
                currentRow.items.back().offsetX = 10.0f;
            } else {
                currentRow.items.push_back(item);
                currentRow.lastActiveTime = now;
            }
        } else {
            // 超时停顿：换到新的一排
            if (!m_rows.empty()) {
                for (auto& row : m_rows) {
                    row.offsetY = -8.0f; // 旧排轻微向上推移
                }
            }
            KeycastRow newRow;
            newRow.items.push_back(item);
            newRow.lastActiveTime = now;
            newRow.offsetY = 0.0f;
            newRow.opacity = 1.0f;
            m_rows.push_back(newRow);

            // 限制最多保留 2 排活跃行
            while (m_rows.size() > 2) {
                m_rows.erase(m_rows.begin());
            }
        }

        m_lastRawKey = keyStr;
        m_lastGlobalPushTime = now;
    }

    if (m_hwnd) PostMessageW(m_hwnd, START_ANIMATION_MESSAGE, 0, 0);
}

void KeycastOverlay::drawWindowsLogo(const D2D1_RECT_F& rect, float alpha) {
    if (!m_renderTarget || !m_brushText) return;

    m_brushText->SetOpacity(0.96f * alpha);
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    float gap = 1.8f * m_dpiScale;
    float halfW = (w - gap) / 2.0f;
    float halfH = (h - gap) / 2.0f;
    float cornerR = 0.8f * m_dpiScale;

    // 4 格高精度 Windows 11 微圆角晶格
    D2D1_ROUNDED_RECT rTL = D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.left + halfW, rect.top + halfH), cornerR, cornerR);
    D2D1_ROUNDED_RECT rTR = D2D1::RoundedRect(D2D1::RectF(rect.left + halfW + gap, rect.top, rect.right, rect.top + halfH), cornerR, cornerR);
    D2D1_ROUNDED_RECT rBL = D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top + halfH + gap, rect.left + halfW, rect.bottom), cornerR, cornerR);
    D2D1_ROUNDED_RECT rBR = D2D1::RoundedRect(D2D1::RectF(rect.left + halfW + gap, rect.top + halfH + gap, rect.right, rect.bottom), cornerR, cornerR);

    m_renderTarget->FillRoundedRectangle(&rTL, m_brushText.Get());
    m_renderTarget->FillRoundedRectangle(&rTR, m_brushText.Get());
    m_renderTarget->FillRoundedRectangle(&rBL, m_brushText.Get());
    m_renderTarget->FillRoundedRectangle(&rBR, m_brushText.Get());
}

void KeycastOverlay::drawKeycapCapsule(const KeycastItem& item, float startX, float startY, float alpha, float dpiScale) {
    if (!m_renderTarget || !m_brushBg || !m_brushText) return;

    float curX = startX + 10.0f * dpiScale;
    float capHeight = 28.0f * dpiScale;
    float capCenterY = startY + 20.0f * dpiScale;

    for (size_t i = 0; i < item.tokens.size(); ++i) {
        const std::string& token = item.tokens[i];

        if (token == "Win") {
            // 修饰键 Win: 拥有专属物理晶体键帽与官方徽标
            float winW = 32.0f * dpiScale;
            float winH = capHeight;
            float topY = capCenterY - winH / 2.0f;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(curX, topY, curX + winW, topY + winH),
                6.0f * dpiScale, 6.0f * dpiScale
            );
            m_brushKeycapBg->SetOpacity(0.24f * alpha);
            m_renderTarget->FillRoundedRectangle(&rrect, m_brushKeycapBg.Get());
            m_brushKeycapBorder->SetOpacity(0.35f * alpha);
            m_renderTarget->DrawRoundedRectangle(&rrect, m_brushKeycapBorder.Get(), 1.0f * dpiScale);

            float logoSize = 13.0f * dpiScale;
            D2D1_RECT_F logoRect = D2D1::RectF(
                curX + (winW - logoSize) / 2.0f,
                capCenterY - logoSize / 2.0f,
                curX + (winW + logoSize) / 2.0f,
                capCenterY + logoSize / 2.0f
            );
            drawWindowsLogo(logoRect, alpha);
            curX += winW;
        } else {
            // 所有其它按键（包括 Ctrl, Alt, Shift, 字母 D, E, Space 等）：统一精致物理键帽！
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wtoken.c_str(), static_cast<UINT32>(wtoken.length()),
                m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &layout);

            DWRITE_TEXT_METRICS m{};
            if (layout) layout->GetMetrics(&m);

            // 键帽最小宽度保证正方形/优雅圆角矩形
            float minCapW = (wtoken.length() <= 1) ? 28.0f * dpiScale : 34.0f * dpiScale;
            float btnW = (std::max)(minCapW, m.width + 16.0f * dpiScale);
            float btnH = capHeight;
            float topY = capCenterY - btnH / 2.0f;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(curX, topY, curX + btnW, topY + btnH),
                6.0f * dpiScale, 6.0f * dpiScale
            );

            m_brushKeycapBg->SetOpacity(0.24f * alpha);
            m_renderTarget->FillRoundedRectangle(&rrect, m_brushKeycapBg.Get());
            m_brushKeycapBorder->SetOpacity(0.35f * alpha);
            m_renderTarget->DrawRoundedRectangle(&rrect, m_brushKeycapBorder.Get(), 1.0f * dpiScale);

            m_brushText->SetOpacity(alpha);
            if (layout) {
                float textDrawX = curX + (btnW - m.width) / 2.0f - m.left;
                float textDrawY = capCenterY - m.height / 2.0f - m.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(textDrawX, textDrawY),
                    layout.Get(), m_brushText.Get());
            }
            curX += btnW;
        }

        // 绘制键间连接符号 '+'
        if (i + 1 < item.tokens.size()) {
            curX += 5.0f * dpiScale;
            std::wstring plusStr = L"+";
            Microsoft::WRL::ComPtr<IDWriteTextLayout> plusLayout;
            m_dwriteFactory->CreateTextLayout(
                plusStr.c_str(), 1, m_textFormat.Get(), 1000.0f, 1000.0f, &plusLayout);
            DWRITE_TEXT_METRICS pm{};
            if (plusLayout) plusLayout->GetMetrics(&pm);

            m_brushPlusText->SetOpacity(0.80f * alpha);
            if (plusLayout) {
                float plusX = curX - pm.left;
                float plusY = capCenterY - pm.height / 2.0f - pm.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(plusX, plusY),
                    plusLayout.Get(), m_brushPlusText.Get());
            }
            curX += pm.width + 5.0f * dpiScale;
        }
    }

    // 重复按键指示器 ×2 / ×3
    if (item.repeatCount > 1) {
        curX += 6.0f * dpiScale;
        std::wstring repStr = L"×" + std::to_wstring(item.repeatCount);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> repLayout;
        m_dwriteFactory->CreateTextLayout(
            repStr.c_str(), static_cast<UINT32>(repStr.length()),
            m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &repLayout);
        DWRITE_TEXT_METRICS rm{};
        if (repLayout) repLayout->GetMetrics(&rm);

        float badgeW = rm.width + 12.0f * dpiScale;
        float badgeH = 22.0f * dpiScale;
        float badgeTopY = capCenterY - badgeH / 2.0f;
        D2D1_ROUNDED_RECT badgeRect = D2D1::RoundedRect(
            D2D1::RectF(curX, badgeTopY, curX + badgeW, badgeTopY + badgeH),
            5.0f * dpiScale, 5.0f * dpiScale
        );
        m_brushKeycapBg->SetOpacity(0.32f * alpha);
        m_renderTarget->FillRoundedRectangle(&badgeRect, m_brushKeycapBg.Get());
        m_brushKeycapBorder->SetOpacity(0.40f * alpha);
        m_renderTarget->DrawRoundedRectangle(&badgeRect, m_brushKeycapBorder.Get(), 1.0f * dpiScale);

        m_brushPlusText->SetOpacity(0.95f * alpha);
        if (repLayout) {
            float rx = curX + (badgeW - rm.width) / 2.0f - rm.left;
            float ry = capCenterY - rm.height / 2.0f - rm.top;
            m_renderTarget->DrawTextLayout(
                D2D1::Point2F(rx, ry),
                repLayout.Get(), m_brushPlusText.Get());
        }
    }
}

void KeycastOverlay::render() {
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0)); // 透明背景

    KeycastSettings settings = getSettings();
    float rowHeight = 48.0f * m_dpiScale;
    float rowGap = 8.0f * m_dpiScale;

    std::vector<KeycastRow> localRows;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        localRows = m_rows;
    }

    float currentY = 10.0f * m_dpiScale;
    for (size_t r = 0; r < localRows.size(); ++r) {
        const auto& row = localRows[r];
        if (row.opacity < 0.02f) continue;

        float currentX = 12.0f * m_dpiScale;
        float actualY = currentY + row.offsetY * m_dpiScale;

        for (size_t c = 0; c < row.items.size(); ++c) {
            const auto& item = row.items[c];
            float itemAlpha = row.opacity * item.opacity;
            if (itemAlpha < 0.02f) continue;

            float drawX = currentX + item.offsetX * m_dpiScale;

            // 预先精确测量该组合键的总宽度（左右内边距各 10px）
            float totalCapsuleW = 20.0f * m_dpiScale;
            for (size_t i = 0; i < item.tokens.size(); ++i) {
                const std::string& token = item.tokens[i];

                if (token == "Win") {
                    totalCapsuleW += 32.0f * m_dpiScale;
                } else {
                    std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                    m_dwriteFactory->CreateTextLayout(
                        wtoken.c_str(), static_cast<UINT32>(wtoken.length()),
                        m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &layout);
                    DWRITE_TEXT_METRICS m{};
                    if (layout) layout->GetMetrics(&m);
                    float minCapW = (wtoken.length() <= 1) ? 28.0f * m_dpiScale : 34.0f * m_dpiScale;
                    totalCapsuleW += (std::max)(minCapW, m.width + 16.0f * m_dpiScale);
                }

                if (i + 1 < item.tokens.size()) {
                    totalCapsuleW += 16.0f * m_dpiScale; // '+' 宽度与两侧间隙
                }
            }
            if (item.repeatCount > 1) {
                std::wstring repStr = L"×" + std::to_wstring(item.repeatCount);
                Microsoft::WRL::ComPtr<IDWriteTextLayout> repLayout;
                m_dwriteFactory->CreateTextLayout(
                    repStr.c_str(), static_cast<UINT32>(repStr.length()),
                    m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &repLayout);
                DWRITE_TEXT_METRICS rm{};
                if (repLayout) repLayout->GetMetrics(&rm);
                totalCapsuleW += rm.width + 18.0f * m_dpiScale;
            }

            // 1. 绘制深色不透明胶囊托盘背景 (0.97f 彻底阻隔背景文字透视穿帮)
            D2D1_ROUNDED_RECT capsuleRect = D2D1::RoundedRect(
                D2D1::RectF(drawX, actualY, drawX + totalCapsuleW, actualY + 40.0f * m_dpiScale),
                8.0f * m_dpiScale, 8.0f * m_dpiScale
            );

            m_brushBg->SetOpacity(0.97f * itemAlpha);
            m_renderTarget->FillRoundedRectangle(&capsuleRect, m_brushBg.Get());

            m_brushBorder->SetOpacity(0.20f * itemAlpha);
            m_renderTarget->DrawRoundedRectangle(&capsuleRect, m_brushBorder.Get(), 1.0f * m_dpiScale);

            // 2. 绘制键帽微凸槽与内容
            drawKeycapCapsule(item, drawX, actualY, itemAlpha, m_dpiScale);

            currentX += totalCapsuleW + 8.0f * m_dpiScale;
        }

        currentY += rowHeight + rowGap;
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

void KeycastOverlay::tickAnimation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t now = GetTickCount64();
    const uint64_t holdTime = static_cast<uint64_t>(m_settings.displayDurationMs);

    for (auto it = m_rows.begin(); it != m_rows.end();) {
        auto& row = *it;
        const uint64_t elapsed = now - row.lastActiveTime;

        // 阻尼缓动插值
        row.offsetY += (0.0f - row.offsetY) * 0.25f;
        for (auto& item : row.items) {
            item.offsetX += (0.0f - item.offsetX) * 0.28f;
            item.opacity += (1.0f - item.opacity) * 0.32f;
        }

        if (elapsed > holdTime) {
            // 超时开始平滑消融淡出
            float fadeProgress = static_cast<float>(elapsed - holdTime) / 400.0f;
            row.opacity = (std::max)(0.0f, 1.0f - fadeProgress);
        }

        if (row.opacity <= 0.01f) {
            it = m_rows.erase(it);
        } else {
            ++it;
        }
    }
}

LRESULT CALLBACK KeycastOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == START_ANIMATION_MESSAGE) {
        auto& self = KeycastOverlay::instance();
        if (!self.updatePlacement() || !self.createResources()) {
            LOG_ERROR("按键回显初始化失败: 无法创建渲染表面");
            return 0;
        }
        self.render();
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        if (!self.m_timerRunning) {
            SetTimer(hwnd, ANIMATION_TIMER_ID, 16, nullptr); // 60fps 物理帧循环
            self.m_timerRunning = true;
        }
        return 0;
    }

    if (msg == WM_TIMER && wParam == ANIMATION_TIMER_ID) {
        auto& self = KeycastOverlay::instance();
        self.tickAnimation();

        bool hasActiveRows = false;
        {
            std::lock_guard<std::mutex> lock(self.m_mutex);
            hasActiveRows = !self.m_rows.empty();
        }

        if (hasActiveRows) {
            self.render();
        } else {
            KillTimer(hwnd, ANIMATION_TIMER_ID);
            self.m_timerRunning = false;
            ShowWindow(hwnd, SW_HIDE);
            easy::core::WinUtils::trimWorkingSet();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::keycast
