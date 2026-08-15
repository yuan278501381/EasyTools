#pragma once

#include "core/utils/DpiUtils.h"

namespace easy::keycast {

struct KeycastStyle {
    static constexpr int BaseWidth = 800;
    static constexpr int BaseHeight = 160;
    static constexpr int BaseBottomMargin = 64;
    static constexpr float BaseFontSize = 36.0f;

    static SIZE windowSizeForDpi(UINT dpi) noexcept {
        const float scale = easy::core::dpi::scaleForDpi(dpi);
        return SIZE{
            easy::core::dpi::scaleMetric(BaseWidth, scale),
            easy::core::dpi::scaleMetric(BaseHeight, scale)};
    }
};

}  // namespace easy::keycast
