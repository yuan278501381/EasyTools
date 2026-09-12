#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeClipboardManagerPage.h — Tools3000 原生剪贴板历史管理器设置页
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_PAGES_NATIVECLIPBOARDMANAGERPAGE_H
#define TOOLS3000_UI_NATIVE_PAGES_NATIVECLIPBOARDMANAGERPAGE_H

#include "ui/native/core/NativePage.h"

namespace tools3000::ui::native {

class NativeClipboardManagerPage : public NativePage {
public:
    NativeClipboardManagerPage();
    virtual ~NativeClipboardManagerPage() override;

    virtual std::string getPageId() const override { return "clipboard_manager"; }
    virtual std::wstring getPageTitle() const override { return L"剪贴板管理"; }
    virtual std::wstring getPageSubtitle() const override {
        return L"本地安全存储剪贴板历史，支持文本、代码、富文本与图像极速检索与回溯粘贴";
    }
    virtual IconType getPageIcon() const override { return IconType::Copy; }
    virtual std::wstring getCategory() const override { return L"扩展应用"; }

    virtual std::shared_ptr<UIElement> buildContent() override;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_PAGES_NATIVECLIPBOARDMANAGERPAGE_H
