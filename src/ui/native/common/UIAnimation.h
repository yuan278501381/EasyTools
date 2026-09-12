#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIAnimation.h — Tools3000 原生物理弹簧与三次贝塞尔缓动动画中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMMON_UIANIMATION_H
#define TOOLS3000_UI_NATIVE_COMMON_UIANIMATION_H

#include "ui/native/common/UIGeometry.h"
#include <algorithm>
#include <cmath>

namespace tools3000::ui::native {

enum class EasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    SmoothCubic, // cubic-bezier(0.16, 1, 0.3, 1) — 极致顺滑的现代微交互
    SpringBounce // 具备微弹簧物理超冲特性的弹性缓动
};

class UIAnimation {
public:
    /// 数值精确求解三次贝塞尔缓动: (x1, y1) -> (x2, y2)
    static float evaluateCubicBezier(float t, float x1, float y1, float x2, float y2);

    /// 阻尼谐振子弹簧物理步进 (Spring Physics Step)
    static void stepSpring(float target, float& current, float& velocity,
                           float stiffness, float damping, float dt);

    /// 标准缓动曲线计算
    static float evaluateEasing(float progress, EasingType type);
};

/// 标量浮点动画属性，支持目标插值与物理阻尼
class AnimatedFloat {
public:
    AnimatedFloat(float initial = 0.0f)
        : m_current(initial), m_target(initial), m_start(initial) {}

    void setImmediate(float value) {
        m_current = value;
        m_target = value;
        m_start = value;
        m_animating = false;
        m_velocity = 0.0f;
    }

    void setTarget(float target, float durationMs = 200.0f, EasingType type = EasingType::SmoothCubic) {
        if (std::abs(target - m_target) < 0.0001f) return;
        m_start = m_current;
        m_target = target;
        m_durationMs = std::max(1.0f, durationMs);
        m_elapsedMs = 0.0f;
        m_easing = type;
        m_animating = true;
    }

    /// 更新步进 (dt 为秒，如 0.016f)
    bool update(float dt);

    float get() const { return m_current; }
    float target() const { return m_target; }
    bool isAnimating() const { return m_animating; }

private:
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_start = 0.0f;
    float m_velocity = 0.0f;
    float m_durationMs = 200.0f;
    float m_elapsedMs = 0.0f;
    EasingType m_easing = EasingType::SmoothCubic;
    bool m_animating = false;
};

/// 颜色平滑插值动画属性
class AnimatedColor {
public:
    AnimatedColor(const Color& initial = Color())
        : m_r(initial.r), m_g(initial.g), m_b(initial.b), m_a(initial.a) {}

    void setImmediate(const Color& c) {
        m_r.setImmediate(c.r);
        m_g.setImmediate(c.g);
        m_b.setImmediate(c.b);
        m_a.setImmediate(c.a);
    }

    void setTarget(const Color& c, float durationMs = 180.0f, EasingType type = EasingType::SmoothCubic) {
        m_r.setTarget(c.r, durationMs, type);
        m_g.setTarget(c.g, durationMs, type);
        m_b.setTarget(c.b, durationMs, type);
        m_a.setTarget(c.a, durationMs, type);
    }

    bool update(float dt) {
        bool b1 = m_r.update(dt);
        bool b2 = m_g.update(dt);
        bool b3 = m_b.update(dt);
        bool b4 = m_a.update(dt);
        return b1 || b2 || b3 || b4;
    }

    Color get() const {
        return Color(m_r.get(), m_g.get(), m_b.get(), m_a.get());
    }

    bool isAnimating() const {
        return m_r.isAnimating() || m_g.isAnimating() || m_b.isAnimating() || m_a.isAnimating();
    }

private:
    AnimatedFloat m_r;
    AnimatedFloat m_g;
    AnimatedFloat m_b;
    AnimatedFloat m_a;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMMON_UIANIMATION_H
