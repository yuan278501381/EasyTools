#pragma once

#include "core/utils/DpiUtils.h"

#include <algorithm>

namespace easy::ui {

struct ToastStyle {
    static constexpr int BaseWidth = 600;
    static constexpr int BaseHeight = 80;
    static constexpr int BaseBottomMargin = 48;
    static constexpr float BaseFontSize = 18.0f;
    static constexpr float StrokeWidth = 3.0f;

    /// 贴在工作区底部，避开任务栏。
    static int originYForWorkArea(int workTop, int workBottom,
                                  int height, int bottomMargin) noexcept {
        if (height <= 0) return workTop;
        const int y = workBottom - height - (std::max)(0, bottomMargin);
        return (std::max)(workTop, y);
    }

    static SIZE windowSizeForDpi(UINT dpi) noexcept {
        const float scale = easy::core::dpi::scaleForDpi(dpi);
        return SIZE{
            easy::core::dpi::scaleMetric(BaseWidth, scale),
            easy::core::dpi::scaleMetric(BaseHeight, scale)};
    }
};

}  // namespace easy::ui
