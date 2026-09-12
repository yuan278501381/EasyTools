#pragma once

#include "core/utils/DpiUtils.h"

namespace tools3000::gesture {

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
        const float scale = tools3000::core::dpi::scaleForDpi(dpi);
        return {
            tools3000::core::dpi::scaleMetric(BaseWindowSize, scale),
            tools3000::core::dpi::scaleMetric(BaseOuterRadius, scale),
            tools3000::core::dpi::scaleMetric(BaseInnerRadius, scale)};
    }
};

}  // namespace tools3000::gesture
