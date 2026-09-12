#include "ui/native/core/UIElement.h"
#include "ui/native/window/NativeWindowHost.h"
#include <algorithm>

namespace tools3000::ui::native {

UIElement::UIElement() = default;

UIElement::~UIElement() {
    clearChildren();
}

void UIElement::addChild(std::shared_ptr<UIElement> child) {
    if (!child) return;
    try {
        child->m_parent = weak_from_this();
    } catch (...) {
    }
    child->setWindowHost(m_windowHost);
    m_children.push_back(child);
    markNeedsLayout();
}

void UIElement::removeChild(std::shared_ptr<UIElement> child) {
    if (!child) return;
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        child->m_parent.reset();
        child->setWindowHost(nullptr);
        m_children.erase(it);
        markNeedsLayout();
    }
}

void UIElement::clearChildren() {
    for (auto& child : m_children) {
        if (child) {
            child->m_parent.reset();
            child->setWindowHost(nullptr);
        }
    }
    m_children.clear();
    markNeedsLayout();
}

void UIElement::setWindowHost(NativeWindowHost* host) {
    m_windowHost = host;
    for (auto& child : m_children) {
        if (child) {
            try {
                child->m_parent = weak_from_this();
            } catch (...) {}
            child->setWindowHost(host);
        }
    }
}

NativeWindowHost* UIElement::getWindowHost() const {
    if (m_windowHost) return m_windowHost;
    auto p = m_parent.lock();
    return p ? p->getWindowHost() : nullptr;
}

void UIElement::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        markNeedsLayout();
    }
}

void UIElement::markNeedsLayout() {
    auto host = getWindowHost();
    if (host) {
        host->requestLayout();
    }
}

void UIElement::markNeedsPaint() {
    auto host = getWindowHost();
    if (host) {
        host->requestPaint();
    }
}

Size UIElement::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    if (!m_visible) {
        m_desiredSize = Size(0.0f, 0.0f);
        return m_desiredSize;
    }

    m_desiredSize = UILayout::measureChildren(m_children, m_layoutParams, availableWidth, availableHeight, ctx);
    return m_desiredSize;
}

void UIElement::layout(const Rect& bounds, UIRenderContext& ctx) {
    m_bounds = bounds;
    if (!m_visible) return;

    for (auto& child : m_children) {
        if (child) {
            try {
                child->m_parent = weak_from_this();
            } catch (...) {}
        }
    }

    UILayout::arrangeChildren(m_children, m_layoutParams, m_bounds, ctx);
}

void UIElement::render(UIRenderContext& ctx) {
    if (!m_visible) return;

    if (m_clipToBounds) {
        ctx.pushClip(m_bounds);
    }

    if (m_backgroundColor.a > 0.0f) {
        ctx.fillRect(m_bounds, m_backgroundColor);
    }

    onRenderContent(ctx);

    for (const auto& child : m_children) {
        if (child && child->isVisible()) {
            child->render(ctx);
        }
    }

    if (m_clipToBounds) {
        ctx.popClip();
    }
}

bool UIElement::update(float dt) {
    bool anyActive = false;
    for (const auto& child : m_children) {
        if (child) {
            anyActive |= child->update(dt);
        }
    }
    return anyActive;
}

std::shared_ptr<UIElement> UIElement::hitTest(Point pt) {
    if (!m_visible || !m_enabled || !m_bounds.contains(pt)) {
        return nullptr;
    }

    // 从后往前查找顶层子元素
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        const auto& child = *it;
        if (child && child->isVisible() && child->isEnabled()) {
            auto hit = child->hitTest(pt);
            if (hit) return hit;
        }
    }

    return shared_from_this();
}

bool UIElement::onMouseMove(const UIMouseEvent& e) {
    (void)e;
    return false;
}

bool UIElement::onMouseDown(const UIMouseEvent& e) {
    m_pressed = true;
    markNeedsPaint();
    (void)e;
    return false;
}

bool UIElement::onMouseUp(const UIMouseEvent& e) {
    m_pressed = false;
    markNeedsPaint();
    (void)e;
    return false;
}

bool UIElement::onMouseWheel(const UIMouseEvent& e) {
    (void)e;
    return false;
}

void UIElement::onMouseEnter() {
    m_hovered = true;
    markNeedsPaint();
}

void UIElement::onMouseLeave() {
    m_hovered = false;
    m_pressed = false;
    markNeedsPaint();
}

bool UIElement::onKeyDown(const UIKeyEvent& e) {
    (void)e;
    return false;
}

bool UIElement::onKeyUp(const UIKeyEvent& e) {
    (void)e;
    return false;
}

bool UIElement::onChar(const UIKeyEvent& e) {
    (void)e;
    return false;
}

void UIElement::onFocus() {
    m_focused = true;
    markNeedsPaint();
}

void UIElement::onBlur() {
    m_focused = false;
    markNeedsPaint();
}

} // namespace tools3000::ui::native
