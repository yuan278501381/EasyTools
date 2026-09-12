// ─────────────────────────────────────────────────────────────────────────────
// CornerRadiusHelper.cpp — 跨选区/美化外壳/矩形标注通用圆角手柄交互框架实现
// ─────────────────────────────────────────────────────────────────────────────

#include "CornerRadiusHelper.h"
#include <format>

namespace tools3000::capture {

bool CornerRadiusHelper::canShowHandles(float width, float height, float scale, float minDimension) {
    const float threshold = minDimension * (scale > 0.0f ? scale : 1.0f);
    return width >= threshold && height >= threshold;
}

float CornerRadiusHelper::calculateOffset(float cornerRadius, float width, float height, float scale,
                                         float minOffsetLogical, float baseMinLogical) {
    const float s = (scale > 0.0f ? scale : 1.0f);
    const float minDimension = (std::min)(width, height);
    const float maxOffset = minDimension * 0.40f;
    const float minOffset = minOffsetLogical * s;
    const float baseOffset = (std::max)(baseMinLogical, cornerRadius + 8.0f) * s;

    if (maxOffset < minOffset) {
        return (std::max)(1.0f, maxOffset);
    }
    return std::clamp(baseOffset, minOffset, maxOffset);
}

std::vector<CornerHandle> CornerRadiusHelper::getCornerHandles(
    float left, float top, float right, float bottom,
    float cornerRadius, float scale, bool singlePrimaryCorner)
{
    const float w = right - left;
    const float h = bottom - top;
    const float offset = calculateOffset(cornerRadius, w, h, scale);

    std::vector<CornerHandle> handles;
    handles.reserve(singlePrimaryCorner ? 1 : 4);

    // 0: 左上角 LT (主控制手柄)
    handles.push_back({
        left + offset,
        top + offset,
        0.70710678f,
        0.70710678f,
        0
    });

    if (singlePrimaryCorner) {
        return handles;
    }

    // 1: 右上角 RT
    handles.push_back({
        right - offset,
        top + offset,
        -0.70710678f,
        0.70710678f,
        1
    });

    // 2: 右下角 RB
    handles.push_back({
        right - offset,
        bottom - offset,
        -0.70710678f,
        -0.70710678f,
        2
    });

    // 3: 左下角 LB
    handles.push_back({
        left + offset,
        bottom - offset,
        0.70710678f,
        -0.70710678f,
        3
    });

    return handles;
}

int CornerRadiusHelper::hitTestHandles(
    float left, float top, float right, float bottom,
    float cornerRadius, float px, float py,
    float scale, float hitRadius, bool /*singlePrimaryCorner*/)
{
    // 始终获取 4 个角的手柄进行命中测试，保证用户点击/拖拽任意角都能精确响应
    const auto handles = getCornerHandles(left, top, right, bottom, cornerRadius, scale, false);
    const float s = (scale > 0.0f ? scale : 1.0f);
    const float armDist = 14.0f * s;
    int radVal = static_cast<int>(std::round(cornerRadius));
    std::wstring rText = std::format(L"{}", radVal);
    float badgeW = (28.0f + static_cast<float>(rText.size()) * 8.0f) * s;
    float badgeH = 20.0f * s;
    float badgeHalfDiag = std::hypot(badgeW * 0.5f, badgeH * 0.5f);
    float badgeDist = armDist + badgeHalfDiag + 8.0f * s;

    const float boxW = right - left;
    const float boxH = bottom - top;
    const bool canShowBadge = (boxW >= 130.0f * s && boxH >= 130.0f * s);

    for (const auto& hnd : handles) {
        // 1. 手柄微圆心判定
        const float r = (std::max)(14.0f * s, hitRadius);
        if (std::abs(px - hnd.x) <= r && std::abs(py - hnd.y) <= r) {
            return hnd.cornerIndex;
        }

        // 2. 对角发光微导轨连线段精准判定 (投影长度 armDist，垂直容差 6px)
        float dx = px - hnd.x;
        float dy = py - hnd.y;
        float proj = dx * hnd.inDirX + dy * hnd.inDirY;
        float perp = std::abs(dx * hnd.inDirY - dy * hnd.inDirX);
        if (proj >= -armDist - 3.0f * s && proj <= armDist + 3.0f * s && perp <= 6.0f * s) {
            return hnd.cornerIndex;
        }

        // 3. 实时微胶囊 [ ◖ 10 ] 命中判定 (仅在满足最小展示尺寸的大中型选区生效，防止小选区中央区域被误判定)
        if (canShowBadge) {
            float bcX = hnd.x + hnd.inDirX * badgeDist;
            float bcY = hnd.y + hnd.inDirY * badgeDist;
            float bx = bcX - badgeW * 0.5f;
            float by = bcY - badgeH * 0.5f;

            float minBx = left + 4.0f * s;
            float maxBx = right - badgeW - 4.0f * s;
            float minBy = top + 4.0f * s;
            float maxBy = bottom - badgeH - 4.0f * s;
            if (minBx <= maxBx) bx = std::clamp(bx, minBx, maxBx);
            if (minBy <= maxBy) by = std::clamp(by, minBy, maxBy);

            if (px >= bx - 4.0f * s && px <= bx + badgeW + 4.0f * s &&
                py >= by - 4.0f * s && py <= by + badgeH + 4.0f * s) {
                return hnd.cornerIndex;
            }
        }
    }
    return -1;
}

float CornerRadiusHelper::calculateDraggedRadius(
    float left, float top, float right, float bottom,
    float dragStartX, float dragStartY,
    float startRadius,
    float currentCursorX, float currentCursorY,
    float maxRadiusLimit,
    int cornerIndex,
    float scale)
{
    const float s = (scale > 0.0f ? scale : 1.0f);
    const float w = right - left;
    const float h = bottom - top;
    const float cx = (left + right) * 0.5f;
    const float cy = (top + bottom) * 0.5f;

    float maxR = ((std::min)(w, h) * 0.5f) / s;
    if (maxRadiusLimit > 0.0f && maxRadiusLimit < maxR) {
        maxR = maxRadiusLimit;
    }

    float signX = 1.0f;
    float signY = 1.0f;
    if (cornerIndex == 0) {
        // 0: 左上角 LT (往内下拉 X+, Y+ 增大圆角)
        signX = 1.0f;
        signY = 1.0f;
    } else if (cornerIndex == 1) {
        // 1: 右上角 RT (往内下拉 X-, Y+ 增大圆角)
        signX = -1.0f;
        signY = 1.0f;
    } else if (cornerIndex == 2) {
        // 2: 右下角 RB (往内上拉 X-, Y- 增大圆角)
        signX = -1.0f;
        signY = -1.0f;
    } else if (cornerIndex == 3) {
        // 3: 左下角 LB (往内上拉 X+, Y- 增大圆角)
        signX = 1.0f;
        signY = -1.0f;
    } else {
        signX = (dragStartX < cx) ? 1.0f : -1.0f;
        signY = (dragStartY < cy) ? 1.0f : -1.0f;
    }

    const float dx = (currentCursorX - dragStartX) * signX;
    const float dy = (currentCursorY - dragStartY) * signY;
    const float delta = ((dx + dy) * 0.5f) / s;

    return std::clamp(startRadius + delta, 0.0f, maxR);
}

float CornerRadiusHelper::applyIncrementalDrag(
    float currentRadius,
    float dx, float dy,
    float signX, float signY,
    float maxRadius)
{
    const float delta = (dx * signX + dy * signY) * 0.5f;
    return std::clamp(currentRadius + delta, 0.0f, maxRadius);
}

std::wstring CornerRadiusHelper::getToastMessage(CornerRadiusTarget target, float radius, bool isZh) {
    const int rVal = static_cast<int>(std::round(radius));
    switch (target) {
        case CornerRadiusTarget::Selection:
            return isZh ? std::format(L"选区圆角: {} px", rVal) : std::format(L"Selection Radius: {} px", rVal);
        case CornerRadiusTarget::BeautifyShell:
            return isZh ? std::format(L"美化外壳圆角: {} px", rVal) : std::format(L"Beauty Shell Radius: {} px", rVal);
        case CornerRadiusTarget::RectangleMarkup:
            return isZh ? std::format(L"矩形圆角: {} px", rVal) : std::format(L"Corner Radius: {} px", rVal);
        default:
            return isZh ? std::format(L"圆角半径: {} px", rVal) : std::format(L"Radius: {} px", rVal);
    }
}

} // namespace tools3000::capture
