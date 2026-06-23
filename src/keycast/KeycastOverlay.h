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

namespace easy::keycast {

class KeycastOverlay {
public:
    static KeycastOverlay& instance();

    bool init();
    void cleanup();
    
    // push a new keystroke combination to display
    void pushKey(const std::string& keyStr);

private:
    KeycastOverlay() = default;
    ~KeycastOverlay() = default;

    bool createResources();
    void render();
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBorder;

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
