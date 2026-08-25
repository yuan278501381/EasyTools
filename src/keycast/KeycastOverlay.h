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

class KeycastOverlay {
public:
    static KeycastOverlay& instance();

    bool init();
    void cleanup();
    
    // push a new keystroke combination to display
    void pushKey(const std::string& keyStr);

    /// 全屏免打扰
    void setAutoBypassFullscreen(bool enable) { m_autoBypassFullscreen.store(enable); }
    bool autoBypassFullscreen() const { return m_autoBypassFullscreen.load(); }

private:
    KeycastOverlay() = default;
    ~KeycastOverlay() = default;

    bool createResources();
    void discardResources();
    bool updatePlacement();
    void render();
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    std::atomic<bool> m_autoBypassFullscreen{true};
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
