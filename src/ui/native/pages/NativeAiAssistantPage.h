#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeAiAssistantPage.h — Tools3000 原生 AI 极速助手设置与交互演练页
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_PAGES_NATIVEAIASSISTANTPAGE_H
#define TOOLS3000_UI_NATIVE_PAGES_NATIVEAIASSISTANTPAGE_H

#include "ui/native/core/NativePage.h"

namespace tools3000::ui::native {

class NativeAiAssistantPage : public NativePage {
public:
    NativeAiAssistantPage();
    virtual ~NativeAiAssistantPage() override;

    virtual std::string getPageId() const override { return "ai_assistant"; }
    virtual std::wstring getPageTitle() const override { return L"AI 极速助手"; }
    virtual std::wstring getPageSubtitle() const override {
        return L"配置本地大模型与在线 AI 引擎，提供一键划词翻译、代码优化与智能总结";
    }
    virtual IconType getPageIcon() const override { return IconType::Bot; }
    virtual std::wstring getCategory() const override { return L"扩展应用"; }

    virtual std::shared_ptr<UIElement> buildContent() override;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_PAGES_NATIVEAIASSISTANTPAGE_H
