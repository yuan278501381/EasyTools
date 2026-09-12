#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativePage.h — Tools3000 纯原生多态设置页面基类
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_CORE_NATIVEPAGE_H
#define TOOLS3000_UI_NATIVE_CORE_NATIVEPAGE_H

#include "ui/native/core/NativeView.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <string>
#include <memory>

namespace tools3000::ui::native {

class NativePage : public NativeView {
public:
    NativePage();
    virtual ~NativePage() override;

    // ── 页面元数据（由派生具体配置页多态实现） ────────────────────────────────
    virtual std::string getPageId() const = 0;
    virtual std::wstring getPageTitle() const = 0;
    virtual std::wstring getPageSubtitle() const = 0;
    virtual IconType getPageIcon() const { return IconType::Settings; }
    virtual std::wstring getCategory() const { return L"核心效率工具"; }
    virtual std::string getRequiresPlugin() const { return ""; }

    /// 构建或返回此页面的 UIElement 内容树
    virtual std::shared_ptr<UIElement> buildContent() = 0;

    /// 页面状态刷新钩子（如配置变更、数据重新载入）
    virtual void refreshState();

    // ── NativeView 契约实现 ──────────────────────────────────────────────────
    virtual std::string getViewName() const override { return "Page_" + getPageId(); }

protected:
    std::shared_ptr<UIElement> m_cachedContent;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_CORE_NATIVEPAGE_H
