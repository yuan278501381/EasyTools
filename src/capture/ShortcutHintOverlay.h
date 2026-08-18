#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace easy::capture {

enum class ShortcutHintContext {
    CaptureSelecting,
    CaptureSelected,
    RecordSelecting,
    ScrollCapture,
    Recording,
    RecordingPaused,
};

struct ShortcutHintItem {
    std::wstring key;
    std::wstring label;
};

/// Small, click-through contextual shortcut guide anchored to the lower-left
/// of the active monitor. It owns no timers and destroys its layered window
/// when hidden, so it has zero idle composition cost.
class ShortcutHintOverlay {
public:
    static ShortcutHintOverlay& instance();

    void show(ShortcutHintContext context, POINT anchor = {LONG_MIN, LONG_MIN});
    void hide();
    void shutdown() { hide(); }
    bool isVisible() const;
    std::vector<ShortcutHintItem> getItemsForContext(ShortcutHintContext context) const { return itemsFor(context); }

private:
    struct PositionedItem {
        ShortcutHintItem item;
        float x = 0.0f;
        float y = 0.0f;
        float keyWidth = 0.0f;
        float labelWidth = 0.0f;
    };

    ShortcutHintOverlay() = default;
    ~ShortcutHintOverlay() { hide(); }

    bool createWindow();
    bool createResources(float scale);
    std::vector<ShortcutHintItem> itemsFor(ShortcutHintContext context) const;
    float measureText(const std::wstring& text, IDWriteTextFormat* format) const;
    bool layout(const std::vector<ShortcutHintItem>& items, int workWidth, float scale,
                int& width, int& height);
    void render();
    void discardResources();
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_module = nullptr;
    ShortcutHintContext m_context = ShortcutHintContext::CaptureSelecting;
    bool m_hasContext = false;
    RECT m_workArea{};
    float m_scale = 1.0f;
    int m_width = 0;
    int m_height = 0;
    std::vector<PositionedItem> m_items;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_keyFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_labelFormat;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_panelBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_keyBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_keyTextBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_labelBrush;
};

}  // namespace easy::capture
