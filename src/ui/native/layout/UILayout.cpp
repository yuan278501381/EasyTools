#include "ui/native/layout/UILayout.h"
#include "ui/native/core/UIElement.h"
#include <algorithm>
#include <numeric>

namespace tools3000::ui::native {

Size UILayout::measureChildren(
    const std::vector<std::shared_ptr<UIElement>>& children,
    const LayoutParams& params,
    float availableWidth,
    float availableHeight,
    UIRenderContext& ctx
) {
    if (params.fixedWidth >= 0.0f && params.fixedHeight >= 0.0f) {
        return Size(params.fixedWidth, params.fixedHeight);
    }

    const float innerWidth = std::max(0.0f, availableWidth - params.padding.horizontal());
    const float innerHeight = std::max(0.0f, availableHeight - params.padding.vertical());

    float mainSize = 0.0f;
    float crossSize = 0.0f;
    int visibleCount = 0;

    for (const auto& child : children) {
        if (!child || !child->isVisible()) continue;

        const auto& cParams = child->layoutParams();
        const float cAvailW = (params.direction == FlexDirection::Row && params.fixedWidth >= 0.0f)
            ? std::max(0.0f, innerWidth - mainSize)
            : innerWidth;
        const float cAvailH = (params.direction == FlexDirection::Column && params.fixedHeight >= 0.0f)
            ? std::max(0.0f, innerHeight - mainSize)
            : innerHeight;

        Size childSize = child->measure(cAvailW, cAvailH, ctx);

        if (params.direction == FlexDirection::Row) {
            mainSize += childSize.width + cParams.margin.horizontal();
            crossSize = std::max(crossSize, childSize.height + cParams.margin.vertical());
        } else {
            mainSize += childSize.height + cParams.margin.vertical();
            crossSize = std::max(crossSize, childSize.width + cParams.margin.horizontal());
        }
        visibleCount++;
    }

    if (visibleCount > 1) {
        mainSize += (visibleCount - 1) * params.gap;
    }

    float finalW = (params.direction == FlexDirection::Row ? mainSize : crossSize) + params.padding.horizontal();
    float finalH = (params.direction == FlexDirection::Column ? mainSize : crossSize) + params.padding.vertical();

    if (params.fixedWidth >= 0.0f) finalW = params.fixedWidth;
    if (params.fixedHeight >= 0.0f) finalH = params.fixedHeight;

    finalW = std::clamp(finalW, params.minWidth, params.maxWidth);
    finalH = std::clamp(finalH, params.minHeight, params.maxHeight);

    return Size(finalW, finalH);
}

void UILayout::arrangeChildren(
    const std::vector<std::shared_ptr<UIElement>>& children,
    const LayoutParams& params,
    const Rect& finalBounds,
    UIRenderContext& ctx
) {
    const Rect contentRect = finalBounds.inset(params.padding);
    const float contentMain = (params.direction == FlexDirection::Row) ? contentRect.width() : contentRect.height();
    const float contentCross = (params.direction == FlexDirection::Row) ? contentRect.height() : contentRect.width();

    std::vector<std::shared_ptr<UIElement>> visibleChildren;
    visibleChildren.reserve(children.size());
    for (const auto& child : children) {
        if (child && child->isVisible()) {
            visibleChildren.push_back(child);
        }
    }

    if (visibleChildren.empty()) return;

    // 1. 计算所有子元素原始尺寸与 Flex 权重
    float totalBaseMain = 0.0f;
    float totalGrow = 0.0f;
    float totalShrink = 0.0f;

    struct ChildLayoutData {
        float mainSize = 0.0f;
        float crossSize = 0.0f;
    };
    std::vector<ChildLayoutData> layoutData(visibleChildren.size());

    for (size_t i = 0; i < visibleChildren.size(); ++i) {
        const auto& child = visibleChildren[i];
        const auto& cp = child->layoutParams();
        const auto& dSize = child->desiredSize();

        if (params.direction == FlexDirection::Row) {
            if (cp.fixedWidth >= 0.0f) {
                layoutData[i].mainSize = cp.fixedWidth;
            } else if (cp.flexBasis >= 0.0f) {
                layoutData[i].mainSize = cp.flexBasis;
            } else if (cp.flexGrow > 0.0f) {
                layoutData[i].mainSize = 0.0f;
            } else {
                layoutData[i].mainSize = dSize.width;
            }
            layoutData[i].crossSize = (cp.fixedHeight >= 0.0f) ? cp.fixedHeight : dSize.height;
            totalBaseMain += layoutData[i].mainSize + cp.margin.horizontal();
        } else {
            if (cp.fixedHeight >= 0.0f) {
                layoutData[i].mainSize = cp.fixedHeight;
            } else if (cp.flexBasis >= 0.0f) {
                layoutData[i].mainSize = cp.flexBasis;
            } else if (cp.flexGrow > 0.0f) {
                layoutData[i].mainSize = 0.0f;
            } else {
                layoutData[i].mainSize = dSize.height;
            }
            layoutData[i].crossSize = (cp.fixedWidth >= 0.0f) ? cp.fixedWidth : dSize.width;
            totalBaseMain += layoutData[i].mainSize + cp.margin.vertical();
        }

        totalGrow += cp.flexGrow;

        float itemShrink = cp.flexShrink;
        if (params.direction == FlexDirection::Row && cp.fixedWidth >= 0.0f && cp.flexShrink == 1.0f) {
            itemShrink = 0.0f;
        } else if (params.direction == FlexDirection::Column && cp.fixedHeight >= 0.0f && cp.flexShrink == 1.0f) {
            itemShrink = 0.0f;
        }
        totalShrink += itemShrink;
    }

    const float totalGaps = (visibleChildren.size() > 1) ? (visibleChildren.size() - 1) * params.gap : 0.0f;
    float remainingSpace = contentMain - (totalBaseMain + totalGaps);

    // 2. 空间分配 (Grow or Shrink)
    if (remainingSpace > 0.0f && totalGrow > 0.0f) {
        for (size_t i = 0; i < visibleChildren.size(); ++i) {
            const auto& cp = visibleChildren[i]->layoutParams();
            if (cp.flexGrow > 0.0f) {
                layoutData[i].mainSize += remainingSpace * (cp.flexGrow / totalGrow);
            }
        }
        remainingSpace = 0.0f;
    } else if (remainingSpace < 0.0f && totalShrink > 0.0f) {
        for (size_t i = 0; i < visibleChildren.size(); ++i) {
            const auto& cp = visibleChildren[i]->layoutParams();
            float itemShrink = cp.flexShrink;
            if (params.direction == FlexDirection::Row && cp.fixedWidth >= 0.0f && cp.flexShrink == 1.0f) {
                itemShrink = 0.0f;
            } else if (params.direction == FlexDirection::Column && cp.fixedHeight >= 0.0f && cp.flexShrink == 1.0f) {
                itemShrink = 0.0f;
            }
            if (itemShrink > 0.0f) {
                const float shrinkAmount = (-remainingSpace) * (itemShrink / totalShrink);
                layoutData[i].mainSize = std::max(0.0f, layoutData[i].mainSize - shrinkAmount);
            }
        }
        remainingSpace = 0.0f;
    }

    // 3. JustifyContent 计算起点与间距
    float currentMain = (params.direction == FlexDirection::Row) ? contentRect.left : contentRect.top;
    float actualGap = params.gap;

    if (remainingSpace > 0.0f) {
        switch (params.justify) {
        case JustifyContent::Center:
            currentMain += remainingSpace * 0.5f;
            break;
        case JustifyContent::FlexEnd:
            currentMain += remainingSpace;
            break;
        case JustifyContent::SpaceBetween:
            if (visibleChildren.size() > 1) {
                actualGap += remainingSpace / (visibleChildren.size() - 1);
            }
            break;
        case JustifyContent::SpaceAround:
            if (!visibleChildren.empty()) {
                const float unit = remainingSpace / visibleChildren.size();
                currentMain += unit * 0.5f;
                actualGap += unit;
            }
            break;
        case JustifyContent::SpaceEvenly:
            if (!visibleChildren.empty()) {
                const float unit = remainingSpace / (visibleChildren.size() + 1);
                currentMain += unit;
                actualGap += unit;
            }
            break;
        case JustifyContent::FlexStart:
        default:
            break;
        }
    }

    // 4. 定位并安排每个子元素
    const float contentCrossStart = (params.direction == FlexDirection::Row) ? contentRect.top : contentRect.left;

    for (size_t i = 0; i < visibleChildren.size(); ++i) {
        const auto& child = visibleChildren[i];
        const auto& cp = child->layoutParams();
        const auto& ld = layoutData[i];

        // 交叉轴对齐计算
        float childCrossPos = contentCrossStart;
        float finalCrossSize = ld.crossSize;

        AlignItems effectiveAlign = params.align;
        switch (effectiveAlign) {
        case AlignItems::Center:
            if (params.direction == FlexDirection::Row) {
                childCrossPos += (contentCross - (ld.crossSize + cp.margin.vertical())) * 0.5f + cp.margin.top;
            } else {
                childCrossPos += (contentCross - (ld.crossSize + cp.margin.horizontal())) * 0.5f + cp.margin.left;
            }
            break;
        case AlignItems::FlexEnd:
            if (params.direction == FlexDirection::Row) {
                childCrossPos += contentCross - ld.crossSize - cp.margin.bottom;
            } else {
                childCrossPos += contentCross - ld.crossSize - cp.margin.right;
            }
            break;
        case AlignItems::Stretch:
            if (params.direction == FlexDirection::Row) {
                childCrossPos += cp.margin.top;
                if (cp.fixedHeight < 0.0f) {
                    const float avail = std::max(0.0f, contentCross - cp.margin.vertical());
                    finalCrossSize = std::clamp(avail, cp.minHeight, cp.maxHeight);
                    if (finalCrossSize < avail) {
                        childCrossPos += (avail - finalCrossSize) * 0.5f;
                    }
                }
            } else {
                childCrossPos += cp.margin.left;
                if (cp.fixedWidth < 0.0f) {
                    const float avail = std::max(0.0f, contentCross - cp.margin.horizontal());
                    finalCrossSize = std::clamp(avail, cp.minWidth, cp.maxWidth);
                    if (finalCrossSize < avail) {
                        childCrossPos += (avail - finalCrossSize) * 0.5f;
                    }
                }
            }
            break;
        case AlignItems::FlexStart:
        default:
            if (params.direction == FlexDirection::Row) {
                childCrossPos += cp.margin.top;
            } else {
                childCrossPos += cp.margin.left;
            }
            break;
        }

        Rect childRect;
        if (params.direction == FlexDirection::Row) {
            currentMain += cp.margin.left;
            childRect = Rect(currentMain, childCrossPos,
                             currentMain + ld.mainSize, childCrossPos + finalCrossSize);
            currentMain += ld.mainSize + cp.margin.right + actualGap;
        } else {
            currentMain += cp.margin.top;
            childRect = Rect(childCrossPos, currentMain,
                             childCrossPos + finalCrossSize, currentMain + ld.mainSize);
            currentMain += ld.mainSize + cp.margin.bottom + actualGap;
        }

        child->layout(childRect, ctx);
    }
}

} // namespace tools3000::ui::native
