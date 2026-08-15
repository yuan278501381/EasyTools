#pragma once

#include "core/utils/DpiUtils.h"

namespace easy::gesture {

struct RadialMenuMetrics {
    int windowSize = 400;
    int outerRadius = 150;
    int innerRadius = 40;
};

struct RadialMenuStyle {
    static constexpr int BaseWindowSize = 400;
    static constexpr int BaseOuterRadius = 150;
    static constexpr int BaseInnerRadius = 40;
    static constexpr float BaseFontSize = 14.0f;

    static RadialMenuMetrics metricsForDpi(UINT dpi) noexcept {
        const float scale = easy::core::dpi::scaleForDpi(dpi);
        return {
            easy::core::dpi::scaleMetric(BaseWindowSize, scale),
            easy::core::dpi::scaleMetric(BaseOuterRadius, scale),
            easy::core::dpi::scaleMetric(BaseInnerRadius, scale)};
    }
};

}  // namespace easy::gesture
