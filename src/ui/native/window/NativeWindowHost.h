#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeWindowHost.h — Tools3000 纯原生 Direct2D 1.1 + DirectWrite 窗口宿主
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_WINDOW_NATIVEWINDOWHOST_H
#define TOOLS3000_UI_NATIVE_WINDOW_NATIVEWINDOWHOST_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <chrono>

#include "ui/native/common/UIGeometry.h"
#include "ui/native/core/UIElement.h"
#include "ui/native/graphics/UIRenderContext.h"

namespace tools3000::ui::native {

class UIScrollView;

struct NativeWindowConfig {
    std::wstring className;
    std::wstring title = L"Tools3000";
    int width = 1040;
    int height = 680;
    int minWidth = 720;
    int minHeight = 480;
    bool centerOnScreen = true;
    bool resizable = true;
    bool seamlessTitlebar = true;
    bool isPopup = false;
    bool isToolWindow = false;
    bool alwaysOnTop = false;
};

class NativeWindowHost {
public:
    NativeWindowHost();
    virtual ~NativeWindowHost();

    /// 创建窗口
    bool create(HINSTANCE hInstance, const NativeWindowConfig& config = NativeWindowConfig());

    /// 显示窗口
    void show(int cmdShow = SW_SHOW);

    /// 隐藏窗口并在冷路径主动执行工作集内存修剪 (trimWorkingSet)
    void hide();

    /// 销毁窗口
    void destroy();

    /// 设置根 UI 元素
    void setRootElement(std::shared_ptr<UIElement> root);
    std::shared_ptr<UIElement> getRootElement() const { return m_rootElement; }

    HWND hwnd() const { return m_hwnd; }
    bool isVisible() const { return m_hwnd && IsWindow(m_hwnd) && IsWindowVisible(m_hwnd); }
    bool isMaximized() const;

    /// 窗口控制操作
    void minimize();
    void toggleMaximize();
    void close();

    /// 请求重排与重绘
    void requestLayout();
    void requestPaint();

    /// 弹出层/浮动面板支持 (下拉菜单、浮动面板、右键菜单)
    void showPopup(std::shared_ptr<UIElement> popup, const Rect& anchorRect = Rect(), std::function<void()> onDismiss = nullptr);
    void closePopup();
    bool hasActivePopup() const { return m_activePopup != nullptr; }
    std::shared_ptr<UIElement> getActivePopup() const { return m_activePopup; }

    float getDpiScale() const { return m_dpiScale; }

    /// 窗口尺寸或最大化状态变化回调
    void setOnSizeChanged(std::function<void(int w, int h, bool isMaximized)> cb) { m_onSizeChanged = std::move(cb); }

    /// 设置与获取当前键盘焦点元素
    void setFocusedElement(std::shared_ptr<UIElement> element);
    std::shared_ptr<UIElement> getFocusedElement() const { return m_focusedElement; }

    /// 启动或刷新动画循环
    void wakeAnimationTimer();

protected:
    virtual LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool initDirect2D();
    void resizeD2D(UINT width, UINT height);
    void paint();
    bool updateAnimations();
    static std::shared_ptr<UIScrollView> findScrollViewAt(const std::shared_ptr<UIElement>& root, Point pt);

    void applyRoundedCorners();
    EventModifiers getKeyboardModifiers() const;

private:
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    NativeWindowConfig m_config;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;

    std::shared_ptr<UIElement> m_rootElement;
    std::shared_ptr<UIElement> m_hoveredElement;
    std::shared_ptr<UIElement> m_focusedElement;
    std::shared_ptr<UIElement> m_capturedElement;

    std::shared_ptr<UIElement> m_activePopup;
    Rect m_activePopupAnchor;
    std::function<void()> m_onPopupDismiss;
    std::function<void(int, int, bool)> m_onSizeChanged;

    float m_dpiScale = 1.0f;
    bool m_needsLayout = true;
    bool m_needsPaint = true;
    bool m_isTimerActive = false;

    std::chrono::steady_clock::time_point m_lastFrameTime;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_WINDOW_NATIVEWINDOWHOST_H
