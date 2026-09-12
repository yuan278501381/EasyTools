#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UILayout.h — Tools3000 原生弹性盒模型 (Flexbox / Box Layout Engine)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_LAYOUT_UILAYOUT_H
#define TOOLS3000_UI_NATIVE_LAYOUT_UILAYOUT_H

#include "ui/native/common/UIGeometry.h"
#include <vector>
#include <memory>

namespace tools3000::ui::native {

class UIElement;
class UIRenderContext;

enum class FlexDirection {
    Row,
    Column
};

enum class JustifyContent {
    FlexStart,
    Center,
    FlexEnd,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

enum class AlignItems {
    FlexStart,
    Center,
    FlexEnd,
    Stretch
};

struct LayoutParams {
    FlexDirection direction = FlexDirection::Column;
    JustifyContent justify = JustifyContent::FlexStart;
    AlignItems align = AlignItems::Stretch;
    float gap = 0.0f;
    Thickness padding;
    Thickness margin;

    // 显式尺寸约束 (-1.0f 代表未指定/自适应)
    float fixedWidth = -1.0f;
    float fixedHeight = -1.0f;
    float minWidth = 0.0f;
    float maxWidth = 100000.0f;
    float minHeight = 0.0f;
    float maxHeight = 100000.0f;

    // Flex 子项特有属性
    float flexGrow = 0.0f;
    float flexShrink = 1.0f;
    float flexBasis = -1.0f;
};

class UILayout {
public:
    /// 自底向上测量子元素集合期望尺寸
    static Size measureChildren(
        const std::vector<std::shared_ptr<UIElement>>& children,
        const LayoutParams& params,
        float availableWidth,
        float availableHeight,
        UIRenderContext& ctx
    );

    /// 自顶向下为子元素分配最终空间与物理坐标 Bounds
    static void arrangeChildren(
        const std::vector<std::shared_ptr<UIElement>>& children,
        const LayoutParams& params,
        const Rect& finalBounds,
        UIRenderContext& ctx
    );
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_LAYOUT_UILAYOUT_H
