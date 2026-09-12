#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeFramelessWindow.h — Tools3000 纯原生微晶拟态无边框窗口外壳中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_WINDOW_NATIVEFRAMELESSWINDOW_H
#define TOOLS3000_UI_NATIVE_WINDOW_NATIVEFRAMELESSWINDOW_H

#include "ui/native/window/NativeWindowHost.h"
#include <functional>

namespace tools3000::ui::native {

enum class FramelessMode {
    Normal = 0,       // 标准现代无缝自绘无边框窗口 (如设置中心主窗体)
    Popup,            // 轻量浮动弹出层 (无缩放手柄，自动阴影)
    TrayMenu,         // 系统托盘上下文菜单 (吸附任务栏，自动收起，自适应高度)
    SpotlightCenter   // 聚光灯快速呼出中心 (居中悬浮，支持快捷键直通，如搜索中心与预览器)
};

class NativeFramelessWindow : public NativeWindowHost {
public:
    NativeFramelessWindow();
    virtual ~NativeFramelessWindow() override;

    /// 设置无边框运行模式
    void setFramelessMode(FramelessMode mode);
    FramelessMode getFramelessMode() const { return m_mode; }

    /// 设置总在最前 (TOPMOST)
    void setAlwaysOnTop(bool alwaysOnTop);
    bool isAlwaysOnTop() const { return m_alwaysOnTop; }

    /// 设置失焦时是否自动收起/隐藏
    void setAutoCloseOnBlur(bool autoClose) { m_autoCloseOnBlur = autoClose; }
    bool isAutoCloseOnBlur() const { return m_autoCloseOnBlur; }

    /// 注册窗口失焦回调
    void setOnFocusLost(std::function<void()> cb) { m_onFocusLost = std::move(cb); }

    /// 将窗口精准定位并吸附在托盘通知图标周围
    void positionNearTray(int anchorX, int anchorY, int contentW, int contentH);

    /// 将窗口居中于当前活跃屏幕
    void positionCentered(int w, int h);

    /// 动态设置物理窗口尺寸 (DIP)
    void setLogicalSize(int widthDip, int heightDip);

protected:
    virtual LRESULT handleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    FramelessMode m_mode = FramelessMode::Normal;
    bool m_alwaysOnTop = false;
    bool m_autoCloseOnBlur = false;
    std::function<void()> m_onFocusLost;
    uint64_t m_lastShowTick = 0;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_WINDOW_NATIVEFRAMELESSWINDOW_H
