#pragma once

#include "core/utils/DpiUtils.h"

namespace easy::ui {

struct ToastStyle {
    static constexpr int BaseWidth = 600;
    static constexpr int BaseHeight = 80;
    static constexpr int BaseTopMargin = 80;
    static constexpr float BaseFontSize = 18.0f;

    static SIZE windowSizeForDpi(UINT dpi) noexcept {
        const float scale = easy::core::dpi::scaleForDpi(dpi);
        return SIZE{
            easy::core::dpi::scaleMetric(BaseWidth, scale),
            easy::core::dpi::scaleMetric(BaseHeight, scale)};
    }
};

}  // namespace easy::ui
