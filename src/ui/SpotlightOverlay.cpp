#include "ui/SpotlightOverlay.h"
#include "core/logger/Logger.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"
#include "gesture/GestureInputPolicy.h"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")

namespace easy::ui {

static constexpr const wchar_t* SPOTLIGHT_CLASS = L"EasyTools_SpotlightOverlay";
static constexpr UINT_PTR TIMER_ANIM_ID = 201;
static constexpr int ANIM_INTERVAL_MS = 16; // 60 FPS

SpotlightOverlay& SpotlightOverlay::instance() {
    static SpotlightOverlay inst;
    return inst;
}

D2D1_COLOR_F SpotlightOverlay::parseColor(const std::string& hexStr, float alpha) const {
    std::string s = hexStr;
    if (s.empty() || s == "auto") {
        std::string accent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "violet");
        if (accent == "blue") s = "#3b82f6";
        else if (accent == "cyan") s = "#06b6d4";
        else if (accent == "amber") s = "#f59e0b";
        else if (accent == "mint") s = "#10b981";
        else if (accent == "coral") s = "#f43f5e";
        else s = "#8b5cf6"; // violet
    }
    if (!s.empty() && s.front() == '#') {
        s = s.substr(1);
    }
    if (s.length() == 6) {
        try {
            unsigned long val = std::stoul(s, nullptr, 16);
            float r = ((val >> 16) & 0xFF) / 255.0f;
            float g = ((val >> 8) & 0xFF) / 255.0f;
            float b = (val & 0xFF) / 255.0f;
            return D2D1::ColorF(r, g, b, alpha);
        } catch (...) {
            // fallback
        }
    }
    return D2D1::ColorF(0.545f, 0.361f, 0.965f, alpha);
}

bool SpotlightOverlay::initialize(HINSTANCE hInstance) {
    auto& cfg = easy::core::ConfigManager::instance();
    m_settings.enabled = cfg.get<bool>("/spotlight/enabled", true);
    m_settings.triggerDoubleCtrl = cfg.get<bool>("/spotlight/triggerDoubleCtrl", true);
    m_settings.triggerShakeMouse = cfg.get<bool>("/spotlight/triggerShakeMouse", false);
    m_settings.autoBypassFullscreen = cfg.get<bool>("/spotlight/autoBypassFullscreen", true);
    m_settings.spotlightColor = cfg.get<std::string>("/spotlight/spotlightColor", "auto");
    m_settings.spotlightSize = cfg.get<int>("/spotlight/spotlightSize", 200);
    m_settings.animationDurationMs = cfg.get<int>("/spotlight/animationDurationMs", 1000);
    m_settings.holdDurationMs = cfg.get<int>("/spotlight/holdDurationMs", 800);
    m_settings.shakeThreshold = cfg.get<int>("/spotlight/shakeThreshold", 7);

    m_settings.clickRippleEnabled = cfg.get<bool>("/spotlight/clickRippleEnabled", false);
    m_settings.mouseTrailEnabled = cfg.get<bool>("/spotlight/mouseTrailEnabled", false);
    m_settings.leftClickColor = cfg.get<std::string>("/spotlight/leftClickColor", "auto");
    m_settings.rightClickColor = cfg.get<std::string>("/spotlight/rightClickColor", "#fb7185");
    m_settings.middleClickColor = cfg.get<std::string>("/spotlight/middleClickColor", "#fbbf24");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = SPOTLIGHT_CLASS;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        SPOTLIGHT_CLASS, L"",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    easy::core::WinUtils::excludeWindowFromCapture(m_hwnd);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    easy::core::EventBus::instance().subscribe<easy::core::MouseActivityEvent>(
        [this](const easy::core::MouseActivityEvent& e) {
            POINT pt{e.x, e.y};
            if (e.button == -1) {
                onMouseMove(pt);
            } else {
                onMouseDown(e.button, pt);
            }
        }
    );

    LOG_INFO("鼠标聚光灯与演示特效 Overlay 初始化完成");
    return true;
}

void SpotlightOverlay::shutdown() {
    discardResources();
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_ANIM_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_d2dFactory.Reset();
}

void SpotlightOverlay::updateSettings(const SpotlightSettings& settings) {
    std::lock_guard lock(m_mutex);
    m_settings = settings;
    auto& cfg = easy::core::ConfigManager::instance();
    cfg.set("/spotlight/enabled", m_settings.enabled);
    cfg.set("/spotlight/triggerDoubleCtrl", m_settings.triggerDoubleCtrl);
    cfg.set("/spotlight/triggerShakeMouse", m_settings.triggerShakeMouse);
    cfg.set("/spotlight/autoBypassFullscreen", m_settings.autoBypassFullscreen);
    cfg.set("/spotlight/spotlightColor", m_settings.spotlightColor);
    cfg.set("/spotlight/spotlightSize", m_settings.spotlightSize);
    cfg.set("/spotlight/animationDurationMs", m_settings.animationDurationMs);
    cfg.set("/spotlight/holdDurationMs", m_settings.holdDurationMs);
    cfg.set("/spotlight/shakeThreshold", m_settings.shakeThreshold);

    cfg.set("/spotlight/clickRippleEnabled", m_settings.clickRippleEnabled);
    cfg.set("/spotlight/mouseTrailEnabled", m_settings.mouseTrailEnabled);
    cfg.set("/spotlight/leftClickColor", m_settings.leftClickColor);
    cfg.set("/spotlight/rightClickColor", m_settings.rightClickColor);
    cfg.set("/spotlight/middleClickColor", m_settings.middleClickColor);
}

SpotlightSettings SpotlightOverlay::getSettings() const {
    std::lock_guard lock(m_mutex);
    return m_settings;
}

void SpotlightOverlay::resetDefaults() {
    SpotlightSettings def;
    updateSettings(def);
}

bool SpotlightOverlay::isActive() const {
    std::lock_guard lock(m_mutex);
    return m_animState != AnimState::Idle || !m_ripples.empty() || !m_trail.empty();
}

void SpotlightOverlay::trigger(POINT pt, bool autoFetch) {
    std::lock_guard lock(m_mutex);
    if (!m_settings.enabled) return;

    if (m_settings.autoBypassFullscreen) {
        HWND fg = GetForegroundWindow();
        if (fg && easy::core::WinUtils::isWindowFullscreen(fg)) {
            const std::wstring classWide = easy::core::WinUtils::getWindowClassName(fg);
            if (easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide))) {
                LOG_INFO("前台处于全屏独占应用，自动免打扰跳过鼠标聚光灯触发: hwnd=0x{:X}", reinterpret_cast<uintptr_t>(fg));
                return;
            }
        }
    }

    if (autoFetch || (pt.x == 0 && pt.y == 0)) {
        GetCursorPos(&pt);
    }
    m_targetPos = pt;

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (!ensureSurface(vw, vh)) return;

    SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, vw, vh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    m_animState = AnimState::FadeIn;
    m_animStartTime = std::chrono::steady_clock::now();
    m_currentAlpha = 0.05f;

    if (!m_timerRunning) {
        SetTimer(m_hwnd, TIMER_ANIM_ID, ANIM_INTERVAL_MS, nullptr);
        m_timerRunning = true;
    }
    render();
}

void SpotlightOverlay::dismiss() {
    std::lock_guard lock(m_mutex);
    if (m_animState == AnimState::Idle) return;
    m_animState = AnimState::FadeOut;
    m_animStartTime = std::chrono::steady_clock::now();
}

void SpotlightOverlay::onKeyboardEvent(DWORD vkCode, WPARAM wParam) {
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        if (m_animState != AnimState::Idle) {
            dismiss();
            return;
        }

        if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) {
            if (!m_settings.enabled || !m_settings.triggerDoubleCtrl) return;

            auto now = std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCtrlDownTime).count();
            if (diff <= 300) {
                m_ctrlPressCount++;
                if (m_ctrlPressCount >= 2) {
                    m_ctrlPressCount = 0;
                    m_lastCtrlDownTime = {};
                    trigger();
                    return;
                }
            } else {
                m_ctrlPressCount = 1;
            }
            m_lastCtrlDownTime = now;
        } else {
            m_ctrlPressCount = 0;
        }
    }
}

void SpotlightOverlay::onMouseDown(int button, POINT pt) {
    std::lock_guard lock(m_mutex);

    // 聚光灯显示期间点击鼠标，提前平滑淡出
    if (m_animState != AnimState::Idle) {
        m_animState = AnimState::FadeOut;
        m_animStartTime = std::chrono::steady_clock::now();
    }

    if (!m_settings.clickRippleEnabled) return;

    if (m_settings.autoBypassFullscreen) {
        HWND fg = GetForegroundWindow();
        if (fg && easy::core::WinUtils::isWindowFullscreen(fg)) {
            const std::wstring classWide = easy::core::WinUtils::getWindowClassName(fg);
            if (easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide))) {
                return;
            }
        }
    }

    std::string color = m_settings.leftClickColor;
    if (button == 1) color = m_settings.rightClickColor;
    else if (button == 2) color = m_settings.middleClickColor;

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (!ensureSurface(vw, vh)) return;

    SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, vw, vh,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    ClickRipple rip;
    rip.pt = pt;
    rip.startTime = std::chrono::steady_clock::now();
    rip.color = color;
    rip.maxRadius = 52.0f;
    rip.durationMs = 450.0f;
    m_ripples.push_back(rip);

    if (!m_timerRunning) {
        SetTimer(m_hwnd, TIMER_ANIM_ID, ANIM_INTERVAL_MS, nullptr);
        m_timerRunning = true;
    }
}

void SpotlightOverlay::onMouseMove(POINT pt) {
    std::lock_guard lock(m_mutex);

    auto now = std::chrono::steady_clock::now();

    // 1. 鼠标轨迹特效记录
    if (m_settings.mouseTrailEnabled) {
        bool bypass = false;
        if (m_settings.autoBypassFullscreen) {
            HWND fg = GetForegroundWindow();
            if (fg && easy::core::WinUtils::isWindowFullscreen(fg)) {
                const std::wstring classWide = easy::core::WinUtils::getWindowClassName(fg);
                bypass = easy::gesture::shouldAutoBypassFullscreenGestures(true, easy::gesture::isProductivityToolkitClassName(classWide));
            }
        }

        if (!bypass) {
            float dist = 0.0f;
            if (!m_trail.empty()) {
                float dx = static_cast<float>(pt.x - m_trail.back().pt.x);
                float dy = static_cast<float>(pt.y - m_trail.back().pt.y);
                dist = std::sqrt(dx * dx + dy * dy);
            } else {
                dist = 100.0f;
            }

            if (dist > 6.0f) {
                int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
                int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

                if (ensureSurface(vw, vh)) {
                    SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, vw, vh,
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.size = 9.0f;
                    p.durationMs = 380.0f;
                    p.color = m_settings.spotlightColor;
                    m_trail.push_back(p);

                    // 限制最大粒子队列
                    if (m_trail.size() > 40) {
                        m_trail.erase(m_trail.begin(), m_trail.begin() + (m_trail.size() - 40));
                    }

                    if (!m_timerRunning) {
                        SetTimer(m_hwnd, TIMER_ANIM_ID, ANIM_INTERVAL_MS, nullptr);
                        m_timerRunning = true;
                    }
                }
            }
        }
    }

    // 2. 摇晃鼠标寻找光标检测
    if (m_animState == AnimState::Idle && m_settings.enabled && m_settings.triggerShakeMouse) {
        if (m_lastMousePos.x == 0 && m_lastMousePos.y == 0) {
            m_lastMousePos = pt;
            m_shakeWindowStart = now;
            return;
        }

        int dx = pt.x - m_lastMousePos.x;
        m_lastMousePos = pt;

        if (std::abs(dx) > 30) {
            int dir = (dx > 0) ? 1 : -1;
            if (m_lastMoveDir != 0 && dir != m_lastMoveDir) {
                auto windowElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_shakeWindowStart).count();
                if (windowElapsed > 600) {
                    m_shakeReversals = 1;
                    m_shakeWindowStart = now;
                } else {
                    m_shakeReversals++;
                    int threshold = std::max(3, m_settings.shakeThreshold);
                    if (m_shakeReversals >= threshold) {
                        m_shakeReversals = 0;
                        m_shakeWindowStart = {};
                        // 触发聚光灯
                        trigger(pt, false);
                    }
                }
            }
            m_lastMoveDir = dir;
        }
    }
}

void SpotlightOverlay::tickAnimation() {
    std::lock_guard lock(m_mutex);

    auto now = std::chrono::steady_clock::now();

    // 1. 聚光灯动效更新
    if (m_animState != AnimState::Idle) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_animStartTime).count();
        const float fadeInDuration = 200.0f;
        const float fadeOutDuration = std::max(200.0f, static_cast<float>(m_settings.animationDurationMs) * 0.5f);

        if (m_animState == AnimState::FadeIn) {
            m_currentAlpha = std::clamp(static_cast<float>(elapsedMs) / fadeInDuration, 0.0f, 1.0f);
            if (elapsedMs >= fadeInDuration) {
                m_currentAlpha = 1.0f;
                m_animState = AnimState::Holding;
                m_holdStartTime = now;
            }
        } else if (m_animState == AnimState::Holding) {
            m_currentAlpha = 1.0f;
            auto holdElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_holdStartTime).count();
            if (holdElapsed >= m_settings.holdDurationMs) {
                m_animState = AnimState::FadeOut;
                m_animStartTime = now;
            }
        } else if (m_animState == AnimState::FadeOut) {
            m_currentAlpha = std::clamp(1.0f - (static_cast<float>(elapsedMs) / fadeOutDuration), 0.0f, 1.0f);
            if (elapsedMs >= fadeOutDuration || m_currentAlpha <= 0.01f) {
                m_animState = AnimState::Idle;
                m_currentAlpha = 0.0f;
            }
        }
    }

    // 2. 清理过期的点击水波纹
    m_ripples.erase(
        std::remove_if(m_ripples.begin(), m_ripples.end(), [now](const ClickRipple& r) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - r.startTime).count();
            return el >= r.durationMs;
        }),
        m_ripples.end()
    );

    // 3. 清理过期的轨迹粒子
    m_trail.erase(
        std::remove_if(m_trail.begin(), m_trail.end(), [now](const TrailParticle& p) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            return el >= p.durationMs;
        }),
        m_trail.end()
    );

    // 4. 判断是否全部活动已结束
    if (m_animState == AnimState::Idle && m_ripples.empty() && m_trail.empty()) {
        hideNow();
        return;
    }

    render();
}

void SpotlightOverlay::hideNow() {
    m_animState = AnimState::Idle;
    m_currentAlpha = 0.0f;
    m_ripples.clear();
    m_trail.clear();
    if (m_timerRunning) {
        KillTimer(m_hwnd, TIMER_ANIM_ID);
        m_timerRunning = false;
    }
    ShowWindow(m_hwnd, SW_HIDE);
    releaseSurface();
    discardResources();
    // 冷路径释放物理内存
    easy::core::WinUtils::trimWorkingSet();
}

bool SpotlightOverlay::ensureSurface(int width, int height) {
    if (m_memDc && m_memBmp && m_surfaceW == width && m_surfaceH == height) {
        return true;
    }
    releaseSurface();

    HDC screenDc = GetDC(nullptr);
    m_memDc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);

    if (!m_memDc) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // Top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    m_memBmp = CreateDIBSection(m_memDc, &bi, DIB_RGB_COLORS, &m_bmpBits, nullptr, 0);
    if (!m_memBmp) {
        DeleteDC(m_memDc);
        m_memDc = nullptr;
        return false;
    }

    m_oldBmp = reinterpret_cast<HBITMAP>(SelectObject(m_memDc, m_memBmp));
    m_surfaceW = width;
    m_surfaceH = height;
    return true;
}

void SpotlightOverlay::releaseSurface() {
    if (m_memDc) {
        if (m_oldBmp) {
            SelectObject(m_memDc, m_oldBmp);
            m_oldBmp = nullptr;
        }
        if (m_memBmp) {
            DeleteObject(m_memBmp);
            m_memBmp = nullptr;
        }
        DeleteDC(m_memDc);
        m_memDc = nullptr;
    }
    m_bmpBits = nullptr;
    m_surfaceW = 0;
    m_surfaceH = 0;
}

bool SpotlightOverlay::createResources() {
    if (m_dcRenderTarget) return true;
    if (!m_d2dFactory) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, m_dcRenderTarget.GetAddressOf()))) {
        return false;
    }
    return true;
}

void SpotlightOverlay::discardResources() {
    m_dcRenderTarget.Reset();
}

void SpotlightOverlay::render() {
    if (!m_hwnd || !m_memDc || m_surfaceW <= 0 || m_surfaceH <= 0) return;
    if (!createResources()) return;

    RECT rc = {0, 0, m_surfaceW, m_surfaceH};
    if (FAILED(m_dcRenderTarget->BindDC(m_memDc, &rc))) return;

    m_dcRenderTarget->BeginDraw();
    m_dcRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0.0f));

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    auto now = std::chrono::steady_clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    // 1. 绘制聚光灯 (全屏半透明暗化 + 鼠标镂空 + 发光环)
    // ─────────────────────────────────────────────────────────────────────────
    if (m_animState != AnimState::Idle && m_currentAlpha > 0.01f) {
        float localCenterX = static_cast<float>(m_targetPos.x - vx);
        float localCenterY = static_cast<float>(m_targetPos.y - vy);
        float radius = static_cast<float>(std::max(40, m_settings.spotlightSize)) / 2.0f;
        float alpha = m_currentAlpha;

        Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> screenGeo;
        Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> holeGeo;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> combinedGeo;

        D2D1_RECT_F fullRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_surfaceW), static_cast<float>(m_surfaceH));
        m_d2dFactory->CreateRectangleGeometry(fullRect, screenGeo.GetAddressOf());

        D2D1_ELLIPSE holeEllipse = D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), radius, radius);
        m_d2dFactory->CreateEllipseGeometry(holeEllipse, holeGeo.GetAddressOf());

        m_d2dFactory->CreatePathGeometry(combinedGeo.GetAddressOf());
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (combinedGeo && SUCCEEDED(combinedGeo->Open(sink.GetAddressOf()))) {
            screenGeo->CombineWithGeometry(holeGeo.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
            sink->Close();

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dimBrush;
            m_dcRenderTarget->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f * alpha),
                dimBrush.GetAddressOf()
            );
            if (dimBrush) {
                m_dcRenderTarget->FillGeometry(combinedGeo.Get(), dimBrush.Get());
            }
        }

        // 外发光光晕
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
        D2D1_COLOR_F baseColor = parseColor(m_settings.spotlightColor, 1.0f);
        D2D1_GRADIENT_STOP stops[3] = {
            { 0.0f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.45f * alpha) },
            { 0.7f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.15f * alpha) },
            { 1.0f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.0f) }
        };
        if (SUCCEEDED(m_dcRenderTarget->CreateGradientStopCollection(stops, 3, stopCollection.GetAddressOf()))) {
            float glowRadius = radius + 32.0f;
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES glowProps = D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(localCenterX, localCenterY),
                D2D1::Point2F(0, 0),
                glowRadius, glowRadius
            );
            Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> glowBrush;
            if (SUCCEEDED(m_dcRenderTarget->CreateRadialGradientBrush(glowProps, stopCollection.Get(), glowBrush.GetAddressOf()))) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), glowRadius, glowRadius), glowBrush.Get());
            }
        }

        // 高亮外轮廓圈
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ringBrush;
        m_dcRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.95f * alpha),
            ringBrush.GetAddressOf()
        );
        if (ringBrush) {
            m_dcRenderTarget->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), radius, radius),
                ringBrush.Get(),
                3.5f
            );
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. 绘制鼠标轨迹流光粒子 (图 5 所示)
    // ─────────────────────────────────────────────────────────────────────────
    for (const auto& p : m_trail) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
        float progress = std::clamp(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
        float alpha = (1.0f - progress) * 0.85f;
        float r = p.size * (1.0f - progress * 0.4f);

        float px = static_cast<float>(p.pt.x - vx);
        float py = static_cast<float>(p.pt.y - vy);

        D2D1_COLOR_F c = parseColor(p.color, alpha);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trailBrush;
        m_dcRenderTarget->CreateSolidColorBrush(c, trailBrush.GetAddressOf());
        if (trailBrush) {
            m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r, r), trailBrush.Get());
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3. 绘制鼠标点击水波纹光圈 (图 3, 图 4 所示)
    // ─────────────────────────────────────────────────────────────────────────
    for (const auto& rip : m_ripples) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - rip.startTime).count();
        float progress = std::clamp(static_cast<float>(el) / rip.durationMs, 0.0f, 1.0f);
        float alpha = (1.0f - progress);

        float cx = static_cast<float>(rip.pt.x - vx);
        float cy = static_cast<float>(rip.pt.y - vy);
        float currentRadius = 8.0f + rip.maxRadius * std::sin(progress * 1.57079f); // ease-out

        D2D1_COLOR_F baseC = parseColor(rip.color, 1.0f);

        // 内层中心实心亮点
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> centerBrush;
        m_dcRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(baseC.r, baseC.g, baseC.b, 0.8f * alpha),
            centerBrush.GetAddressOf()
        );
        if (centerBrush) {
            m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.0f * (1.0f - progress * 0.5f), 6.0f * (1.0f - progress * 0.5f)), centerBrush.Get());
        }

        // 外层水波扩散波纹圈 (双重同心光圈)
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> waveBrush1;
        m_dcRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(baseC.r, baseC.g, baseC.b, 0.9f * alpha),
            waveBrush1.GetAddressOf()
        );
        if (waveBrush1) {
            m_dcRenderTarget->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cx, cy), currentRadius, currentRadius),
                waveBrush1.Get(),
                2.5f * (1.0f - progress * 0.3f)
            );
        }

        // 第二道滞后辅助微波纹 (波纹层叠感)
        if (progress > 0.15f) {
            float subProgress = (progress - 0.15f) / 0.85f;
            float subRadius = 4.0f + (rip.maxRadius * 0.65f) * std::sin(subProgress * 1.57079f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> waveBrush2;
            m_dcRenderTarget->CreateSolidColorBrush(
                D2D1::ColorF(baseC.r, baseC.g, baseC.b, 0.5f * (1.0f - subProgress)),
                waveBrush2.GetAddressOf()
            );
            if (waveBrush2) {
                m_dcRenderTarget->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(cx, cy), subRadius, subRadius),
                    waveBrush2.Get(),
                    1.8f
                );
            }
        }
    }

    m_dcRenderTarget->EndDraw();

    // 4. UpdateLayeredWindow
    POINT ptSrc = {0, 0};
    POINT ptDst = {vx, vy};
    SIZE sizeDst = {m_surfaceW, m_surfaceH};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDc = GetDC(nullptr);
    UpdateLayeredWindow(
        m_hwnd, screenDc,
        &ptDst, &sizeDst,
        m_memDc, &ptSrc,
        0, &blend, ULW_ALPHA
    );
    ReleaseDC(nullptr, screenDc);
}

LRESULT CALLBACK SpotlightOverlay::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SpotlightOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ANIM_ID && self) {
            self->tickAnimation();
            return 0;
        }
        break;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::ui
