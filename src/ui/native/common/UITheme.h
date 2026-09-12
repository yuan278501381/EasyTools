#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UITheme.h — Tools3000 原生设计系统令牌与主题中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMMON_UITHEME_H
#define TOOLS3000_UI_NATIVE_COMMON_UITHEME_H

#include "ui/native/common/UIGeometry.h"
#include <string>

namespace tools3000::ui::native {

enum class ThemeMode {
    Dark,
    Light,
    System
};

struct ThemePalette {
    // 背景层级
    Color bgBase;
    Color bgSurface;
    Color bgElevated;
    Color bgOverlay;

    // 侧边栏微晶底板与边框
    Color sidebarBg;
    Color sidebarBorder;

    // 卡片与边框
    Color cardBg;
    Color cardBorder;
    Color cardHover;
    Color cardBorderHover;

    // 文本颜色
    Color textPrimary;
    Color textSecondary;
    Color textMuted;
    Color textDisabled;

    // 动态强调色
    Color primary;
    Color primaryHover;
    Color primaryDim;
    Color primaryGlow;

    // 语义色彩
    Color success;
    Color warning;
    Color danger;
    Color info;

    // 输入与交互
    Color inputBg;
    Color inputBorder;
    Color inputFocus;

    // 滚动条与微晶分割
    Color scrollbarThumb;
    Color scrollbarHover;
    Color separatorShadow;
    Color separatorHighlight;

    // 顶部极细微晶反光线条
    Color topEdgeSpecular;
    Color subtleOuterBorder;

    // 通用兼容别名
    Color border;
    Color text;
    Color accent;

    // 圆角标准
    float radiusSm = 8.0f;
    float radiusMd = 12.0f;
    float radiusLg = 16.0f;
    float radiusPill = 9999.0f;
};

class UITheme {
public:
    static UITheme& instance();

    /// 从本地配置中心全量同步当前主题偏好与强调色
    void syncWithConfig();

    /// 设置主题模式 (Dark / Light / System)
    void setThemeMode(ThemeMode mode);
    ThemeMode getThemeMode() const { return m_mode; }

    /// 设置动态强调色名 (如 "blue", "purple", "emerald", "amber", "rose", "cyan")
    void setAccentColor(const std::string& accentName);
    const std::string& getAccentColor() const { return m_accentName; }

    /// 当前实际呈现是否为深色模式
    bool isEffectiveDark() const;

    /// 获取当前生效的主题调色板
    const ThemePalette& palette() const { return m_palette; }

    /// 重新根据系统或配置刷新调色板
    void refreshPalette();

private:
    UITheme();
    ~UITheme() = default;
    UITheme(const UITheme&) = delete;
    UITheme& operator=(const UITheme&) = delete;

    ThemeMode m_mode = ThemeMode::Dark;
    std::string m_accentName = "blue";
    ThemePalette m_palette;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMMON_UITHEME_H
