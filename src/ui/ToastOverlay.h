#ifndef EASYTOOLS_UI_ToastOverlay_H
#define EASYTOOLS_UI_ToastOverlay_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <mutex>
#include <vector>

namespace easy::ui {

class ToastOverlay {
public:
    static ToastOverlay& instance();

    bool initialize(HINSTANCE hInstance);
    void shutdown();

    // 显示按键序列
    void showToast(const std::string& text);
    
    // 是否启用
    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    ToastOverlay() = default;
    ~ToastOverlay() = default;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void render();
    void createResources();
    void discardResources();
    void updateWindow();

    HWND m_hwnd = nullptr;
    bool m_enabled = false;
    
    // UI 数据
    std::string m_displayText;
    std::mutex m_mutex;
    UINT_PTR m_timerId = 0;
    float m_opacity = 0.0f;
    float m_animScale = 1.0f; // For subtle pop-up animation
    bool m_fadingOut = false;
    bool m_fadingIn = false;

    // D2D 资源
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_strokeBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
};

} // namespace easy::ui

#endif // EASYTOOLS_UI_ToastOverlay_H
