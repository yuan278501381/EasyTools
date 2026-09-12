#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIGeometry.h — Tools3000 原生 UI 核心几何与色彩基础类型
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMMON_UIGEOMETRY_H
#define TOOLS3000_UI_NATIVE_COMMON_UIGEOMETRY_H

#include <d2d1.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tools3000::ui::native {

struct Point {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Point() = default;
    constexpr Point(float px, float py) : x(px), y(py) {}

    constexpr bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Point& o) const { return !(*this == o); }

    D2D1_POINT_2F toD2D() const { return D2D1::Point2F(x, y); }
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;

    constexpr Size() = default;
    constexpr Size(float w, float h) : width(w), height(h) {}

    constexpr bool operator==(const Size& o) const { return width == o.width && height == o.height; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }

    D2D1_SIZE_F toD2D() const { return D2D1::SizeF(width, height); }
};

struct Thickness {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Thickness() = default;
    constexpr Thickness(float uniform)
        : left(uniform), top(uniform), right(uniform), bottom(uniform) {}
    constexpr Thickness(float horizontal, float vertical)
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    constexpr Thickness(float l, float t, float r, float b)
        : left(l), top(t), right(r), bottom(b) {}

    constexpr bool operator==(const Thickness& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    constexpr bool operator!=(const Thickness& o) const { return !(*this == o); }

    constexpr float horizontal() const { return left + right; }
    constexpr float vertical() const { return top + bottom; }
};

struct CornerRadius {
    float topLeft = 0.0f;
    float topRight = 0.0f;
    float bottomRight = 0.0f;
    float bottomLeft = 0.0f;

    constexpr CornerRadius() = default;
    constexpr CornerRadius(float uniform)
        : topLeft(uniform), topRight(uniform), bottomRight(uniform), bottomLeft(uniform) {}
    constexpr CornerRadius(float tl, float tr, float br, float bl)
        : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

    constexpr bool operator==(const CornerRadius& o) const {
        return topLeft == o.topLeft && topRight == o.topRight &&
               bottomRight == o.bottomRight && bottomLeft == o.bottomLeft;
    }
    constexpr bool operator!=(const CornerRadius& o) const { return !(*this == o); }
};

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float l, float t, float r, float b)
        : left(l), top(t), right(r), bottom(b) {}
    constexpr Rect(Point origin, Size size)
        : left(origin.x), top(origin.y), right(origin.x + size.width), bottom(origin.y + size.height) {}

    constexpr bool operator==(const Rect& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    constexpr bool operator!=(const Rect& o) const { return !(*this == o); }

    constexpr float width() const { return std::max(0.0f, right - left); }
    constexpr float height() const { return std::max(0.0f, bottom - top); }
    constexpr Point origin() const { return Point(left, top); }
    constexpr Size size() const { return Size(width(), height()); }
    constexpr Point center() const { return Point(left + width() * 0.5f, top + height() * 0.5f); }

    constexpr bool contains(Point pt) const {
        return pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom;
    }

    constexpr bool intersects(const Rect& other) const {
        return left < other.right && right > other.left &&
               top < other.bottom && bottom > other.top;
    }

    constexpr Rect inset(const Thickness& t) const {
        return Rect(left + t.left, top + t.top, right - t.right, bottom - t.bottom);
    }

    constexpr Rect offset(float dx, float dy) const {
        return Rect(left + dx, top + dy, right + dx, bottom + dy);
    }

    D2D1_RECT_F toD2D() const {
        return D2D1::RectF(left, top, right, bottom);
    }

    D2D1_ROUNDED_RECT toRoundedD2D(float radius) const {
        return D2D1::RoundedRect(toD2D(), radius, radius);
    }
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float red, float green, float blue, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}

    static constexpr Color fromRgba(int red, int green, int blue, float alpha = 1.0f) {
        return Color(red / 255.0f, green / 255.0f, blue / 255.0f, alpha);
    }

    static constexpr Color fromHex(uint32_t hex, float alpha = 1.0f) {
        float red = ((hex >> 16) & 0xFF) / 255.0f;
        float green = ((hex >> 8) & 0xFF) / 255.0f;
        float blue = (hex & 0xFF) / 255.0f;
        return Color(red, green, blue, alpha);
    }

    Color withAlpha(float newAlpha) const {
        return Color(r, g, b, newAlpha);
    }

    D2D1_COLOR_F toD2D() const {
        return D2D1::ColorF(r, g, b, a);
    }

    static Color lerp(const Color& c1, const Color& c2, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color(
            c1.r + (c2.r - c1.r) * t,
            c1.g + (c2.g - c1.g) * t,
            c1.b + (c2.b - c1.b) * t,
            c1.a + (c2.a - c1.a) * t
        );
    }
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMMON_UIGEOMETRY_H
