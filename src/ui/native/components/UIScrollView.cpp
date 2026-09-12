#include "ui/native/components/UIScrollView.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/window/NativeWindowHost.h"
#include <algorithm>

namespace tools3000::ui::native {

UIScrollView::UIScrollView(std::shared_ptr<UIElement> content) {
    m_clipToBounds = true;
    m_content = content;
    if (m_content) {
        m_children.push_back(m_content);
    }
}

void UIScrollView::setContent(std::shared_ptr<UIElement> content) {
    if (m_content) {
        removeChild(m_content);
    }
    m_content = content;
    if (m_content) {
        addChild(m_content);
    }
    markNeedsLayout();
}

float UIScrollView::getMaxScrollOffset() const {
    if (!m_content) return 0.0f;
    return std::max(0.0f, m_content->desiredSize().height - m_bounds.height());
}

void UIScrollView::scrollTo(float offsetY, bool animated) {
    float maxScroll = getMaxScrollOffset();
    float clamped = std::clamp(offsetY, 0.0f, maxScroll);
    if (animated) {
        m_scrollY.setTarget(clamped, 180.0f, EasingType::SmoothCubic);
    } else {
        m_scrollY.setImmediate(clamped);
    }
    m_scrollbarAlpha.setTarget(1.0f, 100.0f, EasingType::SmoothCubic);
    m_scrollbarFadeTimer = 1.5f;
    markNeedsPaint();
}

Size UIScrollView::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    if (m_content && m_content->isVisible()) {
        const auto& cp = m_content->layoutParams();
        float targetW = availableWidth;
        if (cp.maxWidth > 0.0f && cp.maxWidth < targetW) {
            targetW = cp.maxWidth;
        }
        m_content->measure(targetW, 100000.0f, ctx);
    }
    m_desiredSize = Size(availableWidth, availableHeight);
    return m_desiredSize;
}

void UIScrollView::updateContentLayout(UIRenderContext& ctx) {
    if (!m_visible || !m_content || !m_content->isVisible()) return;

    float currentScroll = m_scrollY.get();
    float maxScroll = getMaxScrollOffset();
    if (currentScroll > maxScroll) {
        m_scrollY.setImmediate(maxScroll);
        currentScroll = maxScroll;
    }

    const float contentH = m_content->desiredSize().height;
    float contentW = m_bounds.width();
    float contentLeft = m_bounds.left;
    const auto& cp = m_content->layoutParams();
    if (cp.maxWidth > 0.0f && cp.maxWidth < contentW) {
        contentW = cp.maxWidth;
        contentLeft = m_bounds.left + (m_bounds.width() - contentW) * 0.5f;
    }

    Rect contentBounds(contentLeft, m_bounds.top - currentScroll, contentLeft + contentW, m_bounds.top - currentScroll + contentH);
    m_content->layout(contentBounds, ctx);
}

void UIScrollView::layout(const Rect& bounds, UIRenderContext& ctx) {
    m_bounds = bounds;
    if (!m_visible || !m_content || !m_content->isVisible()) return;

    if (m_content) {
        try { m_content->setParent(shared_from_this()); } catch (...) {}
        m_content->setWindowHost(getWindowHost());
    }

    updateContentLayout(ctx);
}

bool UIScrollView::update(float dt) {
    bool dirty = m_scrollY.update(dt);
    dirty |= m_scrollbarAlpha.update(dt);

    if (m_scrollbarFadeTimer > 0.0f) {
        m_scrollbarFadeTimer -= dt;
        if (m_scrollbarFadeTimer <= 0.0f && !m_isDraggingThumb) {
            m_scrollbarAlpha.setTarget(0.0f, 300.0f, EasingType::SmoothCubic);
            dirty = true;
        }
    }

    if (dirty) {
        float dpi = 1.0f;
        auto host = getWindowHost();
        if (host) dpi = host->getDpiScale();
        UIRenderContext dummyCtx(nullptr, nullptr, dpi);
        updateContentLayout(dummyCtx);
        markNeedsPaint();
    }

    bool active = m_scrollY.isAnimating() || m_scrollbarAlpha.isAnimating() || (m_scrollbarFadeTimer > 0.0f);
    active |= UIElement::update(dt);
    return active;
}

Rect UIScrollView::getScrollbarTrackRect() const {
    return Rect(m_bounds.right - 8.0f, m_bounds.top + 4.0f, m_bounds.right - 2.0f, m_bounds.bottom - 4.0f);
}

Rect UIScrollView::getScrollbarThumbRect() const {
    Rect track = getScrollbarTrackRect();
    if (!m_content || m_bounds.height() <= 0.0f) return Rect();

    float contentH = m_content->desiredSize().height;
    if (contentH <= m_bounds.height()) return Rect();

    float ratio = m_bounds.height() / contentH;
    float thumbH = std::max(24.0f, track.height() * ratio);
    float maxScroll = getMaxScrollOffset();
    float scrollRatio = (maxScroll > 0.0f) ? (m_scrollY.get() / maxScroll) : 0.0f;
    float thumbTop = track.top + (track.height() - thumbH) * scrollRatio;

    return Rect(track.left, thumbTop, track.right, thumbTop + thumbH);
}

bool UIScrollView::onMouseWheel(const UIMouseEvent& e) {
    if (!m_enabled) return false;
    float maxScroll = getMaxScrollOffset();
    if (maxScroll <= 0.0f) return false;

    // 每次滚轮滚动 70px，配合三次贝塞尔平滑插值产生极为顺滑的物理阻尼感
    float newOffset = m_scrollY.target() - e.wheelDelta * 0.7f;
    scrollTo(newOffset, true);
    return true;
}

bool UIScrollView::onMouseDown(const UIMouseEvent& e) {
    if (e.button == MouseButton::Left && m_enabled && getMaxScrollOffset() > 0.0f) {
        Rect thumb = getScrollbarThumbRect();
        Rect track = getScrollbarTrackRect();
        Rect hitArea(m_bounds.right - 16.0f, m_bounds.top, m_bounds.right, m_bounds.bottom);
        if (hitArea.contains(e.position)) {
            if (thumb.contains(e.position)) {
                m_isDraggingThumb = true;
                m_dragStartY = e.position.y;
                m_dragStartScrollY = m_scrollY.get();
                m_scrollbarAlpha.setTarget(1.0f, 50.0f, EasingType::SmoothCubic);
                return true;
            } else {
                float availableTrack = track.height() - thumb.height();
                if (availableTrack > 0.0f) {
                    float clickRatio = std::clamp((e.position.y - track.top - thumb.height() * 0.5f) / availableTrack, 0.0f, 1.0f);
                    scrollTo(clickRatio * getMaxScrollOffset(), true);
                    return true;
                }
            }
        }
    }
    return UIElement::onMouseDown(e);
}

bool UIScrollView::onMouseMove(const UIMouseEvent& e) {
    if (m_isDraggingThumb) {
        Rect track = getScrollbarTrackRect();
        Rect thumb = getScrollbarThumbRect();
        float availableTrack = track.height() - thumb.height();
        if (availableTrack > 0.0f) {
            float deltaY = e.position.y - m_dragStartY;
            float scrollDelta = deltaY / availableTrack * getMaxScrollOffset();
            scrollTo(m_dragStartScrollY + scrollDelta, false);
        }
        return true;
    }
    if (m_bounds.contains(e.position) && e.position.x >= m_bounds.right - 16.0f && getMaxScrollOffset() > 0.0f) {
        m_scrollbarAlpha.setTarget(1.0f, 100.0f, EasingType::SmoothCubic);
        m_scrollbarFadeTimer = 1.2f;
        markNeedsPaint();
    }
    return UIElement::onMouseMove(e);
}

bool UIScrollView::onMouseUp(const UIMouseEvent& e) {
    if (m_isDraggingThumb) {
        m_isDraggingThumb = false;
        m_scrollbarFadeTimer = 1.0f;
        return true;
    }
    return UIElement::onMouseUp(e);
}

void UIScrollView::onMouseLeave() {
    UIElement::onMouseLeave();
    if (!m_isDraggingThumb) {
        m_scrollbarFadeTimer = 0.5f;
    }
}

std::shared_ptr<UIElement> UIScrollView::hitTest(Point pt) {
    if (!m_visible || !m_enabled || !m_bounds.contains(pt)) return nullptr;

    // 优先允许内容区内具体控件（如按钮、开关、下拉菜单）响应点击
    if (m_content && m_content->isVisible()) {
        auto hit = m_content->hitTest(pt);
        if (hit && hit != m_content) {
            return hit;
        }
    }

    // 当具有可滚动空间且光标位于右侧 14px 滚动条感应区域时，由滚动容器捕获交互
    if (getMaxScrollOffset() > 0.0f && pt.x >= m_bounds.right - 14.0f) {
        return shared_from_this();
    }

    if (m_content && m_content->isVisible()) {
        auto hit = m_content->hitTest(pt);
        if (hit) return hit;
    }

    return shared_from_this();
}

void UIScrollView::render(UIRenderContext& ctx) {
    if (!m_visible) return;

    // 每一帧平滑将最新滚动偏移作用于 content 局部布局，杜绝全局 DOM 树重排
    updateContentLayout(ctx);

    // 裁剪视口
    ctx.pushClip(m_bounds);

    if (m_content && m_content->isVisible()) {
        m_content->render(ctx);
    }

    // 绘制滚动条指示器
    float alpha = m_scrollbarAlpha.get();
    if (alpha > 0.01f && getMaxScrollOffset() > 0.0f) {
        Rect thumbRect = getScrollbarThumbRect();
        const auto& theme = UITheme::instance();
        const auto& p = theme.palette();
        Color thumbColor = m_isDraggingThumb ? p.scrollbarHover : p.scrollbarThumb;
        ctx.fillRoundedRect(thumbRect, 3.0f, thumbColor.withAlpha(thumbColor.a * alpha));
    }

    ctx.popClip();
}

} // namespace tools3000::ui::native
