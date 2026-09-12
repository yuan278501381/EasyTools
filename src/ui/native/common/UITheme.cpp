#include "ui/native/common/UITheme.h"
#include "core/utils/ThemeUtils.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include <algorithm>

namespace tools3000::ui::native {

UITheme& UITheme::instance() {
    static UITheme s_instance;
    return s_instance;
}

UITheme::UITheme() {
    syncWithConfig();
}

void UITheme::syncWithConfig() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    std::string theme = cfg.get<std::string>("/general/theme", "system");
    if (theme == "dark") m_mode = ThemeMode::Dark;
    else if (theme == "light") m_mode = ThemeMode::Light;
    else m_mode = ThemeMode::System;
    m_accentName = cfg.get<std::string>("/general/accentColor", "blue");
    refreshPalette();
}

bool UITheme::isEffectiveDark() const {
    if (m_mode == ThemeMode::Dark) return true;
    if (m_mode == ThemeMode::Light) return false;
    return tools3000::core::WinUtils::isSystemDarkMode();
}

void UITheme::setThemeMode(ThemeMode mode) {
    m_mode = mode;
    refreshPalette();
}

void UITheme::setAccentColor(const std::string& accentName) {
    m_accentName = accentName;
    refreshPalette();
}

void UITheme::refreshPalette() {
    const bool dark = isEffectiveDark();
    const auto rgb = tools3000::core::getAccentColorRGB(m_accentName);

    m_palette.primary = Color(rgb.r, rgb.g, rgb.b, 1.0f);
    m_palette.primaryHover = Color(
        std::min(1.0f, rgb.r * 1.15f + 0.05f),
        std::min(1.0f, rgb.g * 1.15f + 0.05f),
        std::min(1.0f, rgb.b * 1.15f + 0.05f),
        1.0f
    );
    m_palette.primaryDim = Color(rgb.r, rgb.g, rgb.b, dark ? 0.22f : 0.14f);
    m_palette.primaryGlow = Color(rgb.r, rgb.g, rgb.b, dark ? 0.45f : 0.28f);

    m_palette.success = Color(0.06f, 0.72f, 0.44f, 1.0f);
    m_palette.warning = Color(0.98f, 0.75f, 0.14f, 1.0f);
    m_palette.danger  = Color(0.97f, 0.44f, 0.44f, 1.0f);
    m_palette.info    = Color(0.38f, 0.65f, 0.98f, 1.0f);

    if (dark) {
        // 暗色黑曜石磨砂质感 (Obsidian Glass)
        m_palette.bgBase = Color::fromHex(0x12111a, 1.0f);
        m_palette.bgSurface = Color::fromHex(0x191724, 0.94f);
        m_palette.bgElevated = Color::fromHex(0x211e30, 0.92f);
        m_palette.bgOverlay = Color(0.0f, 0.0f, 0.0f, 0.75f);

        m_palette.sidebarBg = Color::fromHex(0x191724, 0.96f);
        m_palette.sidebarBorder = Color(1.0f, 1.0f, 1.0f, 0.07f);

        m_palette.cardBg = Color::fromHex(0x1e1c2c, 0.88f);
        m_palette.cardBorder = Color(1.0f, 1.0f, 1.0f, 0.08f);
        m_palette.cardHover = Color::fromHex(0x262338, 0.92f);
        m_palette.cardBorderHover = Color(1.0f, 1.0f, 1.0f, 0.16f);

        m_palette.textPrimary = Color::fromHex(0xf8f8fc, 1.0f);
        m_palette.textSecondary = Color::fromHex(0xc4c4d6, 0.92f);
        m_palette.textMuted = Color::fromHex(0x9494aa, 0.85f);
        m_palette.textDisabled = Color::fromHex(0x555568, 0.65f);

        m_palette.inputBg = Color::fromHex(0x0f0f19, 0.65f);
        m_palette.inputBorder = Color(1.0f, 1.0f, 1.0f, 0.10f);
        m_palette.inputFocus = m_palette.primary;

        m_palette.scrollbarThumb = Color(1.0f, 1.0f, 1.0f, 0.15f);
        m_palette.scrollbarHover = Color(1.0f, 1.0f, 1.0f, 0.28f);
        m_palette.separatorShadow = Color(0.0f, 0.0f, 0.0f, 0.40f);
        m_palette.separatorHighlight = Color(1.0f, 1.0f, 1.0f, 0.08f);

        m_palette.topEdgeSpecular = Color(1.0f, 1.0f, 1.0f, 0.15f);
        m_palette.subtleOuterBorder = Color(1.0f, 1.0f, 1.0f, 0.10f);
    } else {
        // 浅色纯净陶瓷微晶 (Crystal White Glass)
        m_palette.bgBase = Color::fromHex(0xf4f6fb, 1.0f);
        m_palette.bgSurface = Color::fromHex(0xffffff, 0.94f);
        m_palette.bgElevated = Color::fromHex(0xf8fafc, 0.92f);
        m_palette.bgOverlay = Color(0.0f, 0.0f, 0.0f, 0.40f);

        m_palette.sidebarBg = Color::fromHex(0xfcfdfd, 0.96f);
        m_palette.sidebarBorder = Color(0.06f, 0.09f, 0.16f, 0.08f);

        m_palette.cardBg = Color::fromHex(0xffffff, 0.90f);
        m_palette.cardBorder = Color(0.06f, 0.09f, 0.16f, 0.08f);
        m_palette.cardHover = Color::fromHex(0xf1f5f9, 0.96f);
        m_palette.cardBorderHover = Color(0.06f, 0.09f, 0.16f, 0.14f);

        m_palette.textPrimary = Color::fromHex(0x0f172a, 1.0f);
        m_palette.textSecondary = Color::fromHex(0x334155, 0.92f);
        m_palette.textMuted = Color::fromHex(0x64748b, 0.85f);
        m_palette.textDisabled = Color::fromHex(0x94a3b8, 0.65f);

        m_palette.inputBg = Color::fromHex(0xffffff, 0.85f);
        m_palette.inputBorder = Color(0.06f, 0.09f, 0.16f, 0.12f);
        m_palette.inputFocus = m_palette.primary;

        m_palette.scrollbarThumb = Color(0.0f, 0.0f, 0.0f, 0.15f);
        m_palette.scrollbarHover = Color(0.0f, 0.0f, 0.0f, 0.28f);
        m_palette.separatorShadow = Color(0.06f, 0.09f, 0.16f, 0.08f);
        m_palette.separatorHighlight = Color(1.0f, 1.0f, 1.0f, 0.70f);

        m_palette.topEdgeSpecular = Color(1.0f, 1.0f, 1.0f, 0.75f);
        m_palette.subtleOuterBorder = Color(0.06f, 0.09f, 0.16f, 0.08f);
    }

    m_palette.border = m_palette.cardBorder;
    m_palette.text = m_palette.textPrimary;
    m_palette.accent = m_palette.primary;
}

} // namespace tools3000::ui::native
