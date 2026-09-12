#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UIDeclarative.h — Tools3000 原生声明式组件装配语法糖 (Declarative Builder DSL)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_CORE_UIDECLARATIVE_H
#define TOOLS3000_UI_NATIVE_CORE_UIDECLARATIVE_H

#include "ui/native/core/UIElement.h"
#include "ui/native/components/UILabel.h"
#include "ui/native/components/UIIcon.h"
#include "ui/native/components/UIButton.h"
#include "ui/native/components/UIToggle.h"
#include "ui/native/components/UICard.h"
#include "ui/native/components/UISettingRow.h"
#include "ui/native/components/UISettingGroup.h"
#include "ui/native/components/UIBadge.h"
#include "ui/native/components/UISelect.h"
#include "ui/native/components/UITextInput.h"
#include "ui/native/components/UINumberInput.h"
#include "ui/native/components/UIHotkeyInput.h"
#include "ui/native/components/UIScrollView.h"
#include "ui/native/components/UITabs.h"
#include "ui/native/components/UICodeBadge.h"
#include "ui/native/components/UISeparator.h"

namespace tools3000::ui::native::declarative {

inline std::shared_ptr<UIElement> VStack(std::initializer_list<std::shared_ptr<UIElement>> items,
                                        float gap = 8.0f,
                                        const Thickness& padding = Thickness(0.0f)) {
    auto container = std::make_shared<UIElement>();
    container->layoutParams().direction = FlexDirection::Column;
    container->layoutParams().gap = gap;
    container->layoutParams().padding = padding;
    for (const auto& item : items) {
        container->addChild(item);
    }
    return container;
}

inline std::shared_ptr<UIElement> HStack(std::initializer_list<std::shared_ptr<UIElement>> items,
                                        float gap = 8.0f,
                                        JustifyContent justify = JustifyContent::FlexStart,
                                        AlignItems align = AlignItems::Center,
                                        const Thickness& padding = Thickness(0.0f)) {
    auto container = std::make_shared<UIElement>();
    container->layoutParams().direction = FlexDirection::Row;
    container->layoutParams().gap = gap;
    container->layoutParams().justify = justify;
    container->layoutParams().align = align;
    container->layoutParams().padding = padding;
    for (const auto& item : items) {
        container->addChild(item);
    }
    return container;
}

inline std::shared_ptr<UILabel> Label(const std::wstring& text,
                                      FontToken font = FontToken::Base,
                                      FontWeight weight = FontWeight::Medium,
                                      FontFamilyType family = FontFamilyType::Sans,
                                      TextColorRole role = TextColorRole::Default) {
    auto label = std::make_shared<UILabel>(text, font, weight, family);
    label->setColorRole(role);
    return label;
}

inline std::shared_ptr<UIIcon> Icon(IconType icon,
                                    float size = 16.0f,
                                    const Color& color = Color(),
                                    IconColorRole role = IconColorRole::Default) {
    return std::make_shared<UIIcon>(icon, size, color, role);
}

inline std::shared_ptr<UIButton> Button(const std::wstring& text,
                                        ButtonVariant variant = ButtonVariant::Secondary,
                                        ButtonSize size = ButtonSize::Md,
                                        std::function<void()> onClick = nullptr) {
    auto btn = std::make_shared<UIButton>(text, variant, size);
    if (onClick) btn->setOnClick(std::move(onClick));
    return btn;
}

inline std::shared_ptr<UIButton> Button(const std::wstring& text,
                                        IconType icon,
                                        ButtonVariant variant = ButtonVariant::Secondary,
                                        ButtonSize size = ButtonSize::Md,
                                        std::function<void()> onClick = nullptr) {
    auto btn = std::make_shared<UIButton>(text, variant, size);
    if (icon != IconType::None) btn->setIcon(icon);
    if (onClick) btn->setOnClick(std::move(onClick));
    return btn;
}

inline std::shared_ptr<UIToggle> Toggle(bool checked, std::function<void(bool)> onToggle = nullptr) {
    return std::make_shared<UIToggle>(checked, std::move(onToggle));
}

inline std::shared_ptr<UICard> Card(const std::wstring& title,
                                    const std::wstring& subtitle,
                                    std::initializer_list<std::shared_ptr<UIElement>> items,
                                    std::shared_ptr<UIElement> headerAction = nullptr) {
    auto card = std::make_shared<UICard>(title, subtitle);
    if (headerAction) card->setHeaderAction(headerAction);
    for (const auto& item : items) {
        card->addChild(item);
    }
    return card;
}

inline std::shared_ptr<UISettingRow> SettingRow(const std::wstring& label,
                                                const std::wstring& desc,
                                                std::shared_ptr<UIElement> control) {
    return std::make_shared<UISettingRow>(label, desc, control);
}

inline std::shared_ptr<UISettingGroup> SettingGroup(const std::wstring& title,
                                                    IconType icon,
                                                    std::initializer_list<std::shared_ptr<UIElement>> items) {
    auto group = std::make_shared<UISettingGroup>(title, icon);
    for (const auto& item : items) {
        group->addChild(item);
    }
    return group;
}

inline std::shared_ptr<UIBadge> Badge(const std::wstring& text, BadgeVariant variant = BadgeVariant::Primary) {
    return std::make_shared<UIBadge>(text, variant);
}

inline std::shared_ptr<UISelect> Select(const std::vector<SelectOption>& options,
                                        const std::string& currentVal,
                                        std::function<void(const std::string&)> onSelect = nullptr) {
    return std::make_shared<UISelect>(options, currentVal, std::move(onSelect));
}

inline std::shared_ptr<UITextInput> TextInput(const std::wstring& value,
                                              const std::wstring& placeholder = L"",
                                              std::function<void(const std::wstring&)> onChange = nullptr) {
    return std::make_shared<UITextInput>(value, placeholder, std::move(onChange));
}

inline std::shared_ptr<UINumberInput> NumberInput(int val, int minVal, int maxVal, int step = 1,
                                                  const std::wstring& unit = L"",
                                                  std::function<void(int)> onChange = nullptr) {
    return std::make_shared<UINumberInput>(val, minVal, maxVal, step, unit, std::move(onChange));
}

inline std::shared_ptr<UIHotkeyInput> HotkeyInput(const std::string& hotkey,
                                                  std::function<void(const std::string&)> onChange = nullptr) {
    return std::make_shared<UIHotkeyInput>(hotkey, std::move(onChange));
}

inline std::shared_ptr<UIScrollView> ScrollView(std::shared_ptr<UIElement> content) {
    return std::make_shared<UIScrollView>(content);
}

inline std::shared_ptr<UITabs> Tabs(const std::vector<TabItem>& tabs,
                                    const std::string& activeId = "",
                                    std::function<void(const std::string&)> onTabChange = nullptr) {
    return std::make_shared<UITabs>(tabs, activeId, std::move(onTabChange));
}

inline std::shared_ptr<UICodeBadge> CodeBadge(const std::wstring& code) {
    return std::make_shared<UICodeBadge>(code);
}

inline std::shared_ptr<UISeparator> Separator(bool vertical = false) {
    return std::make_shared<UISeparator>(vertical);
}

} // namespace tools3000::ui::native::declarative

#endif // TOOLS3000_UI_NATIVE_CORE_UIDECLARATIVE_H
