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

struct KeycastDynamicMetrics {
    float fontSize;       // 主键基准字号 (baseFontSize 20 -> 15.0px @ 1x DPI)
    float keycapFontSize; // 修饰键字号 (fontSize * 0.88f -> 13.2px, 绝不溢出)
    float plusFontSize;   // 加号字号 (fontSize * 0.58f -> 8.7px)
    float charWidth;      // 单字符估算宽度
    float capHeight;      // 键帽底座高度 = fontSize * 1.50f (~22.5px)
    float capRadius;      // 键帽圆角 = 5.5f * dpiScale (~5.5px)
    float capsuleHeight;  // 外层胶囊高度 = capHeight + 10.0f * dpiScale (~32.5px)
    float capsuleRadius;  // 外层胶囊圆角 = 8.5f * dpiScale (~8.5px)
    float paddingX;       // 胶囊左右内边距 = fontSize * 0.50f (~7.5px)
    float winWidth;       // Windows 键宽度 = fontSize * 2.25f (~33.8px, 长宽比 1.5:1)
    float logoSize;       // Windows 徽标尺寸 = capHeight * 0.44f (~10px, 充裕呼吸感)
    float plusWidth;      // 加号总占用宽度 = fontSize * 0.55f
    float rowStep;        // 多行垂直步进 = capsuleHeight + 8.0f * dpiScale
    float borderWidth;    // 动态微边框 = 1.0f * dpiScale
};

inline KeycastDynamicMetrics computeKeycastMetrics(int baseFontSize, float dpiScale) {
    KeycastDynamicMetrics m;
    m.fontSize = (std::max)(12.0f, static_cast<float>(baseFontSize) * 0.75f) * dpiScale;
    m.keycapFontSize = m.fontSize * 0.88f;
    m.plusFontSize = m.fontSize * 0.58f;
    m.charWidth = m.fontSize * 0.58f;
    m.capHeight = m.fontSize * 1.50f;
    m.capRadius = 5.5f * dpiScale;
    m.capsuleHeight = m.capHeight + 10.0f * dpiScale;
    m.capsuleRadius = 8.5f * dpiScale;
    m.paddingX = m.fontSize * 0.50f;
    m.winWidth = m.fontSize * 2.25f;
    m.logoSize = m.capHeight * 0.44f;
    m.plusWidth = m.fontSize * 0.55f;
    m.rowStep = m.capsuleHeight + 8.0f * dpiScale;
    m.borderWidth = (std::max)(1.0f * dpiScale, 1.0f);
    return m;
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
        m_settings.includeFunctionKeys = cfg.get<bool>("/keycast/includeFunctionKeys", true);
        m_settings.position = cfg.get<std::string>("/keycast/position", "top_left");
        m_settings.mergeRecentKeys = cfg.get<bool>("/keycast/mergeRecentKeys", true);
        m_settings.mergeTimeoutMs = cfg.get<int>("/keycast/mergeTimeoutMs", 1200);
        m_settings.displayDurationMs = cfg.get<int>("/keycast/displayDurationMs", 2500);
        m_settings.fontSize = cfg.get<int>("/keycast/fontSize", 20);
        m_settings.textColor = cfg.get<std::string>("/keycast/textColor", "#ffffff");
        m_settings.backgroundColor = cfg.get<std::string>("/keycast/backgroundColor", "#1c1c22");
        m_settings.modifierKeycapColor = cfg.get<std::string>("/keycast/modifierKeycapColor", "auto");
        m_settings.modifierKeycapOpacity = cfg.get<int>("/keycast/modifierKeycapOpacity", 65);
        m_settings.modifierTextColor = cfg.get<std::string>("/keycast/modifierTextColor", "auto");
        m_settings.firstKeyAnim = cfg.get<std::string>("/keycast/firstKeyAnim", "slide");
        m_settings.subsequentKeyAnim = cfg.get<std::string>("/keycast/subsequentKeyAnim", "fade");
        m_settings.rowCascadeAnim = cfg.get<bool>("/keycast/rowCascadeAnim", true);
        m_settings.exitDriftAnim = cfg.get<bool>("/keycast/exitDriftAnim", true);
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
    cfg.set("/keycast/includeFunctionKeys", settings.includeFunctionKeys);
    cfg.set("/keycast/position", settings.position);
    cfg.set("/keycast/mergeRecentKeys", settings.mergeRecentKeys);
    cfg.set("/keycast/mergeTimeoutMs", settings.mergeTimeoutMs);
    cfg.set("/keycast/displayDurationMs", settings.displayDurationMs);
    cfg.set("/keycast/fontSize", settings.fontSize);
    cfg.set("/keycast/textColor", settings.textColor);
    cfg.set("/keycast/backgroundColor", settings.backgroundColor);
    cfg.set("/keycast/modifierKeycapColor", settings.modifierKeycapColor);
    cfg.set("/keycast/modifierKeycapOpacity", settings.modifierKeycapOpacity);
    cfg.set("/keycast/modifierTextColor", settings.modifierTextColor);
    cfg.set("/keycast/firstKeyAnim", settings.firstKeyAnim);
    cfg.set("/keycast/subsequentKeyAnim", settings.subsequentKeyAnim);
    cfg.set("/keycast/rowCascadeAnim", settings.rowCascadeAnim);
    cfg.set("/keycast/exitDriftAnim", settings.exitDriftAnim);

    discardResources();
    updatePlacement();
}

void KeycastOverlay::resetDefaults() {
    KeycastSettings def;
    updateSettings(def);
}

void KeycastOverlay::setAutoBypassFullscreen(bool enable) {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.autoBypassFullscreen = enable;
}

bool KeycastOverlay::autoBypassFullscreen() const {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    return m_settings.autoBypassFullscreen;
}

D2D1_COLOR_F KeycastOverlay::parseColor(const std::string& hex, float alpha) const {
    if (hex == "auto") {
        std::string accent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "blue");
        if (accent == "cyan") return D2D1::ColorF(0.25f, 0.75f, 0.85f, alpha);
        if (accent == "amber") return D2D1::ColorF(0.96f, 0.65f, 0.15f, alpha);
        if (accent == "mint") return D2D1::ColorF(0.25f, 0.78f, 0.58f, alpha);
        if (accent == "coral") return D2D1::ColorF(0.96f, 0.40f, 0.48f, alpha);
        if (accent == "violet") return D2D1::ColorF(0.60f, 0.48f, 0.88f, alpha);
        // 默认 auto 采用高雅浅紫罗兰微晶色 (#7B6BA3)，通透优雅
        return D2D1::ColorF(0.48f, 0.42f, 0.64f, alpha);
    }

    if (hex.length() == 7 && hex[0] == '#') {
        int r, g, b;
        if (sscanf_s(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, alpha);
        }
    }
    return D2D1::ColorF(0.15f, 0.15f, 0.18f, alpha); // fallback
}

void KeycastOverlay::discardResources() {
    m_brushText.Reset();
    m_brushModifierText.Reset();
    m_brushBg.Reset();
    m_brushBorder.Reset();
    m_brushKeycapBg.Reset();
    m_brushKeycapBorder.Reset();
    m_brushFuncKeyBg.Reset();
    m_brushFuncKeyBorder.Reset();
    m_brushBadgeBg.Reset();
    m_brushBadgeBorder.Reset();
    m_brushPlusText.Reset();
    m_renderTarget.Reset();
    m_textFormat.Reset();
    m_keycapTextFormat.Reset();
    m_repeatTextFormat.Reset();
    m_plusTextFormat.Reset();
    if (m_memoryDC && m_oldBitmap) SelectObject(m_memoryDC, m_oldBitmap);
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) DeleteObject(m_memoryBitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
    m_memoryBitmap = nullptr;
    m_memoryDC = nullptr;
}

void KeycastOverlay::onThemeChanged() {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_lastAccentColor.clear();
    discardResources();
    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        createResources();
        render();
    }
}

bool KeycastOverlay::createResources() {
    if (!m_hwnd) return false;
    if (m_renderTarget && m_brushText && m_brushBg && m_brushKeycapBg) return true;

    if (!m_d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
            return false;
        }
    }
    if (!m_dwriteFactory) {
        if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        ))) {
            return false;
        }
    }

    if (!m_memoryDC) {
        HDC hdcScreen = GetDC(nullptr);
        m_memoryDC = CreateCompatibleDC(hdcScreen);
        ReleaseDC(nullptr, hdcScreen);
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (m_memoryBitmap) {
        DeleteObject(m_memoryBitmap);
        m_memoryBitmap = nullptr;
    }
    void* pBits = nullptr;
    m_memoryBitmap = CreateDIBSection(m_memoryDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!m_memoryBitmap) return false;
    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memoryDC, m_memoryBitmap));

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    );

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcTarget;
    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &dcTarget))) return false;

    RECT rc = { 0, 0, m_width, m_height };
    if (FAILED(dcTarget->BindDC(m_memoryDC, &rc))) return false;
    m_renderTarget = dcTarget;

    KeycastSettings settings = getSettings();
    const KeycastDynamicMetrics dyn = computeKeycastMetrics(settings.fontSize, m_dpiScale);

    // 1. 主按键与普通字符字体: SemiBold (清秀典雅)
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        dyn.fontSize,
        L"zh-cn",
        &m_textFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            dyn.fontSize, L"zh-cn", &m_textFormat);
    }
    if (m_textFormat) {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 2. 内部修饰键键帽字体: SemiBold, 字号紧凑适中 (绝不溢出)
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        dyn.keycapFontSize,
        L"zh-cn",
        &m_keycapTextFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            dyn.keycapFontSize, L"zh-cn", &m_keycapTextFormat);
    }
    if (m_keycapTextFormat) {
        m_keycapTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_keycapTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 3. 连击角标专属轻量字体
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        dyn.fontSize * 0.75f,
        L"zh-cn",
        &m_repeatTextFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            dyn.fontSize * 0.75f, L"zh-cn", &m_repeatTextFormat);
    }
    if (m_repeatTextFormat) {
        m_repeatTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_repeatTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 4. 连接符 '+' 专属微型字体: Medium, 字号小巧谦逊
    if (FAILED(m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable Text",
        nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        dyn.plusFontSize,
        L"zh-cn",
        &m_plusTextFormat
    ))) {
        m_dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            dyn.plusFontSize, L"zh-cn", &m_plusTextFormat);
    }
    if (m_plusTextFormat) {
        m_plusTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_plusTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // 动态画刷体系 (智能双层微透与反差保护)
    auto bgColor = parseColor(settings.backgroundColor, 0.90f);
    float luminance = 0.299f * bgColor.r + 0.587f * bgColor.g + 0.114f * bgColor.b;
    bool isDarkBg = (luminance < 0.55f);

    D2D1_COLOR_F textColor;
    D2D1_COLOR_F modifierTextColor;
    D2D1_COLOR_F plusText;
    D2D1_COLOR_F keycapBg;
    D2D1_COLOR_F keycapBorder;
    D2D1_COLOR_F borderColor;
    D2D1_COLOR_F funcKeyBg;
    D2D1_COLOR_F funcKeyBorder;
    D2D1_COLOR_F badgeBg;
    D2D1_COLOR_F badgeBorder;

    D2D1_COLOR_F baseKeycapColor = parseColor(settings.modifierKeycapColor, 1.0f);

    if (isDarkBg) {
        textColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f);         // 主键: 纯白高亮悬浮
        // 修饰键内部文字/图标: 优雅深暗色 (高对比键帽反差)
        modifierTextColor = (settings.modifierTextColor == "auto") ? 
            D2D1::ColorF(0.08f, 0.08f, 0.12f, 0.95f) : 
            parseColor(settings.modifierTextColor, 0.98f);
        plusText = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.40f);          // 加号: 浅灰半透
        keycapBg = D2D1::ColorF(baseKeycapColor.r, baseKeycapColor.g, baseKeycapColor.b, 1.0f);
        keycapBorder = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f);       // 修饰键框: 细腻微亮边
        funcKeyBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f);
        funcKeyBorder = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f);
        badgeBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f);
        badgeBorder = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f);
        borderColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f);       // 外壳胶囊暗调微边框
    } else {
        textColor = D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.98f);
        modifierTextColor = (settings.modifierTextColor == "auto") ?
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f) :
            parseColor(settings.modifierTextColor, 0.98f);
        plusText = D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.45f);
        keycapBg = D2D1::ColorF(baseKeycapColor.r, baseKeycapColor.g, baseKeycapColor.b, 1.0f);
        keycapBorder = D2D1::ColorF(baseKeycapColor.r, baseKeycapColor.g, baseKeycapColor.b, 0.35f);
        funcKeyBg = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f);
        funcKeyBorder = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.14f);
        badgeBg = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f);
        badgeBorder = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.14f);
        borderColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f);
    }

    m_renderTarget->CreateSolidColorBrush(textColor, &m_brushText);
    m_renderTarget->CreateSolidColorBrush(modifierTextColor, &m_brushModifierText);
    m_renderTarget->CreateSolidColorBrush(bgColor, &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(borderColor, &m_brushBorder);
    m_renderTarget->CreateSolidColorBrush(keycapBg, &m_brushKeycapBg);
    m_renderTarget->CreateSolidColorBrush(keycapBorder, &m_brushKeycapBorder);
    m_renderTarget->CreateSolidColorBrush(funcKeyBg, &m_brushFuncKeyBg);
    m_renderTarget->CreateSolidColorBrush(funcKeyBorder, &m_brushFuncKeyBorder);
    m_renderTarget->CreateSolidColorBrush(badgeBg, &m_brushBadgeBg);
    m_renderTarget->CreateSolidColorBrush(badgeBorder, &m_brushBadgeBorder);
    m_renderTarget->CreateSolidColorBrush(plusText, &m_brushPlusText);

    if (!m_brushText || !m_brushModifierText || !m_brushBg || !m_brushBorder || !m_brushKeycapBg || !m_brushKeycapBorder || !m_brushFuncKeyBg || !m_brushFuncKeyBorder || !m_brushBadgeBg || !m_brushBadgeBorder || !m_brushPlusText) {
        discardResources();
        return false;
    }
    m_lastAccentColor = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "blue");
    return true;
}

float KeycastOverlay::calculateItemWidth(const KeycastItem& item, float dpiScale) const {
    auto isModifier = [](const std::string& tok) -> bool {
        return tok == "Win" || tok == "Ctrl" || tok == "Alt" || tok == "Shift" ||
               tok == "Cmd" || tok == "Meta" || tok == "Option" || tok == "Super" ||
               tok == "Control" || tok == "Command";
    };

    auto isSpecialKey = [&isModifier](const std::string& tok) -> bool {
        if (isModifier(tok)) return true;
        return tok == "Space" || tok == "Tab" || tok == "Enter" || tok == "Return" ||
               tok == "Backspace" || tok == "Delete" || tok == "Del" || tok == "Esc" ||
               tok == "Escape" || tok == "CapsLock" || tok == "Caps" || tok == "Insert" ||
               tok == "Home" || tok == "End" || tok == "PageUp" || tok == "PageDown" ||
               tok == "PgUp" || tok == "PgDn" || tok == "Up" || tok == "Down" ||
               tok == "Left" || tok == "Right" || tok == "PrtScn" || tok == "PrintScreen" ||
               tok == "ScrollLock" || tok == "Pause" || tok == "NumLock" ||
               (tok.size() >= 2 && tok[0] == 'F' && isdigit(static_cast<unsigned char>(tok[1])));
    };

    const KeycastSettings settings = getSettings();
    const KeycastDynamicMetrics dyn = computeKeycastMetrics(settings.fontSize, dpiScale);

    float totalW = dyn.paddingX;
    for (size_t i = 0; i < item.tokens.size(); ++i) {
        const std::string& token = item.tokens[i];
        if (token == "Win") {
            totalW += dyn.winWidth;
        } else if (isModifier(token)) {
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            float textW = static_cast<float>(wtoken.length()) * (dyn.keycapFontSize * 0.58f);
            float btnW = (std::max)(dyn.winWidth, textW + dyn.fontSize * 0.85f);
            totalW += btnW;
        } else if (isSpecialKey(token)) {
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            float textW = static_cast<float>(wtoken.length()) * dyn.charWidth;
            float minCapW = (token == "Space") ? (dyn.fontSize * 2.2f) : dyn.winWidth;
            float btnW = (std::max)(minCapW, textW + dyn.fontSize * 0.70f);
            totalW += btnW;
        } else {
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            float textW = static_cast<float>(wtoken.length()) * dyn.charWidth;
            float btnW = (std::max)(dyn.fontSize * 0.85f, textW + dyn.fontSize * 0.40f);
            totalW += btnW;
        }
        if (i + 1 < item.tokens.size()) {
            totalW += dyn.plusWidth;
        }
    }
    if (item.repeatCount > 1) {
        std::wstring repStr = L"×" + std::to_wstring(item.repeatCount);
        float charW = static_cast<float>(repStr.length()) * (dyn.fontSize * 0.75f * 0.55f);
        float fullBadgeW = dyn.fontSize * 0.20f + charW;
        float progress = (item.badgeWidthProgress > 0.0f) ? item.badgeWidthProgress : 1.0f;
        totalW += fullBadgeW * (std::max)(0.0f, (std::min)(1.0f, progress));
    }
    totalW += dyn.paddingX;
    return totalW + 4.0f * dpiScale;
}

bool KeycastOverlay::updatePlacement() {
    if (!m_hwnd) return false;
    if (m_updatingPlacement) return true;
    m_updatingPlacement = true;
    const HMONITOR monitor = easy::core::dpi::activeMonitor();
    const float newScale = easy::core::dpi::scaleForMonitor(monitor);
    const RECT work = easy::core::dpi::workArea(monitor);
    const float screenW = static_cast<float>(work.right - work.left);

    KeycastSettings settings = getSettings();
    const KeycastDynamicMetrics dyn = computeKeycastMetrics(settings.fontSize, newScale);

    // 宽度以屏幕中线为基准自适应，高度根据动态行距自适应容纳 3 排完整呼吸感
    const int newWidth = static_cast<int>((std::min)(screenW * 0.70f, (std::max)(1200.0f * newScale, screenW * 0.50f + 64.0f * newScale)));
    const int newHeight = static_cast<int>(dyn.rowStep * 3.6f + 32.0f * newScale);

    if (std::abs(newScale - m_dpiScale) >= 0.01f ||
        newWidth != m_width || newHeight != m_height) {
        m_dpiScale = newScale;
        m_width = newWidth;
        m_height = newHeight;
        discardResources();
    }

    const int marginX = easy::core::dpi::scaleMetric(32, m_dpiScale);
    const int marginY = easy::core::dpi::scaleMetric(42, m_dpiScale);

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
        const bool isRecent = (now - m_lastGlobalPushTime <= static_cast<uint64_t>(settings.mergeTimeoutMs));

        KeycastItem item;
        item.rawKey = keyStr;
        item.tokens = splitTokens(keyStr);
        item.pushTime = now;
        item.opacity = 0.0f;

        // 计算屏幕中线最大限制宽度 (不超过屏幕宽度的 46%)
        const float maxRowWidth = (std::max)(520.0f * m_dpiScale, static_cast<float>(m_width) - 40.0f * m_dpiScale);

        float currentRowWidth = 0.0f;
        if (!m_rows.empty()) {
            for (const auto& it : m_rows.back().items) {
                currentRowWidth += calculateItemWidth(it, m_dpiScale) + 8.0f * m_dpiScale;
            }
        }
        float newItemWidth = calculateItemWidth(item, m_dpiScale);
        bool wouldExceedMidline = (currentRowWidth + newItemWidth > maxRowWidth);

        if (settings.mergeRecentKeys && isRecent && !m_rows.empty() && !wouldExceedMidline) {
            // 同一操作时间段且未超屏幕中线：追加到当前排末尾
            auto& currentRow = m_rows.back();
            if (!currentRow.items.empty() && currentRow.items.back().rawKey == keyStr) {
                auto& targetItem = currentRow.items.back();
                targetItem.repeatCount++;
                targetItem.pushTime = now;
                
                // 【世界级连续性】：绝不强行截断当前正在进行的滑行或渐现位移，仅注入二阶物理微脉冲速度！
                targetItem.scaleVel += 0.08f;

                // 连击角标流体气泡激发
                if (targetItem.repeatCount == 2) {
                    targetItem.badgeScale = 0.5f;
                    targetItem.badgeScaleVel = 0.15f;
                    targetItem.badgeOpacity = 0.1f;
                } else {
                    targetItem.badgeScaleVel += 0.18f; // 数字增加微弹跳
                }
            } else {
                // 同一行后续按键入场动效组合
                item.isFirstInRow = false;
                if (settings.subsequentKeyAnim == "pop") {
                    item.offsetX = 0.0f;
                    item.offsetY = 3.0f;
                    item.scale = 0.58f;
                    item.scaleVel = 0.09f;
                    item.opacity = 0.10f;
                } else if (settings.subsequentKeyAnim == "slide") {
                    item.offsetX = 60.0f; // 优雅从容从右侧滑入
                    item.offsetY = 0.0f;
                    item.scale = 1.0f;
                    item.scaleVel = 0.0f;
                    item.opacity = 0.08f;
                } else if (settings.subsequentKeyAnim == "fade") {
                    item.offsetX = 0.0f;
                    item.offsetY = 0.0f;
                    item.scale = 1.0f;
                    item.scaleVel = 0.0f;
                    item.opacity = 0.10f;
                } else { // "none"
                    item.offsetX = 0.0f;
                    item.offsetY = 0.0f;
                    item.scale = 1.0f;
                    item.scaleVel = 0.0f;
                    item.opacity = 1.0f;
                }
                currentRow.items.push_back(item);
                currentRow.lastActiveTime = now;
            }
        } else {
            // 超过屏幕中线限制 或 超时停顿：智能换到新的一行 (将旧行向上平滑推移)
            if (!m_rows.empty() && settings.rowCascadeAnim) {
                for (auto& row : m_rows) {
                    row.targetOffsetY -= 12.0f; // 旧排级联向上平滑推移
                }
            }

            // 每行第 1 个按键入场动效组合
            item.isFirstInRow = true;
            if (settings.firstKeyAnim == "slide") {
                item.offsetX = 100.0f; // 从右向左加长滑行距离 (100px)
                item.offsetY = 0.0f;
                item.scale = 1.0f;
                item.scaleVel = 0.0f;
                item.opacity = 0.05f;
            } else if (settings.firstKeyAnim == "pop") {
                item.offsetX = 0.0f;
                item.offsetY = 3.0f;
                item.scale = 0.58f;
                item.scaleVel = 0.09f;
                item.opacity = 0.10f;
            } else if (settings.firstKeyAnim == "fade") {
                item.offsetX = 0.0f;
                item.offsetY = 0.0f;
                item.scale = 1.0f;
                item.scaleVel = 0.0f;
                item.opacity = 0.10f;
            } else { // "none"
                item.offsetX = 0.0f;
                item.offsetY = 0.0f;
                item.scale = 1.0f;
                item.scaleVel = 0.0f;
                item.opacity = 1.0f;
            }

            KeycastRow newRow;
            newRow.items.push_back(item);
            newRow.lastActiveTime = now;
            newRow.offsetY = settings.rowCascadeAnim ? 10.0f : 0.0f; // 新行自底部轻微跃入
            newRow.targetOffsetY = 0.0f;
            newRow.opacity = 1.0f;
            m_rows.push_back(newRow);

            // 限制最多保留 3 排活跃行，超出排平滑消融
            if (m_rows.size() > 3) {
                m_rows.front().isExiting = true;
                if (m_rows.size() > 4) {
                    m_rows.erase(m_rows.begin());
                }
            }
        }

        m_lastRawKey = keyStr;
        m_lastGlobalPushTime = now;
    }

    if (m_hwnd) PostMessageW(m_hwnd, START_ANIMATION_MESSAGE, 0, 0);
}

void KeycastOverlay::drawWindowsLogo(const D2D1_RECT_F& rect, float alpha) {
    if (!m_renderTarget || !m_brushModifierText || !m_d2dFactory) return;

    m_brushModifierText->SetOpacity(0.95f * alpha);
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;

    auto mapX = [&](float x) -> float { return rect.left + (x / 640.0f) * w; };
    auto mapY = [&](float y) -> float { return rect.top + (y / 640.0f) * h; };

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pathGeo;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&pathGeo))) return;

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pathGeo->Open(&sink))) return;

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    // 窗格 1 [左上]
    sink->BeginFigure(D2D1::Point2F(mapX(0.0f), mapY(290.0f)), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(mapX(0.0f), mapY(95.0f)));
    sink->AddLine(D2D1::Point2F(mapX(265.0f), mapY(58.0f)));
    sink->AddLine(D2D1::Point2F(mapX(265.0f), mapY(290.0f)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    // 窗格 2 [右上]
    sink->BeginFigure(D2D1::Point2F(mapX(305.0f), mapY(52.0f)), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(mapX(640.0f), mapY(0.0f)));
    sink->AddLine(D2D1::Point2F(mapX(640.0f), mapY(290.0f)));
    sink->AddLine(D2D1::Point2F(mapX(305.0f), mapY(290.0f)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    // 窗格 3 [右下]
    sink->BeginFigure(D2D1::Point2F(mapX(640.0f), mapY(350.0f)), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(mapX(640.0f), mapY(640.0f)));
    sink->AddLine(D2D1::Point2F(mapX(305.0f), mapY(588.0f)));
    sink->AddLine(D2D1::Point2F(mapX(305.0f), mapY(350.0f)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    // 窗格 4 [左下]
    sink->BeginFigure(D2D1::Point2F(mapX(265.0f), mapY(350.0f)), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(mapX(265.0f), mapY(582.0f)));
    sink->AddLine(D2D1::Point2F(mapX(0.0f), mapY(545.0f)));
    sink->AddLine(D2D1::Point2F(mapX(0.0f), mapY(350.0f)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

    sink->Close();
    m_renderTarget->FillGeometry(pathGeo.Get(), m_brushModifierText.Get());
}

void KeycastOverlay::drawKeycapCapsule(const KeycastItem& item, float startX, float startY, float alpha, float dpiScale) {
    if (!m_renderTarget || !m_brushBg || !m_brushText) return;

    auto isModifier = [](const std::string& tok) -> bool {
        return tok == "Win" || tok == "Ctrl" || tok == "Alt" || tok == "Shift" ||
               tok == "Cmd" || tok == "Meta" || tok == "Option" || tok == "Super" ||
               tok == "Control" || tok == "Command";
    };

    auto isSpecialKey = [&isModifier](const std::string& tok) -> bool {
        if (isModifier(tok)) return true;
        return tok == "Space" || tok == "Tab" || tok == "Enter" || tok == "Return" ||
               tok == "Backspace" || tok == "Delete" || tok == "Del" || tok == "Esc" ||
               tok == "Escape" || tok == "CapsLock" || tok == "Caps" || tok == "Insert" ||
               tok == "Home" || tok == "End" || tok == "PageUp" || tok == "PageDown" ||
               tok == "PgUp" || tok == "PgDn" || tok == "Up" || tok == "Down" ||
               tok == "Left" || tok == "Right" || tok == "PrtScn" || tok == "PrintScreen" ||
               tok == "ScrollLock" || tok == "Pause" || tok == "NumLock" ||
               (tok.size() >= 2 && tok[0] == 'F' && isdigit(static_cast<unsigned char>(tok[1])));
    };

    KeycastSettings settings = getSettings();
    const KeycastDynamicMetrics dyn = computeKeycastMetrics(settings.fontSize, dpiScale);

    float itemH = dyn.capsuleHeight;
    float capHeight = dyn.capHeight;
    float capCenterY = startY + itemH / 2.0f;

    float curX = startX + dyn.paddingX;
    float keycapAlpha = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(settings.modifierKeycapOpacity) / 100.0f));

    for (size_t i = 0; i < item.tokens.size(); ++i) {
        const std::string& token = item.tokens[i];

        if (token == "Win") {
            float winW = dyn.winWidth;
            float winH = capHeight;
            float topY = capCenterY - winH / 2.0f;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(curX, topY, curX + winW, topY + winH),
                dyn.capRadius, dyn.capRadius
            );
            if (keycapAlpha > 0.01f) {
                m_brushKeycapBg->SetOpacity(keycapAlpha * alpha);
                m_renderTarget->FillRoundedRectangle(&rrect, m_brushKeycapBg.Get());
                m_brushKeycapBorder->SetOpacity((std::min)(1.0f, keycapAlpha * 1.5f + 0.08f) * alpha);
                m_renderTarget->DrawRoundedRectangle(&rrect, m_brushKeycapBorder.Get(), dyn.borderWidth);
            }

            float logoSize = dyn.logoSize;
            D2D1_RECT_F logoRect = D2D1::RectF(
                curX + (winW - logoSize) / 2.0f,
                capCenterY - logoSize / 2.0f,
                curX + (winW + logoSize) / 2.0f,
                capCenterY + logoSize / 2.0f
            );
            drawWindowsLogo(logoRect, alpha);
            curX += winW;
        } else if (isModifier(token)) {
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wtoken.c_str(), static_cast<UINT32>(wtoken.length()),
                m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &layout);

            DWRITE_TEXT_METRICS m{};
            if (layout) layout->GetMetrics(&m);

            float btnW = (std::max)(dyn.winWidth, m.width + dyn.fontSize * 0.85f);
            float btnH = capHeight;
            float topY = capCenterY - btnH / 2.0f;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(curX, topY, curX + btnW, topY + btnH),
                dyn.capRadius, dyn.capRadius
            );

            if (keycapAlpha > 0.01f) {
                m_brushKeycapBg->SetOpacity(keycapAlpha * alpha);
                m_renderTarget->FillRoundedRectangle(&rrect, m_brushKeycapBg.Get());
                m_brushKeycapBorder->SetOpacity((std::min)(1.0f, keycapAlpha * 1.5f + 0.08f) * alpha);
                m_renderTarget->DrawRoundedRectangle(&rrect, m_brushKeycapBorder.Get(), dyn.borderWidth);
            }

            m_brushModifierText->SetOpacity(0.95f * alpha);
            if (layout) {
                float textDrawX = curX + (btnW - m.width) / 2.0f - m.left;
                float textDrawY = capCenterY - m.height / 2.0f - m.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(textDrawX, textDrawY),
                    layout.Get(), m_brushModifierText.Get());
            }
            curX += btnW;
        } else if (isSpecialKey(token)) {
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wtoken.c_str(), static_cast<UINT32>(wtoken.length()),
                m_keycapTextFormat.Get(), 1000.0f, 1000.0f, &layout);

            DWRITE_TEXT_METRICS m{};
            if (layout) layout->GetMetrics(&m);

            float minCapW = (token == "Space") ? (dyn.fontSize * 2.2f) : dyn.winWidth;
            float btnW = (std::max)(minCapW, m.width + dyn.fontSize * 0.70f);
            float btnH = capHeight;
            float topY = capCenterY - btnH / 2.0f;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(curX, topY, curX + btnW, topY + btnH),
                dyn.capRadius, dyn.capRadius
            );

            m_brushFuncKeyBg->SetOpacity(alpha);
            m_renderTarget->FillRoundedRectangle(&rrect, m_brushFuncKeyBg.Get());
            m_brushFuncKeyBorder->SetOpacity(alpha);
            m_renderTarget->DrawRoundedRectangle(&rrect, m_brushFuncKeyBorder.Get(), dyn.borderWidth);

            m_brushText->SetOpacity(alpha);
            if (layout) {
                float textDrawX = curX + (btnW - m.width) / 2.0f - m.left;
                float textDrawY = capCenterY - m.height / 2.0f - m.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(textDrawX, textDrawY),
                    layout.Get(), m_brushText.Get());
            }
            curX += btnW;
        } else {
            // 普通单字符键（如 C, V, E 等）: 使用 m_textFormat (SemiBold 清秀字重，无底座纯白高亮悬浮)
            std::wstring wtoken = easy::core::WinUtils::utf8ToWstring(token);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wtoken.c_str(), static_cast<UINT32>(wtoken.length()),
                m_textFormat.Get(), 1000.0f, 1000.0f, &layout);

            DWRITE_TEXT_METRICS m{};
            if (layout) layout->GetMetrics(&m);

            float btnW = (std::max)(dyn.fontSize * 0.85f, m.width + dyn.fontSize * 0.40f);

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

        // 绘制键间连接符号 '+' (小巧弱化微型字体，绝对水平居中)
        if (i + 1 < item.tokens.size()) {
            curX += dyn.fontSize * 0.08f;
            std::wstring plusStr = L"+";
            Microsoft::WRL::ComPtr<IDWriteTextLayout> plusLayout;
            m_dwriteFactory->CreateTextLayout(
                plusStr.c_str(), 1,
                m_plusTextFormat ? m_plusTextFormat.Get() : m_textFormat.Get(),
                1000.0f, 1000.0f, &plusLayout);
            DWRITE_TEXT_METRICS pm{};
            if (plusLayout) plusLayout->GetMetrics(&pm);

            m_brushPlusText->SetOpacity(0.85f * alpha);
            if (plusLayout) {
                float plusX = curX - pm.left;
                float plusY = capCenterY - pm.height / 2.0f - pm.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(plusX, plusY),
                    plusLayout.Get(), m_brushPlusText.Get());
            }
            curX += pm.width + dyn.fontSize * 0.08f;
        }
    }

    // 重复按键指示器 ×2 / ×4
    if (item.repeatCount > 1 && item.badgeWidthProgress > 0.05f) {
        curX += dyn.fontSize * 0.15f * item.badgeWidthProgress;
        std::wstring repStr = L"×" + std::to_wstring(item.repeatCount);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> repLayout;
        m_dwriteFactory->CreateTextLayout(
            repStr.c_str(), static_cast<UINT32>(repStr.length()),
            m_repeatTextFormat ? m_repeatTextFormat.Get() : m_keycapTextFormat.Get(),
            1000.0f, 1000.0f, &repLayout);
        DWRITE_TEXT_METRICS rm{};
        if (repLayout) repLayout->GetMetrics(&rm);

        float badgeW = rm.width * item.badgeWidthProgress;
        float finalAlpha = alpha * item.badgeOpacity;

        if (finalAlpha > 0.01f && badgeW > 1.0f) {
            D2D1_MATRIX_3X2_F oldTr;
            m_renderTarget->GetTransform(&oldTr);
            const bool hasBadgeScale = (std::abs(item.badgeScale - 1.0f) > 0.01f);
            if (hasBadgeScale) {
                float bcX = curX + badgeW / 2.0f;
                float bcY = capCenterY;
                D2D1_MATRIX_3X2_F sMat = D2D1::Matrix3x2F::Scale(item.badgeScale, item.badgeScale, D2D1::Point2F(bcX, bcY));
                m_renderTarget->SetTransform(sMat * oldTr);
            }

            m_brushBadgeBg->SetOpacity(finalAlpha * 0.95f);
            if (repLayout) {
                float badgeDrawX = curX + (badgeW - rm.width) / 2.0f - rm.left;
                float badgeDrawY = capCenterY - rm.height / 2.0f - rm.top;
                m_renderTarget->DrawTextLayout(
                    D2D1::Point2F(badgeDrawX, badgeDrawY),
                    repLayout.Get(), m_brushBadgeBg.Get());
            }

            if (hasBadgeScale) {
                m_renderTarget->SetTransform(oldTr);
            }
        }
    }
}

void KeycastOverlay::render() {
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;

    // 动态检测全局品牌色/强调色实时变更 (0ms 极速响应)
    std::string curAccent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "blue");
    if (m_lastAccentColor != curAccent) {
        m_lastAccentColor = curAccent;
        discardResources();
        if (!createResources()) return;
    }

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0)); // 透明背景

    KeycastSettings settings = getSettings();
    const KeycastDynamicMetrics dyn = computeKeycastMetrics(settings.fontSize, m_dpiScale);

    std::vector<KeycastRow> localRows;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        localRows = m_rows;
    }

    const bool isBottom = (settings.position == "bottom_left" ||
                           settings.position == "bottom_center" ||
                           settings.position == "bottom_right");

    float currentY = 10.0f * m_dpiScale;
    for (size_t r = 0; r < localRows.size(); ++r) {
        const auto& row = localRows[r];
        if (row.opacity < 0.02f) continue;

        // 1. 精确测量当前行所有胶囊并排的总宽度
        float rowWidth = 0.0f;
        for (size_t c = 0; c < row.items.size(); ++c) {
            rowWidth += calculateItemWidth(row.items[c], m_dpiScale);
            if (c + 1 < row.items.size()) {
                rowWidth += dyn.fontSize * 0.45f; // 胶囊水平间距
            }
        }

        // 2. 根据用户配置对齐模式（左对齐 / 居中对齐 / 右对齐）精确计算起始 X 坐标
        float startX = 12.0f * m_dpiScale;
        if (settings.position == "top_right" || settings.position == "bottom_right") {
            // 右对齐：紧贴右侧边缘
            startX = m_width - 12.0f * m_dpiScale - rowWidth;
        } else if (settings.position == "bottom_center") {
            // 居中对齐：在窗口水平中轴线上精准居中
            startX = (m_width - rowWidth) / 2.0f;
        } else {
            // 左对齐（top_left, bottom_left）
            startX = 12.0f * m_dpiScale;
        }

        // 3. 垂直排版计算：底部位置时最新行位于最底行，历史行向上级联浮动
        float actualY = 0.0f;
        if (isBottom) {
            float baseY = m_height - dyn.capsuleHeight - dyn.fontSize * 0.35f;
            actualY = baseY - static_cast<float>(localRows.size() - 1 - r) * dyn.rowStep + row.offsetY * m_dpiScale;
        } else {
            actualY = currentY + row.offsetY * m_dpiScale;
        }

        float currentX = startX;
        for (size_t c = 0; c < row.items.size(); ++c) {
            const auto& item = row.items[c];
            float itemAlpha = row.opacity * item.opacity;
            if (itemAlpha < 0.02f) continue;

            float drawX = currentX + item.offsetX * m_dpiScale;
            float drawY = actualY + item.offsetY * m_dpiScale;

            // 预先精确测量该组合键的总宽度（左右内边距各自适应 + 2px 呼吸裕量）
            float totalCapsuleW = calculateItemWidth(item, m_dpiScale);

            // 整个胶囊原子化同心同轴矩阵变换 (Scale Pop Matrix)
            D2D1_MATRIX_3X2_F oldTransform;
            m_renderTarget->GetTransform(&oldTransform);
            const bool hasScaleTransform = (std::abs(item.scale - 1.0f) > 0.005f);

            if (hasScaleTransform) {
                float centerX = drawX + totalCapsuleW / 2.0f;
                float centerY = drawY + dyn.capsuleHeight / 2.0f;
                D2D1_MATRIX_3X2_F scaleMatrix = D2D1::Matrix3x2F::Scale(
                    item.scale, item.scale, D2D1::Point2F(centerX, centerY));
                m_renderTarget->SetTransform(scaleMatrix * oldTransform);
            }

            // 1. 绘制深色不透明胶囊托盘背景 (0.97f 彻底阻隔背景文字透视穿帮)
            D2D1_ROUNDED_RECT capsuleRect = D2D1::RoundedRect(
                D2D1::RectF(drawX, drawY, drawX + totalCapsuleW, drawY + dyn.capsuleHeight),
                dyn.capsuleRadius, dyn.capsuleRadius
            );

            m_brushBg->SetOpacity(0.97f * itemAlpha);
            m_renderTarget->FillRoundedRectangle(&capsuleRect, m_brushBg.Get());

            m_brushBorder->SetOpacity(0.20f * itemAlpha);
            m_renderTarget->DrawRoundedRectangle(&capsuleRect, m_brushBorder.Get(), dyn.borderWidth);

            // 2. 绘制键帽微凸槽与内容
            drawKeycapCapsule(item, drawX, drawY, itemAlpha, m_dpiScale);

            if (hasScaleTransform) {
                m_renderTarget->SetTransform(oldTransform);
            }

            currentX += totalCapsuleW + dyn.fontSize * 0.45f;
        }

        if (!isBottom) {
            currentY += dyn.rowStep;
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

void KeycastOverlay::tickAnimation() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const uint64_t now = GetTickCount64();
    const uint64_t holdTime = static_cast<uint64_t>(m_settings.displayDurationMs);

    for (auto it = m_rows.begin(); it != m_rows.end();) {
        auto& row = *it;
        const uint64_t elapsed = now - row.lastActiveTime;

        // 1. 行级物理平滑推移动画 (新行推旧行向上平滑推移)
        row.offsetY += (row.targetOffsetY - row.offsetY) * 0.28f;

        // 2. 项级物理动力学 (连击连续流体演进 + 长距离阻尼滑行 + 二阶弹簧气泡)
        for (auto& item : row.items) {
            // 连击角标与胶囊宽度物理连续延展 (Continuous Badge Fluid Expansion)
            if (item.repeatCount > 1) {
                // 胶囊向右丝滑展开 (Smooth Width Growth)
                item.badgeWidthProgress += (1.0f - item.badgeWidthProgress) * 0.28f;
                if (std::abs(1.0f - item.badgeWidthProgress) < 0.005f) item.badgeWidthProgress = 1.0f;

                // 角标自身二阶弹簧微缩放 (Spring Physics Scale Pop)
                float bSpringK = 0.40f;
                float bDamping = 0.62f;
                float bForce = bSpringK * (1.0f - item.badgeScale);
                item.badgeScaleVel = (item.badgeScaleVel + bForce) * bDamping;
                item.badgeScale += item.badgeScaleVel;

                // 角标渐现
                item.badgeOpacity += (1.0f - item.badgeOpacity) * 0.35f;
                if (item.badgeOpacity > 0.99f) item.badgeOpacity = 1.0f;
            }

            // 主按键弹性微颤阻尼逼近
            if (std::abs(item.scale - 1.0f) > 0.005f || std::abs(item.scaleVel) > 0.005f) {
                float springK = 0.36f;
                float damping = 0.65f;
                float force = springK * (1.0f - item.scale);
                item.scaleVel = (item.scaleVel + force) * damping;
                item.scale += item.scaleVel;
            } else {
                item.scale = 1.0f;
                item.scaleVel = 0.0f;
            }

            if (item.isFirstInRow) {
                // 首项：优雅的长距离惯性阻尼滑行 (从右向左平滑缓入停驻，绝不因连击被打断)
                item.offsetX += (0.0f - item.offsetX) * 0.18f;
                if (std::abs(item.offsetX) < 0.25f) item.offsetX = 0.0f;
                item.opacity += (1.0f - item.opacity) * 0.22f;
            } else {
                if (m_settings.subsequentKeyAnim == "slide") {
                    item.offsetX += (0.0f - item.offsetX) * 0.20f;
                    if (std::abs(item.offsetX) < 0.25f) item.offsetX = 0.0f;
                    item.opacity += (1.0f - item.opacity) * 0.25f;
                } else if (m_settings.subsequentKeyAnim == "pop") {
                    item.offsetY += (0.0f - item.offsetY) * 0.32f;
                    item.opacity += (1.0f - item.opacity) * 0.42f;
                } else {
                    item.opacity += (1.0f - item.opacity) * 0.35f;
                }
            }
        }

        // 3. 寿命到期 或 新行挤出时的平滑消融上浮微动效
        if (elapsed > holdTime || row.isExiting) {
            float fadeSpeed = row.isExiting ? 0.08f : 0.045f;
            row.opacity = (std::max)(0.0f, row.opacity - fadeSpeed);
            if (m_settings.exitDriftAnim) {
                row.targetOffsetY -= 1.0f; // 伴随微幅轻盈向上飘升消融
            }
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
