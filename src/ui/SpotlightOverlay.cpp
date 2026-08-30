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
            LOG_WARN("聚光灯颜色格式无效，使用默认蓝色: {}", hexStr);
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
