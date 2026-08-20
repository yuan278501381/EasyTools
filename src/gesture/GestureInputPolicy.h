#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureInputPolicy — 手势触发键与取消条件的纯判定
//
// 这些规则必须能在没有钩子、没有窗口的情况下单独验证：一旦写进 MouseHook /
// GestureEngine 的回调里，再测就要装全局钩子。
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_GESTUREINPUTPOLICY_H
#define EASYTOOLS_GESTURE_GESTUREINPUTPOLICY_H

#include "gesture/MouseHook.h"
#include "core/utils/ThemeUtils.h"

#include <algorithm>
#include <string_view>

namespace easy::gesture {

/// 当前触发模式下，这个按下事件是否应当开始一笔手势。
inline bool isGestureTriggerDown(MouseEventType type, TriggerMode mode) noexcept {
    if (type == MouseEventType::RightDown) {
        return mode == TriggerMode::RightOnly || mode == TriggerMode::Both;
    }
    if (type == MouseEventType::MiddleDown) {
        return mode == TriggerMode::MiddleOnly || mode == TriggerMode::Both;
    }
    return false;
}

/// 与按下配对的抬起事件。必须按实际按下的键配对，不能用配置项里的默认右键。
inline MouseEventType triggerUpFor(MouseEventType downEvent) noexcept {
    return downEvent == MouseEventType::MiddleDown
        ? MouseEventType::MiddleUp
        : MouseEventType::RightUp;
}

/// 追踪过程中这些事件应立刻取消手势并放行（左键、对侧鼠标键）。
/// 与当前触发键相同的再次按下不取消：那是状态机失步时的噪声，由钩子侧闩锁处理。
inline bool cancelsGestureTracking(MouseEventType type, MouseEventType activeTriggerDown) noexcept {
    if (type == MouseEventType::LeftDown || type == MouseEventType::LeftUp) return true;
    if (type == MouseEventType::RightDown && activeTriggerDown != MouseEventType::RightDown) {
        return true;
    }
    if (type == MouseEventType::MiddleDown && activeTriggerDown != MouseEventType::MiddleDown) {
        return true;
    }
    return false;
}

inline bool shouldShowGestureResultToast(bool recognized, bool hasResultText,
                                         bool excessive) noexcept {
    return excessive || (recognized && hasResultText);
}

/// 绘制过程中覆盖层只扩大、不收缩、不挪原点；否则卡片一闪窗口就搬一次，轨迹会抽搐。
inline void growOverlayRect(int& left, int& top, int& right, int& bottom,
                            int originX, int originY, int width, int height) noexcept {
    if (width <= 0 || height <= 0) return;
    left = (std::min)(left, originX);
    top = (std::min)(top, originY);
    right = (std::max)(right, originX + width);
    bottom = (std::max)(bottom, originY + height);
}

inline bool overlaySurfaceContains(int left, int top, int right, int bottom,
                                   int originX, int originY, int width, int height) noexcept {
    return width > 0 && height > 0 &&
           left >= originX && top >= originY &&
           right <= originX + width && bottom <= originY + height;
}

/// 淡出时钟必须从第一帧真正画出来之后才走。若从松手瞬间起算，重建整屏
/// DIB 的耗时会被算进淡出窗口里，结果动作已经执行、轨迹和 Toast 一帧都没有。
inline bool gestureFadeShouldFinish(bool clockStarted, DWORD elapsedMs,
                                    DWORD fadeHoldMs, DWORD fadeOutMs) noexcept {
    if (!clockStarted) return false;
    return elapsedMs >= fadeHoldMs + fadeOutMs;
}

inline float gestureFadeAlpha(bool clockStarted, DWORD elapsedMs,
                              DWORD fadeHoldMs, DWORD fadeOutMs) noexcept {
    if (!clockStarted || elapsedMs <= fadeHoldMs) return 1.0f;
    if (fadeOutMs == 0) return 0.0f;
    const float progress = std::clamp(
        static_cast<float>(elapsedMs - fadeHoldMs) / static_cast<float>(fadeOutMs),
        0.0f, 1.0f);
    const float ease = 1.0f - progress;
    return ease * ease;
}

/// auto：跟随全局强调色；custom：用手势页里的自定义 HEX。
inline easy::core::AccentColorRGB resolveGestureTrailRgb(
    std::string_view colorMode,
    std::string_view customHex,
    const easy::core::AccentColorRGB& accentRgb) noexcept {
    if (colorMode == "custom" && !customHex.empty()) {
        return easy::core::parseHexColor(std::string(customHex));
    }
    return accentRgb;
}

inline bool gestureTrailUsesLightPalette(std::string_view theme,
                                         bool systemAppsUseLight) noexcept {
    if (theme == "light") return true;
    if (theme == "system") return systemAppsUseLight;
    return false;
}

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREINPUTPOLICY_H
