#ifndef EASYTOOLS_KEYCAST_KEYCASTOVERLAY_H
#define EASYTOOLS_KEYCAST_KEYCASTOVERLAY_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>

namespace easy::keycast {

struct KeycastSettings {
    bool enabled = true;
    bool autoBypassFullscreen = true;
    bool showKeyboard = true;
    std::string filterMode = "smart_shortcuts";
    bool includeFunctionKeys = true; // "smart_shortcuts", "with_single_modifiers", "all_keys"
    std::string position = "top_left";         // "top_left", "top_right", "bottom_left", "bottom_center", "bottom_right"
    bool mergeRecentKeys = true;
    int mergeTimeoutMs = 1200; // 同排连击合并间隔 (ms)
    int displayDurationMs = 2500;
    int fontSize = 20;
    std::string textColor = "#ffffff";
    std::string backgroundColor = "#1c1c22";
    std::string modifierKeycapColor = "auto"; // "auto" (跟随品牌色) 或 HEX 颜色
    int modifierKeycapOpacity = 65;            // 0~100 修饰键底色不透明度 (默认 65% 高雅微晶)
    std::string modifierTextColor = "#000000"; // 默认纯黑实体文字与徽标，呈现顶级键帽反差质感

    // ── 物理微动效自由配置体系 (World-Class Motion Presets & Custom Combos) ──
    std::string firstKeyAnim = "slide";      // "slide" (阻尼滑入), "pop" (气泡冒出), "fade" (渐现), "none" (无动效)
    std::string subsequentKeyAnim = "fade";  // "fade" (渐现默认), "pop" (气泡冒出), "slide" (阻尼滑入), "none" (无动效)
    bool rowCascadeAnim = true;              // 换行时旧行物理级联上推
    bool exitDriftAnim = true;               // 寿命结束时轻盈飘升消融
};

struct KeycastItem {
    std::string rawKey;
    std::vector<std::string> tokens; // e.g. ["Ctrl", "C"]
    int repeatCount = 1;
    uint64_t pushTime = 0;
    float offsetX = 0.0f;     // 横向推入偏移 (首项)
    float offsetY = 0.0f;     // 微垂直位移 (冒出项)
    float scale = 1.0f;       // 弹性气泡缩放比例
    float scaleVel = 0.0f;    // 弹性速度
    float opacity = 0.0f;     // 阻尼渐现
    bool isFirstInRow = true; // 首项推入 vs 后续项

    // ── 连击微动画物理连续性状态 (Continuous Fluid Dynamics) ──
    float badgeScale = 0.0f;          // 角标微弹簧缩放
    float badgeScaleVel = 0.0f;       // 角标弹簧速度
    float badgeOpacity = 0.0f;        // 角标平滑透明度
    float badgeWidthProgress = 0.0f;  // 胶囊向右延伸因子 (0.0f -> 1.0f)
};

struct KeycastRow {
    std::vector<KeycastItem> items;
    uint64_t lastActiveTime = 0;
    float offsetY = 0.0f;         // 行级平滑推移位移 (Cascade Upward Shift)
    float targetOffsetY = 0.0f;
    float opacity = 1.0f;
};

class KeycastOverlay {
public:
    static KeycastOverlay& instance();

    bool init(HINSTANCE hInstance);
    void cleanup();

    void setEnabled(bool enable);
    bool isEnabled() const;

    void setAutoBypassFullscreen(bool enable);
    bool autoBypassFullscreen() const;

    void setKeyboardVisible(bool visible);
    bool isKeyboardVisible() const;

    void pushKey(const std::string& keyStr);

    KeycastSettings getSettings() const;
    void updateSettings(const KeycastSettings& settings);
    void resetDefaults();

    // 单元测试与精确度量导出接口
    float calculateItemWidth(const KeycastItem& item, float dpiScale) const;
    void tickAnimation();
    void render();

private:
    KeycastOverlay();
    ~KeycastOverlay();

    KeycastOverlay(const KeycastOverlay&) = delete;
    KeycastOverlay& operator=(const KeycastOverlay&) = delete;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool updatePlacement();
    bool createResources();
    void discardResources();

    void drawKeycapCapsule(const KeycastItem& item, float startX, float startY, float alpha, float dpiScale);
    void drawWindowsLogo(const D2D1_RECT_F& rect, float alpha);

    D2D1_COLOR_F parseColor(const std::string& hex, float alpha) const;
    std::vector<std::string> splitTokens(const std::string& keyStr) const;

    HWND m_hwnd = nullptr;
    HWND m_helperOwnerHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    float m_dpiScale = 1.0f;
    int m_width = 800;
    int m_height = 200;

    std::atomic<bool> m_enabled{true};
    KeycastSettings m_settings;
    mutable std::mutex m_settingsMutex;

    mutable std::mutex m_mutex;
    std::vector<KeycastRow> m_rows;
    uint64_t m_lastGlobalPushTime = 0;
    bool m_updatingPlacement = false;
    std::string m_lastAccentColor = "";

    // Direct2D & DirectWrite 生产级硬件加速管线
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_keycapTextFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_repeatTextFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_plusTextFormat;

    // 动态画刷体系 (全量配置与微晶反差)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushModifierText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushKeycapBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushKeycapBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFuncKeyBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushFuncKeyBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBadgeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBadgeBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushPlusText;

    HDC m_memoryDC = nullptr;
    HBITMAP m_memoryBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
};

} // namespace easy::keycast

#endif // EASYTOOLS_KEYCAST_KEYCASTOVERLAY_H
