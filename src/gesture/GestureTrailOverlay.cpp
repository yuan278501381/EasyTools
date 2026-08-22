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
#include "gesture/GestureInputPolicy.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/config/ConfigManager.h"
#include "core/accessibility/OverlayAnnouncement.h"
#include "core/accessibility/OverlayUiaProvider.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <climits>

using namespace Microsoft::WRL;

namespace easy::gesture {

namespace {

int snapDown(int value, int grid) {
    if (grid <= 0) return value;
    if (value >= 0) return (value / grid) * grid;
    return -(((-value + grid - 1) / grid) * grid);
}

int snapUp(int value, int grid) {
    if (grid <= 0) return value;
    if (value >= 0) return ((value + grid - 1) / grid) * grid;
    return -((-value / grid) * grid);
}

}  // namespace

static constexpr const wchar_t* OVERLAY_CLASS = L"EasyTools_GestureOverlay";
static constexpr UINT WM_GESTURE_ACCESSIBILITY_RESULT = WM_APP + 73;

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
        // ABOVE_NORMAL 足够追上 60fps；HIGHEST 会在每次 UpdateLayeredWindow 时抢占
        // 钩子所在的主线程，实测每笔移动回调被拖到 20ms+。
        SetThreadPriority(m_renderThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
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
    m_themeDirty.store(true, std::memory_order_release);
    if (m_visible.load(std::memory_order_relaxed) ||
        m_wantVisible.load(std::memory_order_relaxed) ||
        m_fading.load(std::memory_order_relaxed)) {
        m_wakeRender.store(true, std::memory_order_release);
        m_renderCv.notify_one();
    }
}

void GestureTrailOverlay::applyThemeColorsLocked() {
    if (!m_renderTarget) return;

    auto& cfg = easy::core::ConfigManager::instance();
    const std::string colorMode = cfg.get<std::string>("/gesture/trailColorMode", "auto");
    const std::string customHex = cfg.get<std::string>("/gesture/trailColor", "#8B5CF6");
    m_style.lineWidth = cfg.get<float>("/gesture/trailWidth", 4.0f);
    m_style.outlineWidth = clampTrailOutlineWidth(
        cfg.get<float>("/gesture/trailOutlineWidth", 2.5f));

    const std::string accent = cfg.get<std::string>("/general/accentColor", "violet");
    const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);
    const easy::core::AccentColorRGB trailRgb =
        resolveGestureTrailRgb(colorMode, customHex, themeRgb);

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
    bool systemAppsUseLight = false;
    if (theme == "system") {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD value = 1;
            DWORD size = sizeof(value);
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
                systemAppsUseLight = (value != 0);
            }
            RegCloseKey(hKey);
        }
    }
    const bool isLight = gestureTrailUsesLightPalette(theme, systemAppsUseLight);

    // 灰色画笔 (未匹配动作时使用)。亮色主题必须足够深，浅银灰叠在白壁纸上等于没有轨迹。
    if (isLight) {
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.28f, 0.31f, 0.38f, 0.96f),
            m_greyLineBrush.ReleaseAndGetAddressOf()
        );
        m_renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.40f, 0.44f, 0.52f, 0.40f),
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

    // 头部发光核心晶体画笔
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_headCoreBrush.ReleaseAndGetAddressOf()
    );
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_outlineBrush.ReleaseAndGetAddressOf()
    );

    if (!m_toastTarget) return;

    if (isLight) {
        m_toastTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.92f, 0.18f, 0.24f, 0.94f),
            m_excessiveBgBrush.ReleaseAndGetAddressOf()
        );
        m_toastTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.78f, 0.10f, 0.16f, 0.85f),
            m_excessiveBorderBrush.ReleaseAndGetAddressOf()
        );
    } else {
        m_toastTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.52f, 0.08f, 0.12f, 0.92f),
            m_excessiveBgBrush.ReleaseAndGetAddressOf()
        );
        m_toastTarget->CreateSolidColorBrush(
            D2D1::ColorF(0.96f, 0.28f, 0.36f, 0.85f),
            m_excessiveBorderBrush.ReleaseAndGetAddressOf()
        );
    }
    m_toastTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_excessiveDotBrush.ReleaseAndGetAddressOf()
    );
    m_toastTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.82f),
        m_textBgBrush.ReleaseAndGetAddressOf()
    );
    m_toastTarget->CreateSolidColorBrush(
        D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.95f),
        m_themeBgBrush.ReleaseAndGetAddressOf()
    );
    m_toastTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f),
        m_textBorderBrush.ReleaseAndGetAddressOf()
    );
    m_toastTarget->CreateSolidColorBrush(
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
    if (m_toastHwnd) {
        DestroyWindow(m_toastHwnd);
        m_toastHwnd = nullptr;
    }
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
            return m_hideRequested.load(std::memory_order_relaxed) ||
                   m_fading.load(std::memory_order_relaxed) ||
                   m_wakeRender.load(std::memory_order_relaxed) ||
                   stopToken.stop_requested();
        });

        if (stopToken.stop_requested()) break;
        m_wakeRender.store(false, std::memory_order_relaxed);
        lock.unlock();

        if (m_hideRequested.exchange(false, std::memory_order_acq_rel)) {
            applyHideOnRenderThread();
            continue;
        }

        if (m_dismissPrevious.exchange(false, std::memory_order_acq_rel) &&
            !m_wantVisible.load(std::memory_order_relaxed) &&
            !m_fading.load(std::memory_order_relaxed)) {
            if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
            hideToastWindow();
            m_visible.store(false, std::memory_order_relaxed);
        }

        m_renderRequested.store(false, std::memory_order_relaxed);

        bool fading = m_fading.load(std::memory_order_relaxed);
        if (fading) {
            const uint64_t currentEpoch = m_trailEpoch.load(std::memory_order_acquire);
            if (currentEpoch != m_fadeEpoch) {
                // 新手势已开始并打断了上一笔淡出，绝不隐藏窗口或释放资源
                m_fading.store(false, std::memory_order_relaxed);
                m_fadeClockStarted.store(false, std::memory_order_relaxed);
                fading = false;
            } else {
                const DWORD holdMs = static_cast<DWORD>(m_style.fadeHoldMs);
                const DWORD fadeMs = static_cast<DWORD>(m_style.fadeOutMs);
                const bool clockStarted = m_fadeClockStarted.load(std::memory_order_relaxed);
                const DWORD elapsed = clockStarted ? (GetTickCount() - m_fadeStartTick) : 0;
                if (gestureFadeShouldFinish(clockStarted, elapsed, holdMs, fadeMs)) {
                    if (m_trailEpoch.load(std::memory_order_acquire) != m_fadeEpoch) {
                        m_fading.store(false, std::memory_order_relaxed);
                        m_fadeClockStarted.store(false, std::memory_order_relaxed);
                        fading = false;
                    } else {
                        m_fading.store(false, std::memory_order_relaxed);
                        m_fadeClockStarted.store(false, std::memory_order_relaxed);
                        m_fadeAlpha = 0.0f;
                        {
                            std::lock_guard trailLock(m_trailMutex);
                            m_points.clear();
                            m_resultText.clear();
                            m_smoothPathGeometry.Reset();
                        }
                        m_isRecognized.store(false, std::memory_order_relaxed);
                        m_wantVisible.store(false, std::memory_order_relaxed);
                        m_strokeSurfaceLive.store(false, std::memory_order_relaxed);
                        if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
                        hideToastWindow();
                        m_visible.store(false, std::memory_order_relaxed);
                        // 淡出结束只藏窗，整屏 DIB 保持热备。下一笔不必再付 100ms+ 重建税。
                        continue;
                    }
                } else {
                    m_fadeAlpha = gestureFadeAlpha(clockStarted, elapsed, holdMs, fadeMs);
                    const bool presented = render();
                    if (presented && !clockStarted) {
                        m_fadeStartTick = GetTickCount();
                        m_fadeClockStarted.store(true, std::memory_order_release);
                    }
                    continue;
                }
            }
        }

        if (!fading && (m_visible.load(std::memory_order_relaxed) ||
                        m_wantVisible.load(std::memory_order_relaxed))) {
            render();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 轨迹操作：避免同步 I/O；实际耗时应通过 PerformanceMonitor 在目标设备上测量。
// ─────────────────────────────────────────────────────────────────────────────

void GestureTrailOverlay::beginTrail() {
    raiseZOrderForDraw();
    m_trailEpoch.fetch_add(1, std::memory_order_acq_rel);
    m_hideRequested.store(false, std::memory_order_release);
    m_wantVisible.store(false, std::memory_order_release);
    m_strokeSurfaceLive.store(false, std::memory_order_release);
    m_fading.store(false, std::memory_order_release);
    m_fadeClockStarted.store(false, std::memory_order_release);
    m_fadeAlpha = 1.0f;
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_smoothPathGeometry.Reset();
    }

    m_isRecognized.store(false, std::memory_order_relaxed);
    m_themeDirty.store(true, std::memory_order_release);
    m_dismissPrevious.store(true, std::memory_order_release);
    m_wakeRender.store(true, std::memory_order_release);
    m_renderCv.notify_one();
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
        const bool firstVisible = !m_wantVisible.exchange(true, std::memory_order_acq_rel);
        m_renderRequested.store(true, std::memory_order_release);
        const DWORD now = GetTickCount();
        const DWORD lastWake = m_lastWakeTick.load(std::memory_order_relaxed);
        if (firstVisible || (now - lastWake) >= 16) {
            m_lastWakeTick.store(now, std::memory_order_relaxed);
            m_wakeRender.store(true, std::memory_order_release);
            m_renderCv.notify_one();
        }
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
    if (changed && (m_visible.load(std::memory_order_relaxed) ||
                    m_wantVisible.load(std::memory_order_relaxed))) {
        m_wakeRender.store(true, std::memory_order_release);
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
        if (m_visible.load(std::memory_order_relaxed) ||
            m_wantVisible.load(std::memory_order_relaxed)) {
            m_wakeRender.store(true, std::memory_order_release);
            m_renderRequested.store(true, std::memory_order_release);
            m_renderCv.notify_one();
        }
    }
}

void GestureTrailOverlay::endTrail(const std::string& resultText) {
    bool hasPoints = false;
    size_t overlayPoints = 0;
    {
        std::lock_guard lock(m_trailMutex);
        if (!resultText.empty()) {
            m_resultText = resultText;
        }
        hasPoints = !m_points.empty();
        overlayPoints = m_points.size();
    }
    LOG_DEBUG("手势轨迹结束: overlayPoints={}, label={}", overlayPoints, resultText);
    // 松手时才命中的短手势（如 U=关闭窗口）过程中方向可能没变过，
    // live setRecognized(true) 没走到，Toast 不能因此被关掉。
    if (!resultText.empty() && resultText != "•••") {
        m_isRecognized.store(true, std::memory_order_relaxed);
        // endTrail may run on the low-level mouse-hook path. Do not synchronously
        // call a window API here; hand the accessibility update to the HWND's
        // owning thread instead.
        if (m_toastHwnd) {
            PostMessageW(m_toastHwnd, WM_GESTURE_ACCESSIBILITY_RESULT, 0, 0);
        }
    }
    if (hasPoints) {
        m_wantVisible.store(true, std::memory_order_release);
    }
    m_fadeEpoch = m_trailEpoch.load(std::memory_order_acquire);
    m_fadeClockStarted.store(false, std::memory_order_release);
    m_fadeStartTick = 0;
    m_fadeAlpha = 1.0f;
    m_fading.store(true, std::memory_order_release);
    m_wakeRender.store(true, std::memory_order_release);
    m_renderRequested.store(true, std::memory_order_release);
    m_renderCv.notify_one();
}

void GestureTrailOverlay::hide() {
    m_trailEpoch.fetch_add(1, std::memory_order_acq_rel);
    m_fading.store(false, std::memory_order_release);
    m_fadeClockStarted.store(false, std::memory_order_release);
    m_wantVisible.store(false, std::memory_order_release);
    m_visible.store(false, std::memory_order_release);
    m_renderRequested.store(false, std::memory_order_release);
    m_hideRequested.store(true, std::memory_order_release);
    m_wakeRender.store(true, std::memory_order_release);
    m_renderCv.notify_one();
}

void GestureTrailOverlay::yieldZOrderForInput() {
    auto lower = [](HWND hwnd) {
        if (hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd)) {
            // NOTOPMOST 仍会停在普通窗口堆顶，继续挡住目标。先沉底，让 WindowFromPoint / 前台切换落到真实应用。
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
    };
    lower(m_hwnd);
    lower(m_toastHwnd);
    m_zOrderYielded.store(true, std::memory_order_release);
}

void GestureTrailOverlay::raiseZOrderForDraw() {
    auto raise = [](HWND hwnd) {
        if (hwnd && IsWindow(hwnd)) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
    };
    raise(m_hwnd);
    raise(m_toastHwnd);
    m_zOrderYielded.store(false, std::memory_order_release);
}

void GestureTrailOverlay::applyHideOnRenderThread() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
    hideToastWindow();
    {
        std::lock_guard lock(m_trailMutex);
        m_points.clear();
        m_resultText.clear();
        m_smoothPathGeometry.Reset();
    }
    m_isRecognized.store(false);
    m_fadeAlpha = 1.0f;
    m_strokeSurfaceLive.store(false, std::memory_order_relaxed);
    releaseD2DResources();
    m_width = 0;
    m_height = 0;
}

bool GestureTrailOverlay::recreateBitmapLocked(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0 || !m_hwnd) return false;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    if (!m_memoryDC) {
        m_memoryDC = CreateCompatibleDC(hdcScreen);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_memoryDC || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        return false;
    }

    HBITMAP selected = static_cast<HBITMAP>(SelectObject(m_memoryDC, bmp));
    if (m_memoryBitmap && selected == m_memoryBitmap) {
        DeleteObject(m_memoryBitmap);
    } else if (selected && selected != HGDI_ERROR && !m_oldBitmap) {
        m_oldBitmap = selected;
    }
    m_memoryBitmap = bmp;
    m_originX = x;
    m_originY = y;
    m_width = width;
    m_height = height;

    if (m_renderTarget) {
        RECT memRect = {0, 0, width, height};
        if (FAILED(m_renderTarget->BindDC(m_memoryDC, &memRect))) {
            return false;
        }
    }
    LOG_DEBUG("手势轨迹表面重建: {}x{} at ({},{})", width, height, x, y);
    return true;
}

bool GestureTrailOverlay::presentLayeredLocked(HWND hwnd, HDC memDC, int x, int y,
                                               int width, int height) {
    if (!hwnd || !memDC || width <= 0 || height <= 0) return false;
    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    POINT ptSrc = {0, 0};
    POINT ptWin = {x, y};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(
        hwnd, hdcScreen, &ptWin, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    const DWORD err = ok ? 0 : GetLastError();
    ReleaseDC(nullptr, hdcScreen);
    if (!ok) {
        LOG_WARN("手势覆盖层提交失败: {}x{} error={}", width, height, err);
        return false;
    }
    // HWND_BOTTOM 沉底后 WS_EX_TOPMOST 位经常还在，旧逻辑会跳过插队，轨迹画在
    // 最大化 Electron / CEF 窗下面。沉底过就必须无条件回到 TOPMOST 组。
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool yielded = m_zOrderYielded.load(std::memory_order_acquire);
    if (overlayPresentShouldForceTopmost((exStyle & WS_EX_TOPMOST) != 0, yielded)) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return true;
}

void GestureTrailOverlay::hideToastWindow() {
    if (m_toastHwnd && IsWindowVisible(m_toastHwnd)) {
        ShowWindow(m_toastHwnd, SW_HIDE);
    }
}

void GestureTrailOverlay::releaseToastSurfaceLocked() {
    if (m_toastDC && m_toastOldBitmap) {
        SelectObject(m_toastDC, m_toastOldBitmap);
    }
    m_toastOldBitmap = nullptr;
    if (m_toastBitmap) {
        DeleteObject(m_toastBitmap);
        m_toastBitmap = nullptr;
    }
    if (m_toastDC) {
        DeleteDC(m_toastDC);
        m_toastDC = nullptr;
    }
    m_toastWidth = 0;
    m_toastHeight = 0;
}

bool GestureTrailOverlay::ensureToastSurfaceLocked(int width, int height) {
    if (width <= 0 || height <= 0 || !m_toastHwnd) return false;
    if (m_toastDC && m_toastBitmap && m_toastWidth == width && m_toastHeight == height) {
        return true;
    }

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    if (!m_toastDC) {
        m_toastDC = CreateCompatibleDC(hdcScreen);
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);
    if (!m_toastDC || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        return false;
    }
    HBITMAP selected = static_cast<HBITMAP>(SelectObject(m_toastDC, bmp));
    if (m_toastBitmap && selected == m_toastBitmap) {
        DeleteObject(m_toastBitmap);
    } else if (selected && selected != HGDI_ERROR && !m_toastOldBitmap) {
        m_toastOldBitmap = selected;
    }
    m_toastBitmap = bmp;
    m_toastWidth = width;
    m_toastHeight = height;
    return true;
}

bool GestureTrailOverlay::fitSurface(int left, int top, int right, int bottom) {
    if (m_virtualW <= 0 || m_virtualH <= 0) {
        m_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        m_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        m_virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        m_virtualH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    constexpr int kGrid = 128;
    constexpr int kPad = 72;
    constexpr int kMin = 256;
    left = snapDown(left - kPad, kGrid);
    top = snapDown(top - kPad, kGrid);
    right = snapUp(right + kPad, kGrid);
    bottom = snapUp(bottom + kPad, kGrid);

    left = (std::max)(left, m_virtualX);
    top = (std::max)(top, m_virtualY);
    right = (std::min)(right, m_virtualX + m_virtualW);
    bottom = (std::min)(bottom, m_virtualY + m_virtualH);
    if (right - left < kMin) right = left + kMin;
    if (bottom - top < kMin) bottom = top + kMin;
    if (right > m_virtualX + m_virtualW) {
        right = m_virtualX + m_virtualW;
        left = (std::max)(m_virtualX, right - (std::max)(kMin, right - left));
    }
    if (bottom > m_virtualY + m_virtualH) {
        bottom = m_virtualY + m_virtualH;
        top = (std::max)(m_virtualY, bottom - (std::max)(kMin, bottom - top));
    }

    const int neededW = (std::max)(1, right - left);
    const int neededH = (std::max)(1, bottom - top);
    const bool live = m_strokeSurfaceLive.load(std::memory_order_relaxed);
    if (live) {
        growOverlayRect(left, top, right, bottom, m_originX, m_originY, m_width, m_height);
        if (overlaySurfaceContains(left, top, right, bottom,
                                   m_originX, m_originY, m_width, m_height) &&
            m_renderTarget && m_memoryBitmap) {
            return true;
        }
        const bool ok = recreateBitmapLocked(
            left, top, (std::max)(1, right - left), (std::max)(1, bottom - top));
        if (ok) m_strokeSurfaceLive.store(true, std::memory_order_relaxed);
        return ok;
    }

    if (overlaySurfaceContains(left, top, right, bottom,
                               m_originX, m_originY, m_width, m_height) &&
        overlayCanReuseSurface(neededW, neededH, m_width, m_height, 2) &&
        m_renderTarget && m_memoryBitmap) {
        m_strokeSurfaceLive.store(true, std::memory_order_relaxed);
        return true;
    }

    const bool ok = recreateBitmapLocked(left, top, neededW, neededH);
    if (ok) m_strokeSurfaceLive.store(true, std::memory_order_relaxed);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口创建
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createOverlayWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = overlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = OVERLAY_CLASS;
    RegisterClassExW(&wc);

    m_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_virtualH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    m_originX = m_virtualX;
    m_originY = m_virtualY;
    m_width = 256;
    m_height = 256;

    m_helperOwnerHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"EasyTools_GestureTrailHelperOwner",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
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
    if (!easy::core::WinUtils::excludeWindowFromCapture(m_hwnd)) {
        LOG_WARN("当前 Windows 版本无法从捕获中排除手势轨迹窗口: error={}", GetLastError());
    }
    // HWND 需要非零尺寸才能创建；追踪表面从 0 开始，避免把虚拟屏左上角的 256×256
    // 占位框并进第一笔轨迹，把覆盖层钉死在屏幕角落。
    m_width = 0;
    m_height = 0;

    m_toastHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS,
        L"EasyTools Gesture Toast",
        WS_POPUP,
        0, 0, 64, 64,
        // Toast 归轨迹窗口所有。Win32 保证 owned window 始终位于 owner 上方，
        // 即使两者的像素表面在相邻时刻更新，也不会让轨迹盖住 Toast。
        m_hwnd,
        nullptr,
        hInstance,
        this
    );
    if (m_toastHwnd) {
        SetWindowLongPtrW(m_toastHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (!easy::core::WinUtils::excludeWindowFromCapture(m_toastHwnd)) {
            LOG_WARN("当前 Windows 版本无法从捕获中排除手势卡片窗口: error={}", GetLastError());
        }
        ShowWindow(m_toastHwnd, SW_HIDE);
    } else {
        LOG_WARN("创建手势结果卡片窗口失败，轨迹仍可绘制");
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct2D 资源管理
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::createD2DResources() {
    if (m_renderTarget && m_memoryDC && m_memoryBitmap && m_lineBrush && m_textBorderBrush) return true;

    auto fail = [this]() {
        releaseD2DResourcesLocked();
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
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE
    );

    hr = m_d2dFactory->CreateDCRenderTarget(&rtProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return fail();

    if (!m_memoryDC) {
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
    }
    
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

    applyThemeColorsLocked();
    m_themeDirty.store(false, std::memory_order_release);

    if (!m_lineBrush || !m_headCoreBrush) return fail();
    ensureToastTargetLocked();
    return true;
}

bool GestureTrailOverlay::ensureToastTargetLocked() {
    if (m_toastTarget) return true;
    if (!m_d2dFactory || !m_toastHwnd) return false;
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0.0f, 0.0f,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE
    );
    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&rtProps, m_toastTarget.GetAddressOf())) ||
        !m_toastTarget) {
        m_toastTarget.Reset();
        return false;
    }
    m_toastTarget->SetDpi(96.0f, 96.0f);
    applyThemeColorsLocked();
    return m_textBrush && m_textBorderBrush;
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
    releaseD2DResourcesLocked();
}

void GestureTrailOverlay::releaseD2DResourcesLocked() {
    m_smoothPathGeometry.Reset();
    m_headCoreBrush.Reset();
    m_outlineBrush.Reset();
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
    m_toastTarget.Reset();
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
    releaseToastSurfaceLocked();
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

bool GestureTrailOverlay::render() {
    std::lock_guard lock(m_renderMutex);

    std::vector<TrailPoint> points;
    std::string resultText;
    const bool isRecognized = m_isRecognized.load(std::memory_order_relaxed);
    {
        std::lock_guard trailLock(m_trailMutex);
        if (m_points.empty()) return false;
        points = m_points;
        resultText = m_resultText;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    m_dpiScale = easy::core::dpi::scaleAtPoint(cursor);
    const HMONITOR toastMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    const RECT toastWork = easy::core::dpi::workArea(toastMonitor);
    const float toastScale = easy::core::dpi::scaleForMonitor(toastMonitor);
    const int toastCenterX = (toastWork.left + toastWork.right) / 2;
    const int toastCenterY = toastWork.top +
        static_cast<int>(static_cast<float>(toastWork.bottom - toastWork.top) * 0.82f);

    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    for (const auto& p : points) {
        left = (std::min)(left, static_cast<int>(p.x));
        top = (std::min)(top, static_cast<int>(p.y));
        right = (std::max)(right, static_cast<int>(p.x) + 1);
        bottom = (std::max)(bottom, static_cast<int>(p.y) + 1);
    }
    if (!fitSurface(left, top, right, bottom)) return false;

    {
        std::lock_guard trailLock(m_trailMutex);
        if (m_points.empty()) return false;
        points = m_points;
        resultText = m_resultText;
    }

    if (!m_renderTarget || !m_lineBrush) {
        if (!createD2DResources()) return false;
    }
    if (m_themeDirty.exchange(false, std::memory_order_acq_rel)) {
        applyThemeColorsLocked();
    }
    if (!m_renderTarget || !m_lineBrush || !m_memoryDC) return false;

    RECT memRect = {0, 0, m_width, m_height};
    if (FAILED(m_renderTarget->BindDC(m_memoryDC, &memRect))) return false;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));  // 完全透明背景

    ID2D1SolidColorBrush* activeGlow = isRecognized ? m_glowBrush.Get() : m_greyGlowBrush.Get();
    ID2D1SolidColorBrush* activeLine = isRecognized ? m_lineBrush.Get() : m_greyLineBrush.Get();

    auto getPt = [&](size_t idx) -> D2D1_POINT_2F {
        return D2D1::Point2F(points[idx].x - m_originX, points[idx].y - m_originY);
    };

    // DrawGeometry 描边在 layered 预乘 alpha 下经常写出 RGB 却不写 A，整条线变全透明。
    // 轮盘菜单能显示，是因为它用 FillGeometry。轨迹同样：把折线 Widen 成一条带子再填色。
    if (points.size() >= 2 && m_d2dFactory) {
        ComPtr<ID2D1PathGeometry> linePath;
        if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(linePath.GetAddressOf())) && linePath) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(linePath->Open(sink.GetAddressOf())) && sink) {
                sink->BeginFigure(getPt(0), D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < points.size(); ++i) {
                    sink->AddLine(getPt(i));
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (SUCCEEDED(sink->Close())) {
                    auto fillWidened = [&](ID2D1SolidColorBrush* brush, float width, float opacity) {
                        if (!brush || width <= 0.0f) return;
                        ComPtr<ID2D1PathGeometry> fat;
                        if (FAILED(m_d2dFactory->CreatePathGeometry(fat.GetAddressOf())) || !fat) return;
                        ComPtr<ID2D1GeometrySink> fatSink;
                        if (FAILED(fat->Open(fatSink.GetAddressOf())) || !fatSink) return;
                        const HRESULT widenHr = linePath->Widen(
                            width, m_strokeStyle.Get(), D2D1::Matrix3x2F::Identity(), 1.0f,
                            fatSink.Get());
                        if (FAILED(fatSink->Close()) || FAILED(widenHr)) return;
                        brush->SetOpacity(opacity);
                        m_renderTarget->FillGeometry(fat.Get(), brush);
                    };
                    const float coreW = (std::max)(m_style.lineWidth * m_dpiScale, 4.0f);
                    const float outlineW = clampTrailOutlineWidth(m_style.outlineWidth) * m_dpiScale;
                    const float whiteW = trailOutlineWidenWidth(coreW, outlineW);
                    if (whiteW > 0.0f) {
                        fillWidened(m_outlineBrush.Get(), whiteW, 0.96f * m_fadeAlpha);
                        fillWidened(activeGlow, coreW * 1.35f, 0.22f * m_fadeAlpha);
                    } else {
                        fillWidened(activeGlow, coreW * 2.4f, 0.28f * m_fadeAlpha);
                    }
                    fillWidened(activeLine, coreW, 0.96f * m_fadeAlpha);
                }
            }
        }
        if (m_headCoreBrush) {
            const float headR = (std::max)(m_style.lineWidth * m_dpiScale * 0.55f, 3.0f);
            const float outlineW = clampTrailOutlineWidth(m_style.outlineWidth) * m_dpiScale;
            if (m_outlineBrush && outlineW > 0.0f) {
                m_outlineBrush->SetOpacity(m_fadeAlpha);
                m_renderTarget->FillEllipse(
                    D2D1::Ellipse(getPt(points.size() - 1), headR + outlineW, headR + outlineW),
                    m_outlineBrush.Get());
            }
            m_headCoreBrush->SetOpacity(m_fadeAlpha);
            m_renderTarget->FillEllipse(
                D2D1::Ellipse(getPt(points.size() - 1), headR, headR), m_headCoreBrush.Get());
        }
    }

    if (FAILED(m_renderTarget->EndDraw())) {
        LOG_WARN("手势轨迹 Direct2D 帧提交失败");
        return false;
    }

    if (!presentLayeredLocked(m_hwnd, m_memoryDC, m_originX, m_originY, m_width, m_height)) {
        return false;
    }
    m_visible.store(true, std::memory_order_release);

    const bool isExcessive = (resultText == "•••");
    const bool shouldShowToast = shouldShowGestureResultToast(
        isRecognized, !resultText.empty(), isExcessive);
    bool toastOk = true;
    if (shouldShowToast) {
        toastOk = presentToastLocked(resultText, isRecognized, isExcessive,
                                     toastCenterX, toastCenterY, toastScale);
    } else {
        hideToastWindow();
    }

    const uint64_t epoch = m_trailEpoch.load(std::memory_order_relaxed);
    if (m_loggedPresentEpoch != epoch) {
        m_loggedPresentEpoch = epoch;
        LOG_INFO("手势轨迹已提交: points={}, {}x{}", points.size(), m_width, m_height);
    }
    return gestureFrameReadyToFade(true, shouldShowToast, toastOk);
}

bool GestureTrailOverlay::presentToastLocked(const std::string& resultText, bool recognized,
                                             bool excessive, int toastCenterX, int toastCenterY,
                                             float toastScale) {
    (void)recognized;
    if (!m_toastHwnd) return false;
    if (!ensureToastTargetLocked() || !m_toastTarget) return false;

    const int toastW = static_cast<int>(400.0f * toastScale);
    const int toastH = static_cast<int>(120.0f * toastScale);
    if (!ensureToastSurfaceLocked(toastW, toastH)) return false;

    m_toastOriginX = toastCenterX - toastW / 2;
    m_toastOriginY = toastCenterY - toastH / 2;
    RECT toastRect = {0, 0, toastW, toastH};
    if (FAILED(m_toastTarget->BindDC(m_toastDC, &toastRect))) return false;

    m_toastTarget->BeginDraw();
    m_toastTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float resultScale = toastScale;
    const bool hasTextFormat = updateTextFormat(resultScale);
    const float centerX = static_cast<float>(toastW) * 0.5f;
    const float centerY = static_cast<float>(toastH) * 0.5f;

    if (excessive) {
        float boxW = 126.0f * resultScale;
        float boxH = 58.0f * resultScale;
        D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
            D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                        centerX + boxW / 2.0f, centerY + boxH / 2.0f),
            16.0f * resultScale, 16.0f * resultScale);
        if (m_excessiveBgBrush) {
            m_excessiveBgBrush->SetOpacity(0.94f * m_fadeAlpha);
            m_toastTarget->FillRoundedRectangle(&rrect, m_excessiveBgBrush.Get());
        }
        if (m_excessiveBorderBrush) {
            m_excessiveBorderBrush->SetOpacity(0.95f * m_fadeAlpha);
            m_toastTarget->DrawRoundedRectangle(
                &rrect, m_excessiveBorderBrush.Get(), 2.6f * resultScale);
        }
        if (m_excessiveDotBrush) {
            m_excessiveDotBrush->SetOpacity(m_fadeAlpha);
            const float dotRadius = 6.0f * resultScale;
            const float dotSpacing = 22.0f * resultScale;
            m_toastTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(centerX - dotSpacing, centerY), dotRadius, dotRadius),
                m_excessiveDotBrush.Get());
            m_toastTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(centerX, centerY), dotRadius, dotRadius),
                m_excessiveDotBrush.Get());
            m_toastTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(centerX + dotSpacing, centerY), dotRadius, dotRadius),
                m_excessiveDotBrush.Get());
        }
    } else {
        const std::wstring wText = hasTextFormat
            ? easy::core::WinUtils::utf8ToWstring(resultText) : std::wstring{};
        if (hasTextFormat && !wText.empty() && m_dwriteFactory) {
            ComPtr<IDWriteTextLayout> layout;
            m_dwriteFactory->CreateTextLayout(
                wText.c_str(), static_cast<UINT32>(wText.length()),
                m_textFormat.Get(),
                10000.0f, 1000.0f,
                layout.GetAddressOf());

            float boxW = 140.0f * resultScale;
            float boxH = 58.0f * resultScale;
            if (layout) {
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    float paddingX = 38.0f * resultScale;
                    float paddingY = 16.0f * resultScale;
                    boxW = (std::max)(metrics.width + paddingX * 2.0f, 136.0f * resultScale);
                    boxH = (std::max)(metrics.height + paddingY * 2.0f, 58.0f * resultScale);
                }
            }

            D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(
                D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                            centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                16.0f * resultScale, 16.0f * resultScale);
            if (m_fading.load(std::memory_order_relaxed) && m_themeBgBrush) {
                m_themeBgBrush->SetOpacity(0.95f * m_fadeAlpha);
                m_toastTarget->FillRoundedRectangle(&rrect, m_themeBgBrush.Get());
            } else if (m_textBgBrush) {
                m_textBgBrush->SetOpacity(0.82f * m_fadeAlpha);
                m_toastTarget->FillRoundedRectangle(&rrect, m_textBgBrush.Get());
            }
            if (m_textBorderBrush) {
                m_textBorderBrush->SetOpacity(0.95f * m_fadeAlpha);
                m_toastTarget->DrawRoundedRectangle(
                    &rrect, m_textBorderBrush.Get(), 2.6f * resultScale);
            }
            if (m_textBrush) {
                m_textBrush->SetOpacity(m_fadeAlpha);
                m_toastTarget->DrawText(
                    wText.c_str(),
                    static_cast<UINT32>(wText.size()),
                    m_textFormat.Get(),
                    D2D1::RectF(centerX - boxW / 2.0f, centerY - boxH / 2.0f,
                                centerX + boxW / 2.0f, centerY + boxH / 2.0f),
                    m_textBrush.Get());
            }
        }
    }

    if (FAILED(m_toastTarget->EndDraw())) {
        LOG_WARN("手势结果卡片 Direct2D 帧提交失败");
        return false;
    }
    return presentLayeredLocked(
        m_toastHwnd, m_toastDC, m_toastOriginX, m_toastOriginY, m_toastWidth, m_toastHeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// 窗口过程
// ─────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK GestureTrailOverlay::overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<GestureTrailOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {

        case WM_GETOBJECT:
            if (self && hwnd == self->m_toastHwnd) {
                return easy::core::accessibility::respondToOverlayUiaGetObject(
                    hwnd, wParam, lParam,
                    {L"EasyTools.GestureResult", L"Recognized mouse gesture result",
                     easy::core::accessibility::OverlayUiaRole::Text, true});
            }
            return easy::core::accessibility::respondToOverlayUiaGetObject(
                hwnd, wParam, lParam,
                {L"EasyTools.GestureTrail", L"Mouse gesture trail",
                 easy::core::accessibility::OverlayUiaRole::Pane, false});

        case WM_GESTURE_ACCESSIBILITY_RESULT:
            if (self && hwnd == self->m_toastHwnd) {
                std::string result;
                {
                    std::lock_guard lock(self->m_trailMutex);
                    result = self->m_resultText;
                }
                if (!result.empty()) {
                    easy::core::accessibility::announceOverlay(
                        hwnd, easy::core::WinUtils::utf8ToWstring(result));
                }
            }
            return 0;

        case WM_DISPLAYCHANGE: {
            if (self) {
                self->m_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                self->m_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                self->m_virtualW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                self->m_virtualH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            }
            return 0;
        }

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_NCDESTROY:
            easy::core::accessibility::disconnectOverlayUiaProvider(hwnd);
            return DefWindowProcW(hwnd, msg, wParam, lParam);

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace easy::gesture
