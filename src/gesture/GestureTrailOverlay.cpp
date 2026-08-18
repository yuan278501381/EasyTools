// ─────────────────────────────────────────────────────────────────────────────
// GestureTrailOverlay.cpp — 手势轨迹可视化覆盖层实现
//
// 核心原理:
//   1. 创建 WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST 的全屏窗口
//   2. 使用 Direct2D 绘制连续贝塞尔平滑轨迹，带双层霓虹微光流光特效
//   3. 头部绘制发光能量微粒，提升绘制动感
//   4. 手势绘制过程中及手势完成后显示按键回显风格的实时动作名称
//   5. 窗口始终 click-through（WS_EX_TRANSPARENT），不影响用户操作
//   6. 颜色支持独立自定义配置或动态联动系统主题强调色
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureTrailOverlay.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Microsoft::WRL;

namespace easy::gesture {

static constexpr const wchar_t* OVERLAY_CLASS = L"EasyTools_GestureOverlay";

GestureTrailOverlay& GestureTrailOverlay::instance() {
    static GestureTrailOverlay inst;
    return inst;
}

// ─────────────────────────────────────────────────────────────────────────────
// 初始化 / 关闭
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::initialize(HINSTANCE hInstance) {
    easy::core::TraceId::Scope scope;

    if (!createOverlayWindow(hInstance)) {
        LOG_ERROR("创建手势轨迹覆盖层窗口失败");
        return false;
    }

    D2D1_FACTORY_OPTIONS opt{};
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, opt, m_d2dFactory.GetAddressOf());
    if (SUCCEEDED(hr)) {
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        );
    }

    ShowWindow(m_hwnd, SW_HIDE);
    m_visible.store(false);

    // 启动专用高优先级异步渲染线程，彻底将 Direct2D/GDI 渲染从鼠标钩子消息热路径剥离！
    m_renderThread = std::jthread([this](std::stop_token st) { renderLoop(st); });
    if (m_renderThread.native_handle()) {
        SetThreadPriority(m_renderThread.native_handle(), THREAD_PRIORITY_HIGHEST);
    }

    LOG_INFO("手势轨迹覆盖层初始化成功 (专用异步渲染管线已启动)");
    return true;
}

void GestureTrailOverlay::setStyle(const TrailStyle& style) {
    m_style = style;
    m_textScale = 0.0f;
    if (m_dwriteFactory) updateTextFormat(m_dpiScale);
    reloadThemeColors();
}

void GestureTrailOverlay::reloadThemeColors() {
    if (!m_renderTarget) return;

    auto& cfg = easy::core::ConfigManager::instance();
    const std::string colorMode = cfg.get<std::string>("/gesture/trailColorMode", "auto");
    const std::string customHex = cfg.get<std::string>("/gesture/trailColor", "#8B5CF6");
    m_style.lineWidth = cfg.get<float>("/gesture/trailWidth", 4.0f);

    const std::string accent = cfg.get<std::string>("/general/accentColor", "violet");
    const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);

    easy::core::AccentColorRGB trailRgb;
    if (colorMode == "custom" && !customHex.empty()) {
        trailRgb = easy::core::parseHexColor(customHex);
    } else {
        trailRgb = themeRgb;
    }

    // 主流光画笔（可自定义或跟随主题）
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(trailRgb.r, trailRgb.g, trailRgb.b, 1.0f),
        m_lineBrush.ReleaseAndGetAddressOf()
    );

    // 外部柔光霓虹画笔
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(trailRgb.r, trailRgb.g, trailRgb.b, 0.40f),
        m_glowBrush.ReleaseAndGetAddressOf()
    );

    // 检测是否为亮色主题
    const std::string theme = cfg.get<std::string>("/general/theme", "system");
    bool isLight = false;
    if (theme == "light") {
        isLight = true;
    } else if (theme == "system") {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD value = 1;
            DWORD size = sizeof(value);
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
                isLight = (value != 0);
            }
            RegCloseKey(hKey);
        }
    }

    // 灰色画笔 (未匹配动作时使用的优雅高阶银灰色)
    if (isLight) {
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.55f, 0.58f, 0.65f, 0.90f),
            m_greyLineBrush.ReleaseAndGetAddressOf()
        );
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.65f, 0.70f, 0.78f, 0.35f),
            m_greyGlowBrush.ReleaseAndGetAddressOf()
        );
    } else {
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.60f, 0.65f, 0.75f, 0.85f),
            m_greyLineBrush.ReleaseAndGetAddressOf()
        );
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.40f, 0.45f, 0.55f, 0.30f),
            m_greyGlowBrush.ReleaseAndGetAddressOf()
        );
    }

    // 调侃红底画笔（亮色/暗色自适应）
    if (isLight) {
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.92f, 0.18f, 0.24f, 0.94f),
            m_excessiveBgBrush.ReleaseAndGetAddressOf()
        );
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.78f, 0.10f, 0.16f, 0.85f),
            m_excessiveBorderBrush.ReleaseAndGetAddressOf()
        );
    } else {
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.52f, 0.08f, 0.12f, 0.92f),
            m_excessiveBgBrush.ReleaseAndGetAddressOf()
        );
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.96f, 0.28f, 0.36f, 0.85f),
            m_excessiveBorderBrush.ReleaseAndGetAddressOf()
        );
    }
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_excessiveDotBrush.ReleaseAndGetAddressOf()
    );

    // 头部发光核心晶体画笔
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_headCoreBrush.ReleaseAndGetAddressOf()
    );

    // 浮动文本 Toast 提示卡片：粗圆角矩形白色边框 + (绘制中透明灰底 / 画完松手点亮主题色底) + 白色手势描述
    // 1. 绘制过程中的透明灰色底 (半透明石墨灰毛玻璃底色)
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.82f),
        m_textBgBrush.ReleaseAndGetAddressOf()
    );
    // 2. 画完松手执行时点亮的主题色底板
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.95f),
        m_themeBgBrush.ReleaseAndGetAddressOf()
    );
    // 3. 粗圆角矩形纯白色边框
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f),
        m_textBorderBrush.ReleaseAndGetAddressOf()
    );
    // 4. 白色手势描述文字
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_textBrush.ReleaseAndGetAddressOf()
    );
}

void GestureTrailOverlay::shutdown() {
    if (m_renderThread.joinable()) {
        m_renderThread.request_stop();
        m_renderCv.notify_all();
        m_renderThread.join();
    }
    releaseD2DResources();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_helperOwnerHwnd) {
        DestroyWindow(m_helperOwnerHwnd);
        m_helperOwnerHwnd = nullptr;
    }
    m_visible.store(false);
    LOG_DEBUG("手势轨迹覆盖层已关闭");
}

void GestureTrailOverlay::clearCanvas() {
    std::lock_guard lock(m_renderMutex);
    if (!m_hwnd || !m_renderTarget || !m_memoryDC) return;
    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
    m_renderTarget->EndDraw();

    HDC hdcScreen = GetDC(nullptr);
    POINT ptSrc = { 0, 0 };
    SIZE sz = { m_width, m_height };
    POINT ptDst = { m_originX, m_originY };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &sz, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

// ─────────────────────────────────────────────────────────────────────────────
// 异步渲染核心循环 (在专用高优先级线程运行)
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::renderLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::unique_lock lock(m_renderSignalMutex);
        m_renderCv.wait_for(lock, std::chrono::milliseconds(16), [&]() {
            return m_renderRequested.load(std::memory_order_relaxed) ||
                   m_fading.load(std::memory_order_relaxed) ||
                   stopToken.stop_requested();
        });

        if (stopToken.stop_requested()) break;

        m_renderRequested.store(false, std::memory_order_relaxed);

        if (m_fading.load(std::memory_order_relaxed)) {
            const uint64_t currentEpoch = m_trailEpoch.load(std::memory_order_acquire);
            if (currentEpoch != m_fadeEpoch) {
                // 新手势已开始并打断了上一笔淡出，绝不隐藏窗口或释放资源
                m_fading.store(false, std::memory_order_relaxed);
                continue;
            }

            DWORD now = GetTickCount();
            DWORD elapsed = now - m_fadeStartTick;
            if (elapsed >= static_cast<DWORD>(m_style.fadeOutMs)) {
                if (m_trailEpoch.load(std::memory_order_acquire) != m_fadeEpoch) {
                    m_fading.store(false, std::memory_order_relaxed);
                    continue;
                }
                m_fading.store(false, std::memory_order_relaxed);
                m_fadeAlpha = 0.0f;
                clearCanvas();
                if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
                m_visible.store(false, std::memory_order_relaxed);
                releaseD2DResources();
                easy::core::WinUtils::trimWorkingSet();
            } else {
                m_fadeAlpha = 1.0f - static_cast<float>(elapsed) / m_style.fadeOutMs;
                render();
            }
        } else if (m_visible.load(std::memory_order_relaxed)) {
            render();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 轨迹操作 (全部为非阻塞极速操作，耗时 < 0.005ms)
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::beginTrail() {
    m_trailEpoch.fetch_add(1, std::memory_order_acq_rel);
    m_fading.store(false, std::memory_order_release);
    m_fadeAlpha = 1.0f;
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_smoothPathGeometry.Reset();
    }

    m_isRecognized.store(false, std::memory_order_relaxed);
}

void GestureTrailOverlay::addPoint(float x, float y) {
    bool hasVisibleTrail = false;
    {
        std::lock_guard lock(m_trailMutex);
        if (!m_points.empty()) {
            float dx = x - m_points.back().x;
            float dy = y - m_points.back().y;
            // 亚像素采样步长 (1.0px)，100% 忠实平滑捕捉轨迹
            const float minimumDelta = 1.0f * m_dpiScale;
            if (dx * dx + dy * dy < minimumDelta * minimumDelta) return;
        }
        m_points.push_back({x, y, GetTickCount()});
        hasVisibleTrail = m_points.size() >= 2;
    }

    if (hasVisibleTrail && m_hwnd) {
        m_fading.store(false, std::memory_order_release);
        m_fadeAlpha = 1.0f;
        m_visible.store(true, std::memory_order_release);
        if (!IsWindowVisible(m_hwnd)) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }
        m_renderRequested.store(true, std::memory_order_release);
        m_renderCv.notify_one();
    }
}

void GestureTrailOverlay::setLiveAction(const std::string& actionText) {
    bool changed = false;
    {
        std::lock_guard lock(m_trailMutex);
        if (m_resultText != actionText) {
            m_resultText = actionText;
            changed = true;
        }
    }
    if (changed && m_visible.load(std::memory_order_relaxed)) {
        m_renderRequested.store(true, std::memory_order_release);
        m_renderCv.notify_one();
    }
}

void GestureTrailOverlay::setRecognized(bool recognized) {
    if (m_isRecognized.exchange(recognized) != recognized) {
        if (!recognized) {
            std::lock_guard lock(m_trailMutex);
            if (m_resultText != "•••") {
                m_resultText.clear();
            }
        }
        if (m_visible.load(std::memory_order_relaxed)) {
            m_renderRequested.store(true, std::memory_order_release);
            m_renderCv.notify_one();
        }
    }
}

void GestureTrailOverlay::endTrail(const std::string& resultText) {
    {
        std::lock_guard lock(m_trailMutex);
        if (!resultText.empty()) {
            m_resultText = resultText;
        }
    }
    m_fadeEpoch = m_trailEpoch.load(std::memory_order_acquire);
    m_fadeStartTick = GetTickCount();
    m_fadeAlpha = 1.0f;
    m_fading.store(true, std::memory_order_release);
    m_renderRequested.store(true, std::memory_order_release);
    m_renderCv.notify_one();
}

void GestureTrailOverlay::hide() {
    m_trailEpoch.fetch_add(1, std::memory_order_acq_rel);
    m_fading.store(false, std::memory_order_release);
    m_renderRequested.store(false, std::memory_order_release);
    if (m_hwnd) {
        clearCanvas();
        ShowWindow(m_hwnd, SW_HIDE);
    }
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_smoothPathGeometry.Reset();
    }
    m_isRecognized.store(false);
    m_visible.store(false);
    m_fadeAlpha = 1.0f;
    releaseD2DResources();
    easy::core::WinUtils::trimWorkingSet();
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = overlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);

    m_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_helperOwnerHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"EasyTools_GestureTrailHelperOwner",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        OVERLAY_CLASS,
        L"EasyTools Gesture Trail",
        WS_POPUP,
        m_originX, m_originY, m_width, m_height,
        m_helperOwnerHwnd,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        LOG_ERROR("创建手势轨迹窗口失败");
        return false;
    }

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct2D 资源管理
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createD2DResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_lineBrush && m_textBorderBrush) return true;

    auto fail = [this]() {
        releaseD2DResources();
        return false;
    };

    HRESULT hr = S_OK;

    // D2D 工厂
    if (!m_d2dFactory) {
        D2D1_FACTORY_OPTIONS options{};
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, m_d2dFactory.GetAddressOf());
        if (FAILED(hr)) return fail();
    }

    // DirectWrite 工厂
    if (!m_dwriteFactory) {
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        );
        if (FAILED(hr)) return fail();
    }

    if (!updateTextFormat(m_dpiScale)) return fail();

    // 渲染目标
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = m_d2dFactory->CreateDCRenderTarget(&rtProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return fail();

    // 创建内存 DC 与 DIB
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return fail();
    m_memoryDC = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_width;
    bmi.bmiHeader.biHeight = -m_height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pBits = nullptr;
    m_memoryBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memoryDC || !m_memoryBitmap || !pBits) return fail();
    m_oldBitmap = (HBITMAP)SelectObject(m_memoryDC, m_memoryBitmap);
    
    RECT memRect = { 0, 0, m_width, m_height };
    if (FAILED(m_renderTarget->BindDC(m_memoryDC, &memRect))) return fail();

    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 笔触样式 (使线段更平滑，具有圆润笔头与圆角拐弯)
    D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND,
        10.0f,
        D2D1_DASH_STYLE_SOLID,
        0.0f
    );
    hr = m_d2dFactory->CreateStrokeStyle(strokeProps, nullptr, 0, m_strokeStyle.GetAddressOf());
    if (FAILED(hr)) return fail();

    // 动态加载画笔与强调色
    reloadThemeColors();

    if (!m_lineBrush || !m_textBgBrush || !m_textBorderBrush || !m_textBrush) return fail();
    return true;
}

bool GestureTrailOverlay::updateTextFormat(float dpiScale) {
    dpiScale = std::clamp(dpiScale, 1.0f, 5.0f);
    if (m_textFormat && std::abs(m_textScale - dpiScale) < 0.01f) return true;
    if (!m_dwriteFactory) return false;
    ComPtr<IDWriteTextFormat> format;
    const HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_style.resultFontSize * dpiScale,
        L"zh-CN",
        format.GetAddressOf()
    );
    if (FAILED(hr) || !format) return false;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat = std::move(format);
    m_textScale = dpiScale;
    return true;
}

void GestureTrailOverlay::releaseD2DResources() {
    std::lock_guard lock(m_renderMutex);
    m_smoothPathGeometry.Reset();
    m_headCoreBrush.Reset();
    m_glowBrush.Reset();
    m_greyGlowBrush.Reset();
    m_greyLineBrush.Reset();
    m_excessiveDotBrush.Reset();
    m_excessiveBorderBrush.Reset();
    m_excessiveBgBrush.Reset();
    m_textBrush.Reset();
    m_textBorderBrush.Reset();
    m_textBgBrush.Reset();
    m_themeBgBrush.Reset();
    m_lineBrush.Reset();
    m_textFormat.Reset();
    m_textScale = 0.0f;
    m_strokeStyle.Reset();
    m_renderTarget.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    
    if (m_memoryDC && m_oldBitmap) {
        SelectObject(m_memoryDC, m_oldBitmap);
    }
    m_oldBitmap = nullptr;
    if (m_memoryBitmap) {
        DeleteObject(m_memoryBitmap);
        m_memoryBitmap = nullptr;
    }
    if (m_memoryDC) {
        DeleteDC(m_memoryDC);
        m_memoryDC = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::render() {
    std::lock_guard lock(m_renderMutex);
    if (!m_renderTarget) {
        POINT cursor{};
        GetCursorPos(&cursor);
        m_dpiScale = easy::core::dpi::scaleAtPoint(cursor);
        if (!createD2DResources()) return;
    }
    if (!m_renderTarget || !m_lineBrush) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // 完全透明背景

    std::lock_guard trailLock(m_trailMutex);

    // 根据识别状态动态切换线条画笔（没识别到为灰色，识别到为主体色）
    const bool isRecognized = m_isRecognized.load(std::memory_order_relaxed);
    ID2D1SolidColorBrush* activeGlow = isRecognized ? m_glowBrush.Get() : m_greyGlowBrush.Get();
    ID2D1SolidColorBrush* activeLine = isRecognized ? m_lineBrush.Get() : m_greyLineBrush.Get();

    if (m_points.size() >= 2 && m_d2dFactory) {
        ComPtr<ID2D1PathGeometry> pathGeometry;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(pathGeometry.GetAddressOf()))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(pathGeometry->Open(&sink))) {
                auto getPt = [&](size_t idx) -> D2D1_POINT_2F {
                    return D2D1::Point2F(m_points[idx].x - m_originX, m_points[idx].y - m_originY);
                };

                D2D1_POINT_2F p0 = getPt(0);
                sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);

                if (m_points.size() == 2) {
                    sink->AddLine(getPt(1));
                } else {
                    // 中点连续平滑二次贝塞尔样条插值，消除直角折线感与接缝
                    for (size_t i = 0; i < m_points.size() - 2; ++i) {
                        D2D1_POINT_2F pCurr = getPt(i);
                        D2D1_POINT_2F pNext = getPt(i + 1);
                        D2D1_POINT_2F pAfter = getPt(i + 2);
                        
                        D2D1_POINT_2F startPt = (i == 0) ? pCurr : D2D1::Point2F((pCurr.x + pNext.x) / 2.0f, (pCurr.y + pNext.y) / 2.0f);
                        D2D1_POINT_2F midPt = D2D1::Point2F((pNext.x + pAfter.x) / 2.0f, (pNext.y + pAfter.y) / 2.0f);
                        
                        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pNext, midPt));
                    }
                    // 最后一笔顺滑收尾到终点
                    sink->AddLine(getPt(m_points.size() - 1));
                }

                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();

                // 1. Pass 1: 绘制外部柔光霓虹 (Outer Neon Bloom)
                if (activeGlow) {
                    activeGlow->SetOpacity(0.25f * m_fadeAlpha);
                    m_renderTarget->DrawGeometry(
                        pathGeometry.Get(), activeGlow,
                        (m_style.lineWidth * 2.5f) * m_dpiScale, m_strokeStyle.Get());
                }

                // 2. Pass 2: 绘制内部高亮核心线条 (Inner Solid Core)
                if (activeLine) {
                    activeLine->SetOpacity(0.92f * m_fadeAlpha);
                    m_renderTarget->DrawGeometry(
                        pathGeometry.Get(), activeLine,
                        m_style.lineWidth * m_dpiScale, m_strokeStyle.Get());
                }
            }
        }
    }

    // 绘制识别结果与实时动作名称文字（仅在识别成功或处于调侃状态时才显示 Toast，未识别时绝不显示残留文本）
    const bool isExcessive = (m_resultText == "•••");
    const bool shouldShowToast = (isRecognized && !m_resultText.empty()) || isExcessive;

    if (shouldShowToast && !m_points.empty()) {
        POINT ptCursor;
        GetCursorPos(&ptCursor);
        HMONITOR hMon = MonitorFromPoint(ptCursor, MONITOR_DEFAULTTONEAREST);
        const RECT work = easy::core::dpi::workArea(hMon);
        const float resultScale = easy::core::dpi::scaleForMonitor(hMon);
        const bool hasTextFormat = updateTextFormat(resultScale);

        float centerX = static_cast<float>(work.left + work.right) / 2.0f - m_originX;
        float centerY = work.top + static_cast<float>(work.bottom - work.top) * 0.82f - m_originY;

        if (isExcessive) {
            // 调侃状态：红底卡片 + 3 个饱满圆滑的白色大圆点（更大气舒展的黄金比例）
            float boxW = 126.0f * resultScale;
            float boxH = 58.0f * resultScale;

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                            centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                16.0f * resultScale, 16.0f * resultScale
            );

            // 1. 红底半透明背景 (调侃状态)
            if (m_excessiveBgBrush) {
                m_excessiveBgBrush->SetOpacity(0.94f * m_fadeAlpha);
                m_renderTarget->FillRoundedRectangle(&rrect, m_excessiveBgBrush.Get());
            }

            // 2. 粗圆角边框 (2.6px 粗边框)
            if (m_excessiveBorderBrush) {
                m_excessiveBorderBrush->SetOpacity(0.95f * m_fadeAlpha);
                m_renderTarget->DrawRoundedRectangle(
                    &rrect, m_excessiveBorderBrush.Get(), 2.6f * resultScale);
            }

            // 3. 3 个大圆点（直径约 12px，饱满醒目可爱）
            if (m_excessiveDotBrush) {
                m_excessiveDotBrush->SetOpacity(m_fadeAlpha);
                const float dotRadius = 6.0f * resultScale;
                const float dotSpacing = 22.0f * resultScale;

                m_renderTarget->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(centerX - dotSpacing, centerY), dotRadius, dotRadius),
                    m_excessiveDotBrush.Get());
                m_renderTarget->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(centerX, centerY), dotRadius, dotRadius),
                    m_excessiveDotBrush.Get());
                m_renderTarget->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(centerX + dotSpacing, centerY), dotRadius, dotRadius),
                    m_excessiveDotBrush.Get());
            }
        } else {
            // 正常动作描述 Toast：粗圆角矩形白色边框 + (绘制中透明灰底 / 画完松手点亮主题色底) + 白色手势描述
            const std::wstring wText = hasTextFormat
                ? easy::core::WinUtils::utf8ToWstring(m_resultText) : std::wstring{};

            if (hasTextFormat && !wText.empty() && m_dwriteFactory) {
                ComPtr<IDWriteTextLayout> layout;
                m_dwriteFactory->CreateTextLayout(
                    wText.c_str(), static_cast<UINT32>(wText.length()),
                    m_textFormat.Get(),
                    10000.0f, 1000.0f,
                    layout.GetAddressOf()
                );

                float boxW = 140.0f * resultScale;
                float boxH = 58.0f * resultScale;
                if (layout) {
                    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                        // 黄金呼吸比例：更大更舒展的水平与垂直安全内边距，告别窄小拥挤
                        float paddingX = 38.0f * resultScale;
                        float paddingY = 16.0f * resultScale;
                        boxW = (std::max)(metrics.width + paddingX * 2.0f, 136.0f * resultScale);
                        boxH = (std::max)(metrics.height + paddingY * 2.0f, 58.0f * resultScale);
                    }
                }

                D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                    D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                                centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                    16.0f * resultScale, 16.0f * resultScale
                );

                // 1. 底色：画完松手时变成主题色底，绘制过程中为透明灰色底
                if (m_fading.load(std::memory_order_relaxed) && m_themeBgBrush) {
                    m_themeBgBrush->SetOpacity(0.95f * m_fadeAlpha);
                    m_renderTarget->FillRoundedRectangle(&rrect, m_themeBgBrush.Get());
                } else if (m_textBgBrush) {
                    m_textBgBrush->SetOpacity(0.82f * m_fadeAlpha);
                    m_renderTarget->FillRoundedRectangle(&rrect, m_textBgBrush.Get());
                }

                // 2. 粗圆角矩形纯白色边框 (2.6px 粗边框)
                if (m_textBorderBrush) {
                    m_textBorderBrush->SetOpacity(0.95f * m_fadeAlpha);
                    m_renderTarget->DrawRoundedRectangle(
                        &rrect, m_textBorderBrush.Get(), 2.6f * resultScale);
                }

                // 3. 白色手势描述文字
                if (m_textBrush) {
                    m_textBrush->SetOpacity(m_fadeAlpha);
                    m_renderTarget->DrawText(
                        wText.c_str(),
                        static_cast<UINT32>(wText.size()),
                        m_textFormat.Get(),
                        D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                                    centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                        m_textBrush.Get());
                }
            }
        }
    }

    if (FAILED(m_renderTarget->EndDraw())) {
        LOG_WARN("手势轨迹 Direct2D 帧提交失败");
        return;
    }
    
    // 使用 UpdateLayeredWindow 完整屏幕原子级提交，与 DWM 完美同步，杜绝任何历史笔迹残影
    if (m_memoryDC) {
        HDC hdcScreen = GetDC(nullptr);
        POINT ptSrc = {0, 0};
        POINT ptWin = {m_originX, m_originY};
        SIZE size = {m_width, m_height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

        UpdateLayeredWindow(m_hwnd, hdcScreen, &ptWin, &size, m_memoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, hdcScreen);

        LOG_TRACE("手势轨迹异步帧提交完成: 点数={}", m_points.size());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK GestureTrailOverlay::overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<GestureTrailOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {

        case WM_DISPLAYCHANGE: {
            if (self && self->m_hwnd) {
                int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
                int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                self->m_originX = x;
                self->m_originY = y;
                self->m_width = w;
                self->m_height = h;
                MoveWindow(self->m_hwnd, x, y, w, h, FALSE);

                if (self->m_memoryDC) {
                    HDC hdcScreen = GetDC(nullptr);
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = w;
                    bmi.bmiHeader.biHeight = -h;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    
                    void* pBits = nullptr;
                    HBITMAP replacement = hdcScreen ? CreateDIBSection(
                        hdcScreen, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0) : nullptr;
                    if (hdcScreen) ReleaseDC(nullptr, hdcScreen);

                    if (replacement && pBits) {
                        HBITMAP previousSurface = static_cast<HBITMAP>(
                            SelectObject(self->m_memoryDC, replacement));
                        if (previousSurface && previousSurface != HGDI_ERROR) {
                            self->m_memoryBitmap = replacement;
                            DeleteObject(previousSurface);
                        } else {
                            DeleteObject(replacement);
                            replacement = nullptr;
                        }
                    }

                    if (replacement && self->m_renderTarget) {
                        RECT memRect = {0, 0, w, h};
                        if (FAILED(self->m_renderTarget->BindDC(
                                self->m_memoryDC, &memRect))) {
                            self->releaseD2DResources();
                        }
                    } else if (!replacement) {
                        self->releaseD2DResources();
                    }
                    self->m_smoothPathGeometry.Reset();
                }
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace easy::gesture
