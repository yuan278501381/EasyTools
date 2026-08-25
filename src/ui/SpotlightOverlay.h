#ifndef EASYTOOLS_UI_SPOTLIGHTOVERLAY_H
#define EASYTOOLS_UI_SPOTLIGHTOVERLAY_H

#include <windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

namespace easy::ui {

struct SpotlightSettings {
    bool enabled = true;
    bool triggerDoubleCtrl = true;
    bool triggerShakeMouse = false;
    bool autoBypassFullscreen = true;
    std::string spotlightColor = "auto";
    int spotlightSize = 200;           // 直径 (像素)
    int animationDurationMs = 1000;    // 渐变动画速度 (ms)
    int holdDurationMs = 800;          // 停留保持时间 (ms)
    int shakeThreshold = 7;            // 摇晃检测灵敏度 (默认 7)

    // 鼠标点击与轨迹特效 (演示辅助)
    bool clickRippleEnabled = false;
    bool mouseTrailEnabled = false;
    std::string clickRippleStyle = "ripple_ring";      // ripple_ring, sparkle_burst, target_pulse, soft_glow
    std::string mouseTrailStyle = "stardust_orbs";     // stardust_orbs, aurora_ribbon, sonar_pulses, classic_comet
    std::string mouseTrailColorMode = "rainbow";       // rainbow, accent
    std::string leftClickColor = "auto";
    std::string rightClickColor = "#fb7185";
    std::string middleClickColor = "#fbbf24";
};

class SpotlightOverlay {
public:
    static SpotlightOverlay& instance();

    bool initialize(HINSTANCE hInstance);
    void shutdown();

    /// 触发聚光灯，若 pt={0,0} 且 autoFetch=true 则自动采集当前光标坐标
    void trigger(POINT pt = {0, 0}, bool autoFetch = true);

    /// 立即取消或提前平滑淡出聚光灯（例如按键按下或鼠标点击）
    void dismiss();

    /// 设置与持久化
    void updateSettings(const SpotlightSettings& settings);
    SpotlightSettings getSettings() const;
    void resetDefaults();

    /// 是否处于活跃/显示状态
    bool isActive() const;

    /// 钩子快速检测辅助
    void onKeyboardEvent(DWORD vkCode, WPARAM wParam);
    void onMouseMove(POINT pt);
    void onMouseDown(int button, POINT pt); // 0: Left, 1: Right, 2: Middle
    void tickAnimation();
    D2D1_COLOR_F parseColor(const std::string& hexStr, float alpha) const;
    static D2D1_COLOR_F hslToRgb(float h, float s, float l, float alpha);

    struct ViewportBounds {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool isFullscreen = false;
    };
    ViewportBounds calculateViewportBoundsLocked() const;

    enum class TrailParticleKind {
        OrbMain,     // 主能量光球 (6.5px ~ 8.0px, 带微光晕)
        OrbSub,      // 次级漂浮球 (3.5px ~ 5.0px)
        Sparklet,    // 伴生微星 (1.5px ~ 2.5px, 随机微偏移)
        RibbonNode,  // 极光流丝平滑节点
        SonarRing,   // 扩散声纳微环
        CometDot     // 经典彗星连线点
    };

    struct ClickSparkle {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float size = 3.0f;
    };

private:
    SpotlightOverlay() = default;
    ~SpotlightOverlay() = default;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void render();
    bool createResources();

    void discardResources();
    bool ensureSurface(int width, int height);
    void releaseSurface();
    void hideNow();

    HWND m_hwnd = nullptr;
    HWND m_helperOwnerHwnd = nullptr;
    SpotlightSettings m_settings;
    mutable std::mutex m_mutex;

    // 聚光灯动效状态机
    enum class AnimState {
        Idle,
        FadeIn,
        Holding,
        FadeOut
    };
    AnimState m_animState = AnimState::Idle;
    std::chrono::steady_clock::time_point m_animStartTime;
    std::chrono::steady_clock::time_point m_holdStartTime;
    POINT m_targetPos{0, 0};
    float m_currentAlpha = 0.0f;
    UINT_PTR m_animTimerId = 0;
    bool m_timerRunning = false;

    // 双击 Ctrl 检测状态
    std::chrono::steady_clock::time_point m_lastCtrlDownTime{};
    int m_ctrlPressCount = 0;

    // 摇晃鼠标检测状态
    POINT m_lastMousePos{0, 0};
    int m_lastMoveDir = 0;
    int m_shakeReversals = 0;
    std::chrono::steady_clock::time_point m_shakeWindowStart{};

    // 点击水波纹特效数据
    struct ClickRipple {
        POINT pt;
        std::chrono::steady_clock::time_point startTime;
        std::string color;
        std::string style = "ripple_ring";
        float maxRadius = 42.0f;
        float durationMs = 450.0f;
        std::vector<ClickSparkle> sparklets;
    };
    std::vector<ClickRipple> m_ripples;

    // 鼠标流光/星尘轨迹微粒数据
    struct TrailParticle {
        POINT pt;
        std::chrono::steady_clock::time_point time;
        TrailParticleKind kind = TrailParticleKind::OrbMain;
        float size = 7.5f;
        float durationMs = 280.0f;
        std::string color;
        float hue = 0.0f;
    };
    std::vector<TrailParticle> m_trail;
    float m_trailHue = 195.0f; // 七彩色相累加器

    // DIB Surface
    HDC m_memDc = nullptr;
    HBITMAP m_memBmp = nullptr;
    HBITMAP m_oldBmp = nullptr;
    void* m_bmpBits = nullptr;
    int m_surfaceW = 0;
    int m_surfaceH = 0;

    // Direct2D 资源
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_dcRenderTarget;
};

} // namespace easy::ui

#endif // EASYTOOLS_UI_SPOTLIGHTOVERLAY_H
