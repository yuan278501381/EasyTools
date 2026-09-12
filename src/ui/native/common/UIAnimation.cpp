#include "ui/native/common/UIAnimation.h"
#include <algorithm>
#include <cmath>

namespace tools3000::ui::native {

float UIAnimation::evaluateCubicBezier(float t, float x1, float y1, float x2, float y2) {
    t = std::clamp(t, 0.0f, 1.0f);

    // Newton-Raphson 迭代求解参数 u 使得 B_x(u) == t
    float u = t;
    for (int i = 0; i < 5; ++i) {
        float oneMinusU = 1.0f - u;
        float currentX = 3.0f * oneMinusU * oneMinusU * u * x1 +
                         3.0f * oneMinusU * u * u * x2 +
                         u * u * u;
        float currentSlope = 3.0f * oneMinusU * oneMinusU * x1 +
                             6.0f * oneMinusU * u * (x2 - x1) +
                             3.0f * u * u * (1.0f - x2);
        if (std::abs(currentSlope) < 1e-6f) break;
        float error = currentX - t;
        u -= error / currentSlope;
        u = std::clamp(u, 0.0f, 1.0f);
        if (std::abs(error) < 1e-4f) break;
    }

    // 计算 B_y(u)
    float oneMinusU = 1.0f - u;
    return 3.0f * oneMinusU * oneMinusU * u * y1 +
           3.0f * oneMinusU * u * u * y2 +
           u * u * u;
}

void UIAnimation::stepSpring(float target, float& current, float& velocity,
                             float stiffness, float damping, float dt) {
    // 经典阻尼谐振子积分
    const float displacement = current - target;
    const float springForce = -stiffness * displacement;
    const float dampingForce = -damping * velocity;
    const float totalForce = springForce + dampingForce;

    velocity += totalForce * dt;
    current += velocity * dt;

    if (std::abs(displacement) < 0.001f && std::abs(velocity) < 0.001f) {
        current = target;
        velocity = 0.0f;
    }
}

float UIAnimation::evaluateEasing(float progress, EasingType type) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    switch (type) {
    case EasingType::Linear:
        return t;
    case EasingType::EaseIn:
        return t * t;
    case EasingType::EaseOut:
        return t * (2.0f - t);
    case EasingType::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    case EasingType::SmoothCubic:
        // CSS --ease-smooth: cubic-bezier(0.16, 1, 0.3, 1)
        return evaluateCubicBezier(t, 0.16f, 1.0f, 0.3f, 1.0f);
    case EasingType::SpringBounce:
        // CSS --ease-spring: cubic-bezier(0.34, 1.56, 0.64, 1)
        return evaluateCubicBezier(t, 0.34f, 1.56f, 0.64f, 1.0f);
    default:
        return t;
    }
}

bool AnimatedFloat::update(float dt) {
    if (!m_animating) return false;

    if (m_easing == EasingType::SpringBounce) {
        // 使用弹簧物理模型步进
        const float prev = m_current;
        UIAnimation::stepSpring(m_target, m_current, m_velocity, 260.0f, 22.0f, dt);
        if (m_current == m_target && m_velocity == 0.0f) {
            m_animating = false;
        }
        return std::abs(m_current - prev) > 0.0001f;
    }

    m_elapsedMs += dt * 1000.0f;
    if (m_elapsedMs >= m_durationMs) {
        m_current = m_target;
        m_animating = false;
        return true;
    }

    const float progress = m_elapsedMs / m_durationMs;
    const float eased = UIAnimation::evaluateEasing(progress, m_easing);
    m_current = m_start + (m_target - m_start) * eased;
    return true;
}

} // namespace tools3000::ui::native
