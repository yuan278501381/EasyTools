#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ElevationPolicy — 管理员运行偏好与重启动作的纯判定
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CORE_UTILS_ELEVATIONPOLICY_H
#define EASYTOOLS_CORE_UTILS_ELEVATIONPOLICY_H

namespace easy::core {

enum class ElevationRestartAction : unsigned char {
    None = 0,
    Elevate = 1,
    DropElevation = 2,
};

inline ElevationRestartAction elevationRestartAfterSetting(
    bool wantAdmin, bool currentlyElevated) noexcept {
    if (wantAdmin && !currentlyElevated) return ElevationRestartAction::Elevate;
    if (!wantAdmin && currentlyElevated) return ElevationRestartAction::DropElevation;
    return ElevationRestartAction::None;
}

inline bool shouldAutoElevateOnStartup(bool wantAdmin,
                                       bool currentlyElevated,
                                       bool elevateSuppressed) noexcept {
    return wantAdmin && !currentlyElevated && !elevateSuppressed;
}

}  // namespace easy::core

#endif  // EASYTOOLS_CORE_UTILS_ELEVATIONPOLICY_H
