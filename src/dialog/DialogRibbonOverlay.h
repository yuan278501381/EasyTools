/**
 * EasyTools - High Performance Windows Productivity Suite
 * 
 * Copyright (c) 2026 Yy1 (GitHub yuan278501381) <https://github.com/yuan278501381> & EasyTools contributors
 * 
 * Licensed under the MIT License.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace easy::dialog {

struct RibbonButtonRect {
    enum class Type { ExplorerSync, Recent, Favorites };
    Type type;
    D2D1_RECT_F rect;
    std::string text;
    std::string extraData;
    bool hovered{false};
};

class DialogRibbonOverlay {
public:
    static DialogRibbonOverlay& instance();

    bool init();
    void cleanup();

    // 绑定并展示在指定文件对话框顶部
    void attachToDialog(HWND dialogHwnd, const std::string& processName);
    void updatePosition();
    void hide();

    HWND getHwnd() const { std::lock_guard lock(m_mutex); return m_hwnd; }
    HWND getTargetDialog() const { std::lock_guard lock(m_mutex); return m_targetDialog; }

private:
    DialogRibbonOverlay();
    ~DialogRibbonOverlay();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool doUpdatePosition();
    void doAttachToDialog();
    void doHide();
    void doHideLocked();
    void render();
    void updateButtonsLayout(float dpiScale);
    void onLButtonDown(int x, int y);
    void onMouseMove(int x, int y);
    void onMouseLeave();
    void showRecentMenu(int screenX, int screenY);
    void showFavoritesMenu(int screenX, int screenY);

    HWND m_hwnd{nullptr};
    HWND m_targetDialog{nullptr};
    DWORD m_targetProcessId{0};
    std::string m_targetProcess;
    std::string m_activeExplorerPath;

    int m_width{360};
    int m_height{38};
    float m_dpiScale{1.0f};

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_boldFormat;

    HDC m_memDC{nullptr};
    HBITMAP m_memBitmap{nullptr};
    HBITMAP m_oldBitmap{nullptr};
    int m_renderedWidth{0};
    int m_renderedHeight{0};
    bool m_menuOpen{false};

    std::vector<RibbonButtonRect> m_buttons;
    bool m_trackingMouse{false};
    mutable std::mutex m_mutex;
};

} // namespace easy::dialog
