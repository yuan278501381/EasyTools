#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIElement.h — Tools3000 原生组件模型基类与层级生命周期中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_CORE_UIELEMENT_H
#define TOOLS3000_UI_NATIVE_CORE_UIELEMENT_H

#include "ui/native/common/UIGeometry.h"
#include "ui/native/common/UIEvent.h"
#include "ui/native/layout/UILayout.h"
#include "ui/native/graphics/UIRenderContext.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace tools3000::ui::native {

class NativeWindowHost;

class UIElement : public std::enable_shared_from_this<UIElement> {
public:
    UIElement();
    virtual ~UIElement();

    // ── 基础标识与层级 ────────────────────────────────────────────────────────
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void addChild(std::shared_ptr<UIElement> child);
    void removeChild(std::shared_ptr<UIElement> child);
    void clearChildren();
    const std::vector<std::shared_ptr<UIElement>>& getChildren() const { return m_children; }
    std::shared_ptr<UIElement> getParent() const { return m_parent.lock(); }
    void setParent(std::shared_ptr<UIElement> parent) { m_parent = parent; }

    void setWindowHost(NativeWindowHost* host);
    NativeWindowHost* getWindowHost() const;

    // ── 布局参数 ──────────────────────────────────────────────────────────────
    LayoutParams& layoutParams() { return m_layoutParams; }
    const LayoutParams& layoutParams() const { return m_layoutParams; }

    const Rect& bounds() const { return m_bounds; }
    void setBounds(const Rect& r) { m_bounds = r; }

    const Size& desiredSize() const { return m_desiredSize; }

    bool clipToBounds() const { return m_clipToBounds; }
    void setClipToBounds(bool clip) { m_clipToBounds = clip; }

    void setBackgroundColor(const Color& color) { m_backgroundColor = color; markNeedsPaint(); }
    const Color& getBackgroundColor() const { return m_backgroundColor; }

    // ── 状态与可见性 ──────────────────────────────────────────────────────────
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_pressed; }
    bool isFocused() const { return m_focused; }

    // ── 生命周期虚拟方法 ──────────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx);
    virtual void layout(const Rect& bounds, UIRenderContext& ctx);
    virtual void render(UIRenderContext& ctx);
    virtual bool update(float dt);

    // ── 命中测试与事件路由 ────────────────────────────────────────────────────
    virtual std::shared_ptr<UIElement> hitTest(Point pt);

    virtual bool onMouseMove(const UIMouseEvent& e);
    virtual bool onMouseDown(const UIMouseEvent& e);
    virtual bool onMouseUp(const UIMouseEvent& e);
    virtual bool onMouseWheel(const UIMouseEvent& e);
    virtual void onMouseEnter();
    virtual void onMouseLeave();

    virtual bool onKeyDown(const UIKeyEvent& e);
    virtual bool onKeyUp(const UIKeyEvent& e);
    virtual bool onChar(const UIKeyEvent& e);

    virtual void onFocus();
    virtual void onBlur();

    // ── 重绘与重排标记 ────────────────────────────────────────────────────────
    void markNeedsLayout();
    void markNeedsPaint();

protected:
    virtual void onRenderContent(UIRenderContext& ctx) { (void)ctx; }

protected:
    std::string m_id;
    std::weak_ptr<UIElement> m_parent;
    std::vector<std::shared_ptr<UIElement>> m_children;
    NativeWindowHost* m_windowHost = nullptr;

    Rect m_bounds;
    Size m_desiredSize;
    LayoutParams m_layoutParams;

    bool m_visible = true;
    bool m_enabled = true;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_focused = false;
    bool m_clipToBounds = false;
    Color m_backgroundColor = Color(0.0f, 0.0f, 0.0f, 0.0f);
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_CORE_UIELEMENT_H
