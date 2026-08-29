#include "ui/SpotlightOverlay.h"
#include "core/logger/Logger.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"
#include "gesture/GestureInputPolicy.h"

#include <algorithm>
#include <cmath>
#include <mmsystem.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "winmm.lib")

namespace easy::ui {

static constexpr const wchar_t* SPOTLIGHT_CLASS = L"EasyTools_SpotlightOverlay";
static constexpr UINT_PTR TIMER_ANIM_ID = 201;
static constexpr int ANIM_INTERVAL_MS = 16; // 60 FPS

SpotlightOverlay& SpotlightOverlay::instance() {
    static SpotlightOverlay inst;
    return inst;
}

D2D1_COLOR_F SpotlightOverlay::hslToRgb(float h, float s, float l, float alpha) {
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    float c = (1.0f - std::abs(2.0f * l - 1.0f)) * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = l - c / 2.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f) { r = c; g = x; b = 0.0f; }
    else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
    else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
    else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
    else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
    else { r = c; g = 0.0f; b = x; }
    return D2D1::ColorF(r + m, g + m, b + m, alpha);
}

D2D1_COLOR_F SpotlightOverlay::parseColor(const std::string& hexStr, float alpha) const {
    std::string s = hexStr;
    if (s.empty() || s == "auto") {
        std::string accent = easy::core::ConfigManager::instance().get<std::string>("/general/accentColor", "blue");
        if (accent == "blue") s = "#3b82f6";
        else if (accent == "cyan") s = "#06b6d4";
        else if (accent == "amber") s = "#f59e0b";
        else if (accent == "mint") s = "#10b981";
        else if (accent == "coral") s = "#f43f5e";
        else if (accent == "violet") s = "#8b5cf6";
        else s = "#3b82f6"; // 默认蓝色
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
    return D2D1::ColorF(0.231f, 0.510f, 0.965f, alpha);
}

bool SpotlightOverlay::initialize(HINSTANCE hInstance) {
    auto& cfg = easy::core::ConfigManager::instance();
    m_settings.enabled = cfg.get<bool>("/spotlight/enabled", true);
    m_settings.triggerDoubleCtrl = cfg.get<bool>("/spotlight/triggerDoubleCtrl", true);
    m_settings.triggerShakeMouse = cfg.get<bool>("/spotlight/triggerShakeMouse", false);
    m_settings.autoBypassFullscreen = cfg.get<bool>("/spotlight/autoBypassFullscreen", true);
    m_settings.spotlightColor = cfg.get<std::string>("/spotlight/spotlightColor", "auto");
    m_settings.spotlightSize = cfg.get<int>("/spotlight/spotlightSize", 300);
    m_settings.animationDurationMs = cfg.get<int>("/spotlight/animationDurationMs", 1000);
    m_settings.holdDurationMs = cfg.get<int>("/spotlight/holdDurationMs", 800);
    m_settings.shakeThreshold = cfg.get<int>("/spotlight/shakeThreshold", 4);
    m_settings.spotlightAnimStyle = cfg.get<std::string>("/spotlight/spotlightAnimStyle", "inward_gravity");

    m_settings.clickRippleEnabled = cfg.get<bool>("/spotlight/clickRippleEnabled", false);
    m_settings.clickRippleStyle = cfg.get<std::string>("/spotlight/clickRippleStyle", "sparkle_burst");
    m_settings.mouseTrailEnabled = cfg.get<bool>("/spotlight/mouseTrailEnabled", false);
    m_settings.mouseTrailStyle = cfg.get<std::string>("/spotlight/mouseTrailStyle", "sonar_pulses");
    m_settings.mouseTrailColorMode = cfg.get<std::string>("/spotlight/mouseTrailColorMode", "rainbow");
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

    m_helperOwnerHwnd = easy::core::WinUtils::createOverlayHelperOwner(hInstance, L"EasyTools_SpotlightHelperOwner");

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        SPOTLIGHT_CLASS, L"",
        WS_POPUP,
        0, 0, 1, 1,
        m_helperOwnerHwnd, nullptr, hInstance, nullptr
    );

    if (!m_hwnd) {
        if (m_helperOwnerHwnd) {
            DestroyWindow(m_helperOwnerHwnd);
            m_helperOwnerHwnd = nullptr;
        }
        return false;
    }

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    easy::core::WinUtils::applyTaskbarSafeOverlayStyle(m_hwnd, false);
    SetWindowDisplayAffinity(m_hwnd, WDA_NONE);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf()))) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        if (m_helperOwnerHwnd) {
            DestroyWindow(m_helperOwnerHwnd);
            m_helperOwnerHwnd = nullptr;
        }
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

    LOG_INFO("鼠标演示与特效 Overlay 初始化完成 (Taskbar Safe)");
    return true;
}

void SpotlightOverlay::shutdown() {
    discardResources();
    if (m_mmTimerId) {
        timeKillEvent(m_mmTimerId);
        m_mmTimerId = 0;
    }
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_ANIM_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_helperOwnerHwnd) {
        DestroyWindow(m_helperOwnerHwnd);
        m_helperOwnerHwnd = nullptr;
    }
    m_d2dFactory.Reset();
}

void SpotlightOverlay::updateSettings(const SpotlightSettings& settings) {
    std::lock_guard lock(m_mutex);
    m_settings = settings;
    if (!m_settings.enabled) {
        m_animState = AnimState::Idle;
        m_currentAlpha = 0.0f;
        m_ripples.clear();
        m_trail.clear();
        hideNow();
    } else {
        if (!m_settings.clickRippleEnabled) {
            m_ripples.clear();
        }
        if (!m_settings.mouseTrailEnabled) {
            m_trail.clear();
        }
        if (m_animState == AnimState::Idle && m_ripples.empty() && m_trail.empty()) {
            hideNow();
        }
    }

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
    cfg.set("/spotlight/spotlightAnimStyle", m_settings.spotlightAnimStyle);

    cfg.set("/spotlight/clickRippleEnabled", m_settings.clickRippleEnabled);
    cfg.set("/spotlight/clickRippleStyle", m_settings.clickRippleStyle);
    cfg.set("/spotlight/mouseTrailEnabled", m_settings.mouseTrailEnabled);
    cfg.set("/spotlight/mouseTrailStyle", m_settings.mouseTrailStyle);
    cfg.set("/spotlight/mouseTrailColorMode", m_settings.mouseTrailColorMode);
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
    std::lock_guard lock(m_mutex);
    m_animState = AnimState::Idle;
    m_currentAlpha = 0.0f;
    m_ripples.clear();
    m_trail.clear();
    m_ctrlState = CtrlDoubleTapState::Idle;
    m_firstCtrlDownTime = {};
    m_firstCtrlUpTime = {};
    m_ctrlIsPhysicallyDown = false;
}

bool SpotlightOverlay::isActive() const {
    std::lock_guard lock(m_mutex);
    return m_animState != AnimState::Idle || !m_ripples.empty() || !m_trail.empty();
}

SpotlightOverlay::ViewportBounds SpotlightOverlay::calculateViewportBoundsLocked() const {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (m_animState != AnimState::Idle) {
        // 聚光灯全屏暗角模式：物理尺寸微调避让 1 像素，打破 Explorer 全屏独占判定
        int safeW = (vw > 2) ? (vw - 1) : vw;
        int safeH = (vh > 2) ? (vh - 1) : vh;
        return {vx, vy, safeW, safeH, true};
    }

    if (m_ripples.empty() && m_trail.empty()) {
        return {0, 0, 0, 0, false};
    }

    // 局部自适应动态包围盒 (Dynamic Union Bounding Box)
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (const auto& rip : m_ripples) {
        int r = static_cast<int>(rip.maxRadius + 24.0f);
        minX = (std::min)(minX, static_cast<int>(rip.pt.x - r));
        minY = (std::min)(minY, static_cast<int>(rip.pt.y - r));
        maxX = (std::max)(maxX, static_cast<int>(rip.pt.x + r));
        maxY = (std::max)(maxY, static_cast<int>(rip.pt.y + r));
    }
    for (const auto& p : m_trail) {
        int r = static_cast<int>(p.size + 16.0f);
        minX = (std::min)(minX, static_cast<int>(p.pt.x - r));
        minY = (std::min)(minY, static_cast<int>(p.pt.y - r));
        maxX = (std::max)(maxX, static_cast<int>(p.pt.x + r));
        maxY = (std::max)(maxY, static_cast<int>(p.pt.y + r));
    }

    minX = (std::clamp)(minX, vx, vx + vw - 1);
    minY = (std::clamp)(minY, vy, vy + vh - 1);
    maxX = (std::clamp)(maxX, minX + 1, vx + vw);
    maxY = (std::clamp)(maxY, minY + 1, vy + vh);

    int w = maxX - minX;
    int h = maxY - minY;

    // 按 16 像素网格向上对齐，避免每帧尺寸微变导致 DIB Surface 频繁重新分配
    int alignedW = ((w + 15) / 16) * 16;
    int alignedH = ((h + 15) / 16) * 16;
    alignedW = (std::min)(alignedW, vw);
    alignedH = (std::min)(alignedH, vh);

    return {minX, minY, alignedW, alignedH, false};
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

    m_animState = AnimState::FadeIn;
    m_animStartTime = std::chrono::steady_clock::now();
    m_currentAlpha = 0.05f;
    m_focusProgress = 0.0f;
    m_scalePulse = 1.0f;
    m_reticleAngle = 0.0f;

    if (!m_timerRunning) {
        m_mmTimerId = timeSetEvent(ANIM_INTERVAL_MS, 1, onTimerTick, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
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
    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    bool isCtrl = (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);

    std::lock_guard lock(m_mutex);

    if (isDown) {
        if (m_animState != AnimState::Idle) {
            dismiss();
            return;
        }

        if (isCtrl) {
            if (!m_settings.enabled || !m_settings.triggerDoubleCtrl) return;

            auto now = std::chrono::steady_clock::now();

            // 1. 硬件自动连发（Auto-Repeat）静默拦截：按住不放期间收到的重复 KeyDown 绝不当作新按键！
            if (m_ctrlIsPhysicallyDown) {
                // 如果单次按住超过 260ms，判定为用户意在长按或准备按快捷键，立即熔断重置
                if (m_ctrlState == CtrlDoubleTapState::FirstPressed) {
                    auto holdDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstCtrlDownTime).count();
                    if (holdDuration > 260) {
                        m_ctrlState = CtrlDoubleTapState::Idle;
                    }
                }
                return;
            }

            m_ctrlIsPhysicallyDown = true;

            // 2. 状态流转
            if (m_ctrlState == CtrlDoubleTapState::WaitingSecond) {
                auto upToDownGap = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstCtrlUpTime).count();
                if (upToDownGap >= 30 && upToDownGap <= 380) {
                    // 🎉 完美命中「双击 Ctrl」闭环！
                    m_ctrlState = CtrlDoubleTapState::Idle;
                    m_firstCtrlDownTime = {};
                    m_firstCtrlUpTime = {};
                    trigger();
                    return;
                } else {
                    // 超出连击时间窗，转为新的第 1 次按下
                    m_ctrlState = CtrlDoubleTapState::FirstPressed;
                    m_firstCtrlDownTime = now;
                }
            } else {
                // 首次按下 (First Down)
                m_ctrlState = CtrlDoubleTapState::FirstPressed;
                m_firstCtrlDownTime = now;
            }
        } else {
            // 3. 组合键/杂键污染熔断 (Pollution Abort)：用户按了 C, V, Tab, Space 等，立即取消双击判定
            m_ctrlState = CtrlDoubleTapState::Idle;
            m_firstCtrlDownTime = {};
            m_firstCtrlUpTime = {};
        }
    } else if (isUp) {
        if (isCtrl) {
            m_ctrlIsPhysicallyDown = false;
            auto now = std::chrono::steady_clock::now();

            if (m_ctrlState == CtrlDoubleTapState::FirstPressed) {
                auto holdDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstCtrlDownTime).count();
                if (holdDuration <= 280) {
                    // 第一次快速敲击并松开（Hold <= 280ms），顺利进入等待第二次按下的时间窗
                    m_ctrlState = CtrlDoubleTapState::WaitingSecond;
                    m_firstCtrlUpTime = now;
                } else {
                    // 长按后松开，不作为双击的前奏
                    m_ctrlState = CtrlDoubleTapState::Idle;
                }
            }
        }
    }
}

void SpotlightOverlay::onMouseDown(int button, POINT pt) {
    std::lock_guard lock(m_mutex);

    // 鼠标点击重置双击 Ctrl 状态机（防止边点击鼠标边按 Ctrl 发生误判）
    m_ctrlState = CtrlDoubleTapState::Idle;

    // 演示者模式：聚光灯活跃期间点击鼠标，在当前位置激发全屏水波涟漪并优雅退出
    if (m_animState == AnimState::FadeIn || m_animState == AnimState::Holding) {
        m_animState = AnimState::FadeOut;
        m_animStartTime = std::chrono::steady_clock::now();
        m_targetPos = pt;
        if (!m_timerRunning) {
            m_mmTimerId = timeSetEvent(ANIM_INTERVAL_MS, 1, onTimerTick, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
            m_timerRunning = true;
        }
        render();
        return;
    }

    if (!m_settings.enabled || !m_settings.clickRippleEnabled) return;

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

    ClickRipple rip;
    rip.pt = pt;
    rip.startTime = std::chrono::steady_clock::now();
    rip.color = color;
    rip.style = m_settings.clickRippleStyle;

    if (rip.style == "sparkle_burst") {
        rip.maxRadius = 32.0f;
        rip.durationMs = 380.0f;
        // 生成 6 颗带初速度的星芒微粒
        for (int i = 0; i < 6; ++i) {
            float angle = static_cast<float>(i) * (3.14159265f / 3.0f) + 0.2f;
            float speed = 22.0f + static_cast<float>((i * 7) % 15);
            ClickSparkle sp;
            sp.x = static_cast<float>(pt.x);
            sp.y = static_cast<float>(pt.y);
            sp.vx = std::cos(angle) * speed;
            sp.vy = std::sin(angle) * speed;
            sp.size = 2.5f + static_cast<float>((i % 3)) * 0.8f;
            rip.sparklets.push_back(sp);
        }
    } else if (rip.style == "supernova") {
        rip.maxRadius = 55.0f;
        rip.durationMs = 460.0f;
        for (int i = 0; i < 8; ++i) {
            float angle = static_cast<float>(i) * (3.14159265f / 4.0f) + 0.1f;
            float speed = 30.0f + static_cast<float>((i * 5) % 12);
            ClickSparkle sp;
            sp.x = static_cast<float>(pt.x);
            sp.y = static_cast<float>(pt.y);
            sp.vx = std::cos(angle) * speed;
            sp.vy = std::sin(angle) * speed;
            sp.size = 3.0f;
            rip.sparklets.push_back(sp);
        }
    } else if (rip.style == "emp_discharge") {
        rip.maxRadius = 42.0f;
        rip.durationMs = 300.0f;
        for (int i = 0; i < 4; ++i) {
            float angle = static_cast<float>(i) * (3.14159265f / 2.0f) + static_cast<float>((i * 17) % 25) * 0.01745f;
            float speed = 26.0f;
            ClickSparkle sp;
            sp.x = static_cast<float>(pt.x);
            sp.y = static_cast<float>(pt.y);
            sp.vx = std::cos(angle) * speed;
            sp.vy = std::sin(angle) * speed;
            sp.size = 1.5f;
            rip.sparklets.push_back(sp);
        }
    } else if (rip.style == "ink_droplet") {
        rip.maxRadius = 45.0f;
        rip.durationMs = 520.0f;
    } else if (rip.style == "hexagon_lock") {
        rip.maxRadius = 36.0f;
        rip.durationMs = 360.0f;
    } else if (rip.style == "bubble_pop") {
        rip.maxRadius = 32.0f;
        rip.durationMs = 340.0f;
        for (int i = 0; i < 4; ++i) {
            float angle = static_cast<float>(i) * (3.14159265f / 2.0f) + 0.39f;
            float speed = 18.0f;
            ClickSparkle sp;
            sp.x = static_cast<float>(pt.x);
            sp.y = static_cast<float>(pt.y);
            sp.vx = std::cos(angle) * speed;
            sp.vy = std::sin(angle) * speed;
            sp.size = 2.0f;
            rip.sparklets.push_back(sp);
        }
    } else if (rip.style == "target_pulse") {
        rip.maxRadius = 36.0f;
        rip.durationMs = 340.0f;
    } else if (rip.style == "soft_glow") {
        rip.maxRadius = 28.0f;
        rip.durationMs = 280.0f;
    } else {
        // ripple_ring
        rip.maxRadius = 52.0f;
        rip.durationMs = 450.0f;
    }

    m_ripples.push_back(rip);

    if (!m_timerRunning) {
        m_mmTimerId = timeSetEvent(ANIM_INTERVAL_MS, 1, onTimerTick, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
        m_timerRunning = true;
    }
    render();
}

void SpotlightOverlay::onMouseMove(POINT pt) {
    std::lock_guard lock(m_mutex);
    if (!m_settings.enabled) return;

    auto now = std::chrono::steady_clock::now();

    // 0. 演示者模式：聚光灯活跃期间仅记录鼠标坐标，由高精度时钟节流渲染，彻底消除 1000Hz 钩子卡顿！
    if (m_animState == AnimState::FadeIn || m_animState == AnimState::Holding) {
        m_targetPos = pt;
        if (!m_timerRunning) {
            m_mmTimerId = timeSetEvent(ANIM_INTERVAL_MS, 1, onTimerTick, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
            m_timerRunning = true;
        }
    }

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

            const std::string& style = m_settings.mouseTrailStyle;
            float threshold = 28.0f; // 默认 stardust_orbs 大步进
            if (style == "aurora_ribbon") threshold = 10.0f;
            else if (style == "sonar_pulses") threshold = 42.0f;
            else if (style == "classic_comet") threshold = 6.0f;
            else if (style == "quantum_lens") threshold = 20.0f;
            else if (style == "tesla_arc") threshold = 18.0f;
            else if (style == "zen_ink") threshold = 14.0f;
            else if (style == "blueprint_grid") threshold = 32.0f;
            else if (style == "morning_dew") threshold = 26.0f;

            if (dist >= threshold) {
                if (style == "stardust_orbs") {
                    m_trailHue = std::fmod(m_trailHue + 18.0f, 360.0f);
                    // 1.1 主透亮能量球 (6.5px ~ 7.5px)
                    TrailParticle pMain;
                    pMain.pt = pt;
                    pMain.time = now;
                    pMain.kind = TrailParticleKind::OrbMain;
                    pMain.size = 7.2f;
                    pMain.durationMs = 280.0f;
                    pMain.color = m_settings.spotlightColor;
                    pMain.hue = m_trailHue;
                    m_trail.push_back(pMain);

                    // 1.2 伴生微星 (1.8px, 轻微空间偏移)
                    static int subCounter = 0;
                    subCounter++;
                    float angleOffset = static_cast<float>((subCounter * 73) % 360) * (3.14159265f / 180.0f);
                    float offsetDist = 5.0f + static_cast<float>((subCounter * 5) % 4);
                    POINT sparkletPt{
                        pt.x + static_cast<LONG>(std::cos(angleOffset) * offsetDist),
                        pt.y + static_cast<LONG>(std::sin(angleOffset) * offsetDist)
                    };
                    TrailParticle pSpark;
                    pSpark.pt = sparkletPt;
                    pSpark.time = now;
                    pSpark.kind = TrailParticleKind::Sparklet;
                    pSpark.size = 2.0f;
                    pSpark.durationMs = 210.0f;
                    pSpark.color = m_settings.spotlightColor;
                    pSpark.hue = std::fmod(m_trailHue + 12.0f, 360.0f);
                    m_trail.push_back(pSpark);

                    // 1.3 偶尔插入次级球 (4.0px)
                    if (subCounter % 2 == 0) {
                        TrailParticle pSub;
                        pSub.pt = POINT{pt.x - static_cast<LONG>(std::cos(angleOffset) * 3.0f),
                                        pt.y - static_cast<LONG>(std::sin(angleOffset) * 3.0f)};
                        pSub.time = now;
                        pSub.kind = TrailParticleKind::OrbSub;
                        pSub.size = 4.0f;
                        pSub.durationMs = 240.0f;
                        pSub.color = m_settings.spotlightColor;
                        pSub.hue = std::fmod(m_trailHue - 10.0f, 360.0f);
                        m_trail.push_back(pSub);
                    }
                } else if (style == "aurora_ribbon") {
                    m_trailHue = std::fmod(m_trailHue + 6.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::RibbonNode;
                    p.size = 2.2f;
                    p.durationMs = 300.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                } else if (style == "sonar_pulses") {
                    m_trailHue = std::fmod(m_trailHue + 38.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::SonarRing;
                    p.size = 16.0f;
                    p.durationMs = 360.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                } else if (style == "quantum_lens") {
                    m_trailHue = std::fmod(m_trailHue + 24.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::QuantumOrb;
                    p.size = 8.0f;
                    p.durationMs = 320.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    p.extra = static_cast<float>((m_trail.size() * 45) % 360);
                    m_trail.push_back(p);
                } else if (style == "tesla_arc") {
                    m_trailHue = std::fmod(m_trailHue + 15.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::TeslaBolt;
                    p.size = 2.0f;
                    p.durationMs = 220.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    p.extra = static_cast<float>((rand() % 14) - 7);
                    m_trail.push_back(p);
                } else if (style == "zen_ink") {
                    m_trailHue = std::fmod(m_trailHue + 8.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::InkStroke;
                    p.size = (std::clamp)(14.0f - dist * 0.25f, 3.0f, 12.0f);
                    p.durationMs = 400.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                } else if (style == "blueprint_grid") {
                    m_trailHue = std::fmod(m_trailHue + 12.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::GridRuler;
                    p.size = 14.0f;
                    p.durationMs = 300.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                } else if (style == "morning_dew") {
                    m_trailHue = std::fmod(m_trailHue + 20.0f, 360.0f);
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::DewBubble;
                    p.size = 5.5f + static_cast<float>((m_trail.size() % 3) * 1.5f);
                    p.durationMs = 350.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                } else {
                    // classic_comet
                    TrailParticle p;
                    p.pt = pt;
                    p.time = now;
                    p.kind = TrailParticleKind::CometDot;
                    p.size = 8.5f;
                    p.durationMs = 360.0f;
                    p.color = m_settings.spotlightColor;
                    p.hue = m_trailHue;
                    m_trail.push_back(p);
                }

                // 限制最大粒子队列
                if (m_trail.size() > 48) {
                    m_trail.erase(m_trail.begin(), m_trail.begin() + (m_trail.size() - 48));
                }

                if (!m_timerRunning) {
                    m_mmTimerId = timeSetEvent(ANIM_INTERVAL_MS, 1, onTimerTick, reinterpret_cast<DWORD_PTR>(this), TIME_PERIODIC | TIME_CALLBACK_FUNCTION);
                    m_timerRunning = true;
                }
            }
        }
    }

    // 2. 摇晃鼠标寻找光标检测 (macOS/Windows 级高灵敏累积折返加速算法)
    if (m_animState == AnimState::Idle && m_settings.triggerShakeMouse) {
        if (m_lastMousePos.x == 0 && m_lastMousePos.y == 0) {
            m_lastMousePos = pt;
            m_shakeWindowStart = now;
            return;
        }

        int dx = pt.x - m_lastMousePos.x;
        int dy = pt.y - m_lastMousePos.y;
        m_lastMousePos = pt;

        // 计算主要位移轴向
        int delta = (std::abs(dx) >= std::abs(dy)) ? dx : dy;
        if (std::abs(delta) >= 6) {
            int dir = (delta > 0) ? 1 : -1;
            auto windowElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_shakeWindowStart).count();
            if (windowElapsed > 1000) {
                m_shakeReversals = 0;
                m_shakeWindowStart = now;
                m_lastMoveDir = dir;
            } else if (m_lastMoveDir != 0 && dir != m_lastMoveDir) {
                m_shakeReversals++;
                m_lastMoveDir = dir;
                int threshold = (std::max)(3, (std::min)(10, m_settings.shakeThreshold));
                if (m_shakeReversals >= threshold) {
                    m_shakeReversals = 0;
                    m_shakeWindowStart = {};
                    trigger(pt, false);
                }
            } else {
                m_lastMoveDir = dir;
            }
        }
    }
}

void SpotlightOverlay::tickAnimation() {
    std::lock_guard lock(m_mutex);

    auto now = std::chrono::steady_clock::now();

    // 1. 聚光灯动效更新
    if (m_animState != AnimState::Idle) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_animStartTime).count();
        const float fadeInDuration = 420.0f;  // 420ms 极度丝滑温润聚拢
        const float fadeOutDuration = 380.0f; // 380ms 全屏水波涟漪漫溢散开

        if (m_animState == AnimState::FadeIn) {
            float progress = (std::clamp)(static_cast<float>(elapsedMs) / fadeInDuration, 0.0f, 1.0f);
            m_focusProgress = progress;

            // 超丝滑高阶贝塞尔减速收敛进入 (Quintic Ease Out · 顶级电影感)
            float easeOut = 1.0f - std::pow(1.0f - progress, 4.0f);
            m_currentAlpha = easeOut;

            // 全屏向心凝结微物理弹性回弹
            if (progress > 0.72f) {
                float pulseT = (progress - 0.72f) / 0.28f; // 0.0 -> 1.0
                m_scalePulse = 1.0f + 0.08f * std::sin(pulseT * 3.14159265f);
            } else {
                m_scalePulse = 1.0f + (1.0f - easeOut) * 2.2f;
            }

            // 战术准星旋转
            m_reticleAngle = easeOut * 45.0f;

            if (elapsedMs >= fadeInDuration) {
                m_currentAlpha = 1.0f;
                m_focusProgress = 1.0f;
                m_scalePulse = 1.0f;
                m_reticleAngle = 45.0f;
                m_animState = AnimState::Holding;
                m_holdStartTime = now;
            }
        } else if (m_animState == AnimState::Holding) {
            m_currentAlpha = 1.0f;
            m_focusProgress = 1.0f;
            m_scalePulse = 1.0f;
            // 演示者模式：持续跟随鼠标光标移动，不自动超时退出，直到用户点击鼠标或按下快捷键/Esc 退出
        } else if (m_animState == AnimState::FadeOut) {
            // 全屏水波巨浪漫溢散开：如海浪向全屏四角极速推进并平滑消散
            float progress = (std::clamp)(static_cast<float>(elapsedMs) / fadeOutDuration, 0.0f, 1.0f);
            m_focusProgress = progress;

            float waveEase = 1.0f - std::pow(1.0f - progress, 2.5f);
            m_scalePulse = 1.0f + waveEase * 5.5f;
            m_currentAlpha = (std::clamp)(std::pow(1.0f - progress, 1.4f), 0.0f, 1.0f);

            if (elapsedMs >= fadeOutDuration || m_currentAlpha <= 0.01f) {
                m_animState = AnimState::Idle;
                m_currentAlpha = 0.0f;
                m_focusProgress = 0.0f;
                m_scalePulse = 1.0f;
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

    m_lastRenderedPos = m_targetPos;
    render();
}

void SpotlightOverlay::hideNow() {
    m_animState = AnimState::Idle;
    m_currentAlpha = 0.0f;
    m_ripples.clear();
    m_trail.clear();
    if (m_mmTimerId) {
        timeKillEvent(m_mmTimerId);
        m_mmTimerId = 0;
    }
    if (m_timerRunning) {
        KillTimer(m_hwnd, TIMER_ANIM_ID);
        m_timerRunning = false;
    }
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
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
    if (!m_hwnd) return;

    ViewportBounds bounds = calculateViewportBoundsLocked();
    if (bounds.w <= 0 || bounds.h <= 0) {
        return;
    }

    if (!ensureSurface(bounds.w, bounds.h)) return;
    if (!createResources()) return;

    RECT rc = {0, 0, m_surfaceW, m_surfaceH};
    if (FAILED(m_dcRenderTarget->BindDC(m_memDc, &rc))) return;

    m_dcRenderTarget->BeginDraw();
    m_dcRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0.0f));

    auto now = std::chrono::steady_clock::now();

    // ─────────────────────────────────────────────────────────────────────────
    // 1. 绘制聚光灯 (极致通透纯净全屏暗角 + 激光微晶细环 + 优雅向心视线导引)
    // ─────────────────────────────────────────────────────────────────────────
    if (m_animState != AnimState::Idle && m_currentAlpha > 0.01f) {
        float localCenterX = static_cast<float>(m_targetPos.x - bounds.x);
        float localCenterY = static_cast<float>(m_targetPos.y - bounds.y);
        float baseRadius = static_cast<float>((std::max)(40, m_settings.spotlightSize)) / 2.0f;
        float radius = baseRadius * m_scalePulse;
        float alpha = m_currentAlpha;

        D2D1_COLOR_F baseColor = parseColor(m_settings.spotlightColor, 1.0f);

        // 纯净全屏暗角：精准几何对齐，在 radius 边缘 1 像素内平滑过渡到深灰暗幕，彻底消除任何内外白边缝隙
        float outerRadius = (std::max)(radius + 60.0f, radius * 1.35f);
        float stopTransparent = (std::clamp)((radius - 0.5f) / outerRadius, 0.0f, 0.98f);
        float stopDark = (std::clamp)((radius + 1.2f) / outerRadius, stopTransparent + 0.002f, 0.999f);

        // 1. 全球电影级深邃纯净暗角 (58% 深度，纯净背景压暗)
        D2D1_COLOR_F dimColor = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.58f * alpha);

        D2D1_GRADIENT_STOP maskStops[3] = {
            { stopTransparent, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) },
            { stopDark, dimColor },
            { 1.0f, dimColor }
        };

        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> maskStopColl;
        if (SUCCEEDED(m_dcRenderTarget->CreateGradientStopCollection(
                maskStops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, maskStopColl.GetAddressOf()))) {
            D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES radialProps = D2D1::RadialGradientBrushProperties(
                D2D1::Point2F(localCenterX, localCenterY),
                D2D1::Point2F(0, 0),
                outerRadius, outerRadius
            );
            Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> maskBrush;
            if (SUCCEEDED(m_dcRenderTarget->CreateRadialGradientBrush(
                    radialProps, maskStopColl.Get(), maskBrush.GetAddressOf()))) {
                D2D1_RECT_F fullRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_surfaceW), static_cast<float>(m_surfaceH));
                m_dcRenderTarget->FillRectangle(fullRect, maskBrush.Get());
            }
        }

        // 2. 绘制 3D 晶莹珍珠水晶球体 (3D Luminous Sphere Orb · 纯净同色系透光，0 白边溢出)
        if (radius > 4.0f) {
            float innerDiscRadius = radius;

            // 2.1 底层：3D 球体体积漫反射与偏心高光 (同色系高亮，绝无生硬白光)
            D2D1_POINT_2F highlightOffset = D2D1::Point2F(-innerDiscRadius * 0.32f, -innerDiscRadius * 0.32f);

            D2D1_COLOR_F highlightColor = D2D1::ColorF(
                (std::min)(1.0f, baseColor.r * 1.35f + 0.15f),
                (std::min)(1.0f, baseColor.g * 1.35f + 0.15f),
                (std::min)(1.0f, baseColor.b * 1.35f + 0.15f),
                0.28f * alpha
            );

            D2D1_GRADIENT_STOP sphereStops[5] = {
                { 0.00f, highlightColor },                                                                                                                         // 偏心高光核心 (晶莹通透同色系高亮)
                { 0.28f, D2D1::ColorF((std::min)(1.0f, baseColor.r * 1.15f), (std::min)(1.0f, baseColor.g * 1.15f), (std::min)(1.0f, baseColor.b * 1.15f), 0.20f * alpha) }, // 高光柔和过渡
                { 0.68f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.14f * alpha) },                                                                   // 球体主体色彩 (极度通透清晰)
                { 0.92f, D2D1::ColorF(baseColor.r * 0.65f, baseColor.g * 0.65f, baseColor.b * 0.65f, 0.16f * alpha) },                                         // 球面法线阴影暗化 (Limb Darkening)
                { 1.00f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) }                                                                                                 // 外轮廓消散
            };

            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> sphereStopColl;
            if (SUCCEEDED(m_dcRenderTarget->CreateGradientStopCollection(
                    sphereStops, 5, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, sphereStopColl.GetAddressOf()))) {
                D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES sphereProps = D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(localCenterX, localCenterY),
                    highlightOffset,
                    innerDiscRadius, innerDiscRadius
                );
                Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> sphereBrush;
                if (SUCCEEDED(m_dcRenderTarget->CreateRadialGradientBrush(
                        sphereProps, sphereStopColl.Get(), sphereBrush.GetAddressOf()))) {
                    m_dcRenderTarget->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), innerDiscRadius, innerDiscRadius),
                        sphereBrush.Get()
                    );
                }
            }

            // 2.2 底部：3D 次级环境反射弧光 (Bottom-Up Secondary Bounce Light)
            D2D1_POINT_2F bounceOffset = D2D1::Point2F(innerDiscRadius * 0.20f, innerDiscRadius * 0.28f);
            D2D1_GRADIENT_STOP bounceStops[3] = {
                { 0.00f, D2D1::ColorF((std::min)(1.0f, baseColor.r * 1.15f), (std::min)(1.0f, baseColor.g * 1.15f), (std::min)(1.0f, baseColor.b * 1.15f), 0.12f * alpha) },
                { 0.60f, D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.06f * alpha) },
                { 1.00f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f) }
            };
            Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> bounceStopColl;
            if (SUCCEEDED(m_dcRenderTarget->CreateGradientStopCollection(
                    bounceStops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, bounceStopColl.GetAddressOf()))) {
                D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES bounceProps = D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(localCenterX + bounceOffset.x, localCenterY + bounceOffset.y),
                    D2D1::Point2F(0, 0),
                    innerDiscRadius * 0.65f, innerDiscRadius * 0.65f
                );
                Microsoft::WRL::ComPtr<ID2D1RadialGradientBrush> bounceBrush;
                if (SUCCEEDED(m_dcRenderTarget->CreateRadialGradientBrush(
                        bounceProps, bounceStopColl.Get(), bounceBrush.GetAddressOf()))) {
                    m_dcRenderTarget->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), innerDiscRadius, innerDiscRadius),
                        bounceBrush.Get()
                    );
                }
            }
        }

        // 3. 退出散开阶段：全屏水波巨浪漫溢散开 (Full-Screen Tidal Ripple Wavefronts on Dismiss)
        if (m_animState == AnimState::FadeOut) {
            float maxSpan = std::sqrt(static_cast<float>(m_surfaceW * m_surfaceW + m_surfaceH * m_surfaceH));
            if (maxSpan < 800.0f) maxSpan = 1600.0f;

            // 第 1 道前锋巨浪波 (覆盖到屏幕外围)
            float rip1Radius = radius + m_focusProgress * maxSpan * 0.90f;
            float rip1Alpha = (1.0f - m_focusProgress) * 0.55f * alpha;
            if (rip1Alpha > 0.01f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ripBrush1;
                m_dcRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, rip1Alpha),
                    ripBrush1.GetAddressOf()
                );
                if (ripBrush1) {
                    m_dcRenderTarget->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), rip1Radius, rip1Radius),
                        ripBrush1.Get(), 2.5f
                    );
                }
            }

            // 第 2 道中程温润波
            float rip2Radius = radius + std::pow(m_focusProgress, 1.2f) * maxSpan * 0.55f;
            float rip2Alpha = (1.0f - m_focusProgress) * 0.40f * alpha;
            if (rip2Alpha > 0.01f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ripBrush2;
                m_dcRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF((std::min)(1.0f, baseColor.r * 1.15f), (std::min)(1.0f, baseColor.g * 1.15f), (std::min)(1.0f, baseColor.b * 1.15f), rip2Alpha),
                    ripBrush2.GetAddressOf()
                );
                if (ripBrush2) {
                    m_dcRenderTarget->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), rip2Radius, rip2Radius),
                        ripBrush2.Get(), 1.8f
                    );
                }
            }

            // 第 3 道内层柔和回声波
            float rip3Radius = radius + std::pow(m_focusProgress, 1.4f) * maxSpan * 0.28f;
            float rip3Alpha = (1.0f - m_focusProgress) * 0.28f * alpha;
            if (rip3Alpha > 0.01f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ripBrush3;
                m_dcRenderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, rip3Alpha),
                    ripBrush3.GetAddressOf()
                );
                if (ripBrush3) {
                    m_dcRenderTarget->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), rip3Radius, rip3Radius),
                        ripBrush3.Get(), 1.2f
                    );
                }
            }
        }

        const std::string& animStyle = m_settings.spotlightAnimStyle;

        if (animStyle == "tactical_sonar") {
            // ─────────────────────────────────────────────────────────────────
            // 方案 B：科技声纳雷达 + 战术 HUD 准星锁定模式 (极客硬核)
            // ─────────────────────────────────────────────────────────────────
            if (m_animState == AnimState::FadeIn) {
                for (int i = 1; i <= 3; ++i) {
                    float ringProgress = std::fmod(m_focusProgress * 1.6f + static_cast<float>(i) * 0.28f, 1.0f);
                    float ringRadius = radius + ringProgress * 85.0f;
                    float ringAlpha = (1.0f - ringProgress) * 0.65f * alpha;
                    if (ringAlpha > 0.02f) {
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sonarBrush;
                        m_dcRenderTarget->CreateSolidColorBrush(
                            D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, ringAlpha),
                            sonarBrush.GetAddressOf()
                        );
                        if (sonarBrush) {
                            m_dcRenderTarget->DrawEllipse(
                                D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), ringRadius, ringRadius),
                                sonarBrush.Get(), 1.5f
                            );
                        }
                    }
                }
            } else if (m_animState == AnimState::Holding) {
                auto holdEl = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_holdStartTime).count();
                float waveCycle = std::fmod(static_cast<float>(holdEl) / 700.0f, 1.0f);
                float waveR = radius + waveCycle * 50.0f;
                float waveA = (1.0f - waveCycle) * 0.35f * alpha;
                if (waveA > 0.02f) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> waveBrush;
                    m_dcRenderTarget->CreateSolidColorBrush(
                        D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, waveA),
                        waveBrush.GetAddressOf()
                    );
                    if (waveBrush) {
                        m_dcRenderTarget->DrawEllipse(
                            D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), waveR, waveR),
                            waveBrush.Get(), 1.2f
                        );
                    }
                }
            }

            // 绘制 4 组 CAD 战术 L 型瞄准框
            float reticleOffset = radius + 8.0f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> reticleBrush;
            m_dcRenderTarget->CreateSolidColorBrush(
                D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.95f * alpha),
                reticleBrush.GetAddressOf()
            );
            if (reticleBrush) {
                float angles[4] = { 45.0f, 135.0f, 225.0f, 315.0f };
                float armLen = 10.0f;
                for (float ang : angles) {
                    float rad = (ang + m_reticleAngle) * (3.14159265f / 180.0f);
                    float cx = localCenterX + std::cos(rad) * reticleOffset;
                    float cy = localCenterY + std::sin(rad) * reticleOffset;

                    float tanRad = rad + (3.14159265f / 2.0f);
                    float tx = std::cos(tanRad) * armLen * 0.5f;
                    float ty = std::sin(tanRad) * armLen * 0.5f;
                    float rx = std::cos(rad) * armLen * 0.5f;
                    float ry = std::sin(rad) * armLen * 0.5f;

                    m_dcRenderTarget->DrawLine(
                        D2D1::Point2F(cx - tx, cy - ty),
                        D2D1::Point2F(cx + tx, cy + ty),
                        reticleBrush.Get(), 2.0f
                    );
                    m_dcRenderTarget->DrawLine(
                        D2D1::Point2F(cx, cy),
                        D2D1::Point2F(cx + rx, cy + ry),
                        reticleBrush.Get(), 2.0f
                    );
                }
            }
        } else if (animStyle == "aurora_ripple") {
            // ─────────────────────────────────────────────────────────────────
            // 方案 C：极简极光涟漪 · 柔和呼吸氛围模式
            // ─────────────────────────────────────────────────────────────────
            if (m_animState == AnimState::FadeIn) {
                for (int i = 1; i <= 2; ++i) {
                    float ripProg = (std::clamp)(m_focusProgress * 1.4f - static_cast<float>(i - 1) * 0.28f, 0.0f, 1.0f);
                    if (ripProg > 0.0f) {
                        float rippleRadius = radius + ripProg * 45.0f;
                        float rippleAlpha = (1.0f - ripProg) * 0.55f * alpha;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ripBrush;
                        m_dcRenderTarget->CreateSolidColorBrush(
                            D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, rippleAlpha),
                            ripBrush.GetAddressOf()
                        );
                        if (ripBrush) {
                            m_dcRenderTarget->DrawEllipse(
                                D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), rippleRadius, rippleRadius),
                                ripBrush.Get(), 2.0f
                            );
                        }
                    }
                }
            }

            float breathe = 1.0f;
            if (m_animState == AnimState::Holding) {
                auto holdEl = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_holdStartTime).count();
                breathe = 0.85f + 0.15f * std::sin(static_cast<float>(holdEl) * 0.006f);
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> auroraGlowBrush;
            m_dcRenderTarget->CreateSolidColorBrush(
                D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, 0.32f * breathe * alpha),
                auroraGlowBrush.GetAddressOf()
            );
            if (auroraGlowBrush) {
                m_dcRenderTarget->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), radius + 3.0f, radius + 3.0f),
                    auroraGlowBrush.Get(), 6.0f
                );
            }
        } else {
            // ─────────────────────────────────────────────────────────────────
            // 方案 A（默认 · 推荐）：向心引力折叠超聚焦 (极简高雅双激光微环)
            // ─────────────────────────────────────────────────────────────────
            if (m_animState == AnimState::FadeIn) {
                float inward1 = (1.0f - m_focusProgress); // 1.0 -> 0.0
                float ring1Radius = radius + std::pow(inward1, 1.4f) * 140.0f;
                float ring1Alpha = (0.35f + 0.65f * m_focusProgress) * alpha;

                if (ring1Alpha > 0.02f) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ring1Brush;
                    m_dcRenderTarget->CreateSolidColorBrush(
                        D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, ring1Alpha * 0.95f),
                        ring1Brush.GetAddressOf()
                    );
                    if (ring1Brush) {
                        m_dcRenderTarget->DrawEllipse(
                            D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), ring1Radius, ring1Radius),
                            ring1Brush.Get(), 2.0f
                        );
                    }
                }

                float inward2 = (std::clamp)(1.0f - m_focusProgress * 1.2f, 0.0f, 1.0f);
                float ring2Radius = radius + std::pow(inward2, 1.1f) * 220.0f;
                float ring2Alpha = (0.20f + 0.50f * m_focusProgress) * alpha;

                if (ring2Alpha > 0.02f) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ring2Brush;
                    m_dcRenderTarget->CreateSolidColorBrush(
                        D2D1::ColorF(baseColor.r, baseColor.g, baseColor.b, ring2Alpha * 0.60f),
                        ring2Brush.GetAddressOf()
                    );
                    if (ring2Brush) {
                        m_dcRenderTarget->DrawEllipse(
                            D2D1::Ellipse(D2D1::Point2F(localCenterX, localCenterY), ring2Radius, ring2Radius),
                            ring2Brush.Get(), 1.2f
                        );
                    }
                }
            }
        }

    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. 绘制鼠标轨迹特效 (9 款大师级风格渲染管线)
    // ─────────────────────────────────────────────────────────────────────────
    const std::string& trailStyle = m_settings.mouseTrailStyle;
    const bool isRainbow = (m_settings.mouseTrailColorMode == "rainbow");

    if (trailStyle == "stardust_orbs") {
        // 2.1 梦幻七彩星尘大中小光球
        for (size_t i = 0; i < m_trail.size(); ++i) {
            const auto& p = m_trail[i];
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float ease = (1.0f - progress) * (1.0f - progress);
            float alpha = ease * 0.90f;
            float r = p.size * (1.0f - progress * 0.45f);

            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.85f, 0.60f, alpha) : parseColor(p.color, alpha);

            // 主球绘制外层柔光 Bloom
            if (p.kind == TrailParticleKind::OrbMain) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> haloBrush;
                D2D1_COLOR_F haloC = isRainbow ? hslToRgb(p.hue, 0.85f, 0.65f, alpha * 0.35f) : parseColor(p.color, alpha * 0.35f);
                m_dcRenderTarget->CreateSolidColorBrush(haloC, haloBrush.GetAddressOf());
                if (haloBrush) {
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r + 2.0f, r + 2.0f), haloBrush.Get());
                }
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> orbBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, orbBrush.GetAddressOf());
            if (orbBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r, r), orbBrush.Get());
            }
        }
    } else if (trailStyle == "aurora_ribbon") {
        // 2.2 极光轻量流体丝带
        for (size_t i = 0; i < m_trail.size(); ++i) {
            const auto& p = m_trail[i];
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * 0.75f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.85f, 0.62f, alpha) : parseColor(p.color, alpha);

            if (i + 1 < m_trail.size()) {
                const auto& nextP = m_trail[i + 1];
                float nx = static_cast<float>(nextP.pt.x - bounds.x);
                float ny = static_cast<float>(nextP.pt.y - bounds.y);
                float dist = std::sqrt((nx - px) * (nx - px) + (ny - py) * (ny - py));
                if (dist < 40.0f && dist > 1.0f) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lineBrush;
                    m_dcRenderTarget->CreateSolidColorBrush(c, lineBrush.GetAddressOf());
                    if (lineBrush) {
                        float stroke = (std::max)(0.8f, 2.2f * (1.0f - progress * 0.6f));
                        m_dcRenderTarget->DrawLine(D2D1::Point2F(px, py), D2D1::Point2F(nx, ny), lineBrush.Get(), stroke);
                    }
                }
            }
        }
    } else if (trailStyle == "sonar_pulses") {
        // 2.3 稀疏彩色声纳微环 (默认)
        for (const auto& p : m_trail) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float ease = 1.0f - std::pow(1.0f - progress, 2.5f);
            float currentR = 3.0f + p.size * ease;
            float alpha = (1.0f - ease) * 0.80f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.85f, 0.60f, alpha) : parseColor(p.color, alpha);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ringBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, ringBrush.GetAddressOf());
            if (ringBrush) {
                m_dcRenderTarget->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(px, py), currentR, currentR),
                    ringBrush.Get(),
                    (std::max)(0.6f, 1.5f * (1.0f - ease * 0.6f))
                );
            }
        }
    } else if (trailStyle == "quantum_lens") {
        // 2.4 量子引力微子
        for (size_t i = 0; i < m_trail.size(); ++i) {
            const auto& p = m_trail[i];
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * 0.85f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);
            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.90f, 0.65f, alpha) : parseColor(p.color, alpha);

            // 中心微引力核
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> coreBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, coreBrush.GetAddressOf());
            if (coreBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), 2.2f, 2.2f), coreBrush.Get());
            }

            // 自旋量子微光子 (绕中心公转)
            float angle1 = p.extra * (3.14159265f / 180.0f) + progress * 6.28f;
            float orbDist = 8.0f * (1.0f - progress * 0.3f);
            float qx = px + std::cos(angle1) * orbDist;
            float qy = py + std::sin(angle1) * orbDist;

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> qBrush;
            D2D1_COLOR_F qc = isRainbow ? hslToRgb(p.hue + 60.0f, 0.95f, 0.70f, alpha * 0.9f) : parseColor(p.color, alpha * 0.9f);
            m_dcRenderTarget->CreateSolidColorBrush(qc, qBrush.GetAddressOf());
            if (qBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(qx, qy), 1.6f, 1.6f), qBrush.Get());
            }
        }
    } else if (trailStyle == "tesla_arc") {
        // 2.5 特斯拉电弧微流
        for (size_t i = 0; i < m_trail.size(); ++i) {
            const auto& p = m_trail[i];
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * 0.90f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);
            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.95f, 0.65f, alpha) : parseColor(p.color, alpha);

            if (i + 1 < m_trail.size()) {
                const auto& nextP = m_trail[i + 1];
                float nx = static_cast<float>(nextP.pt.x - bounds.x);
                float ny = static_cast<float>(nextP.pt.y - bounds.y);
                float midX = (px + nx) * 0.5f + p.extra;
                float midY = (py + ny) * 0.5f - p.extra;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> arcBrush;
                m_dcRenderTarget->CreateSolidColorBrush(c, arcBrush.GetAddressOf());
                if (arcBrush) {
                    m_dcRenderTarget->DrawLine(D2D1::Point2F(px, py), D2D1::Point2F(midX, midY), arcBrush.Get(), 1.2f);
                    m_dcRenderTarget->DrawLine(D2D1::Point2F(midX, midY), D2D1::Point2F(nx, ny), arcBrush.Get(), 1.2f);
                }
            }
        }
    } else if (trailStyle == "zen_ink") {
        // 2.6 宣纸水墨烟云流韵
        for (const auto& p : m_trail) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float ease = 1.0f - std::pow(1.0f - progress, 2.0f);
            float alpha = (1.0f - ease) * 0.40f;
            float r = p.size * (1.0f + ease * 0.5f);
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.40f, 0.35f, alpha) : parseColor(p.color, alpha);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inkBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, inkBrush.GetAddressOf());
            if (inkBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r, r), inkBrush.Get());
            }
        }
    } else if (trailStyle == "blueprint_grid") {
        // 2.7 CAD 矢量标尺
        for (const auto& p : m_trail) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * 0.85f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.80f, 0.60f, alpha) : parseColor(p.color, alpha);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gridBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, gridBrush.GetAddressOf());
            if (gridBrush) {
                float len = 6.0f;
                m_dcRenderTarget->DrawLine(D2D1::Point2F(px - len, py), D2D1::Point2F(px + len, py), gridBrush.Get(), 1.0f);
                m_dcRenderTarget->DrawLine(D2D1::Point2F(px, py - len), D2D1::Point2F(px, py + len), gridBrush.Get(), 1.0f);
                m_dcRenderTarget->DrawRectangle(D2D1::RectF(px - 1.5f, py - 1.5f, px + 1.5f, py + 1.5f), gridBrush.Get(), 1.0f);
            }
        }
    } else if (trailStyle == "morning_dew") {
        // 2.8 晨露微气泡
        for (const auto& p : m_trail) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * 0.65f;
            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y) - progress * 14.0f; // 向上微浮动
            float r = p.size * (1.0f - progress * 0.3f);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.80f, 0.70f, alpha) : parseColor(p.color, alpha);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dewBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, dewBrush.GetAddressOf());
            if (dewBrush) {
                m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r, r), dewBrush.Get(), 1.2f);
                // 高光小白点
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shineBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha * 0.8f), shineBrush.GetAddressOf());
                if (shineBrush) {
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px - r * 0.35f, py - r * 0.35f), 1.0f, 1.0f), shineBrush.Get());
                }
            }
        }
    } else {
        // 2.9 经典彗星连线流光
        for (size_t i = 0; i < m_trail.size(); ++i) {
            const auto& p = m_trail[i];
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - p.time).count();
            float progress = (std::clamp)(static_cast<float>(el) / p.durationMs, 0.0f, 1.0f);
            float alpha = (1.0f - progress) * (1.0f - progress) * 0.85f;
            float r = p.size * (1.0f - progress * 0.65f);

            float px = static_cast<float>(p.pt.x - bounds.x);
            float py = static_cast<float>(p.pt.y - bounds.y);

            D2D1_COLOR_F c = isRainbow ? hslToRgb(p.hue, 0.85f, 0.60f, alpha) : parseColor(p.color, alpha);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trailBrush;
            m_dcRenderTarget->CreateSolidColorBrush(c, trailBrush.GetAddressOf());
            if (trailBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px, py), r, r), trailBrush.Get());
                if (i + 1 < m_trail.size()) {
                    const auto& nextP = m_trail[i + 1];
                    float nx = static_cast<float>(nextP.pt.x - bounds.x);
                    float ny = static_cast<float>(nextP.pt.y - bounds.y);
                    float dist = std::sqrt((nx - px) * (nx - px) + (ny - py) * (ny - py));
                    if (dist < 45.0f && dist > 1.0f) {
                        m_dcRenderTarget->DrawLine(
                            D2D1::Point2F(px, py),
                            D2D1::Point2F(nx, ny),
                            trailBrush.Get(),
                            r * 1.5f
                        );
                    }
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 3. 绘制鼠标点击特效 (9 款大师级风格渲染管线)
    // ─────────────────────────────────────────────────────────────────────────
    for (const auto& rip : m_ripples) {
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - rip.startTime).count();
        float progress = (std::clamp)(static_cast<float>(el) / rip.durationMs, 0.0f, 1.0f);
        float ease = 1.0f - std::pow(1.0f - progress, 3.0f); // Cubic Ease-Out
        float cx = static_cast<float>(rip.pt.x - bounds.x);
        float cy = static_cast<float>(rip.pt.y - bounds.y);
        D2D1_COLOR_F baseC = parseColor(rip.color, 1.0f);

        if (rip.style == "sparkle_burst") {
            // 3.1 星芒微粒迸发 (默认)
            for (const auto& sp : rip.sparklets) {
                float sx = cx + sp.vx * ease * (rip.maxRadius / 22.0f);
                float sy = cy + sp.vy * ease * (rip.maxRadius / 22.0f);
                float sa = (1.0f - progress) * 0.95f;
                float sr = (std::max)(0.6f, sp.size * (1.0f - progress * 0.5f));
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sparkBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, sa), sparkBrush.GetAddressOf());
                if (sparkBrush) {
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), sr, sr), sparkBrush.Get());
                }
            }
            if (progress < 0.35f) {
                float centerA = (1.0f - progress / 0.35f) * 0.90f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, centerA), cBrush.GetAddressOf());
                if (cBrush) {
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.0f, 3.0f), cBrush.Get());
                }
            }
        } else if (rip.style == "supernova") {
            // 3.2 超新星微爆发
            for (const auto& sp : rip.sparklets) {
                float sx = cx + sp.vx * ease * (rip.maxRadius / 30.0f);
                float sy = cy + sp.vy * ease * (rip.maxRadius / 30.0f);
                float sa = (1.0f - progress) * 0.95f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> photonBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, sa), photonBrush.GetAddressOf());
                if (photonBrush) {
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), 2.4f, 2.4f), photonBrush.Get());
                }
            }
            // 光子激波外环
            float shockR = 4.0f + rip.maxRadius * ease;
            float shockA = (1.0f - ease) * 0.85f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shockBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, shockA), shockBrush.GetAddressOf());
            if (shockBrush) {
                m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), shockR, shockR), shockBrush.Get(), 1.5f);
            }
        } else if (rip.style == "emp_discharge") {
            // 3.3 电磁脉冲放电
            float alpha = (1.0f - progress) * 0.95f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> empBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), empBrush.GetAddressOf());
            if (empBrush) {
                for (const auto& sp : rip.sparklets) {
                    float ex = cx + sp.vx * ease * (rip.maxRadius / 26.0f);
                    float ey = cy + sp.vy * ease * (rip.maxRadius / 26.0f);
                    float mx = (cx + ex) * 0.5f + static_cast<float>((rand() % 8) - 4);
                    float my = (cy + ey) * 0.5f + static_cast<float>((rand() % 8) - 4);
                    m_dcRenderTarget->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(mx, my), empBrush.Get(), 1.2f);
                    m_dcRenderTarget->DrawLine(D2D1::Point2F(mx, my), D2D1::Point2F(ex, ey), empBrush.Get(), 1.2f);
                }
            }
        } else if (rip.style == "ink_droplet") {
            // 3.4 宣纸墨滴晕染
            float r = 6.0f + rip.maxRadius * ease;
            float alpha = (1.0f - ease) * 0.45f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inkBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), inkBrush.GetAddressOf());
            if (inkBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), inkBrush.Get());
            }
        } else if (rip.style == "hexagon_lock") {
            // 3.5 六边形蜂巢锁定
            float hexR = (std::max)(14.0f, rip.maxRadius * (1.0f - ease * 0.55f));
            float alpha = (1.0f - progress) * 0.90f;
            float rotAngle = progress * 0.785f; // 45度平滑旋转
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hexBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), hexBrush.GetAddressOf());
            if (hexBrush) {
                D2D1_POINT_2F pts[6];
                for (int k = 0; k < 6; ++k) {
                    float a = rotAngle + static_cast<float>(k) * (3.14159265f / 3.0f);
                    pts[k] = D2D1::Point2F(cx + std::cos(a) * hexR, cy + std::sin(a) * hexR);
                }
                for (int k = 0; k < 6; ++k) {
                    m_dcRenderTarget->DrawLine(pts[k], pts[(k + 1) % 6], hexBrush.Get(), 1.4f);
                }
                // 中心准星微点
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.8f, 1.8f), hexBrush.Get());
            }
        } else if (rip.style == "bubble_pop") {
            // 3.6 微气泡轻破
            if (progress < 0.45f) {
                float bEase = progress / 0.45f;
                float r = 6.0f + 16.0f * bEase;
                float alpha = (1.0f - bEase * 0.3f) * 0.85f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), bBrush.GetAddressOf());
                if (bBrush) {
                    m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), bBrush.Get(), 1.2f);
                }
            } else {
                float popEase = (progress - 0.45f) / 0.55f;
                float sa = (1.0f - popEase) * 0.80f;
                for (const auto& sp : rip.sparklets) {
                    float sx = cx + sp.vx * popEase * 1.2f;
                    float sy = cy + sp.vy * popEase * 1.2f;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mistBrush;
                    m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, sa), mistBrush.GetAddressOf());
                    if (mistBrush) {
                        m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), 1.4f, 1.4f), mistBrush.Get());
                    }
                }
            }
        } else if (rip.style == "target_pulse") {
            // 3.7 精密雷达靶心
            float shrinkR = rip.maxRadius * (1.0f - std::pow(progress, 0.7f));
            float alpha = (1.0f - progress) * 0.90f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> targetBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), targetBrush.GetAddressOf());
            if (targetBrush) {
                float lineLen = 14.0f * (1.0f - progress * 0.35f);
                m_dcRenderTarget->DrawLine(D2D1::Point2F(cx - lineLen, cy), D2D1::Point2F(cx + lineLen, cy), targetBrush.Get(), 1.2f);
                m_dcRenderTarget->DrawLine(D2D1::Point2F(cx, cy - lineLen), D2D1::Point2F(cx, cy + lineLen), targetBrush.Get(), 1.2f);
                m_dcRenderTarget->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(cx, cy), (std::max)(2.5f, shrinkR), (std::max)(2.5f, shrinkR)),
                    targetBrush.Get(),
                    1.5f
                );
            }
        } else if (rip.style == "soft_glow") {
            // 3.8 柔光微晕气泡
            float glowR = 8.0f + rip.maxRadius * ease;
            float alpha = (1.0f - ease) * 0.45f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glowBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, alpha), glowBrush.GetAddressOf());
            if (glowBrush) {
                m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), glowR, glowR), glowBrush.Get());
            }
        } else {
            // 3.9 流体光圈冲击波 (经典双层扩散)
            float currentRadius = 6.0f + (rip.maxRadius - 6.0f) * ease;
            float alpha = (1.0f - ease) * (1.0f - progress * 0.2f);
            float strokeW = (std::max)(0.6f, 3.6f * (1.0f - ease * 0.8f));

            if (progress < 0.35f) {
                float sparkA = (1.0f - progress / 0.35f) * 0.95f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sparkBrush;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, sparkA), sparkBrush.GetAddressOf());
                if (sparkBrush) {
                    float sparkR = 3.5f * (1.0f - progress / 0.35f);
                    m_dcRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), sparkR, sparkR), sparkBrush.Get());
                }
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> waveBrush1;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, 0.95f * alpha), waveBrush1.GetAddressOf());
            if (waveBrush1) {
                m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), currentRadius, currentRadius), waveBrush1.Get(), strokeW);
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bloomBrush;
            m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, 0.25f * alpha), bloomBrush.GetAddressOf());
            if (bloomBrush) {
                m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), currentRadius + 2.5f, currentRadius + 2.5f), bloomBrush.Get(), strokeW * 1.8f);
            }

            if (progress > 0.12f) {
                float subProgress = (progress - 0.12f) / 0.88f;
                float subEase = 1.0f - std::pow(1.0f - subProgress, 2.5f);
                float subRadius = 4.0f + (rip.maxRadius * 0.68f) * subEase;
                float subAlpha = (1.0f - subEase) * 0.55f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> waveBrush2;
                m_dcRenderTarget->CreateSolidColorBrush(D2D1::ColorF(baseC.r, baseC.g, baseC.b, subAlpha), waveBrush2.GetAddressOf());
                if (waveBrush2) {
                    m_dcRenderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), subRadius, subRadius), waveBrush2.Get(), (std::max)(0.5f, strokeW * 0.6f));
                }
            }
        }
    }

    HRESULT hrDraw = m_dcRenderTarget->EndDraw();
    if (hrDraw == D2DERR_RECREATE_TARGET || hrDraw == static_cast<HRESULT>(0x887A0007L) /* DXGI_ERROR_DEVICE_RESET */) {
        discardResources();
        return;
    }

    // 4. 同步定位并提交局部/全屏分层窗口
    SetWindowPos(m_hwnd, HWND_TOPMOST, bounds.x, bounds.y, bounds.w, bounds.h,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

    POINT ptSrc = {0, 0};
    POINT ptDst = {bounds.x, bounds.y};
    SIZE sizeDst = {bounds.w, bounds.h};
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

void CALLBACK SpotlightOverlay::onTimerTick(UINT /*uTimerID*/, UINT /*uMsg*/, DWORD_PTR dwUser, DWORD_PTR /*dw1*/, DWORD_PTR /*dw2*/) {
    auto* self = reinterpret_cast<SpotlightOverlay*>(dwUser);
    if (self && self->m_hwnd) {
        PostMessageW(self->m_hwnd, WM_TIMER, TIMER_ANIM_ID, 0);
    }
}

} // namespace easy::ui
