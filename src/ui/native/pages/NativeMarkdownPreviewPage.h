#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeMarkdownPreviewPage.h — Tools3000 原生 Markdown 极速预览器设置页
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_PAGES_NATIVEMARKDOWNPREVIEWPAGE_H
#define TOOLS3000_UI_NATIVE_PAGES_NATIVEMARKDOWNPREVIEWPAGE_H

#include "ui/native/core/NativePage.h"

namespace tools3000::ui::native {

class NativeMarkdownPreviewPage : public NativePage {
public:
    NativeMarkdownPreviewPage();
    virtual ~NativeMarkdownPreviewPage() override;

    virtual std::string getPageId() const override { return "markdown_preview"; }
    virtual std::wstring getPageTitle() const override { return L"Markdown 预览"; }
    virtual std::wstring getPageSubtitle() const override {
        return L"空格极速预览 Markdown 文档，支持代码语法高亮、LaTeX 数学公式与 Mermaid 架构图";
    }
    virtual IconType getPageIcon() const override { return IconType::FileText; }
    virtual std::wstring getCategory() const override { return L"扩展应用"; }

    virtual std::shared_ptr<UIElement> buildContent() override;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_PAGES_NATIVEMARKDOWNPREVIEWPAGE_H
