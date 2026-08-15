#pragma once

#include "core/utils/DpiUtils.h"

namespace easy::capture {

/// Centralized visual metrics for the contextual shortcut guide. Values are
/// expressed in 96-DPI logical pixels, then scaled explicitly because the
/// overlay's D2D DC render target is configured in physical-pixel space.
struct ShortcutHintStyle {
    static constexpr float BaseKeyFont = 13.5f;
    static constexpr float BaseLabelFont = 13.25f;
    static constexpr float BaseKeyHeight = 30.0f;
    static constexpr float BaseHorizontalPadding = 14.0f;
    static constexpr float BaseVerticalPadding = 11.0f;
    static constexpr float BaseKeyHorizontalPadding = 11.0f;
    static constexpr float BaseItemGap = 18.0f;
    static constexpr float BaseLabelGap = 8.0f;
    static constexpr float BaseRowGap = 9.0f;
    static constexpr float BaseCornerRadius = 10.0f;
    static constexpr float BaseKeyCornerRadius = 6.0f;
    static constexpr float BaseScreenMargin = 22.0f;

    static float scaleForDpi(unsigned dpi) noexcept {
        return easy::core::dpi::scaleForDpi(dpi);
    }
};

}  // namespace easy::capture
