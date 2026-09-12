#include "ui/native/app/NativeTrayApp.h"
#include "ui/native/app/NativeSettingsApp.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/app/NativeSearchApp.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include "core/utils/DpiUtils.h"
#include "core/plugin/PluginManager.h"
#include <algorithm>

namespace tools3000::ui::native {

NativeTrayView::NativeTrayView() {
    initControls();
    refreshState();
}

NativeTrayView::~NativeTrayView() = default;

void NativeTrayView::initControls() {
    m_pills = {
        { "gesture",         "gesture",        L"手势",   L"鼠标手势与边缘触发",   IconType::Hand,        true },
        { "keycast",         "keycast",        L"回显",   L"按键实时屏幕播报",     IconType::Keyboard,    true },
        { "capture",         "capture",        L"截图",   L"超级截图与硬件录屏",   IconType::Camera,      true },
        { "search",          "search",         L"搜索",   L"NTFS 毫秒级闪电搜索",  IconType::Search,      true },
        { "spotlight",       "spotlight",      L"特效",   L"鼠标特效与聚光灯",     IconType::Sparkles,    true },
        { "dialog_enhancer", "dialogenhancer", L"对话框", L"文件对话框智能跳跃",   IconType::FolderOpen,  true },
        { "remote_boost",    "remote_boost",   L"远控",   L"远控热键直通加速",     IconType::Cpu,         true }
    };
}

void NativeTrayView::refreshState() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    // 检查各插件在 ConfigManager 中的真实启用状态（兼容 /plugins/<id>/enabled 与 /<id>/enabled）
    for (auto& pill : m_pills) {
        std::string pKey = "/plugins/" + pill.pluginId + "/enabled";
        std::string rKey = "/" + pill.id + "/enabled";
        if (pill.id == "dialog_enhancer") rKey = "/dialogenhancer/enabled";
        if (cfg.has(pKey)) {
            pill.active = cfg.get<bool>(pKey, true);
        } else {
            pill.active = cfg.get<bool>(rKey, true);
        }
    }

    m_isElevated = tools3000::core::WinUtils::isCurrentProcessElevated();
    rebuildMenu();
}

void NativeTrayView::rebuildMenu() {
    m_menuItems.clear();

    // 1. 设置中心
    m_menuItems.push_back({
        "settings", L"设置中心", IconType::Settings, false, false, false,
        [this]() {
            if (m_host) m_host->hide();
            NativeSettingsApp::instance().show();
        }
    });

    // 2. 截图与录屏 (如果 capture 活跃)
    bool captureActive = m_pills[2].active;
    if (captureActive) {
        m_menuItems.push_back({
            "screenshot", L"屏幕截图", IconType::Camera, false, false, false,
            [this]() {
                if (m_host) m_host->hide();
                tools3000::core::MessageBridge::instance().handleMessageAsync(
                    "{\"id\":\"tray_shot\",\"method\":\"capture.start\",\"params\":{}}", [](std::string){}
                );
            }
        });
        m_menuItems.push_back({
            "recording", L"屏幕录制", IconType::Video, false, false, false,
            [this]() {
                if (m_host) m_host->hide();
                tools3000::core::MessageBridge::instance().handleMessageAsync(
                    "{\"id\":\"tray_rec\",\"method\":\"capture.startRecording\",\"params\":{}}", [](std::string){}
                );
            }
        });
    }

    // 3. 全局搜索 (如果 search 活跃)
    bool searchActive = m_pills[3].active;
    if (searchActive) {
        m_menuItems.push_back({
            "search", L"全局文件搜索", IconType::Search, false, false, false,
            [this]() {
                if (m_host) m_host->hide();
                tools3000::ui::native::NativeSearchApp::instance().show(GetModuleHandleW(nullptr));
            }
        });
    }

    // 4. 远控热键急救 (如果 remote_boost 活跃)
    bool remoteActive = m_pills[6].active;
    if (remoteActive) {
        m_menuItems.push_back({
            "emergencyFlush", L"远控按键急救", IconType::Zap, false, false, false,
            [this]() {
                if (m_host) m_host->hide();
                tools3000::core::MessageBridge::instance().handleMessageAsync(
                    "{\"id\":\"tray_flush\",\"method\":\"remote.emergencyFlush\",\"params\":{}}", [](std::string){}
                );
            }
        });
    }

    // 5. 管理员提权 / 降权
    m_menuItems.push_back({
        "elevate",
        m_isElevated ? L"以最高权限运行中" : L"以管理员身份重启",
        m_isElevated ? IconType::Shield : IconType::Shield,
        false, true, m_isElevated,
        [this]() {
            if (m_host) m_host->hide();
            if (!m_isElevated) {
                tools3000::core::MessageBridge::instance().handleMessageAsync(
                    "{\"id\":\"tray_elevate\",\"method\":\"app.restartElevated\",\"params\":{}}", [](std::string){}
                );
            }
        }
    });

    // 6. 退出程序
    m_menuItems.push_back({
        "exit", L"退出 Tools3000", IconType::LogOut, true, false, false,
        [this]() {
            if (m_host) m_host->hide();
            PostQuitMessage(0);
        }
    });

    markNeedsLayout();
    markNeedsPaint();
}

void NativeTrayView::togglePill(const std::string& pillId) {
    auto& cfg = tools3000::core::ConfigManager::instance();
    for (auto& pill : m_pills) {
        if (pill.id == pillId) {
            pill.active = !pill.active;
            std::string pKey = "/plugins/" + pill.pluginId + "/enabled";
            std::string rKey = "/" + pill.id + "/enabled";
            if (pill.id == "dialog_enhancer") rKey = "/dialogenhancer/enabled";
            cfg.set(pKey, pill.active);
            cfg.set(rKey, pill.active);
            tools3000::core::PluginManager::instance().reconcilePluginConfigs();
            m_pendingRestart = true;
            break;
        }
    }
    rebuildMenu();
}

Size NativeTrayView::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)availableHeight;
    (void)ctx;
    float w = availableWidth > 0 ? availableWidth : 240.0f;

    // 计算总高度:
    // 胶囊网格: 2 行 (4 + 3) * 30px + 6px gap + 16px padding = 82px
    // 重启提示条 (如果有): 34px
    // 分割线: 8px
    // 菜单项: m_menuItems.size() * 34px
    // 分割线 * 2: 16px
    float h = 12.0f + 72.0f + 8.0f;
    if (m_pendingRestart) h += 34.0f;
    h += 8.0f; // 分割线
    h += m_menuItems.size() * 34.0f + 16.0f;

    m_desiredSize = Size(w, h);
    return m_desiredSize;
}

void NativeTrayView::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeTrayView::update(float dt) {
    if (m_pendingRestart) {
        m_spinAngle += dt * 360.0f;
        if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;
        return true;
    }
    return false;
}

bool NativeTrayView::onMouseMove(const UIMouseEvent& e) {
    int prevHover = m_hoveredIndex;
    m_hoveredIndex = -1;

    // 胶囊区域
    float px = m_bounds.left + 10.0f;
    float py = m_bounds.top + 10.0f;
    float pillW = (m_bounds.width() - 20.0f - 12.0f) / 4.0f;

    for (size_t i = 0; i < m_pills.size(); ++i) {
        float x = px + (i % 4) * (pillW + 4.0f);
        float y = py + (i / 4) * 32.0f;
        Rect r(x, y, x + pillW, y + 28.0f);
        if (r.contains(e.position)) {
            m_hoveredIndex = static_cast<int>(i);
            break;
        }
    }

    // 菜单项区域
    float itemY = m_bounds.top + 10.0f + 68.0f + (m_pendingRestart ? 34.0f : 0.0f) + 12.0f;
    for (size_t i = 0; i < m_menuItems.size(); ++i) {
        Rect r(m_bounds.left + 6.0f, itemY, m_bounds.right - 6.0f, itemY + 32.0f);
        if (r.contains(e.position)) {
            m_hoveredIndex = 100 + static_cast<int>(i);
            break;
        }
        itemY += 34.0f;
    }

    if (m_hoveredIndex != prevHover) {
        markNeedsPaint();
    }
    return true;
}

void NativeTrayView::onMouseLeave() {
    if (m_hoveredIndex != -1) {
        m_hoveredIndex = -1;
        markNeedsPaint();
    }
}

bool NativeTrayView::onMouseDown(const UIMouseEvent& e) {
    // 点击快捷胶囊
    float px = m_bounds.left + 10.0f;
    float py = m_bounds.top + 10.0f;
    float pillW = (m_bounds.width() - 20.0f - 12.0f) / 4.0f;

    for (size_t i = 0; i < m_pills.size(); ++i) {
        float x = px + (i % 4) * (pillW + 4.0f);
        float y = py + (i / 4) * 32.0f;
        Rect r(x, y, x + pillW, y + 28.0f);
        if (r.contains(e.position)) {
            togglePill(m_pills[i].id);
            return true;
        }
    }

    // 点击重启横幅
    if (m_pendingRestart) {
        float bannerY = m_bounds.top + 10.0f + 68.0f;
        Rect bannerRect(m_bounds.left + 10.0f, bannerY, m_bounds.right - 10.0f, bannerY + 28.0f);
        if (bannerRect.contains(e.position)) {
            if (m_host) m_host->hide();
            tools3000::core::MessageBridge::instance().handleMessageAsync(
                "{\"id\":\"tray_restart\",\"method\":\"app.restart\",\"params\":{}}", [](std::string){}
            );
            return true;
        }
    }

    // 点击菜单项
    float itemY = m_bounds.top + 10.0f + 68.0f + (m_pendingRestart ? 34.0f : 0.0f) + 12.0f;
    for (size_t i = 0; i < m_menuItems.size(); ++i) {
        Rect r(m_bounds.left + 6.0f, itemY, m_bounds.right - 6.0f, itemY + 32.0f);
        if (r.contains(e.position)) {
            if (m_menuItems[i].action) {
                m_menuItems[i].action();
            }
            return true;
        }
        itemY += 34.0f;
    }

    return false;
}

void NativeTrayView::render(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 1. 顶部快捷控制胶囊网格 ──────────────────────────────────────────────
    float px = m_bounds.left + 10.0f;
    float py = m_bounds.top + 10.0f;
    float pillW = (m_bounds.width() - 20.0f - 12.0f) / 4.0f;

    for (size_t i = 0; i < m_pills.size(); ++i) {
        const auto& pill = m_pills[i];
        float x = px + (i % 4) * (pillW + 4.0f);
        float y = py + (i / 4) * 32.0f;
        Rect r(x, y, x + pillW, y + 28.0f);
        bool isHov = (m_hoveredIndex == static_cast<int>(i));

        if (pill.active) {
            Color pillBg = isDark ? Color(p.primary.r, p.primary.g, p.primary.b, isHov ? 0.28f : 0.18f)
                                  : Color(p.primary.r, p.primary.g, p.primary.b, isHov ? 0.18f : 0.12f);
            ctx.fillRoundedRect(r, 6.0f, pillBg);
            ctx.drawRoundedRect(r, 6.0f, p.primary, 1.2f);
        } else {
            Color idleBg = isDark ? Color(1.0f, 1.0f, 1.0f, isHov ? 0.07f : 0.03f)
                                  : Color(0.0f, 0.0f, 0.0f, isHov ? 0.07f : 0.03f);
            ctx.fillRoundedRect(r, 6.0f, idleBg);
            ctx.drawRoundedRect(r, 6.0f, p.border, 1.0f);
        }

        VectorIconRenderer::drawIcon(
            ctx, pill.icon,
            Rect(r.left + 5.0f, r.top + 7.0f, r.left + 19.0f, r.top + 21.0f),
            pill.active ? p.primary : p.textMuted, 1.5f
        );

        ctx.drawText(
            pill.label,
            Point(r.left + 22.0f, r.top + 7.0f),
            FontToken::Xs, FontWeight::SemiBold,
            pill.active ? (isDark ? Color(1.0f, 1.0f, 1.0f, 0.95f) : p.primary) : p.textSecondary
        );

        // 活跃状态指示绿点
        if (pill.active) {
            ctx.fillCircle(Point(r.right - 6.0f, r.top + 6.0f), 2.5f, Color(0.15f, 0.85f, 0.40f, 1.0f));
        }
    }

    // ── 2. 待生效重启提示条 ──────────────────────────────────────────────────
    float curY = m_bounds.top + 10.0f + 68.0f;
    if (m_pendingRestart) {
        Rect bannerRect(m_bounds.left + 10.0f, curY, m_bounds.right - 10.0f, curY + 28.0f);
        ctx.fillRoundedRect(bannerRect, 5.0f, Color(p.accent.r, p.accent.g, p.accent.b, isDark ? 0.20f : 0.12f));
        ctx.drawRoundedRect(bannerRect, 5.0f, p.accent, 1.0f);

        VectorIconRenderer::drawIcon(
            ctx, IconType::RefreshCw,
            Rect(bannerRect.left + 8.0f, bannerRect.top + 7.0f, bannerRect.left + 22.0f, bannerRect.top + 21.0f),
            p.accent, 1.6f
        );
        ctx.drawText(L"配置已更新，点击重启生效", Point(bannerRect.left + 26.0f, bannerRect.top + 6.0f),
                     FontToken::Xs, FontWeight::Medium, p.accent);
        curY += 34.0f;
    }

    // ── 3. 分割微刻线 ────────────────────────────────────────────────────────
    ctx.drawLine(
        Point(m_bounds.left + 10.0f, curY + 4.0f),
        Point(m_bounds.right - 10.0f, curY + 4.0f),
        p.border, 1.0f
    );
    curY += 8.0f;

    // ── 4. 核心菜单项列表 ────────────────────────────────────────────────────
    for (size_t i = 0; i < m_menuItems.size(); ++i) {
        const auto& item = m_menuItems[i];
        Rect r(m_bounds.left + 6.0f, curY, m_bounds.right - 6.0f, curY + 32.0f);
        bool isHov = (m_hoveredIndex == 100 + static_cast<int>(i));

        if (isHov) {
            Color hovBg = item.isDanger ? Color(p.danger.r, p.danger.g, p.danger.b, 0.15f)
                                        : (isDark ? Color(1.0f, 1.0f, 1.0f, 0.08f) : Color(0.0f, 0.0f, 0.0f, 0.06f));
            ctx.fillRoundedRect(r, 6.0f, hovBg);
        }

        // 图标
        Color iconCol = item.isDanger ? p.danger : (item.checked ? p.success : p.textSecondary);
        VectorIconRenderer::drawIcon(
            ctx, item.icon,
            Rect(r.left + 10.0f, r.top + 8.0f, r.left + 26.0f, r.top + 24.0f),
            iconCol, 1.6f
        );

        // 标签文字
        Color textCol = item.isDanger ? p.danger : (item.checked ? p.text : p.text);
        ctx.drawText(
            item.label,
            Point(r.left + 32.0f, r.top + 7.0f),
            FontToken::Sm, item.checked ? FontWeight::SemiBold : FontWeight::Regular,
            textCol
        );

        // 管理员绿色对勾/绿点
        if (item.isCheckable && item.checked) {
            ctx.fillCircle(Point(r.right - 14.0f, r.top + 16.0f), 3.5f, Color(0.15f, 0.85f, 0.40f, 1.0f));
        }

        curY += 34.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NativeTrayApp 单例宿主实现
// ─────────────────────────────────────────────────────────────────────────────

NativeTrayApp& NativeTrayApp::instance() {
    static NativeTrayApp s_app;
    return s_app;
}

NativeTrayApp::NativeTrayApp() = default;
NativeTrayApp::~NativeTrayApp() {
    destroy();
}

void NativeTrayApp::ensureInitialized(HINSTANCE hInstance) {
    if (m_host) return;

    m_host = std::make_unique<NativeFramelessWindow>();
    NativeWindowConfig config;
    config.title = L"Tools3000 Tray";
    config.width = 240;
    config.height = 360;
    config.minWidth = 200;
    config.minHeight = 240;
    config.centerOnScreen = false;
    config.resizable = false;
    config.seamlessTitlebar = false;
    config.isPopup = true;
    config.isToolWindow = true;
    config.alwaysOnTop = true;

    if (m_host->create(hInstance, config)) {
        m_host->setFramelessMode(FramelessMode::TrayMenu);
        m_view = std::make_shared<NativeTrayView>();
        m_view->onAttachedToHost(m_host.get());
        m_host->setRootElement(m_view);

        // 注册失焦自动收起
        m_host->setOnFocusLost([this]() {
            hide();
        });
    }
}

void NativeTrayApp::preload(HINSTANCE hInstance) {
    ensureInitialized(hInstance);
}

void NativeTrayApp::show(HINSTANCE hInstance, int anchorX, int anchorY) {
    ensureInitialized(hInstance);
    if (!m_host) return;

    m_anchorX = anchorX;
    m_anchorY = anchorY;

    if (m_view) {
        m_view->refreshState();
    }

    // 动态重算内容高度并定位吸附到托盘图标
    UIRenderContext dummyCtx(nullptr, nullptr, m_host->getDpiScale());
    Size desired = m_view ? m_view->measure(240.0f, 600.0f, dummyCtx) : Size(240.0f, 360.0f);

    m_host->positionNearTray(anchorX, anchorY, static_cast<int>(desired.width), static_cast<int>(desired.height));
    m_host->show(SW_SHOW);
    SetForegroundWindow(m_host->hwnd());
}

void NativeTrayApp::hide() {
    if (m_host && m_host->isVisible()) {
        m_host->hide();
    }
}

void NativeTrayApp::destroy() {
    if (m_host) {
        m_host->destroy();
        m_host.reset();
    }
}

bool NativeTrayApp::isVisible() const {
    return m_host && m_host->isVisible();
}

} // namespace tools3000::ui::native
