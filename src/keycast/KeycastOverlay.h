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
    bool onlyShortcuts = false;
    int displayDurationMs = 3000;
    int fontSize = 20;
    std::string textColor = "#ffffff";
    std::string backgroundColor = "#202020";
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
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HWND m_helperOwnerHwnd = nullptr;
    KeycastSettings m_settings;
    mutable std::mutex m_settingsMutex;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBadgeBg;

    HDC m_memoryDC = nullptr;
    HBITMAP m_memoryBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_width = 800;
    int m_height = 160;
    float m_dpiScale = 1.0f;
    bool m_updatingPlacement = false;

    std::string m_currentText;
    std::string m_rawLastKey;
    int m_repeatCount = 1;
    uint64_t m_lastPushTime = 0;
    
    // Animation states
    float m_opacity = 0.0f;
    float m_scale = 0.8f;
    bool m_animating = false;

    std::mutex m_mutex;
};

} // namespace easy::keycast

#endif // EASYTOOLS_KEYCAST_KEYCASTOVERLAY_H
