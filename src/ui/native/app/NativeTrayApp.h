#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeTrayApp.h — Tools3000 纯原生微晶拟态系统托盘应用中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_APP_NATIVETRAYAPP_H
#define TOOLS3000_UI_NATIVE_APP_NATIVETRAYAPP_H

#include "ui/native/core/NativeView.h"
#include "ui/native/window/NativeFramelessWindow.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace tools3000::ui::native {

struct TrayPillItem {
    std::string id;
    std::string pluginId;
    std::wstring label;
    std::wstring tooltip;
    IconType icon;
    bool active = false;
};

struct TrayMenuItemDef {
    std::string id;
    std::wstring label;
    IconType icon;
    bool isDanger = false;
    bool isCheckable = false;
    bool checked = false;
    std::function<void()> action;
};

class NativeTrayView : public NativeView {
public:
    NativeTrayView();
    virtual ~NativeTrayView() override;

    virtual std::string getViewName() const override { return "TrayView"; }

    /// 刷新全部插件与提权状态
    void refreshState();

    /// 切换快捷开关胶囊状态
    void togglePill(const std::string& pillId);

    /// 重构菜单项与布局
    void rebuildMenu();

    /// 诊断与单测接口
    size_t getPillCount() const { return m_pills.size(); }
    const TrayPillItem& getPill(size_t idx) const { return m_pills[idx]; }
    size_t getMenuItemCount() const { return m_menuItems.size(); }
    void setPendingRestart(bool v) { m_pendingRestart = v; markNeedsLayout(); }
    bool isPendingRestart() const { return m_pendingRestart; }

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 鼠标交互 ────────────────────────────────────────────────────────────
    virtual bool onMouseDown(const UIMouseEvent& e) override;
    virtual bool onMouseMove(const UIMouseEvent& e) override;
    virtual void onMouseLeave() override;

private:
    void initControls();
    void updateDimensions();

private:
    std::vector<TrayPillItem> m_pills;
    std::vector<TrayMenuItemDef> m_menuItems;
    bool m_pendingRestart = false;
    bool m_isElevated = false;
    int m_hoveredIndex = -1; // < 100: pill, >= 100: menu item
    float m_spinAngle = 0.0f;
};

class NativeTrayApp {
public:
    static NativeTrayApp& instance();

    /// 预热托盘渲染环境
    void preload(HINSTANCE hInstance = nullptr);

    /// 在指定坐标附近呼出微晶托盘悬浮菜单
    void show(HINSTANCE hInstance, int anchorX, int anchorY);

    /// 隐藏托盘菜单并在冷路径修剪工作集物理内存
    void hide();

    /// 销毁
    void destroy();

    /// 是否可见
    bool isVisible() const;

    HWND hwnd() const { return m_host ? m_host->hwnd() : nullptr; }
    NativeFramelessWindow* getHost() { return m_host.get(); }
    std::shared_ptr<NativeTrayView> getView() const { return m_view; }

private:
    NativeTrayApp();
    ~NativeTrayApp();
    NativeTrayApp(const NativeTrayApp&) = delete;
    NativeTrayApp& operator=(const NativeTrayApp&) = delete;

    void ensureInitialized(HINSTANCE hInstance);

private:
    std::unique_ptr<NativeFramelessWindow> m_host;
    std::shared_ptr<NativeTrayView> m_view;
    int m_anchorX = 0;
    int m_anchorY = 0;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_APP_NATIVETRAYAPP_H
