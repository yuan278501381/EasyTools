#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeView.h — Tools3000 纯原生多态通用视图基类
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_CORE_NATIVEVIEW_H
#define TOOLS3000_UI_NATIVE_CORE_NATIVEVIEW_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <memory>

namespace tools3000::ui::native {

class NativeWindowHost;

class NativeView : public UIElement {
public:
    NativeView();
    virtual ~NativeView() override;

    /// 视图名称标识（如 "TrayView", "SearchView", "QuickLookView"）
    virtual std::string getViewName() const = 0;

    /// 挂载到宿主窗口
    virtual void onAttachedToHost(NativeWindowHost* host);

    /// 从宿主窗口卸载
    virtual void onDetachedFromHost();

    /// 视图获得激活/呼出
    virtual void onActivated();

    /// 视图失焦或进入后台
    virtual void onDeactivated();

    /// 宿主窗口物理尺寸或最大化状态变更通知
    virtual void onWindowSizeChanged(int w, int h, bool isMaximized);

    /// 宿主底层 Win32 消息透传拦截钩子（返回 true 表示已消费该消息）
    virtual bool handleWindowMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& result);

    /// 快速获取关联的宿主窗口
    NativeWindowHost* getHost() const { return m_host; }

    /// 便捷请求宿主重排与重绘
    void requestHostLayout();
    void requestHostPaint();
    void closeHostWindow();

protected:
    NativeWindowHost* m_host = nullptr;
    bool m_isAttached = false;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_CORE_NATIVEVIEW_H
