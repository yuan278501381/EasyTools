#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIScrollView.h — Tools3000 原生极致顺滑虚拟滚动容器组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_UISCROLLVIEW_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_UISCROLLVIEW_H

#include "ui/native/core/UIElement.h"
#include "ui/native/common/UIAnimation.h"

namespace tools3000::ui::native {

class UIScrollView : public UIElement {
public:
    UIScrollView(std::shared_ptr<UIElement> content = nullptr);

    void setContent(std::shared_ptr<UIElement> content);
    std::shared_ptr<UIElement> getContent() const { return m_content; }

    void scrollTo(float offsetY, bool animated = true);
    float getScrollOffset() const { return m_scrollY.get(); }
    float getMaxScrollOffset() const;

    Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    void layout(const Rect& bounds, UIRenderContext& ctx) override;
    void render(UIRenderContext& ctx) override;
    bool update(float dt) override;

    bool onMouseWheel(const UIMouseEvent& e) override;
    bool onMouseMove(const UIMouseEvent& e) override;
    bool onMouseDown(const UIMouseEvent& e) override;
    bool onMouseUp(const UIMouseEvent& e) override;
    void onMouseLeave() override;

    std::shared_ptr<UIElement> hitTest(Point pt) override;

    Rect getScrollbarTrackRect() const;
    Rect getScrollbarThumbRect() const;

private:
    void updateContentLayout(UIRenderContext& ctx);

private:
    std::shared_ptr<UIElement> m_content;
    AnimatedFloat m_scrollY{0.0f};
    AnimatedFloat m_scrollbarAlpha{0.0f};
    float m_scrollbarFadeTimer = 0.0f;

    bool m_isDraggingThumb = false;
    float m_dragStartY = 0.0f;
    float m_dragStartScrollY = 0.0f;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_UISCROLLVIEW_H
