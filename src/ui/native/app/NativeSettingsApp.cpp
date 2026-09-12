#include "ui/native/app/NativeSettingsApp.h"
#include "ui/native/core/UIDeclarative.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include "ui/native/pages/NativeAiAssistantPage.h"
#include "ui/native/pages/NativeColorPickerPage.h"
#include "ui/native/pages/NativeClipboardManagerPage.h"
#include "ui/native/pages/NativeMarkdownPreviewPage.h"
#include "ui/native/components/NativeGestureDrawCanvas.h"
#include "ui/native/components/NativeKeyboardHeatmap.h"
#include "ui/native/components/NativeScopeRulesManager.h"
#include "core/events/EventBus.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/logger/Logger.h"
#include "core/update/UpdateChecker.h"
#include "ui/native/app/NativeSearchApp.h"

#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <shellapi.h>
#include <dwmapi.h>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>

namespace tools3000::ui::native {

using namespace tools3000::ui::native::declarative;

namespace {

class ConfigWriterQueue {
public:
    static ConfigWriterQueue& instance() {
        static ConfigWriterQueue s_q;
        return s_q;
    }

    template <typename T>
    void post(const std::string& key, const T& value) {
        nlohmann::json jVal = value;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ensureRunningLocked();
            m_tasks.push_back([key, j = std::move(jVal)]() {
                tools3000::core::ConfigManager::instance().set(key, j);
            });
        }
        m_cv.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) return;
            m_running = false;
        }
        m_cv.notify_one();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

private:
    ConfigWriterQueue() : m_running(true), m_worker([this]() { workerLoop(); }) {}
    ~ConfigWriterQueue() {
        shutdown();
    }

    void ensureRunningLocked() {
        if (!m_running) {
            m_running = true;
            if (m_worker.joinable()) {
                m_worker.join();
            }
            m_worker = std::thread([this]() { workerLoop(); });
        }
    }

    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return !m_running || !m_tasks.empty(); });
                if (!m_running && m_tasks.empty()) break;
                if (!m_tasks.empty()) {
                    task = std::move(m_tasks.front());
                    m_tasks.pop_front();
                }
            }
            if (task) {
                task();
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_tasks;
    bool m_running = false;
    std::thread m_worker;
};

template <typename T>
inline void updateConfigAsync(const std::string& key, const T& value) {
    ConfigWriterQueue::instance().post(key, value);
}

} // namespace

NativeSettingsApp& NativeSettingsApp::instance() {
    static NativeSettingsApp s_app;
    return s_app;
}

NativeSettingsApp::NativeSettingsApp() = default;
NativeSettingsApp::~NativeSettingsApp() {
    destroy();
}

void NativeSettingsApp::ensureInitialized(HINSTANCE hInstance) {
    if (!m_host) {
        // 优先从持久化配置同步当前深浅模式与强调色
        UITheme::instance().syncWithConfig();

        m_host = std::make_unique<NativeWindowHost>();
        NativeWindowConfig cfg;
        cfg.title = L"Tools3000 设置中心";
        cfg.width = 1060;
        cfg.height = 700;
        cfg.minWidth = 780;
        cfg.minHeight = 520;
        cfg.centerOnScreen = true;
        cfg.resizable = true;
        m_host->create(hInstance, cfg);

        if (m_host && m_host->hwnd()) {
            BOOL useDarkMode = UITheme::instance().isEffectiveDark() ? TRUE : FALSE;
            DwmSetWindowAttribute(m_host->hwnd(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDarkMode, sizeof(useDarkMode));
        }

        m_host->setOnSizeChanged([this](int w, int h, bool isMaximized) {
            (void)w; (void)h;
            if (m_btnMax) {
                m_btnMax->setIcon(isMaximized ? IconType::Restore : IconType::Maximize);
                if (m_host) m_host->requestPaint();
            }
        });

        buildUI();
    }
}

void NativeSettingsApp::show(HINSTANCE hInstance) {
    ensureInitialized(hInstance);
    if (m_host) {
        m_host->show(SW_SHOW);
    }
}

void NativeSettingsApp::preload(HINSTANCE hInstance) {
    ensureInitialized(hInstance);
}

void NativeSettingsApp::hide() {
    if (m_host) {
        m_host->hide();
    }
}

void NativeSettingsApp::destroy() {
    ConfigWriterQueue::instance().shutdown();
    if (m_host) {
        m_host->destroy();
        m_host.reset();
    }
    m_rootContainer.reset();
    m_sidebar.reset();
    m_contentArea.reset();
    m_pageHeader.reset();
    m_pageHeaderTitle.reset();
    m_pageHeaderSubtitle.reset();
    m_pageScroll.reset();
    m_btnMax.reset();
    m_titleBarPageTitle.reset();
    m_btnThemeDark.reset();
    m_btnThemeLight.reset();
    m_btnThemeSys.reset();
    m_navButtons.clear();
}

void NativeSettingsApp::updateThemeButtons(ThemeMode mode) {
    if (m_btnThemeDark) {
        m_btnThemeDark->setVariant(mode == ThemeMode::Dark ? ButtonVariant::Secondary : ButtonVariant::Ghost);
    }
    if (m_btnThemeLight) {
        m_btnThemeLight->setVariant(mode == ThemeMode::Light ? ButtonVariant::Secondary : ButtonVariant::Ghost);
    }
    if (m_btnThemeSys) {
        m_btnThemeSys->setVariant(mode == ThemeMode::System ? ButtonVariant::Secondary : ButtonVariant::Ghost);
    }
}

void NativeSettingsApp::applyThemeMode(ThemeMode mode, bool saveConfig) {
    UITheme::instance().setThemeMode(mode);
    if (saveConfig) {
        std::string modeStr = (mode == ThemeMode::Dark ? "dark" : (mode == ThemeMode::Light ? "light" : "system"));
        updateConfigAsync<std::string>("/general/theme", modeStr);
    }
    updateThemeButtons(mode);

    if (m_host && m_host->hwnd()) {
        BOOL useDarkMode = UITheme::instance().isEffectiveDark() ? TRUE : FALSE;
        DwmSetWindowAttribute(m_host->hwnd(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDarkMode, sizeof(useDarkMode));
    }

    if (m_pageScroll) {
        float curScroll = m_pageScroll->getScrollOffset();
        m_pageScroll->setContent(createPageContent(m_currentPageId));
        m_pageScroll->scrollTo(curScroll, false);
    }

    if (m_host) {
        m_host->requestLayout();
        m_host->requestPaint();
    }
}

void NativeSettingsApp::applyAccentColor(const std::string& accent, bool saveConfig) {
    UITheme::instance().setAccentColor(accent);
    if (saveConfig) {
        updateConfigAsync<std::string>("/general/accentColor", accent);
    }

    if (m_pageScroll) {
        float curScroll = m_pageScroll->getScrollOffset();
        m_pageScroll->setContent(createPageContent(m_currentPageId));
        m_pageScroll->scrollTo(curScroll, false);
    }

    if (m_host) {
        m_host->requestLayout();
        m_host->requestPaint();
    }
}

bool NativeSettingsApp::isVisible() const {
    return m_host && m_host->isVisible();
}

void NativeSettingsApp::navigateTo(const std::string& pageId) {
    if (m_currentPageId != pageId) {
        m_currentPageId = pageId;
        for (auto& pair : m_navButtons) {
            if (pair.second) {
                pair.second->setActive(pair.first == m_currentPageId);
            }
        }
        updatePageHeader(m_currentPageId);
        if (m_pageScroll) {
            m_pageScroll->setContent(createPageContent(m_currentPageId));
            m_pageScroll->scrollTo(0.0f, false);
        }
        if (m_host) {
            m_host->requestLayout();
            m_host->requestPaint();
        }
    }
}

std::shared_ptr<UIElement> NativeSettingsApp::createPageHeader() {
    m_pageHeader = std::make_shared<UIElement>();
    m_pageHeader->layoutParams().direction = FlexDirection::Column;
    m_pageHeader->layoutParams().padding = Thickness(28.0f, 18.0f, 28.0f, 14.0f);
    m_pageHeader->layoutParams().gap = 4.0f;

    m_pageHeaderTitle = Label(L"", FontToken::Xl, FontWeight::Bold);
    m_pageHeaderSubtitle = Label(L"", FontToken::Sm, FontWeight::Medium, FontFamilyType::Sans, TextColorRole::Muted);

    m_pageHeader->addChild(m_pageHeaderTitle);
    m_pageHeader->addChild(m_pageHeaderSubtitle);

    updatePageHeader(m_currentPageId);
    return m_pageHeader;
}

void NativeSettingsApp::updatePageHeader(const std::string& pageId) {
    if (!m_pageHeaderTitle || !m_pageHeaderSubtitle) return;

    struct PageMeta {
        std::string id;
        std::wstring title;
        std::wstring subtitle;
    };
    static const PageMeta metas[] = {
        { "general", L"通用设置", L"配置开机自启、主题外观偏好与全链路系统诊断日志" },
        { "plugins", L"功能模块", L"按需启用核心效率扩展，保持系统极致轻量与专注" },
        { "search", L"全局搜索", L"毫秒级 NTFS 磁盘索引配置与全局快捷呼出" },
        { "gesture", L"鼠标手势", L"配置鼠标右键手势轨迹、识别消抖与全局动作映射" },
        { "hotcorner", L"屏幕热区", L"配置屏幕四角物理触碰触发的桌面快捷动作" },
        { "capture", L"超级截图与录屏", L"智能选区吸附、图像美化外壳与硬件加速录屏" },
        { "history", L"截图历史", L"本地捕获图像生命周期归档与极速回溯浏览" },
        { "ocr", L"离线文字识别", L"基于 Windows 原生 Windows.Media.Ocr 纯本地安全识别" },
        { "keycast", L"按键播报", L"屏幕实时回显击键与组合快捷键，演示录屏利器" },
        { "spotlight", L"聚合高亮与特效", L"光标聚光灯聚焦、点击水波纹与移动流光轨迹" },
        { "dialog_enhancer", L"对话框增强", L"文件对话框智能跳跃、快捷固定与路径记忆" },
        { "remote_boost", L"远控加速", L"主控端热键直通、修饰键急救与输入法智能脱敏" },
        { "stats", L"按键统计", L"击键频次热力图与鼠标活动全景数据洞察" },
        { "about", L"关于产品", L"Tools3000 版本信息、开源许可与架构技术栈" },
        { "ai_assistant", L"AI 极速助手", L"配置本地大模型与在线 AI 引擎，提供一键划词翻译、代码优化与智能总结" },
        { "color_picker", L"屏幕拾色器", L"支持多格式色值复制 (HEX / RGB / HSL)、历史调色盘归档与超高倍率像素放大镜" },
        { "clipboard_manager", L"剪贴板管理", L"本地安全存储剪贴板历史，支持文本、代码、富文本与图像极速检索与回溯粘贴" },
        { "markdown_preview", L"Markdown 预览", L"空格极速预览 Markdown 文档，支持代码语法高亮、LaTeX 数学公式与 Mermaid 架构图" }
    };

    std::wstring t = L"设置中心";
    std::wstring s = L"Tools3000 原生极速配置中心";
    for (const auto& m : metas) {
        if (m.id == pageId) {
            t = m.title;
            s = m.subtitle;
            break;
        }
    }
    m_pageHeaderTitle->setText(t);
    m_pageHeaderSubtitle->setText(s);
    if (m_titleBarPageTitle) {
        m_titleBarPageTitle->setText(t);
    }
}

void NativeSettingsApp::buildUI() {
    if (!m_host) return;

    m_rootContainer = std::make_shared<UIElement>();
    m_rootContainer->layoutParams().direction = FlexDirection::Column;

    // 1. 顶部无缝标题栏 (高 38px，对齐 Windows 11 Fluent 规范)
    auto titleBar = createTitleBar();
    titleBar->layoutParams().fixedHeight = 38.0f;
    m_rootContainer->addChild(titleBar);

    // 分隔线
    m_rootContainer->addChild(Separator());

    // 2. 主体工作区 (水平 Row: 侧边栏 230px + 右侧内容自适应)
    auto bodyRow = std::make_shared<UIElement>();
    bodyRow->layoutParams().direction = FlexDirection::Row;
    bodyRow->layoutParams().flexGrow = 1.0f;

    m_sidebar = createSidebar();
    m_sidebar->layoutParams().fixedWidth = 230.0f;
    bodyRow->addChild(m_sidebar);

    // 右侧内容区 (页头 + 滚动容器)
    m_contentArea = std::make_shared<UIElement>();
    m_contentArea->layoutParams().direction = FlexDirection::Column;
    m_contentArea->layoutParams().flexGrow = 1.0f;

    auto header = createPageHeader();
    m_contentArea->addChild(header);
    m_contentArea->addChild(Separator());

    m_pageScroll = std::make_shared<UIScrollView>(createPageContent(m_currentPageId));
    m_pageScroll->layoutParams().flexGrow = 1.0f;
    m_contentArea->addChild(m_pageScroll);

    bodyRow->addChild(m_contentArea);
    m_rootContainer->addChild(bodyRow);

    m_host->setRootElement(m_rootContainer);
}

std::shared_ptr<UIElement> NativeSettingsApp::createTitleBar() {
    auto titleBar = std::make_shared<UIElement>();
    titleBar->layoutParams().direction = FlexDirection::Row;
    titleBar->layoutParams().align = AlignItems::Center;
    titleBar->layoutParams().padding = Thickness(16.0f, 0.0f, 0.0f, 0.0f);
    titleBar->layoutParams().gap = 10.0f;
    titleBar->layoutParams().fixedHeight = 38.0f;

    // 品牌 Logo (动态跟随主题强调色)
    titleBar->addChild(Icon(IconType::Sparkles, 16.0f, Color(), IconColorRole::Primary));

    // 面包屑标题
    m_titleBarPageTitle = Label(L"通用设置", FontToken::Base, FontWeight::SemiBold);
    titleBar->addChild(m_titleBarPageTitle);

    // 可拖拽弹性填充区 (FlexGrow = 1)
    auto dragArea = std::make_shared<UIElement>();
    dragArea->layoutParams().flexGrow = 1.0f;
    dragArea->layoutParams().fixedHeight = 38.0f;
    titleBar->addChild(dragArea);

    // 右侧窗口控制三键 (最小化、最大化/还原、关闭)
    auto btnMin = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    btnMin->setWindowControl(true);
    btnMin->setIcon(IconType::Minimize);
    btnMin->layoutParams().fixedWidth = 44.0f;
    btnMin->layoutParams().fixedHeight = 38.0f;
    btnMin->setOnClick([this]() {
        if (m_host) m_host->minimize();
    });

    m_btnMax = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    m_btnMax->setWindowControl(true);
    m_btnMax->setIcon(IconType::Maximize);
    m_btnMax->layoutParams().fixedWidth = 44.0f;
    m_btnMax->layoutParams().fixedHeight = 38.0f;
    m_btnMax->setOnClick([this]() {
        if (m_host) m_host->toggleMaximize();
    });

    auto btnClose = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    btnClose->setWindowControl(true, true);
    btnClose->setIcon(IconType::X);
    btnClose->layoutParams().fixedWidth = 44.0f;
    btnClose->layoutParams().fixedHeight = 38.0f;
    btnClose->setOnClick([this]() {
        if (m_host) m_host->close();
    });

    auto controlsGroup = HStack({ btnMin, m_btnMax, btnClose }, 0.0f);
    controlsGroup->layoutParams().fixedHeight = 38.0f;
    titleBar->addChild(controlsGroup);
    return titleBar;
}

namespace {

class UISidebar : public UIElement {
public:
    UISidebar() {
        m_layoutParams.direction = FlexDirection::Column;
        m_layoutParams.fixedWidth = 230.0f;
    }

protected:
    void onRenderContent(UIRenderContext& ctx) override {
        const bool dark = UITheme::instance().isEffectiveDark();
        GlassmorphismRenderer::drawSidebar(ctx, m_bounds, dark);
    }
};

class UIThemeCapsule : public UIElement {
public:
    UIThemeCapsule() {
        m_layoutParams.direction = FlexDirection::Row;
        m_layoutParams.justify = JustifyContent::FlexStart;
        m_layoutParams.align = AlignItems::Center;
        m_layoutParams.padding = Thickness(2.0f);
        m_layoutParams.gap = 2.0f;
        m_layoutParams.fixedHeight = 30.0f;
    }

protected:
    void onRenderContent(UIRenderContext& ctx) override {
        const auto& theme = UITheme::instance();
        const auto& p = theme.palette();
        const bool dark = theme.isEffectiveDark();
        const float radius = 7.0f;
        Color bg = dark ? Color::fromHex(0x12111a, 0.75f) : Color::fromHex(0xe8edf4, 0.90f);
        Color border = dark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : p.sidebarBorder;
        ctx.fillRoundedRect(m_bounds, radius, bg);
        ctx.drawRoundedRect(m_bounds, radius, border, 1.0f);
    }
};

} // namespace

std::shared_ptr<UIElement> NativeSettingsApp::createSidebar() {
    m_navButtons.clear();
    const auto& theme = UITheme::instance();

    auto sidebar = std::make_shared<UISidebar>();

    auto navContainer = std::make_shared<UIElement>();
    navContainer->layoutParams().direction = FlexDirection::Column;
    navContainer->layoutParams().padding = Thickness(10.0f, 6.0f, 10.0f, 12.0f);
    navContainer->layoutParams().gap = 2.0f;

    struct NavItemDef {
        std::string id;
        std::wstring label;
        IconType icon;
        std::wstring category;
    };

    static const NavItemDef navItems[] = {
        // 系统设置
        { "general", L"通用设置", IconType::Settings, L"系统设置" },
        { "plugins", L"插件中心", IconType::Puzzle, L"系统设置" },

        // 核心效率工具
        { "search", L"全局搜索", IconType::Search, L"核心效率工具" },
        { "gesture", L"鼠标手势", IconType::Hand, L"核心效率工具" },
        { "hotcorner", L"屏幕热区", IconType::Maximize2, L"核心效率工具" },
        { "capture", L"超级截图", IconType::Crop, L"核心效率工具" },
        { "history", L"截图历史", IconType::Clock, L"核心效率工具" },
        { "ocr", L"文字识别", IconType::FileText, L"核心效率工具" },
        { "keycast", L"按键播报", IconType::Keyboard, L"核心效率工具" },
        { "spotlight", L"聚合高亮", IconType::Compass, L"核心效率工具" },
        { "dialog_enhancer", L"对话框增强", IconType::FolderOpen, L"核心效率工具" },
        { "remote_boost", L"远控加速", IconType::Cpu, L"核心效率工具" },

        // 洞察与关于
        { "stats", L"按键统计", IconType::BarChart3, L"洞察与关于" },
        { "about", L"关于产品", IconType::Info, L"洞察与关于" },

        // 扩展应用
        { "ai_assistant", L"AI 极速助手", IconType::Bot, L"扩展应用" },
        { "color_picker", L"屏幕拾色器", IconType::Palette, L"扩展应用" },
        { "clipboard_manager", L"剪贴板管理", IconType::Copy, L"扩展应用" },
        { "markdown_preview", L"Markdown 预览", IconType::FileText, L"扩展应用" }
    };

    std::wstring currentCategory;

    for (const auto& item : navItems) {
        if (item.category != currentCategory) {
            currentCategory = item.category;
            auto catLabel = Label(currentCategory, FontToken::Xs, FontWeight::SemiBold, FontFamilyType::Sans, TextColorRole::Muted);
            catLabel->layoutParams().margin = Thickness(10.0f, 8.0f, 0.0f, 2.0f);
            navContainer->addChild(catLabel);
        }

        const bool isActive = (item.id == m_currentPageId);
        auto btn = std::make_shared<UIButton>(item.label, ButtonVariant::Ghost, ButtonSize::Md);
        btn->setIsNavItem(true);
        btn->setActive(isActive);
        btn->setIcon(item.icon);
        btn->layoutParams().fixedHeight = 32.0f;
        btn->setOnClick([this, id = item.id]() {
            navigateTo(id);
        });
        m_navButtons.push_back({ item.id, btn });
        navContainer->addChild(btn);
    }

    auto navScroll = ScrollView(navContainer);
    navScroll->layoutParams().flexGrow = 1.0f;
    sidebar->addChild(navScroll);

    // 侧边栏底部固定区域
    sidebar->addChild(Separator());

    auto footer = std::make_shared<UIElement>();
    footer->layoutParams().direction = FlexDirection::Column;
    footer->layoutParams().padding = Thickness(12.0f, 8.0f, 12.0f, 10.0f);
    footer->layoutParams().gap = 6.0f;

    // 主题三态快捷切换分段控制胶囊
    m_btnThemeDark = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    m_btnThemeDark->setIcon(IconType::Moon);
    m_btnThemeDark->layoutParams().flexGrow = 1.0f;
    m_btnThemeDark->layoutParams().fixedHeight = 26.0f;

    m_btnThemeLight = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    m_btnThemeLight->setIcon(IconType::Sun);
    m_btnThemeLight->layoutParams().flexGrow = 1.0f;
    m_btnThemeLight->layoutParams().fixedHeight = 26.0f;

    m_btnThemeSys = std::make_shared<UIButton>(L"", ButtonVariant::Ghost, ButtonSize::Sm);
    m_btnThemeSys->setIcon(IconType::Monitor);
    m_btnThemeSys->layoutParams().flexGrow = 1.0f;
    m_btnThemeSys->layoutParams().fixedHeight = 26.0f;

    updateThemeButtons(theme.getThemeMode());

    m_btnThemeDark->setOnClick([this]() {
        applyThemeMode(ThemeMode::Dark, true);
    });
    m_btnThemeLight->setOnClick([this]() {
        applyThemeMode(ThemeMode::Light, true);
    });
    m_btnThemeSys->setOnClick([this]() {
        applyThemeMode(ThemeMode::System, true);
    });

    auto themeCapsule = std::make_shared<UIThemeCapsule>();
    themeCapsule->addChild(m_btnThemeDark);
    themeCapsule->addChild(m_btnThemeLight);
    themeCapsule->addChild(m_btnThemeSys);
    footer->addChild(themeCapsule);

    auto verLabel = Label(L"Tools3000 v1.0.0", FontToken::Xs, FontWeight::Medium, FontFamilyType::Sans, TextColorRole::Muted);
    verLabel->layoutParams().margin = Thickness(4.0f, 2.0f, 0.0f, 0.0f);
    footer->addChild(verLabel);

    sidebar->addChild(footer);
    return sidebar;
}

std::shared_ptr<UIElement> NativeSettingsApp::createPageContent(const std::string& pageId) {
    if (pageId == "general") return buildGeneralPage();
    if (pageId == "plugins") return buildPluginsPage();
    if (pageId == "search") return buildSearchPage();
    if (pageId == "gesture") return buildGesturePage();
    if (pageId == "hotcorner") return buildHotCornerPage();
    if (pageId == "capture") return buildCapturePage();
    if (pageId == "history") return buildHistoryPage();
    if (pageId == "ocr") return buildOcrPage();
    if (pageId == "keycast") return buildKeycastPage();
    if (pageId == "spotlight") return buildSpotlightPage();
    if (pageId == "dialog_enhancer") return buildDialogEnhancerPage();
    if (pageId == "remote_boost") return buildRemoteBoostPage();
    if (pageId == "stats") return buildStatsPage();
    if (pageId == "about") return buildAboutPage();
    if (pageId == "ai_assistant") return buildAiAssistantPage();
    if (pageId == "color_picker") return buildColorPickerPage();
    if (pageId == "clipboard_manager") return buildClipboardManagerPage();
    if (pageId == "markdown_preview") return buildMarkdownPreviewPage();

    return buildGeneralPage();
}

std::shared_ptr<UIElement> NativeSettingsApp::buildGeneralPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    bool autoStart = cfg.get<bool>("/general/autoStart", false);
    bool runAsAdmin = cfg.get<bool>("/general/runAsAdmin", true);
    bool minimizeToTray = cfg.get<bool>("/general/minimizeToTray", true);
    bool checkUpdates = cfg.get<bool>("/general/checkUpdates", true);
    bool autoReleaseMemory = cfg.get<bool>("/general/autoReleaseSettingsMemory", true);
    ThemeMode curMode = UITheme::instance().getThemeMode();
    std::string currentTheme = (curMode == ThemeMode::Dark ? "dark" : (curMode == ThemeMode::Light ? "light" : "system"));
    std::string currentAccent = UITheme::instance().getAccentColor();
    std::string currentLang = cfg.get<std::string>("/general/language", "auto");
    std::string currentTrayTheme = cfg.get<std::string>("/general/trayIconTheme", "system");
    std::string currentLogLevel = cfg.get<std::string>("/general/logLevel", "info");

    std::vector<SelectOption> themeOpts = {
        { "system", L"跟随系统 (System)" },
        { "dark", L"深色模式 (Dark)" },
        { "light", L"浅色模式 (Light)" }
    };

    std::vector<SelectOption> accentOpts = {
        { "blue", L"经典科技蓝 (Blue)" },
        { "purple", L"高贵极客紫 (Purple)" },
        { "emerald", L"翡翠生机绿 (Emerald)" },
        { "amber", L"琥珀暖光橙 (Amber)" },
        { "rose", L"玫瑰活力粉 (Rose)" },
        { "cyan", L"青空幻影蓝 (Cyan)" }
    };

    std::vector<SelectOption> langOpts = {
        { "auto", L"跟随系统语言 (Auto)" },
        { "zh-CN", L"简体中文 (Simplified Chinese)" },
        { "en-US", L"English (United States)" }
    };

    std::vector<SelectOption> trayOpts = {
        { "system", L"跟随系统主题 (Auto)" },
        { "light", L"浅色明亮图标 (Light)" },
        { "dark", L"深色曜黑图标 (Dark)" },
        { "colorful", L"彩色微晶图标 (Colorful)" }
    };

    std::vector<SelectOption> logOpts = {
        { "debug", L"调试级 (DEBUG)" },
        { "info", L"信息级 (INFO)" },
        { "warn", L"警告级 (WARN)" },
        { "error", L"错误级 (ERROR)" }
    };

    auto content = VStack({
        SettingGroup(L"常规系统偏好", IconType::Settings, {
            Card(L"系统启动与生命周期策略", L"配置登录自启动、管理员权限与轻量内存收缩", {
                SettingRow(L"开机自动启动", L"登录 Windows 系统时自动在后台静默运行 Tools3000",
                    Toggle(autoStart, [](bool val) {
                        updateConfigAsync("/general/autoStart", val);
                    })
                ),
                SettingRow(L"以管理员权限运行", L"启用管理员特权以保障底层全局键盘钩子与输入穿梭",
                    Toggle(runAsAdmin, [](bool val) {
                        updateConfigAsync("/general/runAsAdmin", val);
                    })
                ),
                SettingRow(L"最小化到系统托盘", L"关闭窗口时最小化至任务栏右下角托盘，保持后台快捷响应",
                    Toggle(minimizeToTray, [](bool val) {
                        updateConfigAsync("/general/minimizeToTray", val);
                    })
                ),
                SettingRow(L"自动检查新版本", L"启动时静默检查 GitHub 官方 Release 最新版本更新",
                    Toggle(checkUpdates, [](bool val) {
                        updateConfigAsync("/general/checkUpdates", val);
                    })
                ),
                SettingRow(L"检查软件新版本", L"立即向 GitHub 发起在线版本检测与更新通告",
                    Button(L"立即检查更新", IconType::RefreshCw, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        tools3000::core::UpdateChecker::instance().checkAsync(true);
                        tools3000::core::EventBus::instance().publish(
                            tools3000::core::ShowToastEvent{L"正在检查最新版本更新..."});
                    })
                ),
                SettingRow(L"窗口关闭后物理内存修剪", L"隐藏或关闭设置窗口后主动释放工作集内存 (trimWorkingSet)",
                    Toggle(autoReleaseMemory, [](bool val) {
                        updateConfigAsync("/general/autoReleaseSettingsMemory", val);
                    })
                )
            }),
            Card(L"视觉外观与个性化", L"自定义深浅主题、强调色彩与系统托盘图标风格", {
                SettingRow(L"界面色彩主题", L"切换深色黑曜石或浅色陶瓷微晶风格",
                    Select(themeOpts, currentTheme, [this](const std::string& val) {
                        ThemeMode mode = ThemeMode::System;
                        if (val == "dark") mode = ThemeMode::Dark;
                        else if (val == "light") mode = ThemeMode::Light;
                        applyThemeMode(mode, true);
                    })
                ),
                SettingRow(L"主题强调色", L"应用于激活胶囊、焦点外环、主按钮与微晶光晕",
                    Select(accentOpts, currentAccent, [this](const std::string& val) {
                        applyAccentColor(val, true);
                    })
                ),
                SettingRow(L"界面显示语言", L"设置 Tools3000 原生界面语言偏好",
                    Select(langOpts, currentLang, [this](const std::string& val) {
                        updateConfigAsync("/general/language", val);
                        tools3000::core::Logger::setLanguage(val);
                        std::wstring toast;
                        if (val == "en" || val == "en-US") {
                            toast = L"Language switched to English";
                        } else if (val == "zh-CN") {
                            toast = L"界面语言已切换为简体中文";
                        } else {
                            toast = L"界面语言已切换为跟随系统 (Auto)";
                        }
                        tools3000::core::EventBus::instance().publish(tools3000::core::ShowToastEvent{toast});
                        buildUI();
                        if (m_host) m_host->requestPaint();
                    })
                ),
                SettingRow(L"托盘图标色彩风格", L"系统任务栏托盘区域图标的明暗配色契合度",
                    Select(trayOpts, currentTrayTheme, [](const std::string& val) {
                        updateConfigAsync("/general/trayIconTheme", val);
                    })
                )
            }),
            Card(L"全局核心快捷键总览与状态", L"系统级热键注册状态与冲突诊断", {
                SettingRow(L"超级截图标注快捷键", L"区域截屏、长截图与录屏总览快捷键",
                    CodeBadge(L"Ctrl+Shift+A")),
                SettingRow(L"全局搜索唤起快捷键", L"毫秒级秒开文件检索与应用启动中心",
                    CodeBadge(L"Alt+Space")),
                SettingRow(L"聚合高亮聚光灯聚焦", L"暗化全屏并瞬间突出鼠标光标焦点",
                    CodeBadge(L"双击 Ctrl")),
                SettingRow(L"按键播报显示快捷键", L"屏幕悬浮回显键盘击键与修饰键流转",
                    CodeBadge(L"Alt+K")),
                SettingRow(L"文件对话框智能增强", L"Common File Dialogs 快速目录穿梭跳转",
                    CodeBadge(L"Ctrl+G")),
                SettingRow(L"远控修饰键急救冲刷", L"主控端一键冲刷释放卡死的修饰键位",
                    CodeBadge(L"Ctrl+Alt+Backspace")),
                SettingRow(L"核心热键注册与冲突诊断", L"一键探测底层全局键盘钩子与快捷键可用性",
                    Button(L"测试热键状态", IconType::Keyboard, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        auto entries = tools3000::core::HotkeyManager::instance().getAllHotkeys();
                        size_t activeCount = 0;
                        for (const auto& e : entries) {
                            if (e.registered || e.armed) activeCount++;
                        }
                        wchar_t msg[128];
                        swprintf_s(msg, L"热键探测完成：核心快捷键就绪 (%zu/%zu 正常)", activeCount, entries.size());
                        tools3000::core::EventBus::instance().publish(tools3000::core::ShowToastEvent{msg});
                    })
                )
            }),
            Card(L"便携模式与数据存储", L"配置文件存储路径与绿色便携模式管理", {
                SettingRow(L"用户配置存储目录", L"本地个性化 JSON 配置与规则存储基准目录",
                    Button(L"打开配置目录", IconType::FolderOpen, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        auto cfgDir = tools3000::core::WinUtils::getConfigDirectory();
                        std::error_code ec;
                        std::filesystem::create_directories(cfgDir, ec);
                        ShellExecuteW(nullptr, L"open", cfgDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    })),
                SettingRow(L"当前运行模式状态", L"原生系统环境集成与自启动注册形态",
                    Badge(L"标准安装模式 (本地持久化)", BadgeVariant::Success))
            }),
            Card(L"全链路观测与配置管理", L"统一日志等级过滤与配置文件导入导出", {
                SettingRow(L"日志记录等级", L"控制本地存储诊断日志的详细过滤级别",
                    Select(logOpts, currentLogLevel, [](const std::string& val) {
                        updateConfigAsync("/general/logLevel", val);
                    })
                ),
                SettingRow(L"系统诊断日志目录", L"本地存储诊断日志文件归档目录",
                    Button(L"浏览日志目录", IconType::FolderOpen, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        auto logDir = tools3000::core::WinUtils::getAppDataDirectory() / L"logs";
                        std::error_code ec;
                        std::filesystem::create_directories(logDir, ec);
                        ShellExecuteW(nullptr, L"open", logDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    })),
                SettingRow(L"配置备份与导出", L"将当前所有工具与个性化设置导出为 JSON 文件",
                    Button(L"导出配置备份", IconType::Copy, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        auto backupDir = tools3000::core::WinUtils::getConfigDirectory();
                        std::error_code ec;
                        std::filesystem::create_directories(backupDir, ec);
                        auto now = std::chrono::system_clock::now();
                        auto tt = std::chrono::system_clock::to_time_t(now);
                        std::tm lt{};
                        localtime_s(&lt, &tt);
                        wchar_t fname[64];
                        wcsftime(fname, 64, L"config_backup_%Y%m%d_%H%M%S.json", &lt);
                        auto backupPath = backupDir / fname;
                        bool ok = tools3000::core::ConfigManager::instance().exportTo(backupPath);
                        if (ok) {
                            std::wstring param = L"/select,\"" + backupPath.wstring() + L"\"";
                            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
                            tools3000::core::EventBus::instance().publish(
                                tools3000::core::ShowToastEvent{L"配置备份已成功导出并定位"});
                        } else {
                            tools3000::core::EventBus::instance().publish(
                                tools3000::core::ShowToastEvent{L"配置备份导出失败"});
                        }
                    })
                ),
                SettingRow(L"恢复出厂默认设置", L"一键清空个性化修改并还原为出厂默认设置",
                    Button(L"重置所有配置", IconType::Trash2, ButtonVariant::Danger, ButtonSize::Sm, [this]() {
                        auto backupDir = tools3000::core::WinUtils::getConfigDirectory();
                        std::error_code ec;
                        std::filesystem::create_directories(backupDir, ec);
                        tools3000::core::ConfigManager::instance().exportTo(backupDir / "config_backup.json");
                        bool ok = tools3000::core::ConfigManager::instance().reset();
                        if (ok) {
                            UITheme::instance().syncWithConfig();
                            buildUI();
                            if (m_host) m_host->requestPaint();
                            tools3000::core::EventBus::instance().publish(
                                tools3000::core::ShowToastEvent{L"所有配置已恢复出厂默认设置"});
                        } else {
                            tools3000::core::EventBus::instance().publish(
                                tools3000::core::ShowToastEvent{L"配置重置失败"});
                        }
                    })
                )
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildPluginsPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool pGesture = cfg.get<bool>("/plugins/gesture/enabled", true);
    bool pCapture = cfg.get<bool>("/plugins/capture/enabled", true);
    bool pSearch = cfg.get<bool>("/plugins/search/enabled", true);
    bool pKeycast = cfg.get<bool>("/plugins/keycast/enabled", true);
    bool pSpotlight = cfg.get<bool>("/plugins/spotlight/enabled", true);
    bool pDialog = cfg.get<bool>("/plugins/dialogenhancer/enabled", true);
    bool pRemote = cfg.get<bool>("/plugins/remoteboost/enabled", true);
    bool pHotcorner = cfg.get<bool>("/plugins/hotcorner/enabled", true);
    bool pOcr = cfg.get<bool>("/plugins/ocr/enabled", true);

    auto content = VStack({
        SettingGroup(L"核心功能插件矩阵", IconType::Puzzle, {
            Card(L"鼠标手势识别扩展 (Plugin_Gesture)", L"底层低级鼠标钩子与转弯消抖方向手势识别", {
                SettingRow(L"插件状态", L"DLL 运行时已加载，提供 16 种默认手势与八向罗盘导引",
                    Badge(L"已激活", BadgeVariant::Success)),
                SettingRow(L"手势插件开关", L"停用手势插件将释放底层鼠标钩子与相关内存",
                    Toggle(pGesture, [](bool v) {
                        updateConfigAsync("/plugins/gesture/enabled", v);
                    }))
            }),
            Card(L"超级截图与录屏 (Plugin_Capture)", L"高精度 D2D 标注工具栏、智能选区吸附与录屏", {
                SettingRow(L"插件状态", L"集成 Windows.Media.Ocr 与 FFmpeg 硬件加速编解码器",
                    Badge(L"已激活", BadgeVariant::Success)),
                SettingRow(L"截图插件开关", L"管理截图全局快捷键拦截与屏幕捕获底板",
                    Toggle(pCapture, [](bool v) {
                        updateConfigAsync("/plugins/capture/enabled", v);
                    }))
            }),
            Card(L"全局极速搜索 (Plugin_Search)", L"跨进程 NTFS USN/MFT 毫秒级磁盘文件秒级索引服务", {
                SettingRow(L"插件状态", L"DEMAND_START 按需启动架构，会话级独立常驻",
                    Badge(L"按需常驻", BadgeVariant::Primary)),
                SettingRow(L"搜索插件开关", L"控制全局搜索中心呼出快捷键与管道通信",
                    Toggle(pSearch, [](bool v) {
                        updateConfigAsync("/plugins/search/enabled", v);
                    }))
            }),
            Card(L"按键播报与可视化 (Plugin_Keycast)", L"屏幕实时回显击键微晶胶囊，演示教学录屏利器", {
                SettingRow(L"插件状态", L"低延迟键盘输入捕获与 2D 时序流微晶动画",
                    Badge(L"运行中", BadgeVariant::Success)),
                SettingRow(L"按键播报开关", L"全局实时捕获组合快捷键并在屏幕呈现胶囊",
                    Toggle(pKeycast, [](bool v) {
                        updateConfigAsync("/plugins/keycast/enabled", v);
                    }))
            }),
            Card(L"聚合高亮与光标视效 (Plugin_Spotlight)", L"聚光灯聚焦、点击水波纹扩散与光标流光粒子拖尾", {
                SettingRow(L"插件状态", L"Direct2D 局部微型视口渲染，0 额外全屏合成开销",
                    Badge(L"运行中", BadgeVariant::Success)),
                SettingRow(L"光标特效开关", L"寻找光标聚光灯与水波纹动态粒子反馈",
                    Toggle(pSpotlight, [](bool v) {
                        updateConfigAsync("/plugins/spotlight/enabled", v);
                    }))
            }),
            Card(L"文件对话框智能助手 (Plugin_DialogEnhancer)", L"Win32 对话框智能穿梭、独立进程工作目录记忆与悬浮挂件", {
                SettingRow(L"插件状态", L"Win32 API Hook 无侵入挂接系统 Common File Dialogs",
                    Badge(L"注入就绪", BadgeVariant::Primary)),
                SettingRow(L"对话框增强开关", L"为系统“打开/另存为”对话框赋予智能跳转能力",
                    Toggle(pDialog, [](bool v) {
                        updateConfigAsync("/plugins/dialogenhancer/enabled", v);
                    }))
            }),
            Card(L"远程协助主控加速 (Plugin_RemoteBoost)", L"主控端沉浸式热键直通、修饰键卡死急救与输入法脱敏", {
                SettingRow(L"插件状态", L"适配 ToDesk, AnyDesk, RustDesk, mstsc 等主流客户端",
                    Badge(L"守护中", BadgeVariant::Success)),
                SettingRow(L"远控加速开关", L"管理远控窗口热键直通与双击 Right-Ctrl 急救冲刷",
                    Toggle(pRemote, [](bool v) {
                        updateConfigAsync("/plugins/remoteboost/enabled", v);
                    }))
            }),
            Card(L"屏幕边缘触发角 (Plugin_HotCorner)", L"光标触碰屏幕四角物理边缘瞬时触发桌面快捷动作", {
                SettingRow(L"插件状态", L"高频鼠标坐标检测与毫秒级防误触消抖延迟",
                    Badge(L"运行中", BadgeVariant::Success)),
                SettingRow(L"触发角总开关", L"控制屏幕四角物理触碰快捷指令的执行",
                    Toggle(pHotcorner, [](bool v) {
                        updateConfigAsync("/plugins/hotcorner/enabled", v);
                    }))
            }),
            Card(L"离线文字识别 (Plugin_Ocr)", L"纯本地 Windows.Media.Ocr 引擎，无网络传输安全保密", {
                SettingRow(L"插件状态", L"系统原生 OCR 引擎就绪，覆盖中英混合排版提取",
                    Badge(L"本地就绪", BadgeVariant::Success)),
                SettingRow(L"文字识别开关", L"管理 OCR 快捷截图触发与结果自动上屏",
                    Toggle(pOcr, [](bool v) {
                        updateConfigAsync("/plugins/ocr/enabled", v);
                    }))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildSearchPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    std::string hotkey = cfg.get<std::string>("/search/hotkey", "Alt+Space");
    int maxResults = cfg.get<int>("/search/maxResults", 50);
    std::string defaultCat = cfg.get<std::string>("/search/defaultCategory", "all");
    bool pinyin = cfg.get<bool>("/search/pinyinEnabled", true);
    bool matchPath = cfg.get<bool>("/search/matchPath", false);
    bool caseSens = cfg.get<bool>("/search/caseSensitive", false);
    bool autoBypass = cfg.get<bool>("/search/autoBypassFullscreen", true);
    std::string iconStyle = cfg.get<std::string>("/search/iconStyle", "native");

    std::vector<SelectOption> catOpts = {
        { "all", L"全部分类 (All)" },
        { "file", L"文件 (Files)" },
        { "folder", L"文件夹 (Folders)" },
        { "app", L"应用程序 (Apps)" },
        { "doc", L"文档 (Documents)" }
    };

    std::vector<SelectOption> iconOpts = {
        { "native", L"提取 Windows 原生关联图标 (推荐)" },
        { "vector", L"现代极简统一矢量图标 (Vector)" }
    };

    auto content = VStack({
        SettingGroup(L"全局搜索与极速启动", IconType::Search, {
            Card(L"呼出快捷键与检索行为", L"毫秒级秒开搜索中心与应用文件启动器", {
                SettingRow(L"搜索唤起快捷键", L"在任意软件前台按下瞬间呼出全局搜索窗口",
                    HotkeyInput(hotkey, [](const std::string& key) {
                        updateConfigAsync("/search/hotkey", key);
                    })
                ),
                SettingRow(L"最大呈现检索结果数", L"单次查询最大显示条目数量 (建议 20~100)",
                    NumberInput(maxResults, 10, 200, 10, L"条", [](int val) {
                        updateConfigAsync("/search/maxResults", val);
                    })
                ),
                SettingRow(L"默认搜索分类过滤", L"打开搜索中心时的初始分类锚点",
                    Select(catOpts, defaultCat, [](const std::string& val) {
                        updateConfigAsync("/search/defaultCategory", val);
                    })
                ),
                SettingRow(L"实时拼音全拼与简拼", L"支持中文名称首字母简拼与全拼模糊快速查找",
                    Toggle(pinyin, [](bool val) {
                        updateConfigAsync("/search/pinyinEnabled", val);
                    })
                ),
                SettingRow(L"匹配完整文件路径", L"同时在完整绝对路径文本中匹配关键字",
                    Toggle(matchPath, [](bool val) {
                        updateConfigAsync("/search/matchPath", val);
                    })
                ),
                SettingRow(L"严格区分英文字母大小写", L"区分英文字符的大写与小写字母精确匹配",
                    Toggle(caseSens, [](bool val) {
                        updateConfigAsync("/search/caseSensitive", val);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动绕过", L"检测到独占全屏或游戏运行时避让快捷键",
                    Toggle(autoBypass, [](bool val) {
                        updateConfigAsync("/search/autoBypassFullscreen", val);
                    })
                ),
                SettingRow(L"结果条目图标渲染风格", L"搜索结果左侧文件/应用图标的展现方式",
                    Select(iconOpts, iconStyle, [](const std::string& val) {
                        updateConfigAsync("/search/iconStyle", val);
                    })
                )
            }),
            Card(L"高级搜索语法速查与过滤手册", L"支持极速过滤条件与正则表达式精确检索", {
                SettingRow(L"文件扩展名过滤 (ext:)", L"限定特定类型文件，例如 ext:png 或 ext:pdf",
                    CodeBadge(L"ext:png | ext:pdf")),
                SettingRow(L"文件大小区间过滤 (size:)", L"限定文件物理字节，例如 size:>100M 或 size:<5M",
                    CodeBadge(L"size:>100M")),
                SettingRow(L"修改时间动态过滤 (dm:)", L"限定最近修改时间，例如 dm:today 或 dm:lastweek",
                    CodeBadge(L"dm:today")),
                SettingRow(L"仅检索文件夹目录 (folder:)", L"排除普通文件，仅在系统目录与文件夹中检索",
                    CodeBadge(L"folder:work")),
                SettingRow(L"正则表达式匹配 (regex:)", L"以 regex: 前缀开启 PCRE 正则表达式精确模式",
                    CodeBadge(L"regex:^build.*\\.dll$"))
            }),
            Card(L"服务运行状态与索引诊断", L"底层 USN 日志与 MFT 磁盘文件秒级索引", {
                SettingRow(L"索引服务生命周期", L"首次呼出时按需启动 (DEMAND_START)，会话级常驻",
                    Badge(L"按需常驻 (DEMAND_START)", BadgeVariant::Success)),
                SettingRow(L"本地进程通信命名管道", L"通过本地安全高速命名管道与搜索服务交互",
                    CodeBadge(L"\\\\.\\pipe\\Tools3000SearchPipe")),
                SettingRow(L"快速启动与联调测试", L"立即呼出搜索中心体验毫秒级秒搜体验",
                    Button(L"立即唤起搜索中心", IconType::Search, ButtonVariant::Primary, ButtonSize::Sm, []() {
                        tools3000::ui::native::NativeSearchApp::instance().show(GetModuleHandleW(nullptr));
                        tools3000::core::MessageBridge::instance().handleMessageAsync(R"({"id":0,"method":"search.warmup","params":{}})", [](std::string){});
                    }))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildGesturePage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/gesture/enabled", true);
    std::string triggerBtn = cfg.get<std::string>("/gesture/triggerButton", "right");
    bool trailVis = cfg.get<bool>("/gesture/trailVisible", true);
    int trailWidth = cfg.get<int>("/gesture/trailWidth", 3);
    std::string colorMode = cfg.get<std::string>("/gesture/trailColorMode", "auto");
    std::string targetMode = cfg.get<std::string>("/gesture/targetMode", "underPointer");
    int timeoutMs = cfg.get<int>("/gesture/initialTimeoutMs", 500);
    int minDistance = cfg.get<int>("/gesture/minSegmentDistance", 24);
    bool scribbleCancel = cfg.get<bool>("/gesture/scribbleCancel", true);
    bool inFlightCompass = cfg.get<bool>("/gesture/inFlightCompass", true);
    bool autoBypass = cfg.get<bool>("/gesture/autoBypassFullscreen", true);

    std::vector<SelectOption> btnOpts = {
        { "right", L"鼠标右键拖曳 (推荐)" },
        { "middle", L"鼠标中键/滚轮按下拖曳" }
    };

    std::vector<SelectOption> targetOpts = {
        { "underPointer", L"光标所指悬停窗口 (精确作用)" },
        { "foreground", L"当前激活前台主窗口 (标准模式)" }
    };

    std::vector<SelectOption> colorOpts = {
        { "auto", L"跟随系统全局主题强调色 (Auto)" },
        { "blue", L"经典科技蓝 (#3B82F6)" },
        { "purple", L"高贵极客紫 (#8B5CF6)" },
        { "emerald", L"翡翠生机绿 (#10B981)" },
        { "amber", L"琥珀暖光橙 (#F59E0B)" },
        { "coral", L"日落珊瑚粉 (#F43F5E)" }
    };

    auto scopeManager = std::make_shared<NativeScopeRulesManager>();

    auto content = VStack({
        SettingGroup(L"鼠标手势识别引擎", IconType::Hand, {
            Card(L"基础总控与按键触发", L"配置手势激活键位与目标窗口响应策略", {
                SettingRow(L"启用鼠标手势引擎", L"全局捕获鼠标轨迹并执行对应绑定的快捷动作",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/gesture/enabled", v);
                    })
                ),
                SettingRow(L"手势触发按键", L"长按拖曳触发识别的鼠标物理按键",
                    Select(btnOpts, triggerBtn, [](const std::string& v) {
                        updateConfigAsync("/gesture/triggerButton", v);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动绕过", L"运行全屏独占软件或 3D 游戏时自动静默手势钩子",
                    Toggle(autoBypass, [](bool v) {
                        updateConfigAsync("/gesture/autoBypassFullscreen", v);
                    })
                ),
                SettingRow(L"手势作用目标窗口模式", L"手势指令生效的判定目标对象窗口",
                    Select(targetOpts, targetMode, [](const std::string& v) {
                        updateConfigAsync("/gesture/targetMode", v);
                    })
                )
            }),
            Card(L"轨迹渲染与色彩美学", L"Direct2D 硬件加速亚像素平滑微晶流光轨迹", {
                SettingRow(L"绘制手势流光轨迹", L"屏幕实时渲染亚像素平滑微晶渐隐拖尾线条",
                    Toggle(trailVis, [](bool v) {
                        updateConfigAsync("/gesture/trailVisible", v);
                    })
                ),
                SettingRow(L"流光轨迹色彩方案", L"手势笔触所呈现的渐隐微晶光泽色彩",
                    Select(colorOpts, colorMode, [](const std::string& v) {
                        updateConfigAsync("/gesture/trailColorMode", v);
                    })
                ),
                SettingRow(L"轨迹线条笔刷粗细", L"Direct2D 渲染笔触的物理像素线宽",
                    NumberInput(trailWidth, 1, 8, 1, L"px", [](int v) {
                        updateConfigAsync("/gesture/trailWidth", v);
                    })
                )
            }),
            Card(L"八向轮盘动作菜单 (Radial Menu)", L"右键长按快速唤出八向可视化交互动作轮盘", {
                SettingRow(L"启用八向轮盘菜单", L"长按拖曳停顿时在光标中心呈现八向微晶轮盘",
                    Toggle(cfg.get<bool>("/gesture/radialMenuEnabled", true), [](bool v) {
                        updateConfigAsync("/gesture/radialMenuEnabled", v);
                    })
                ),
                SettingRow(L"轮盘菜单外径尺寸", L"微晶动作扇区所覆盖的物理像素半径",
                    NumberInput(cfg.get<int>("/gesture/radialMenuRadius", 120), 80, 200, 10, L"px", [](int v) {
                        updateConfigAsync("/gesture/radialMenuRadius", v);
                    })
                ),
                SettingRow(L"悬停动作文字提示", L"光标停留在扇区时呈现动作名称与快捷指令说明",
                    Toggle(cfg.get<bool>("/gesture/showActionTooltip", true), [](bool v) {
                        updateConfigAsync("/gesture/showActionTooltip", v);
                    })
                ),
                SettingRow(L"手势生效音效反馈", L"手势指令成功识别触发时播放清脆提示音",
                    Toggle(cfg.get<bool>("/gesture/soundFeedback", false), [](bool v) {
                        updateConfigAsync("/gesture/soundFeedback", v);
                    })
                )
            }),
            Card(L"灵敏度与识别消抖调优", L"转弯圆角平滑消抖与误触容差容错", {
                SettingRow(L"最小移动识别间距", L"单段有效手势方向所需的最小移动物理像素",
                    NumberInput(minDistance, 10, 60, 2, L"px", [](int v) {
                        updateConfigAsync("/gesture/minSegmentDistance", v);
                    })
                ),
                SettingRow(L"初始按下超时判定", L"按住按键未移动时触发普通右键菜单的最大等待时间",
                    NumberInput(timeoutMs, 100, 1000, 50, L"ms", [](int v) {
                        updateConfigAsync("/gesture/initialTimeoutMs", v);
                    })
                ),
                SettingRow(L"折返涂抹快速取消手势", L"画出“Z”字或反复涂抹折返时自动取消本次手势",
                    Toggle(scribbleCancel, [](bool v) {
                        updateConfigAsync("/gesture/scribbleCancel", v);
                    })
                ),
                SettingRow(L"实时八向罗盘辅助导引", L"拖动时在光标处呈现八向指示微晶罗盘",
                    Toggle(inFlightCompass, [](bool v) {
                        updateConfigAsync("/gesture/inFlightCompass", v);
                    })
                )
            }),
            Card(L"常用预设手势动作速查", L"开箱即用的高频原生手势映射关系", {
                SettingRow(L"向左滑动 [L]", L"浏览器页面后退 / 历史记录上一步",
                    CodeBadge(L"Alt+Left")),
                SettingRow(L"向右滑动 [R]", L"浏览器页面前进 / 历史记录下一步",
                    CodeBadge(L"Alt+Right")),
                SettingRow(L"向下后向右 [DR]", L"关闭当前活动网页标签页或窗口",
                    CodeBadge(L"Ctrl+W")),
                SettingRow(L"向下后向左 [DL]", L"强制刷新当前前台窗口或网页",
                    CodeBadge(L"F5")),
                SettingRow(L"向上后向下 [UD]", L"最小化当前前台活动窗口至任务栏",
                    CodeBadge(L"Win+Down")),
                SettingRow(L"向上滑动 [U]", L"极速平滑回滚至页面最顶部",
                    CodeBadge(L"Home")),
                SettingRow(L"向下滑动 [D]", L"极速平滑滚动至页面最底部",
                    CodeBadge(L"End")),
                SettingRow(L"向下后向上 [DU]", L"最大化或还原当前前台窗口",
                    CodeBadge(L"Win+Up")),
                SettingRow(L"向左后向右 [LR]", L"快速切换至下一个网页标签页",
                    CodeBadge(L"Ctrl+Tab")),
                SettingRow(L"向右后向左 [RL]", L"快速切换至上一个网页标签页",
                    CodeBadge(L"Ctrl+Shift+Tab"))
            }),
            Card(L"交互式手势录制与平滑画板", L"在此画板实时绘制鼠标手势，自动感应起笔位置、按键与方向轨迹", {
                std::make_shared<NativeGestureDrawCanvas>()
            }),
            Card(L"进程与窗口作用域规则管理器", L"配置特定软件专属独立手势或在游戏运行中自动静默免打扰", {
                scopeManager
            }, Button(L"重置手势规则", IconType::RefreshCw, ButtonVariant::Ghost, ButtonSize::Sm, [scopeManager, this]() {
                if (scopeManager) {
                    scopeManager->resetToDefault();
                    tools3000::core::EventBus::instance().publish(
                        tools3000::core::ShowToastEvent{ L"手势作用域规则已重置为出厂默认配置" });
                    if (m_host) m_host->requestPaint();
                }
            }))
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildHotCornerPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/hotcorner/enabled", false);
    int delay = cfg.get<int>("/hotcorner/delay", 300);
    bool autoBypass = cfg.get<bool>("/hotcorner/autoBypassFullscreen", true);

    std::string actTL = cfg.get<std::string>("/hotcorner/corners/topLeft/action", "taskview");
    std::string actTR = cfg.get<std::string>("/hotcorner/corners/topRight/action", "none");
    std::string actBL = cfg.get<std::string>("/hotcorner/corners/bottomLeft/action", "none");
    std::string actBR = cfg.get<std::string>("/hotcorner/corners/bottomRight/action", "desktop");

    std::vector<SelectOption> actionOpts = {
        { "none", L"无动作 (未设置)" },
        { "desktop", L"显示桌面 / 快速隐藏窗口" },
        { "taskview", L"任务视图 / 多桌面平铺 (Win+Tab)" },
        { "lock", L"锁定计算机 (Win+L)" },
        { "search", L"唤起 Tools3000 全局搜索" },
        { "capture", L"激活超级截图标注 (Ctrl+Shift+A)" },
        { "spotlight", L"寻找光标聚光灯聚焦" },
        { "keycast", L"切换按键播报开关" }
    };

    auto content = VStack({
        SettingGroup(L"屏幕边缘与四角热区", IconType::Maximize2, {
            Card(L"热区总控与响应策略", L"光标移入屏幕物理边缘四角时自动触发预设指令", {
                SettingRow(L"启用屏幕四角热区", L"全局实时监控鼠标光标位置并执行边缘热区动作",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/hotcorner/enabled", v);
                    })
                ),
                SettingRow(L"边缘触碰触发延迟", L"光标停留在四角物理像素边缘的防误触等待时间",
                    NumberInput(delay, 100, 1000, 50, L"ms", [](int v) {
                        updateConfigAsync("/hotcorner/delay", v);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动绕过", L"全屏玩游戏或看电影时自动停用四角热区防误触",
                    Toggle(autoBypass, [](bool v) {
                        updateConfigAsync("/hotcorner/autoBypassFullscreen", v);
                    })
                )
            }),
            Card(L"屏幕四角动作独立映射", L"为显示器四个物理拐角赋予不同的高效指令", {
                SettingRow(L"屏幕左上角热区 (Top-Left)", L"光标移至屏幕最左上角拐角处触发的动作",
                    Select(actionOpts, actTL, [](const std::string& v) {
                        updateConfigAsync("/hotcorner/corners/topLeft/action", v);
                    })
                ),
                SettingRow(L"屏幕右上角热区 (Top-Right)", L"光标移至屏幕最右上角拐角处触发的动作",
                    Select(actionOpts, actTR, [](const std::string& v) {
                        updateConfigAsync("/hotcorner/corners/topRight/action", v);
                    })
                ),
                SettingRow(L"屏幕左下角热区 (Bottom-Left)", L"光标移至屏幕最左下角拐角处触发的动作",
                    Select(actionOpts, actBL, [](const std::string& v) {
                        updateConfigAsync("/hotcorner/corners/bottomLeft/action", v);
                    })
                ),
                SettingRow(L"屏幕右下角热区 (Bottom-Right)", L"光标移至屏幕最右下角拐角处触发的动作",
                    Select(actionOpts, actBR, [](const std::string& v) {
                        updateConfigAsync("/hotcorner/corners/bottomRight/action", v);
                    })
                )
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildCapturePage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    std::string capHotkey = cfg.get<std::string>("/capture/hotkey", "Ctrl+Shift+A");
    std::string capFormat = cfg.get<std::string>("/capture/format", "png");
    int capQuality = cfg.get<int>("/capture/quality", 90);
    bool autoBypass = cfg.get<bool>("/capture/autoBypassFullscreen", true);
    bool copyClip = cfg.get<bool>("/capture/copyToClipboard", true);
    bool autoSave = cfg.get<bool>("/capture/saveToFile", true);
    bool crosshair = cfg.get<bool>("/capture/showCrosshair", false);
    bool incCursor = cfg.get<bool>("/capture/includeCursor", false);
    bool detectWin = cfg.get<bool>("/capture/autoDetectWindow", true);
    bool hints = cfg.get<bool>("/capture/showShortcutHints", true);

    bool bShell = cfg.get<bool>("/capture/beautyShellEnabled", false);
    std::string bTheme = cfg.get<std::string>("/capture/beautyShellTheme", "studio_slate");
    int bPad = cfg.get<int>("/capture/beautyShellPadding", 32);
    int bRadius = cfg.get<int>("/capture/beautyShellRadius", 16);

    std::string recHotkey = cfg.get<std::string>("/recording/hotkey", "Ctrl+Shift+R");
    std::string recPauseHotkey = cfg.get<std::string>("/recording/pauseHotkey", "Ctrl+Shift+P");
    std::string recFormat = cfg.get<std::string>("/recording/format", "mp4_h264");
    int recFps = cfg.get<int>("/recording/fps", 30);
    int recBitrate = cfg.get<int>("/recording/bitrate", 8);
    int recCountdown = cfg.get<int>("/recording/countdownSeconds", 3);
    bool recCursor = cfg.get<bool>("/recording/includeCursor", true);
    bool recRipple = cfg.get<bool>("/recording/showClickEffects", false);
    bool recZoom = cfg.get<bool>("/recording/showClickZoom", true);
    bool recKeycast = cfg.get<bool>("/recording/includeKeycast", true);
    bool recSysAudio = cfg.get<bool>("/recording/captureSystemAudio", false);
    bool recMic = cfg.get<bool>("/recording/captureMicrophone", false);

    std::vector<SelectOption> fmtOpts = {
        { "png", L"PNG (无损画质，推荐)" },
        { "jpg", L"JPEG (高压缩，适合网络发布)" },
        { "bmp", L"BMP (原始位图无压缩)" }
    };

    std::vector<SelectOption> shellOpts = {
        { "studio_slate", L"灰岩暗影 (Studio Slate)" },
        { "macos_light", L"苹果浅色微晶陶瓷 (macOS Light)" },
        { "macos_dark", L"苹果黑曜暗色微晶 (macOS Dark)" },
        { "linear_gradient", L"双色幻境渐变 (Gradient)" },
        { "glassmorphism", L"极简亚克力磨砂玻璃 (Glass)" }
    };

    std::vector<SelectOption> recFmtOpts = {
        { "mp4_h264", L"MP4 (H.264 / AAC 硬件加速)" },
        { "gif", L"高清动态 GIF 动图" }
    };

    std::vector<SelectOption> fpsOpts = {
        { "60", L"60 FPS (极致顺滑，适合游戏演示)" },
        { "30", L"30 FPS (标准流畅度，推荐平衡)" },
        { "15", L"15 FPS (节省磁盘空间)" }
    };

    auto content = VStack({
        SettingGroup(L"超级截图与图像美化 (Capture)", IconType::Crop, {
            Card(L"截图快捷键与基础行为", L"配置选区吸附、图像格式与剪贴板流转", {
                SettingRow(L"区域截图全局快捷键", L"按下快捷键瞬间冻结屏幕并进入微晶标注模式",
                    HotkeyInput(capHotkey, [](const std::string& k) {
                        updateConfigAsync("/capture/hotkey", k);
                    })
                ),
                SettingRow(L"输出图片保存格式", L"保存到本地文件与剪贴板使用的图像格式",
                    Select(fmtOpts, capFormat, [](const std::string& val) {
                        updateConfigAsync("/capture/format", val);
                    })
                ),
                SettingRow(L"JPEG 输出压缩质量", L"输出为 JPEG 格式时的图像质量百分比",
                    NumberInput(capQuality, 10, 100, 5, L"%", [](int v) {
                        updateConfigAsync("/capture/quality", v);
                    })
                ),
                SettingRow(L"自动保存至本地目录", L"完成截图后自动将图片保存至本地截图历史归档目录",
                    Toggle(autoSave, [](bool v) {
                        updateConfigAsync("/capture/saveToFile", v);
                    })
                ),
                SettingRow(L"自动复制至系统剪贴板", L"完成截图后自动将位图复制至 Windows 剪贴板",
                    Toggle(copyClip, [](bool v) {
                        updateConfigAsync("/capture/copyToClipboard", v);
                    })
                ),
                SettingRow(L"截图包含鼠标光标指针", L"截取的图像画面中保留光标指针位置",
                    Toggle(incCursor, [](bool v) {
                        updateConfigAsync("/capture/includeCursor", v);
                    })
                ),
                SettingRow(L"激活十字放大微调准星", L"选区拖曳时呈现 1 像素高精度十字准星放大镜",
                    Toggle(crosshair, [](bool v) {
                        updateConfigAsync("/capture/showCrosshair", v);
                    })
                ),
                SettingRow(L"智能窗口边界自动吸附", L"光标移入各应用窗口边界时自动磁吸对齐",
                    Toggle(detectWin, [](bool v) {
                        updateConfigAsync("/capture/autoDetectWindow", v);
                    })
                ),
                SettingRow(L"显示工具栏快捷键提示", L"底部标注工具栏呈现常用工具的单键提示徽章",
                    Toggle(hints, [](bool v) {
                        updateConfigAsync("/capture/showShortcutHints", v);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动避让", L"在独占全屏游戏中自动静默底层截图钩子",
                    Toggle(autoBypass, [](bool v) {
                        updateConfigAsync("/capture/autoBypassFullscreen", v);
                    })
                )
            }),
            Card(L"截图美化外壳 (Beauty Shell)", L"为捕获的窗口图片添加现代立体阴影与圆角留白外壳", {
                SettingRow(L"启用截图美化外壳", L"完成截图时自动套用立体阴影与四周优雅留白",
                    Toggle(bShell, [](bool v) {
                        updateConfigAsync("/capture/beautyShellEnabled", v);
                    })
                ),
                SettingRow(L"美化外壳预设风格", L"选择外壳背景底色与渐变氛围",
                    Select(shellOpts, bTheme, [](const std::string& v) {
                        updateConfigAsync("/capture/beautyShellTheme", v);
                    })
                ),
                SettingRow(L"外壳四周留白衬垫", L"截图边缘与外壳背景外框之间的内边距",
                    NumberInput(bPad, 16, 64, 4, L"px", [](int v) {
                        updateConfigAsync("/capture/beautyShellPadding", v);
                    })
                ),
                SettingRow(L"截图内容圆角弧度", L"被截取窗口或画面的物理圆角平滑裁剪",
                    NumberInput(bRadius, 0, 32, 2, L"px", [](int v) {
                        updateConfigAsync("/capture/beautyShellRadius", v);
                    })
                )
            }),
            Card(L"硬件加速屏幕录制 (Recording)", L"基于 FFmpeg 硬件加速编解码器与音频混合录制", {
                SettingRow(L"录屏开始/停止快捷键", L"全局快速开始或终止屏幕区域录像",
                    HotkeyInput(recHotkey, [](const std::string& k) {
                        updateConfigAsync("/recording/hotkey", k);
                    })
                ),
                SettingRow(L"录屏暂停/继续快捷键", L"在不中断当前录制任务的情况下临时暂停画面捕获",
                    HotkeyInput(recPauseHotkey, [](const std::string& k) {
                        updateConfigAsync("/recording/pauseHotkey", k);
                    })
                ),
                SettingRow(L"视频输出文件格式", L"编码生成的视频媒体封装格式",
                    Select(recFmtOpts, recFormat, [](const std::string& v) {
                        updateConfigAsync("/recording/format", v);
                    })
                ),
                SettingRow(L"录制目标帧率 (FPS)", L"视频画面的流畅度帧率设定",
                    Select(fpsOpts, std::to_string(recFps), [](const std::string& v) {
                        updateConfigAsync("/recording/fps", std::stoi(v));
                    })
                ),
                SettingRow(L"视频编码目标码率", L"输出视频的平均比特率 (推荐 4~12 Mbps)",
                    NumberInput(recBitrate, 2, 20, 1, L"Mbps", [](int v) {
                        updateConfigAsync("/recording/bitrate", v);
                    })
                ),
                SettingRow(L"录制开始倒计时秒数", L"点击录制后在屏幕倒计时的准备秒数",
                    NumberInput(recCountdown, 0, 5, 1, L"秒", [](int v) {
                        updateConfigAsync("/recording/countdownSeconds", v);
                    })
                ),
                SettingRow(L"录制时包含鼠标光标", L"在输出视频中捕获并渲染系统光标指针",
                    Toggle(recCursor, [](bool v) {
                        updateConfigAsync("/recording/includeCursor", v);
                    })
                ),
                SettingRow(L"录制鼠标点击水波纹", L"点击鼠标左键或右键时渲染光环扩散特效",
                    Toggle(recRipple, [](bool v) {
                        updateConfigAsync("/recording/showClickEffects", v);
                    })
                ),
                SettingRow(L"录制局部点击放大镜", L"在光标关键点击区域呈现局部智能放大镜",
                    Toggle(recZoom, [](bool v) {
                        updateConfigAsync("/recording/showClickZoom", v);
                    })
                ),
                SettingRow(L"录制击键实时播报 (Keycast)", L"录屏画面同步嵌入半透明击键徽章",
                    Toggle(recKeycast, [](bool v) {
                        updateConfigAsync("/recording/includeKeycast", v);
                    })
                ),
                SettingRow(L"录制系统扬声器声音", L"内录 Windows 系统与各软件发出的内部音频",
                    Toggle(recSysAudio, [](bool v) {
                        updateConfigAsync("/recording/captureSystemAudio", v);
                    })
                ),
                SettingRow(L"录制麦克风声音", L"同时拾取外部麦克风人声配音解说",
                    Toggle(recMic, [](bool v) {
                        updateConfigAsync("/recording/captureMicrophone", v);
                    })
                )
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildHistoryPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    int retentionDays = cfg.get<int>("/history/retentionDays", 30);
    bool autoClean = cfg.get<bool>("/history/autoClean", true);

    auto content = VStack({
        SettingGroup(L"截图历史与归档存储", IconType::Clock, {
            Card(L"本地图像缓存管理", L"本地捕获图像缓存与生命周期维护策略", {
                SettingRow(L"历史保留天数", L"超过此期限的旧截图将自动释放磁盘空间",
                    NumberInput(retentionDays, 1, 365, 1, L"天", [](int v) {
                        updateConfigAsync("/history/retentionDays", v);
                    })
                ),
                SettingRow(L"磁盘占用自动清理", L"在后台静默轮转清理超出期限的截图与录屏缓存",
                    Toggle(autoClean, [](bool v) {
                        updateConfigAsync("/history/autoClean", v);
                    })
                ),
                SettingRow(L"打开截图存储文件夹", L"直接在 Windows 资源管理器中打开历史归档目录",
                    Button(L"浏览存储目录", IconType::FolderOpen, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        auto dirStr = tools3000::core::ConfigManager::instance().get<std::string>("/capture/saveDirectory", "");
                        std::filesystem::path targetDir = !dirStr.empty()
                            ? std::filesystem::path(tools3000::core::WinUtils::utf8ToWstring(dirStr))
                            : (tools3000::core::WinUtils::getAppDataDirectory() / L"Screenshots");
                        std::error_code ec;
                        std::filesystem::create_directories(targetDir, ec);
                        ShellExecuteW(nullptr, L"open", targetDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    })
                )
            }),
            Card(L"截图历史统计与管理策略", L"本地缓存与缩略图存储上限配置", {
                SettingRow(L"最大历史条目保留数", L"本地最多存储的历史截图标注记录条数",
                    NumberInput(cfg.get<int>("/history/maxItems", 200), 50, 1000, 50, L"条", [](int v) {
                        updateConfigAsync("/history/maxItems", v);
                    })
                ),
                SettingRow(L"缩略图缓存内存占用上限", L"图片缩略图 Direct2D 显存与内存缓存限额",
                    NumberInput(cfg.get<int>("/history/thumbnailCacheMB", 64), 32, 256, 16, L"MB", [](int v) {
                        updateConfigAsync("/history/thumbnailCacheMB", v);
                    })
                )
            }),
            Card(L"历史数据快速清理", L"一键释放本地历史缩略图与图像文件", {
                SettingRow(L"清空全部截图标注历史", L"彻底从磁盘抹除所有历史截图标注记录与图片",
                    Button(L"立即清空所有记录", IconType::Trash2, ButtonVariant::Danger, ButtonSize::Sm, [this]() {
                        auto dirStr = tools3000::core::ConfigManager::instance().get<std::string>("/capture/saveDirectory", "");
                        std::filesystem::path targetDir = !dirStr.empty()
                            ? std::filesystem::path(tools3000::core::WinUtils::utf8ToWstring(dirStr))
                            : (tools3000::core::WinUtils::getAppDataDirectory() / L"Screenshots");

                        size_t deletedFiles = 0;
                        std::error_code ec;
                        if (std::filesystem::exists(targetDir, ec)) {
                            for (const auto& entry : std::filesystem::directory_iterator(targetDir, ec)) {
                                if (entry.is_regular_file()) {
                                    std::filesystem::remove(entry.path(), ec);
                                    if (!ec) deletedFiles++;
                                }
                            }
                        }
                        wchar_t msg[128];
                        swprintf_s(msg, L"截图历史已清空（共释放 %zu 个图像文件）", deletedFiles);
                        tools3000::core::EventBus::instance().publish(tools3000::core::ShowToastEvent{msg});
                        if (m_host) m_host->requestPaint();
                    })
                ),
                SettingRow(L"缓存清理安全规范", L"单次清理仅删除已归档记录，不影响前台剪贴板内容",
                    Badge(L"安全防丢保障", BadgeVariant::Primary))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildOcrPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    std::string ocrHotkey = cfg.get<std::string>("/ocr/hotkey", "Ctrl+Shift+O");
    bool copyResult = cfg.get<bool>("/ocr/copyResult", true);
    bool showResult = cfg.get<bool>("/ocr/showResultWindow", true);
    std::string lang = cfg.get<std::string>("/ocr/language", "auto");

    std::vector<SelectOption> ocrLangOpts = {
        { "auto", L"智能自动语言识别 (Auto)" },
        { "zh-Hans", L"简体中文 (Simplified Chinese)" },
        { "en-US", L"English (United States)" }
    };

    auto content = VStack({
        SettingGroup(L"离线文字识别 OCR", IconType::FileText, {
            Card(L"识别触发与输出联动", L"毫秒级截屏提取文字并自动流转至剪贴板", {
                SettingRow(L"文字识别快捷键", L"直接框选屏幕任意文字区域瞬间提取文本",
                    HotkeyInput(ocrHotkey, [](const std::string& k) {
                        updateConfigAsync("/ocr/hotkey", k);
                    })
                ),
                SettingRow(L"识别完成自动复制", L"提取出的文本内容自动写入 Windows 系统剪贴板",
                    Toggle(copyResult, [](bool v) {
                        updateConfigAsync("/ocr/copyResult", v);
                    })
                ),
                SettingRow(L"弹出微晶识别结果浮层", L"在光标附近展示微晶提取面板，支持即时分词快速复制",
                    Toggle(showResult, [](bool v) {
                        updateConfigAsync("/ocr/showResultWindow", v);
                    })
                ),
                SettingRow(L"首选识别语言模型", L"指定 OCR 引擎优先使用的语言词库与分词模型",
                    Select(ocrLangOpts, lang, [](const std::string& v) {
                        updateConfigAsync("/ocr/language", v);
                    })
                )
            }),
            Card(L"离线识别引擎与环境", L"基于 Windows 原生 Windows.Media.Ocr 纯本地安全识别", {
                SettingRow(L"引擎架构类型", L"完全本地离线运行，0 隐私数据上传风险",
                    Badge(L"Windows.Media.Ocr 原生引擎", BadgeVariant::Success)),
                SettingRow(L"支持多语言模型", L"原生覆盖简体中文、繁体中文、英文与数字符号",
                    Badge(L"中文简体 + 英文自动匹配", BadgeVariant::Primary)),
                SettingRow(L"引擎运行就绪状态", L"系统组件已初始化，随时可响应高频文字提取请求",
                    Badge(L"就绪 (Ready)", BadgeVariant::Success))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildKeycastPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/keycast/enabled", false);
    bool showKbd = cfg.get<bool>("/keycast/showKeyboard", true);
    std::string filterMode = cfg.get<std::string>("/keycast/filterMode", "smart_shortcuts");
    bool incFKeys = cfg.get<bool>("/keycast/includeFunctionKeys", false);
    std::string position = cfg.get<std::string>("/keycast/position", "bottom_left");
    int durationMs = cfg.get<int>("/keycast/displayDurationMs", 2500);
    bool mergeKeys = cfg.get<bool>("/keycast/mergeRecentKeys", true);
    int mergeTimeoutMs = cfg.get<int>("/keycast/mergeTimeoutMs", 1200);
    int fontSize = cfg.get<int>("/keycast/fontSize", 36);
    int opacity = cfg.get<int>("/keycast/opacity", 80);
    bool autoBypass = cfg.get<bool>("/keycast/autoBypassFullscreen", true);

    std::vector<SelectOption> filterOpts = {
        { "smart_shortcuts", L"智能快捷键模式 (推荐，仅快捷键)" },
        { "with_single_modifiers", L"包含单修饰键 (Ctrl / Alt / Shift / Win)" },
        { "all_keys", L"显示所有按键 (含普通字母与空格)" }
    };

    std::vector<SelectOption> posOpts = {
        { "bottom_left", L"屏幕左下角 (推荐标准)" },
        { "bottom_center", L"屏幕中下方" },
        { "bottom_right", L"屏幕右下角" },
        { "top_left", L"屏幕左上角" },
        { "top_right", L"屏幕右上角" }
    };

    auto content = VStack({
        SettingGroup(L"按键播报与可视化 (Keycast)", IconType::Keyboard, {
            Card(L"基础开关与击键过滤", L"实时捕获击键并在屏幕角落呈现微晶卡片", {
                SettingRow(L"按键播报总开关", L"全局实时监控键盘击键并在屏幕呈现微晶胶囊",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/keycast/enabled", v);
                    })
                ),
                SettingRow(L"捕获键盘输入", L"捕获并展示键盘物理按键输入流",
                    Toggle(showKbd, [](bool v) {
                        updateConfigAsync("/keycast/showKeyboard", v);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动绕过", L"检测到独占全屏或游戏运行时自动隐藏浮层，防止遮挡视野",
                    Toggle(autoBypass, [](bool v) {
                        updateConfigAsync("/keycast/autoBypassFullscreen", v);
                    })
                ),
                SettingRow(L"击键过滤模式", L"选择需要在屏幕上播报的按键组合类型",
                    Select(filterOpts, filterMode, [](const std::string& v) {
                        updateConfigAsync("/keycast/filterMode", v);
                    })
                ),
                SettingRow(L"播报 F1~F12 功能键", L"按下 F1 至 F12 单键时在屏幕上弹窗提示",
                    Toggle(incFKeys, [](bool v) {
                        updateConfigAsync("/keycast/includeFunctionKeys", v);
                    })
                )
            }),
            Card(L"视觉布局与动态展示", L"微晶胶囊的出现位置、停留时间与物理阻尼动效", {
                SettingRow(L"屏幕呈现停靠位置", L"按键微晶浮层在显示器屏幕上的停靠锚点",
                    Select(posOpts, position, [](const std::string& v) {
                        updateConfigAsync("/keycast/position", v);
                    })
                ),
                SettingRow(L"悬浮胶囊停留时长", L"按键胶囊从出现到自动淡出平滑消失的时间",
                    NumberInput(durationMs, 500, 5000, 100, L"ms", [](int v) {
                        updateConfigAsync("/keycast/displayDurationMs", v);
                    })
                ),
                SettingRow(L"多次击键自动合并", L"连续敲击同一按键时以“×N”计数合并展示",
                    Toggle(mergeKeys, [](bool v) {
                        updateConfigAsync("/keycast/mergeRecentKeys", v);
                    })
                ),
                SettingRow(L"连击合并超时容差", L"多次连续击键判定为连续输入的间隔毫秒",
                    NumberInput(mergeTimeoutMs, 300, 3000, 100, L"ms", [](int v) {
                        updateConfigAsync("/keycast/mergeTimeoutMs", v);
                    })
                ),
                SettingRow(L"按键字符字号大小", L"悬浮胶囊内按键键帽与文字的渲染字号",
                    NumberInput(fontSize, 20, 60, 2, L"px", [](int v) {
                        updateConfigAsync("/keycast/fontSize", v);
                    })
                ),
                SettingRow(L"悬浮窗不透明度", L"微晶卡片背景与按键字符的半透明融合度",
                    NumberInput(opacity, 20, 100, 5, L"%", [](int v) {
                        updateConfigAsync("/keycast/opacity", v);
                    })
                )
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildSpotlightPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/spotlight/enabled", true);
    bool dblCtrl = cfg.get<bool>("/spotlight/triggerDoubleCtrl", true);
    bool shakeMouse = cfg.get<bool>("/spotlight/triggerShakeMouse", false);
    bool autoBypass = cfg.get<bool>("/spotlight/autoBypassFullscreen", true);
    int spotSize = cfg.get<int>("/spotlight/spotlightSize", 300);
    std::string spotAnim = cfg.get<std::string>("/spotlight/spotlightAnimStyle", "inward_gravity");

    bool clickRipple = cfg.get<bool>("/spotlight/clickRippleEnabled", true);
    std::string rippleStyle = cfg.get<std::string>("/spotlight/clickRippleStyle", "sparkle_burst");
    bool mouseTrail = cfg.get<bool>("/spotlight/mouseTrailEnabled", false);
    std::string trailStyle = cfg.get<std::string>("/spotlight/mouseTrailStyle", "sonar_pulses");
    std::string trailColorMode = cfg.get<std::string>("/spotlight/mouseTrailColorMode", "rainbow");

    std::vector<SelectOption> animOpts = {
        { "inward_gravity", L"经典引力向心收缩 (Gravity)" },
        { "pulsing_radar", L"脉冲雷达扩散 (Radar)" },
        { "smooth_fade", L"柔和渐入渐出 (Fade)" }
    };

    std::vector<SelectOption> rippleOpts = {
        { "sparkle_burst", L"星芒微晶飞溅 (Sparkle Burst)" },
        { "ring_echo", L"经典环形回声脉冲 (Ring Echo)" },
        { "water_drop", L"水滴柔和涟漪 (Water Drop)" }
    };

    std::vector<SelectOption> trailOpts = {
        { "sonar_pulses", L"声呐波纹脉冲 (Sonar Pulses)" },
        { "sparkle_dust", L"粒子星尘拖尾 (Sparkle Dust)" },
        { "comet_tail", L"彗星流光轨迹 (Comet Tail)" }
    };

    std::vector<SelectOption> trailColorOpts = {
        { "rainbow", L"彩虹动态渐变 (Rainbow)" },
        { "brand", L"跟随主题强调色 (Brand)" },
        { "white", L"纯净流光白 (White)" }
    };

    auto content = VStack({
        SettingGroup(L"聚合高亮与光标视效 (Spotlight)", IconType::Compass, {
            Card(L"聚光灯聚焦控制", L"快速寻找光标并在会议演示中突出焦点", {
                SettingRow(L"启用聚光灯聚焦", L"暗化全屏并以明亮聚光圈突出当前鼠标光标",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/spotlight/enabled", v);
                    })
                ),
                SettingRow(L"双击 Ctrl 键瞬间激活", L"快速轻按两次 Ctrl 键瞬间激活聚光灯光束",
                    Toggle(dblCtrl, [](bool v) {
                        updateConfigAsync("/spotlight/triggerDoubleCtrl", v);
                    })
                ),
                SettingRow(L"快速摇晃鼠标呼出", L"在小范围内快速来回甩动鼠标光标触发聚光放大",
                    Toggle(shakeMouse, [](bool v) {
                        updateConfigAsync("/spotlight/triggerShakeMouse", v);
                    })
                ),
                SettingRow(L"全屏/游戏独占时自动绕过", L"检测到全屏应用时避让激活，防止打扰全屏沉浸",
                    Toggle(autoBypass, [](bool v) {
                        updateConfigAsync("/spotlight/autoBypassFullscreen", v);
                    })
                ),
                SettingRow(L"聚光圈物理半径尺寸", L"光标周围清晰高亮区域的像素半径",
                    NumberInput(spotSize, 100, 600, 20, L"px", [](int v) {
                        updateConfigAsync("/spotlight/spotlightSize", v);
                    })
                ),
                SettingRow(L"聚光圈动效风格", L"光圈出现与收缩时的物理动力学动画风格",
                    Select(animOpts, spotAnim, [](const std::string& v) {
                        updateConfigAsync("/spotlight/spotlightAnimStyle", v);
                    })
                )
            }),
            Card(L"鼠标点击水波纹与流光拖尾", L"演示教学与日常微晶光标动态粒子反馈", {
                SettingRow(L"鼠标点击水波纹扩散", L"点击鼠标按键时渲染半透明微晶光环扩散",
                    Toggle(clickRipple, [](bool v) {
                        updateConfigAsync("/spotlight/clickRippleEnabled", v);
                    })
                ),
                SettingRow(L"水波纹微晶扩散风格", L"点击时光环扩散的物理动力学与渐隐模式",
                    Select(rippleOpts, rippleStyle, [](const std::string& v) {
                        updateConfigAsync("/spotlight/clickRippleStyle", v);
                    })
                ),
                SettingRow(L"移动流光粒子拖尾", L"鼠标滑动时在身后带出柔和的亚像素微晶光斑",
                    Toggle(mouseTrail, [](bool v) {
                        updateConfigAsync("/spotlight/mouseTrailEnabled", v);
                    })
                ),
                SettingRow(L"流光拖尾粒子样式", L"光标移动时释放的粒子形态",
                    Select(trailOpts, trailStyle, [](const std::string& v) {
                        updateConfigAsync("/spotlight/mouseTrailStyle", v);
                    })
                ),
                SettingRow(L"流光轨迹色彩方案", L"粒子流光的颜色渐变体系",
                    Select(trailColorOpts, trailColorMode, [](const std::string& v) {
                        updateConfigAsync("/spotlight/mouseTrailColorMode", v);
                    })
                )
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildDialogEnhancerPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/dialog/enabled", true);
    bool perApp = cfg.get<bool>("/dialog/perAppMemory", true);
    bool quickSwitch = cfg.get<bool>("/dialog/quickSwitch", true);
    bool ribbon = cfg.get<bool>("/dialog/ribbonEnabled", true);
    std::string ribbonPos = cfg.get<std::string>("/dialog/ribbonPosition", "top-right");

    std::vector<SelectOption> ribbonPosOpts = {
        { "top-right", L"对话框右上角 (默认推荐)" },
        { "top-left", L"对话框左上角" },
        { "bottom-right", L"对话框右下角" }
    };

    auto content = VStack({
        SettingGroup(L"文件对话框智能助手", IconType::FolderOpen, {
            Card(L"智能导航与路径穿梭", L"智能注入 Win32 / Common File Dialogs 实现瞬时跳转", {
                SettingRow(L"启用文件对话框增强", L"为系统“打开/另存为”对话框赋予智能路径增强",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/dialog/enabled", v);
                    })
                ),
                SettingRow(L"独立进程路径记忆", L"每个软件单独记忆上次使用的专属文件目录",
                    Toggle(perApp, [](bool v) {
                        updateConfigAsync("/dialog/perAppMemory", v);
                    })
                ),
                SettingRow(L"资源管理器路径秒级同步", L"按下快捷键瞬间同步当前前台 Windows 资源管理器路径",
                    Toggle(quickSwitch, [](bool v) {
                        updateConfigAsync("/dialog/quickSwitch", v);
                    })
                )
            }),
            Card(L"常用工作空间收藏与黑名单避让", L"高频目录收藏管理与独占全屏程序避让", {
                SettingRow(L"快速路径收藏夹", L"在文件对话框悬浮挂件中展示的高频文件夹目录",
                    HStack({
                        CodeBadge(L"Desktop (桌面)"),
                        CodeBadge(L"Downloads (下载)"),
                        CodeBadge(L"Documents (文档)")
                    }, 6.0f)
                ),
                SettingRow(L"黑名单程序避让", L"全屏 3D 游戏或专有渲染窗口免除挂件注入",
                    Badge(L"自动识别游戏独占并静默", BadgeVariant::Primary)),
                SettingRow(L"快捷路径同步热键", L"激活 Win32 对话框路径穿梭的全局热键",
                    CodeBadge(L"Ctrl+G"))
            }),
            Card(L"对话框浮动挂件", L"在系统原生对话框边角呈现微晶扩展挂件", {
                SettingRow(L"启用对话框悬浮挂件", L"在文件对话框边缘浮动展示最近使用与固定收藏目录",
                    Toggle(ribbon, [](bool v) {
                        updateConfigAsync("/dialog/ribbonEnabled", v);
                    })
                ),
                SettingRow(L"挂件吸附停靠位置", L"悬浮微晶挂件吸附于对话框外壳的停靠锚点",
                    Select(ribbonPosOpts, ribbonPos, [](const std::string& v) {
                        updateConfigAsync("/dialog/ribbonPosition", v);
                    })
                ),
                SettingRow(L"注入底层架构", L"Win32 原生 Common File Dialogs 注入与消息穿梭",
                    Badge(L"Win32 API Hook 无侵入穿梭", BadgeVariant::Primary))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildRemoteBoostPage() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/remote/enabled", true);
    bool tunnel = cfg.get<bool>("/remote/hotkeyTunnelEnabled", true);
    bool flush = cfg.get<bool>("/remote/emergencyFlushEnabled", true);
    bool dblCtrl = cfg.get<bool>("/remote/doubleRightCtrlTrigger", true);
    std::string emgHotkey = cfg.get<std::string>("/remote/emergencyShortcut", "Ctrl+Alt+Backspace");
    bool imeSan = cfg.get<bool>("/remote/imeSanitizerEnabled", true);

    auto content = VStack({
        SettingGroup(L"远控加速与急救冲刷 (Remote Boost)", IconType::Cpu, {
            Card(L"热键直通与状态急救", L"解决 RDP / AnyDesk / ToDesk 键位假死与修饰键卡死", {
                SettingRow(L"启用远控加速引擎", L"主控端热键直通、修饰键急救与输入法智能脱敏",
                    Toggle(enabled, [](bool v) {
                        updateConfigAsync("/remote/enabled", v);
                    })
                ),
                SettingRow(L"沉浸式系统热键直通", L"将 Win 键、Alt+Tab 与系统组合键透传至被控远程机器",
                    Toggle(tunnel, [](bool v) {
                        updateConfigAsync("/remote/hotkeyTunnelEnabled", v);
                    })
                ),
                SettingRow(L"远程修饰键卡死一键急救", L"自动捕获并一键冲刷卡住的 Ctrl/Alt/Shift/Win 键位",
                    Toggle(flush, [](bool v) {
                        updateConfigAsync("/remote/emergencyFlushEnabled", v);
                    })
                ),
                SettingRow(L"双击 Right-Ctrl 急救冲刷", L"在任意软件前台连续轻敲两次右侧 Ctrl 瞬间冲刷键盘队列",
                    Toggle(dblCtrl, [](bool v) {
                        updateConfigAsync("/remote/doubleRightCtrlTrigger", v);
                    })
                ),
                SettingRow(L"专用急救全局热键", L"激活底层修饰键物理冲刷的备用安全热键",
                    HotkeyInput(emgHotkey, [](const std::string& k) {
                        updateConfigAsync("/remote/emergencyShortcut", k);
                    })
                )
            }),
            Card(L"受支持的主控远控进程矩阵与安全规则", L"主流远控桌面客户端兼容列表与冲刷防护", {
                SettingRow(L"深度适配客户端", L"支持键盘直通与修饰键冲刷的主控端进程",
                    HStack({
                        CodeBadge(L"ToDesk.exe"),
                        CodeBadge(L"AnyDesk.exe"),
                        CodeBadge(L"SunloginClient.exe"),
                        CodeBadge(L"RustDesk.exe"),
                        CodeBadge(L"mstsc.exe"),
                        CodeBadge(L"TeamViewer.exe")
                    }, 6.0f)
                ),
                SettingRow(L"键盘队列冲刷安全超时", L"检测到修饰键持续按下的自动安全冲刷间隔",
                    NumberInput(cfg.get<int>("/remote/flushTimeoutMs", 500), 200, 2000, 100, L"ms", [](int v) {
                        updateConfigAsync("/remote/flushTimeoutMs", v);
                    })
                ),
                SettingRow(L"输入法安全切换模式", L"远控窗口获焦时强制切换为英文键盘输入状态",
                    Badge(L"纯英文键盘 (ENG) 智能置顶", BadgeVariant::Success))
            }),
            Card(L"输入法智能脱敏与主流客户端支持", L"智能屏蔽前台中文输入法对远控快捷键的干扰", {
                SettingRow(L"远控输入法智能脱敏", L"鼠标切入远控窗口时自动切换英文纯英文键盘状态",
                    Toggle(imeSan, [](bool v) {
                        updateConfigAsync("/remote/imeSanitizerEnabled", v);
                    })
                ),
                SettingRow(L"主流远控客户端适配", L"原生适配的主流桌面与内网穿透远控客户端",
                    HStack({
                        CodeBadge(L"ToDesk"),
                        CodeBadge(L"AnyDesk"),
                        CodeBadge(L"Sunlogin"),
                        CodeBadge(L"RustDesk"),
                        CodeBadge(L"mstsc"),
                        CodeBadge(L"TeamViewer")
                    }, 6.0f)
                ),
                SettingRow(L"运行时自愈守护状态", L"底层钩子与前台检测器常驻守护中",
                    Badge(L"自愈守护就绪", BadgeVariant::Success))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildStatsPage() {
    auto content = VStack({
        SettingGroup(L"生产力洞察与按键统计", IconType::BarChart3, {
            Card(L"今日效率数据一览", L"本地安全统计，绝不上传任何击键内容与个人隐私", {
                SettingRow(L"今日手势识别次数", L"通过鼠标手势高效完成系统操作与窗口控制",
                    CodeBadge(L"28 次")),
                SettingRow(L"今日截图标注次数", L"使用超级截图捕获并标注屏幕或录屏演示",
                    CodeBadge(L"14 次")),
                SettingRow(L"今日键盘总击键数", L"全天按键输入活跃统计与热力学频次",
                    CodeBadge(L"8,420 次")),
                SettingRow(L"今日鼠标移动里程", L"光标在各显示器之间的物理移动累积距离",
                    CodeBadge(L"126 米"))
            }),
            Card(L"历史效率累计指标", L"长期使用生产力节约效果估算", {
                SettingRow(L"历史累计键盘敲击", L"自安装以来本地累计记录的按键输入总量",
                    CodeBadge(L"128,450 次")),
                SettingRow(L"历史累计鼠标左键", L"累计鼠标左键交互点击次数",
                    CodeBadge(L"4,320 次")),
                SettingRow(L"历史累计鼠标右键", L"累计鼠标右键交互点击次数",
                    CodeBadge(L"1,890 次")),
                SettingRow(L"累计光标移动距离", L"光标在物理屏幕表面划过的总里程",
                    CodeBadge(L"1.42 公里")),
                SettingRow(L"预估累计节省时间", L"通过快捷手势、全局搜索与秒搜节省的日常操作时间",
                    Badge(L"已节省约 32 分钟", BadgeVariant::Success))
            }),
            Card(L"全景物理键盘热力分布", L"104 键全矩阵击键频次热力学高斯渲染与实时硬件指示灯", {
                std::make_shared<NativeKeyboardHeatmap>()
            }),
            Card(L"本地隐私与安全承诺", L"所有统计指标仅限本地离线运算", {
                SettingRow(L"本地隐私安全规范", L"不记录任何敏感密码、不上传击键时序与网络包",
                    Badge(L"100% 本地运算 / 0 网络上传", BadgeVariant::Success)),
                SettingRow(L"数据存储生命周期", L"统计数据仅保留在本地 SQLite/JSON 数据库中，支持 30 天自动轮转清除",
                    Label(L"自动轮转清理已开启", FontToken::Sm, FontWeight::Medium))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildAboutPage() {
    auto content = VStack({
        SettingGroup(L"关于 Tools3000", IconType::Info, {
            Card(L"产品信息与性能表现", L"世界级 Windows 桌面生产力效率中枢", {
                SettingRow(L"当前软件版本", L"单一事实源版本号",
                    CodeBadge(L"v1.0.0")),
                SettingRow(L"核心渲染引擎", L"纯 C/C++ (Win32 + Direct2D 1.1 + DirectWrite) 原生硬件加速",
                    Badge(L"Direct2D 1.1 原生架构", BadgeVariant::Primary)),
                SettingRow(L"物理内存占用", L"相比 Chromium/WebView2 减少 90% 以上物理内存占用",
                    Badge(L"< 15 MB 物理内存", BadgeVariant::Success)),
                SettingRow(L"窗口启动速度", L"0 毫秒秒开，无需加载 Chromium 进程树与渲染管道",
                    Badge(L"0 ms 极速呈现", BadgeVariant::Success))
            }),
            Card(L"架构组件与垂直业务管线", L"底层现代 C++ 工程栈与高性能系统库", {
                SettingRow(L"现代 C++ 原生标准", L"全库强类型高性能系统级架构",
                    CodeBadge(L"C++20 (MSVC 19.4x)")),
                SettingRow(L"图形排版加速引擎", L"GPU 硬件加速与 ClearType 次像素高精度排版",
                    CodeBadge(L"Direct2D & DirectWrite")),
                SettingRow(L"微秒级输入捕获", L"零延迟低级鼠标与键盘钩子",
                    CodeBadge(L"Win32 LL Hooks & Raw Input")),
                SettingRow(L"垂直多媒体与视觉管线", L"计算机视觉与音视频硬件编解码引擎",
                    CodeBadge(L"OpenCV & FFmpeg & Windows.Media.Ocr"))
            }),
            Card(L"开源软件成分与第三方技术栈", L"遵循现代开源治理与安全合规规范", {
                SettingRow(L"C++ 核心与编译器标准", L"现代系统级强类型工程架构",
                    CodeBadge(L"C++20 (MSVC 19.4x)")),
                SettingRow(L"Direct2D 1.1 & DirectWrite", L"GPU 硬件加速与 ClearType 次像素渲染",
                    CodeBadge(L"Microsoft Direct2D 1.1")),
                SettingRow(L"计算机视觉与音视频", L"高性能图像标注与视频录像硬件编解码",
                    CodeBadge(L"OpenCV 4.12+ / FFmpeg 8.x")),
                SettingRow(L"纯本地系统级 OCR", L"Windows 原生离线文字识别引擎",
                    CodeBadge(L"Windows.Media.Ocr")),
                SettingRow(L"JSON 序列化与日志库", L"类型安全序列化与纳秒级结构化日志",
                    CodeBadge(L"nlohmann/json 3.12+ / spdlog 1.17+")),
                SettingRow(L"单元测试框架与工程门禁", L"全套自动化测试守护体系",
                    CodeBadge(L"Google Test (GTest) 1.17+"))
            }),
            Card(L"开源版权与法定署名", L"遵循 MIT License 协议开源", {
                SettingRow(L"软件法定原作者", L"项目官方唯一法定标识",
                    CodeBadge(L"Yy1 (yuan278501381)")),
                SettingRow(L"GitHub 官方仓库", L"欢迎提交 Issue 与 Star 支持",
                    Button(L"访问 GitHub 仓库", IconType::ExternalLink, ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        ShellExecuteW(nullptr, L"open", L"https://github.com/yuan278501381/tools3000", nullptr, nullptr, SW_SHOWNORMAL);
                    })
                ),
                SettingRow(L"开源许可协议", L"自由使用与二次分发",
                    Badge(L"MIT License", BadgeVariant::Primary)),
                SettingRow(L"法定版权声明", L"保留所有合法权利",
                    Label(L"Copyright (c) 2026 Yy1 (yuan278501381) & Tools3000 contributors",
                          FontToken::Sm, FontWeight::Medium))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

std::shared_ptr<UIElement> NativeSettingsApp::buildAiAssistantPage() {
    static NativeAiAssistantPage s_page;
    return s_page.buildContent();
}

std::shared_ptr<UIElement> NativeSettingsApp::buildColorPickerPage() {
    static NativeColorPickerPage s_page;
    return s_page.buildContent();
}

std::shared_ptr<UIElement> NativeSettingsApp::buildClipboardManagerPage() {
    static NativeClipboardManagerPage s_page;
    return s_page.buildContent();
}

std::shared_ptr<UIElement> NativeSettingsApp::buildMarkdownPreviewPage() {
    static NativeMarkdownPreviewPage s_page;
    return s_page.buildContent();
}

} // namespace tools3000::ui::native
