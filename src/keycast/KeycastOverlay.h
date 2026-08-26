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
    std::string filterMode = "smart_shortcuts"; // "smart_shortcuts", "with_single_modifiers", "all_keys"
    std::string position = "top_left";         // "top_left", "top_right", "bottom_left", "bottom_center", "bottom_right"
    bool mergeRecentKeys = true;
    int mergeTimeoutMs = 1200; // 同排连击合并间隔 (ms)
    int displayDurationMs = 2500;
    int fontSize = 18;
    std::string textColor = "#ffffff";
    std::string backgroundColor = "#1c1c22";
};

struct KeycastItem {
    std::string rawKey;
    std::vector<std::string> tokens; // e.g. ["Ctrl", "C"]
    int repeatCount = 1;
    uint64_t pushTime = 0;
    float offsetX = 28.0f; // 从右向左阻尼推入
    float opacity = 0.0f;  // 阻尼渐现
};

struct KeycastRow {
    std::vector<KeycastItem> items;
    uint64_t lastActiveTime = 0;
    float offsetY = 0.0f; // 向上平移换行
    float opacity = 1.0f;
};

class KeycastOverlay {
public:
    static KeycastOverlay& instance();

    bool init();
    void cleanup();
    
    // push a new keystroke combination to display
    void pushKey(const std::string& keyStr);

    /// 获取配置
    KeycastSettings getSettings() const;

    /// 更新配置
    void updateSettings(const KeycastSettings& settings);

    /// 恢复默认配置
    void resetDefaults();

    /// 全屏免打扰
    void setAutoBypassFullscreen(bool enable);
    bool autoBypassFullscreen() const;

    /// 颜色解析（支持 auto 与十六进制 HEX）
    D2D1_COLOR_F parseColor(const std::string& hex, float alpha = 1.0f) const;

private:
    KeycastOverlay() = default;
    ~KeycastOverlay() = default;

    bool createResources();
    void discardResources();
    bool updatePlacement();
    void render();
    void tickAnimation();
    void drawKeycapCapsule(const KeycastItem& item, float x, float y, float alpha, float dpiScale);
    void drawWindowsLogo(const D2D1_RECT_F& rect, float alpha);

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HWND m_helperOwnerHwnd = nullptr;
    KeycastSettings m_settings;
    mutable std::mutex m_settingsMutex;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_keycapTextFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushKeycapBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushKeycapBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushPlusText;

    HDC m_memoryDC = nullptr;
    HBITMAP m_memoryBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_width = 720;
    int m_height = 200;
    float m_dpiScale = 1.0f;
    bool m_updatingPlacement = false;
    bool m_timerRunning = false;

    std::vector<KeycastRow> m_rows;
    std::string m_lastRawKey;
    uint64_t m_lastGlobalPushTime = 0;

    std::mutex m_mutex;
};

} // namespace easy::keycast

#endif // EASYTOOLS_KEYCAST_KEYCASTOVERLAY_H
