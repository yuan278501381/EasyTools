#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GlassmorphismRenderer.h — Tools3000 微晶玻璃拟态与超质感视觉渲染引擎
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_GRAPHICS_GLASSMORPHISMRENDERER_H
#define TOOLS3000_UI_NATIVE_GRAPHICS_GLASSMORPHISMRENDERER_H

#include "ui/native/graphics/UIRenderContext.h"
#include "ui/native/common/UITheme.h"

namespace tools3000::ui::native {

enum class BadgeVariant {
    Primary,
    Success,
    Warning,
    Danger,
    Muted
};

class GlassmorphismRenderer {
public:
    /// 绘制多层环境光柔和微阴影 (Ambient & Key Multi-Layer Soft Shadows)
    static void drawElevationShadow(
        UIRenderContext& ctx,
        const Rect& rect,
        float radius,
        int elevation = 1,
        bool isDark = true
    );

    /// 绘制通用微晶玻璃浮岛面板 (Glass Panel with Specular Edge Sheen)
    static void drawGlassPanel(
        UIRenderContext& ctx,
        const Rect& rect,
        float radius,
        bool isDark = true,
        int elevation = 1
    );

    /// 绘制侧边栏通透微晶底衬与反光边缘 (Sidebar Ceramic / Obsidian Glass Background)
    static void drawSidebar(
        UIRenderContext& ctx,
        const Rect& bounds,
        bool isDark = true
    );

    /// 绘制世界级微晶背景环境光晕 (Ambient Background Mesh Lighting)
    static void drawWindowBackground(
        UIRenderContext& ctx,
        const Rect& bounds,
        bool isDark,
        const Color& primaryAccent
    );

    /// 绘制微晶卡片底板 (Card Panel with Subtle Border and Inset)
    static void drawCard(
        UIRenderContext& ctx,
        const Rect& rect,
        float radius,
        bool isHovered = false,
        bool isDark = true
    );

    /// 绘制多态微晶按键胶囊 (Interactive Pill Button)
    static void drawButtonPill(
        UIRenderContext& ctx,
        const Rect& rect,
        float radius,
        bool isHovered,
        bool isPressed,
        bool isDanger = false,
        bool isConfirm = false,
        const Color& themeAccent = Color(),
        float alpha = 1.0f,
        bool isDark = true
    );

    /// 绘制微晶细腻 1px 双层刻痕分隔线 (Incision Separator)
    static void drawSeparator(
        UIRenderContext& ctx,
        Point p1,
        Point p2,
        bool isDark = true
    );

    /// 绘制微晶胶囊徽章 (Glass Capsule Badge)
    static void drawBadge(
        UIRenderContext& ctx,
        const Rect& rect,
        const std::wstring& text,
        BadgeVariant variant,
        bool isDark = true
    );

    /// 绘制等宽微晶代码胶囊 (Inline Glass Code Badge)
    static void drawCodeBadge(
        UIRenderContext& ctx,
        const Rect& rect,
        const std::wstring& text,
        bool isDark = true
    );
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_GRAPHICS_GLASSMORPHISMRENDERER_H
