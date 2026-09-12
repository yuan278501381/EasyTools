#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeColorPickerPage.h — Tools3000 原生屏幕拾色器与调色盘设置页
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_PAGES_NATIVECOLORPICKERPAGE_H
#define TOOLS3000_UI_NATIVE_PAGES_NATIVECOLORPICKERPAGE_H

#include "ui/native/core/NativePage.h"

namespace tools3000::ui::native {

class NativeColorPickerPage : public NativePage {
public:
    NativeColorPickerPage();
    virtual ~NativeColorPickerPage() override;

    virtual std::string getPageId() const override { return "color_picker"; }
    virtual std::wstring getPageTitle() const override { return L"屏幕拾色器"; }
    virtual std::wstring getPageSubtitle() const override {
        return L"支持多格式色值复制 (HEX / RGB / HSL)、历史调色盘归档与超高倍率像素放大镜";
    }
    virtual IconType getPageIcon() const override { return IconType::Palette; }
    virtual std::wstring getCategory() const override { return L"扩展应用"; }

    virtual std::shared_ptr<UIElement> buildContent() override;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_PAGES_NATIVECOLORPICKERPAGE_H
