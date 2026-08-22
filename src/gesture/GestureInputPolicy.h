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
#include <cmath>
#include <string_view>
#include <string>

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

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

/// 上一笔留下的超大 DIB 即使几何上还能装下当前轨迹，也不能继续拿来提交：
/// UpdateLayeredWindow 一张近乎整屏的位图，短手势会一帧都画不出来。
inline bool overlayCanReuseSurface(int neededW, int neededH,
                                   int existingW, int existingH,
                                   int maxAreaFactor) noexcept {
    if (neededW <= 0 || neededH <= 0 || existingW <= 0 || existingH <= 0) return false;
    if (maxAreaFactor <= 0) return false;
    const long long neededArea = static_cast<long long>(neededW) * neededH;
    const long long existingArea = static_cast<long long>(existingW) * existingH;
    return existingArea <= neededArea * static_cast<long long>(maxAreaFactor);
}

/// 松手结果卡片必须真正画出来，淡出时钟才能走。否则动作已经执行，用户只看到空白。
inline bool gestureFrameReadyToFade(bool trailPresented, bool toastRequired,
                                    bool toastPresented) noexcept {
    if (!trailPresented) return false;
    return !toastRequired || toastPresented;
}

/// 实时命中不要跟着方向编码每个拐点闪灰：刚命中过的动作保持一小段，直到稳定未匹配或换了新动作。
inline bool keepLiveGestureMatch(bool hasNewMatch, bool hadMatch,
                                 DWORD unmatchedElapsedMs, DWORD holdMs) noexcept {
    if (hasNewMatch) return true;
    if (!hadMatch) return false;
    return unmatchedElapsedMs < holdMs;
}

inline constexpr DWORD kLiveGestureMatchHoldMs = 120;

inline float clampTrailOutlineWidth(float width) noexcept {
    if (!std::isfinite(width) || width <= 0.0f) return 0.0f;
    return (std::min)(width, 8.0f);
}

/// 白色描边作为最外圈：核心宽度 + 两侧描边。
inline float trailOutlineWidenWidth(float coreWidth, float outlineWidth) noexcept {
    if (coreWidth <= 0.0f || outlineWidth <= 0.0f) return 0.0f;
    return coreWidth + outlineWidth * 2.0f;
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

/// EasyTools 自己的顶层窗口类名统一用 EasyTools_ 前缀。
inline bool isEasyToolsUiClassName(std::wstring_view cls) noexcept {
    constexpr std::wstring_view kPrefix = L"EasyTools_";
    return cls.size() >= kPrefix.size() && cls.substr(0, kPrefix.size()) == kPrefix;
}

/// 轨迹 / toast 覆盖层：TOPMOST 分层窗，松手时光标一定压在笔迹上。
inline bool isGestureOverlayClassName(std::wstring_view cls) noexcept {
    return cls == L"EasyTools_GestureOverlay";
}

inline bool isGesturePassThroughClassName(std::wstring_view cls) noexcept {
    return isGestureOverlayClassName(cls) || cls == L"EasyTools_ToastOverlay";
}

/// 从覆盖层往下找真实窗口时，不可见、覆盖层、几何上不含该点的候选都跳过。
inline bool gestureHitTestShouldSkipCandidate(bool visible, bool overlayClass,
                                              bool containsPoint) noexcept {
    return !visible || overlayClass || !containsPoint;
}

/// 手势命中窗口：跳过轨迹/Toast 这类穿透覆盖层，但设置窗、搜索窗仍是合法目标。
inline bool gestureHitTestAcceptsWindow(bool visible, bool passThrough,
                                        bool cloaked, bool containsPoint) noexcept {
    return visible && !passThrough && !cloaked && containsPoint;
}

enum class GestureTargetMode : unsigned char {
    UnderPointer = 0,
    Foreground = 1,
};

inline GestureTargetMode parseGestureTargetMode(std::string_view value) noexcept {
    return value == "foreground" ? GestureTargetMode::Foreground
                                 : GestureTargetMode::UnderPointer;
}

inline const char* gestureTargetModeKey(GestureTargetMode mode) noexcept {
    return mode == GestureTargetMode::Foreground ? "foreground" : "underPointer";
}

/// 0=起点下方, 1=终点下方, 2=前台窗口, -1=放弃。
/// underPointer 不用前台窗口兜底，否则画在设置窗上会打到背后的 Chrome。
inline int pickGestureTargetSlot(GestureTargetMode mode,
                                 bool startOk, bool endOk, bool foregroundOk) noexcept {
    if (mode == GestureTargetMode::Foreground) {
        return foregroundOk ? 2 : -1;
    }
    if (startOk) return 0;
    if (endOk) return 1;
    return -1;
}

/// Alt+F4 应直接向目标窗口投递关闭，而不是再合成按键（覆盖层抢前台时 SendInput 会打空）。
inline bool keyStrokeShouldPostClose(uint8_t modifiers, uint16_t virtualKey) noexcept {
    const uint8_t withoutAlt = static_cast<uint8_t>(modifiers & ~static_cast<uint8_t>(MOD_ALT));
    return withoutAlt == 0 && (modifiers & MOD_ALT) != 0 && virtualKey == VK_F4;
}

inline bool keyStrokeIsCtrlW(uint8_t modifiers, uint16_t virtualKey) noexcept {
    const uint8_t withoutCtrl = static_cast<uint8_t>(modifiers & ~static_cast<uint8_t>(MOD_CONTROL));
    return withoutCtrl == 0 && (modifiers & MOD_CONTROL) != 0 && virtualKey == 'W';
}

inline bool classNameStartsWith(std::wstring_view cls, std::wstring_view prefix) noexcept {
    return cls.size() >= prefix.size() && cls.substr(0, prefix.size()) == prefix;
}

/// Chrome / Edge / Firefox / 资源管理器 / Electron IDE 把 Ctrl+W 当成关标签。
inline bool isTabbedBrowserClassName(std::wstring_view cls) noexcept {
    return classNameStartsWith(cls, L"Chrome_WidgetWin") ||
           cls == L"MozillaWindowClass" ||
           cls == L"CabinetWClass";
}

/// CEF / Electron / Qt / UWP 等生产力宿主：即使无边框铺满屏幕，也不应按游戏全屏免打扰。
inline bool isProductivityToolkitClassName(std::wstring_view cls) noexcept {
    if (isTabbedBrowserClassName(cls) || isEasyToolsUiClassName(cls)) return true;
    if (cls == L"OrpheusBrowserHost" || cls == L"CefBrowserWindow" ||
        cls == L"ApplicationFrameWindow") {
        return true;
    }
    return classNameStartsWith(cls, L"Qt");
}

/// 仅对真正的全屏独占（游戏/播放器）免打扰；IDE / 浏览器 / CEF / Qt 全屏继续手势。
inline bool shouldAutoBypassFullscreenGestures(bool isFullscreen,
                                               bool isProductivityClass) noexcept {
    return isFullscreen && !isProductivityClass;
}

/// Chromium / Electron 用 WS_EX_NOREDIRECTIONBITMAP 走 DirectComposition。
/// 普通 UpdateLayeredWindow 分层窗即使 TOPMOST，也会被合成到这类窗口下面。
inline bool windowUsesCompositorSurface(LONG_PTR exStyle) noexcept {
    return (exStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;
}

/// 沉底让路期间不要把覆盖层拉回来；丢失 TOPMOST 位且未沉底时才补插队。
/// 下一笔 beginTrail 会无条件 raise，不依赖这个 exstyle 位。
inline bool overlayPresentShouldForceTopmost(bool hasTopmostExStyle,
                                             bool yieldedBelow) noexcept {
    return !yieldedBelow && !hasTopmostExStyle;
}

/// 画在 EasyTools 自己的设置/搜索窗上时，关闭标签页应关掉该窗口，而不是把 Ctrl+W 打进 WebView。
inline bool keyStrokeShouldDismissEasyToolsUi(uint8_t modifiers, uint16_t virtualKey) noexcept {
    if (keyStrokeShouldPostClose(modifiers, virtualKey)) return true;
    return keyStrokeIsCtrlW(modifiers, virtualKey);
}

/// Alt+F4 一律关窗。Ctrl+W 只在无标签页的宿主（网易云 Orpheus、微信 Qt 等）升格为关窗。
inline bool keyStrokeShouldCloseWindow(uint8_t modifiers, uint16_t virtualKey,
                                       std::wstring_view className) noexcept {
    if (keyStrokeShouldPostClose(modifiers, virtualKey)) return true;
    if (!keyStrokeIsCtrlW(modifiers, virtualKey)) return false;
    if (isEasyToolsUiClassName(className)) return true;
    return !isTabbedBrowserClassName(className);
}

/// 关窗目标走 owner 链：CEF 内嵌宿主往往是顶层窗，真正该关的是 GA_ROOTOWNER。
inline HWND resolveCloseableWindow(HWND hwnd) noexcept {
    if (!hwnd || !IsWindow(hwnd)) return nullptr;
    HWND ownerRoot = GetAncestor(hwnd, GA_ROOTOWNER);
    return ownerRoot ? ownerRoot : hwnd;
}

inline constexpr DWORD kCloseObserveTimeoutMs = 40;

inline bool windowStillAcceptsClose(bool isWindow, bool isVisible) noexcept {
    return isWindow && isVisible;
}

/// 已投递关闭且窗口还在，才补发 Alt+F4，避免 Chrome 一类宿主被关两次。
inline bool closeShouldSendKeyFallback(bool posted, bool stillAcceptsClose) noexcept {
    return posted && stillAcceptsClose;
}

/// 目标进程相对本进程的完整性。Medium 钩子看不到 High/System 窗口上的鼠标。
enum class ProcessIntegrityRelation : unsigned char {
    SameOrLower = 0,
    Higher = 1,
    Unknown = 2,
};

/// QUERY_LIMITED 对同用户进程通常成功；QUERY_INFORMATION / TOKEN_QUERY
/// 在目标完整性更高时会被 UIPI 拒绝（ACCESS_DENIED）。令牌里的 elevated
/// 只有 token 查询成功时才可信。
inline ProcessIntegrityRelation classifyProcessIntegrityQuery(
    bool queryLimitedOk,
    bool queryInformationOk,
    bool tokenQueryOk,
    bool targetTokenElevated,
    bool selfElevated) noexcept {
    if (!queryLimitedOk) return ProcessIntegrityRelation::Unknown;
    if (!queryInformationOk || !tokenQueryOk) {
        return selfElevated ? ProcessIntegrityRelation::Unknown
                            : ProcessIntegrityRelation::Higher;
    }
    if (targetTokenElevated && !selfElevated) {
        return ProcessIntegrityRelation::Higher;
    }
    return ProcessIntegrityRelation::SameOrLower;
}

inline bool lowLevelHookCanObserveTarget(ProcessIntegrityRelation rel) noexcept {
    return rel != ProcessIntegrityRelation::Higher;
}

inline bool shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation rel,
                                              bool easyToolsUi) noexcept {
    return rel == ProcessIntegrityRelation::Higher && !easyToolsUi;
}

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTUREINPUTPOLICY_H
