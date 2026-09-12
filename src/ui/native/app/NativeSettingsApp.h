#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeSettingsApp.h — Tools3000 纯 C++ 原生设置中心主应用外壳
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_APP_NATIVESETTINGSAPP_H
#define TOOLS3000_UI_NATIVE_APP_NATIVESETTINGSAPP_H

#include "ui/native/common/UITheme.h"
#include "ui/native/window/NativeWindowHost.h"
#include "ui/native/components/UIButton.h"
#include "ui/native/components/UILabel.h"
#include "ui/native/components/UIScrollView.h"
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace tools3000::ui::native {

class NativeSettingsApp {
public:
    static NativeSettingsApp& instance();

    /// 创建并显示设置窗口
    void show(HINSTANCE hInstance = nullptr);

    /// 静默预热原生 Direct2D 渲染环境
    void preload(HINSTANCE hInstance = nullptr);

    /// 隐藏
    void hide();

    /// 销毁
    void destroy();

    /// 是否可见
    bool isVisible() const;

    /// 获取 Win32 宿主窗口句柄
    HWND hwnd() const { return m_host ? m_host->hwnd() : nullptr; }

    /// 导航到指定页面 (如 "general", "gesture", "capture", "about")
    void navigateTo(const std::string& pageId);

    /// 统一应用主题模式 (Dark / Light / System) 并同步全窗口
    void applyThemeMode(ThemeMode mode, bool saveConfig = true);

    /// 统一应用主题强调色并同步全窗口
    void applyAccentColor(const std::string& accent, bool saveConfig = true);

    NativeWindowHost* getHost() { return m_host.get(); }
    std::shared_ptr<UIElement> createPageContent(const std::string& pageId);
    std::shared_ptr<UIElement> getSidebar() const { return m_sidebar; }
    std::shared_ptr<UIButton> getThemeDarkButton() const { return m_btnThemeDark; }
    std::shared_ptr<UIButton> getThemeLightButton() const { return m_btnThemeLight; }
    std::shared_ptr<UIButton> getThemeSysButton() const { return m_btnThemeSys; }

private:
    NativeSettingsApp();
    ~NativeSettingsApp();
    NativeSettingsApp(const NativeSettingsApp&) = delete;
    NativeSettingsApp& operator=(const NativeSettingsApp&) = delete;

    void ensureInitialized(HINSTANCE hInstance);
    void buildUI();
    std::shared_ptr<UIElement> createSidebar();
    std::shared_ptr<UIElement> createTitleBar();
    std::shared_ptr<UIElement> createPageHeader();
    void updatePageHeader(const std::string& pageId);
    void updateThemeButtons(ThemeMode mode);

    // 页面构建器
    std::shared_ptr<UIElement> buildGeneralPage();
    std::shared_ptr<UIElement> buildPluginsPage();
    std::shared_ptr<UIElement> buildSearchPage();
    std::shared_ptr<UIElement> buildGesturePage();
    std::shared_ptr<UIElement> buildHotCornerPage();
    std::shared_ptr<UIElement> buildCapturePage();
    std::shared_ptr<UIElement> buildHistoryPage();
    std::shared_ptr<UIElement> buildOcrPage();
    std::shared_ptr<UIElement> buildKeycastPage();
    std::shared_ptr<UIElement> buildSpotlightPage();
    std::shared_ptr<UIElement> buildDialogEnhancerPage();
    std::shared_ptr<UIElement> buildRemoteBoostPage();
    std::shared_ptr<UIElement> buildStatsPage();
    std::shared_ptr<UIElement> buildAboutPage();
    std::shared_ptr<UIElement> buildAiAssistantPage();
    std::shared_ptr<UIElement> buildColorPickerPage();
    std::shared_ptr<UIElement> buildClipboardManagerPage();
    std::shared_ptr<UIElement> buildMarkdownPreviewPage();

private:
    std::unique_ptr<NativeWindowHost> m_host;
    std::string m_currentPageId = "general";

    std::shared_ptr<UIElement> m_rootContainer;
    std::shared_ptr<UIElement> m_contentArea;
    std::shared_ptr<UIElement> m_pageHeader;
    std::shared_ptr<UILabel> m_pageHeaderTitle;
    std::shared_ptr<UILabel> m_pageHeaderSubtitle;
    std::shared_ptr<UIScrollView> m_pageScroll;
    std::shared_ptr<UIElement> m_sidebar;
    std::shared_ptr<UIButton> m_btnMax;
    std::shared_ptr<UILabel> m_titleBarPageTitle;
    std::shared_ptr<UIButton> m_btnThemeDark;
    std::shared_ptr<UIButton> m_btnThemeLight;
    std::shared_ptr<UIButton> m_btnThemeSys;
    std::vector<std::pair<std::string, std::shared_ptr<UIButton>>> m_navButtons;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_APP_NATIVESETTINGSAPP_H
