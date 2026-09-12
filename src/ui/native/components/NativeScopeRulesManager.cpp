#include "ui/native/components/NativeScopeRulesManager.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/ipc/MessageBridge.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>

namespace tools3000::ui::native {

NativeScopeRulesManager::NativeScopeRulesManager() {
    layoutParams().minHeight = 260.0f;
    layoutParams().flexGrow = 1.0f;

    initDefaultRules();
}

NativeScopeRulesManager::~NativeScopeRulesManager() = default;

void NativeScopeRulesManager::initDefaultRules() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    if (cfg.has("/gesture/scopeRules")) {
        try {
            auto j = cfg.get<nlohmann::json>("/gesture/scopeRules", nlohmann::json::array());
            if (j.is_array() && !j.empty()) {
                m_rules.clear();
                for (const auto& item : j) {
                    ScopeRuleDef r;
                    r.id = item.value("id", "rule_" + std::to_string(GetTickCount64()));
                    r.name = tools3000::core::WinUtils::utf8ToWstring(item.value("name", ""));
                    r.enabled = item.value("enabled", true);
                    r.targetKind = item.value("targetKind", "process");
                    std::string tval = item.value("targetValue", item.value("processName", ""));
                    if (tval.empty()) tval = item.value("windowClass", "");
                    r.targetValue = tools3000::core::WinUtils::utf8ToWstring(tval);
                    r.matchMode = item.value("matchMode", 0);
                    r.effect = item.value("effect", 2);
                    r.profileName = tools3000::core::WinUtils::utf8ToWstring(item.value("profileName", ""));
                    m_rules.push_back(r);
                }
                return;
            }
        } catch (...) {}
    }

    m_rules = {
        { "rule_chrome", L"Google Chrome 浏览器", true, "process", L"chrome.exe", 0, 2, L"Chrome专用配置" },
        { "rule_vscode", L"Visual Studio Code", true, "process", L"Code.exe", 0, 2, L"开发编辑配置" },
        { "rule_game",   L"Steam 游戏客户端",   true, "process", L"steam.exe", 0, 1, L"" }
    };
}

void NativeScopeRulesManager::saveRules() {
    nlohmann::json jArr = nlohmann::json::array();
    for (const auto& r : m_rules) {
        nlohmann::json item;
        item["id"] = r.id;
        item["name"] = tools3000::core::WinUtils::wstringToUtf8(r.name);
        item["enabled"] = r.enabled;
        item["targetKind"] = r.targetKind;
        std::string valUtf8 = tools3000::core::WinUtils::wstringToUtf8(r.targetValue);
        item["targetValue"] = valUtf8;
        if (r.targetKind == "class") {
            item["windowClass"] = valUtf8;
            item["processName"] = "";
        } else {
            item["processName"] = valUtf8;
            item["windowClass"] = "";
        }
        item["matchMode"] = r.matchMode;
        item["effect"] = r.effect;
        item["profileName"] = tools3000::core::WinUtils::wstringToUtf8(r.profileName);
        jArr.push_back(item);
    }
    tools3000::core::ConfigManager::instance().set("/gesture/scopeRules", jArr);

    nlohmann::json req;
    req["id"] = "gesture_scope_update";
    req["method"] = "gesture.updateScopeRules";
    req["params"]["rules"] = jArr;
    tools3000::core::MessageBridge::instance().handleMessageAsync(req.dump(), [](std::string){});

    if (m_onRulesChanged) {
        m_onRulesChanged(m_rules);
    }
}

void NativeScopeRulesManager::addRule(const ScopeRuleDef& rule) {
    m_rules.push_back(rule);
    saveRules();
    markNeedsPaint();
}

void NativeScopeRulesManager::deleteRule(const std::string& id) {
    m_rules.erase(
        std::remove_if(m_rules.begin(), m_rules.end(), [&](const ScopeRuleDef& r) { return r.id == id; }),
        m_rules.end()
    );
    m_selectedIds.erase(id);
    saveRules();
    markNeedsPaint();
}

void NativeScopeRulesManager::toggleRuleEnabled(const std::string& id) {
    for (auto& r : m_rules) {
        if (r.id == id) {
            r.enabled = !r.enabled;
            break;
        }
    }
    saveRules();
    markNeedsPaint();
}

void NativeScopeRulesManager::resetToDefault() {
    auto& cfg = tools3000::core::ConfigManager::instance();
    cfg.remove("/gesture/scopeRules");
    m_rules = {
        { "rule_chrome", L"Google Chrome 浏览器", true, "process", L"chrome.exe", 0, 2, L"Chrome专用配置" },
        { "rule_edge", L"Microsoft Edge 浏览器", true, "process", L"msedge.exe", 0, 2, L"Edge专用配置" },
        { "rule_code", L"Visual Studio Code", true, "process", L"Code.exe", 0, 2, L"VSCode开发增强" },
        { "rule_explorer", L"Windows 资源管理器", true, "windowClass", L"CabinetWClass", 0, 2, L"文件目录导航" },
        { "rule_games", L"全屏竞技游戏引擎", true, "process", L"valorant.exe", 0, 1, L"全屏独占静默" }
    };
    saveRules();
    markNeedsPaint();
}

void NativeScopeRulesManager::scanRunningWindows() {
    m_runningApps.clear();

    struct EnumParam {
        std::vector<RunningAppInfo>* apps;
    } param{ &m_runningApps };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto p = reinterpret_cast<EnumParam*>(lParam);
        if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

        int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) return TRUE;

        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, 255);

        wchar_t cls[128] = {};
        GetClassNameW(hwnd, cls, 127);

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0) return TRUE;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        wchar_t exePath[MAX_PATH] = {};
        DWORD size = MAX_PATH;
        if (hProc) {
            QueryFullProcessImageNameW(hProc, 0, exePath, &size);
            CloseHandle(hProc);
        }

        std::wstring procName = exePath;
        size_t slash = procName.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            procName = procName.substr(slash + 1);
        }

        if (!procName.empty() && procName != L"Tools3000.exe" && procName != L"explorer.exe") {
            p->apps->push_back({ title, procName, cls, hwnd });
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&param));
}

void NativeScopeRulesManager::startCrosshairCountdown() {
    m_countdownSeconds = 3;
    m_countdownElapsed = 0.0f;
    markNeedsPaint();
}

void NativeScopeRulesManager::captureForegroundWindow() {
    HWND fg = GetForegroundWindow();
    if (!fg) return;

    wchar_t title[256] = {};
    GetWindowTextW(fg, title, 255);

    wchar_t cls[128] = {};
    GetClassNameW(fg, cls, 127);

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);

    std::wstring procName;
    if (pid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t exePath[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            QueryFullProcessImageNameW(hProc, 0, exePath, &size);
            CloseHandle(hProc);
            procName = exePath;
            size_t slash = procName.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                procName = procName.substr(slash + 1);
            }
        }
    }

    if (!procName.empty()) {
        m_editingRule.targetValue = procName;
        m_editingRule.name = title[0] ? title : procName;
        m_editingRule.targetKind = "process";
    } else {
        m_editingRule.targetValue = cls;
        m_editingRule.name = title[0] ? title : cls;
        m_editingRule.targetKind = "class";
    }
    m_countdownSeconds = -1;
    markNeedsPaint();
}

Size NativeScopeRulesManager::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    (void)availableHeight;
    float w = availableWidth > 0 ? availableWidth : 680.0f;
    float h = 50.0f + m_rules.size() * 42.0f + 30.0f;
    m_desiredSize = Size(w, std::max(220.0f, h));
    return m_desiredSize;
}

void NativeScopeRulesManager::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeScopeRulesManager::update(float dt) {
    if (m_countdownSeconds > 0) {
        m_countdownElapsed += dt;
        if (m_countdownElapsed >= 1.0f) {
            m_countdownElapsed -= 1.0f;
            m_countdownSeconds--;
            if (m_countdownSeconds == 0) {
                captureForegroundWindow();
            }
            markNeedsPaint();
        }
        return true;
    }
    return false;
}

bool NativeScopeRulesManager::onMouseDown(const UIMouseEvent& e) {
    // ── 弹窗开启时的事件捕获 ────────────────────────────────────────────────
    if (m_modalOpen) {
        Rect modalRect(m_bounds.left + (m_bounds.width() - 560.0f) * 0.5f,
                       m_bounds.top + 10.0f,
                       m_bounds.left + (m_bounds.width() + 560.0f) * 0.5f,
                       m_bounds.top + 420.0f);

        // 关闭 X 按钮
        Rect closeBtn(modalRect.right - 32.0f, modalRect.top + 8.0f, modalRect.right - 8.0f, modalRect.top + 32.0f);
        if (closeBtn.contains(e.position)) {
            m_modalOpen = false;
            markNeedsPaint();
            return true;
        }

        // 十字准星拾取按钮
        Rect radarBtn(modalRect.left + 20.0f, modalRect.top + 48.0f, modalRect.right - 20.0f, modalRect.top + 84.0f);
        if (radarBtn.contains(e.position)) {
            startCrosshairCountdown();
            return true;
        }

        // Tab 切换 (运行中应用 vs 预设)
        Rect tabRunning(modalRect.left + 20.0f, modalRect.top + 92.0f, modalRect.left + 150.0f, modalRect.top + 118.0f);
        Rect tabPresets(modalRect.left + 155.0f, modalRect.top + 92.0f, modalRect.left + 285.0f, modalRect.top + 118.0f);
        if (tabRunning.contains(e.position)) {
            m_modalTab = 0;
            scanRunningWindows();
            markNeedsPaint();
            return true;
        }
        if (tabPresets.contains(e.position)) {
            m_modalTab = 1;
            markNeedsPaint();
            return true;
        }

        // 策略卡片选择
        Rect cardCustom(modalRect.left + 20.0f, modalRect.top + 280.0f, modalRect.left + 270.0f, modalRect.top + 345.0f);
        Rect cardDisable(modalRect.left + 285.0f, modalRect.top + 280.0f, modalRect.right - 20.0f, modalRect.top + 345.0f);
        if (cardCustom.contains(e.position)) {
            m_editingRule.effect = 2;
            markNeedsPaint();
            return true;
        }
        if (cardDisable.contains(e.position)) {
            m_editingRule.effect = 1;
            markNeedsPaint();
            return true;
        }

        // 确认保存按钮
        Rect saveBtn(modalRect.right - 100.0f, modalRect.bottom - 44.0f, modalRect.right - 20.0f, modalRect.bottom - 12.0f);
        if (saveBtn.contains(e.position)) {
            if (m_isNewRule) {
                m_editingRule.id = "rule_" + std::to_string(GetTickCount64());
                addRule(m_editingRule);
            } else {
                for (auto& r : m_rules) {
                    if (r.id == m_editingRule.id) {
                        r = m_editingRule;
                        break;
                    }
                }
                if (m_onRulesChanged) m_onRulesChanged(m_rules);
                markNeedsPaint();
            }
            m_modalOpen = false;
            return true;
        }

        // 取消按钮
        Rect cancelBtn(modalRect.right - 180.0f, modalRect.bottom - 44.0f, modalRect.right - 110.0f, modalRect.bottom - 12.0f);
        if (cancelBtn.contains(e.position)) {
            m_modalOpen = false;
            markNeedsPaint();
            return true;
        }

        return true;
    }

    // ── 表格工具栏 ──────────────────────────────────────────────────────────
    Rect toolbar(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 36.0f);
    Rect addBtn(toolbar.right - 88.0f, toolbar.top + 2.0f, toolbar.right - 4.0f, toolbar.bottom - 2.0f);
    if (addBtn.contains(e.position)) {
        m_modalOpen = true;
        m_isNewRule = true;
        m_editingRule = { "", L"新规则", true, "process", L"chrome.exe", 0, 2, L"" };
        scanRunningWindows();
        markNeedsPaint();
        return true;
    }

    // ── 表格数据行 ──────────────────────────────────────────────────────────
    float y = m_bounds.top + 40.0f + 28.0f;
    for (const auto& r : m_rules) {
        Rect rowRect(m_bounds.left, y, m_bounds.right, y + 38.0f);
        if (rowRect.contains(e.position)) {
            // 启用开关
            Rect toggleRect(m_bounds.left + 10.0f, y + 8.0f, m_bounds.left + 46.0f, y + 28.0f);
            if (toggleRect.contains(e.position)) {
                toggleRuleEnabled(r.id);
                return true;
            }

            // 编辑按钮
            Rect editBtn(rowRect.right - 68.0f, y + 6.0f, rowRect.right - 40.0f, y + 32.0f);
            if (editBtn.contains(e.position)) {
                m_modalOpen = true;
                m_isNewRule = false;
                m_editingRule = r;
                scanRunningWindows();
                markNeedsPaint();
                return true;
            }

            // 删除按钮
            Rect delBtn(rowRect.right - 36.0f, y + 6.0f, rowRect.right - 8.0f, y + 32.0f);
            if (delBtn.contains(e.position)) {
                deleteRule(r.id);
                return true;
            }
        }
        y += 42.0f;
    }

    return false;
}

bool NativeScopeRulesManager::onMouseMove(const UIMouseEvent& e) {
    (void)e;
    return false;
}

void NativeScopeRulesManager::render(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 1. 顶部工具栏 (Toolbar) ──────────────────────────────────────────────
    Rect tb(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 34.0f);

    wchar_t countBuf[64];
    swprintf_s(countBuf, L"生效中的规则: %zu 项", m_rules.size());
    ctx.drawText(countBuf, Point(tb.left + 4.0f, tb.top + 8.0f), FontToken::Sm, FontWeight::SemiBold, p.text);

    // "+ 添加规则" 按钮
    Rect addBtn(tb.right - 92.0f, tb.top + 2.0f, tb.right - 4.0f, tb.bottom - 2.0f);
    ctx.fillRoundedRect(addBtn, 4.0f, p.primary);
    VectorIconRenderer::drawIcon(ctx, IconType::Plus, Rect(addBtn.left + 8.0f, addBtn.top + 7.0f, addBtn.left + 22.0f, addBtn.top + 21.0f), Color(1.0f, 1.0f, 1.0f, 1.0f), 1.8f);
    ctx.drawText(L"添加规则", Point(addBtn.left + 26.0f, addBtn.top + 6.0f), FontToken::Xs, FontWeight::SemiBold, Color(1.0f, 1.0f, 1.0f, 1.0f));

    // ── 2. 表格表头 (Table Header) ──────────────────────────────────────────
    float y = m_bounds.top + 40.0f;
    Rect headerRect(m_bounds.left, y, m_bounds.right, y + 26.0f);
    ctx.fillRoundedRect(headerRect, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.03f) : Color(0.0f, 0.0f, 0.0f, 0.03f));

    ctx.drawText(L"状态", Point(headerRect.left + 14.0f, y + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);
    ctx.drawText(L"规则名称", Point(headerRect.left + 64.0f, y + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);
    ctx.drawText(L"匹配目标", Point(headerRect.left + 240.0f, y + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);
    ctx.drawText(L"生效策略", Point(headerRect.left + 440.0f, y + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);
    ctx.drawText(L"操作", Point(headerRect.right - 60.0f, y + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);

    y += 30.0f;

    // ── 3. 规则行列表渲染 ────────────────────────────────────────────────────
    for (const auto& r : m_rules) {
        Rect rowRect(m_bounds.left, y, m_bounds.right, y + 38.0f);
        ctx.fillRoundedRect(rowRect, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.02f) : Color(1.0f, 1.0f, 1.0f, 0.60f));
        ctx.drawRoundedRect(rowRect, 4.0f, p.border, 1.0f);

        // 启用状态微胶囊开关
        Rect toggleBox(rowRect.left + 12.0f, y + 9.0f, rowRect.left + 44.0f, y + 27.0f);
        ctx.fillRoundedRect(toggleBox, 9.0f, r.enabled ? p.primary : (isDark ? Color(0.2f, 0.22f, 0.28f, 1.0f) : Color(0.82f, 0.84f, 0.88f, 1.0f)));
        float thumbX = r.enabled ? (toggleBox.right - 16.0f) : (toggleBox.left + 2.0f);
        ctx.fillCircle(Point(thumbX + 7.0f, toggleBox.top + 9.0f), 7.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));

        // 规则名称
        ctx.drawText(r.name, Point(rowRect.left + 64.0f, y + 10.0f), FontToken::Sm, FontWeight::Medium, r.enabled ? p.text : p.textMuted);

        // 目标徽章 (进程 / 类名)
        Rect badgeKind(rowRect.left + 240.0f, y + 8.0f, rowRect.left + 284.0f, y + 28.0f);
        ctx.fillRoundedRect(badgeKind, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.06f) : Color(0.0f, 0.0f, 0.0f, 0.06f));
        ctx.drawText(r.targetKind == "process" ? L"进程" : L"类名", Point(badgeKind.left + 8.0f, y + 10.0f), FontToken::Xs, FontWeight::SemiBold, p.textSecondary);

        // 目标值代码徽章 (如 chrome.exe)
        ctx.drawText(r.targetValue, Point(rowRect.left + 294.0f, y + 10.0f), FontToken::Sm, FontWeight::SemiBold, p.primary);

        // 策略徽章
        Rect stratBadge(rowRect.left + 440.0f, y + 8.0f, rowRect.left + 530.0f, y + 28.0f);
        if (r.effect == 2) {
            ctx.fillRoundedRect(stratBadge, 4.0f, Color(p.primary.r, p.primary.g, p.primary.b, isDark ? 0.22f : 0.14f));
            ctx.drawText(L"✦ 独立手势", Point(stratBadge.left + 8.0f, y + 10.0f), FontToken::Xs, FontWeight::SemiBold, p.primary);
        } else {
            ctx.fillRoundedRect(stratBadge, 4.0f, Color(p.danger.r, p.danger.g, p.danger.b, isDark ? 0.22f : 0.14f));
            ctx.drawText(L"⊘ 禁用手势", Point(stratBadge.left + 8.0f, y + 10.0f), FontToken::Xs, FontWeight::SemiBold, p.danger);
        }

        // 操作按钮 (Edit & Delete)
        Rect editBtn(rowRect.right - 64.0f, y + 8.0f, rowRect.right - 40.0f, y + 30.0f);
        VectorIconRenderer::drawIcon(ctx, IconType::Edit3, editBtn, p.textSecondary, 1.5f);

        Rect delBtn(rowRect.right - 34.0f, y + 8.0f, rowRect.right - 10.0f, y + 30.0f);
        VectorIconRenderer::drawIcon(ctx, IconType::Trash2, delBtn, p.danger, 1.5f);

        y += 42.0f;
    }

    // ── 4. 模态编辑弹窗 (TargetAppPickerModal) ──────────────────────────────
    if (m_modalOpen) {
        // 半透明遮罩层
        ctx.fillRect(Rect(0.0f, 0.0f, 2000.0f, 2000.0f), Color(0.0f, 0.0f, 0.0f, 0.45f));

        Rect modal(m_bounds.left + (m_bounds.width() - 560.0f) * 0.5f,
                   m_bounds.top + 10.0f,
                   m_bounds.left + (m_bounds.width() + 560.0f) * 0.5f,
                   m_bounds.top + 410.0f);

        ctx.fillRoundedRect(modal, 8.0f, isDark ? Color(0.12f, 0.13f, 0.18f, 0.98f) : Color(1.0f, 1.0f, 1.0f, 0.98f));
        ctx.drawRoundedRect(modal, 8.0f, p.border, 1.2f);

        // 弹窗头部
        ctx.drawText(m_isNewRule ? L"添加作用域目标规则" : L"编辑作用域目标规则",
                     Point(modal.left + 20.0f, modal.top + 16.0f), FontToken::Lg, FontWeight::Bold, p.text);

        Rect closeBtn(modal.right - 32.0f, modal.top + 14.0f, modal.right - 12.0f, modal.top + 34.0f);
        VectorIconRenderer::drawIcon(ctx, IconType::X, closeBtn, p.textSecondary, 1.6f);

        // 十字准星前台雷达拾取大胶囊条
        Rect radar(modal.left + 20.0f, modal.top + 50.0f, modal.right - 20.0f, modal.top + 90.0f);
        ctx.fillRoundedRect(radar, 6.0f, Color(p.primary.r, p.primary.g, p.primary.b, isDark ? 0.18f : 0.10f));
        ctx.drawRoundedRect(radar, 6.0f, p.primary, 1.2f);

        VectorIconRenderer::drawIcon(ctx, IconType::Crosshair, Rect(radar.left + 16.0f, radar.top + 10.0f, radar.left + 36.0f, radar.top + 30.0f), p.primary, 1.8f);

        if (m_countdownSeconds > 0) {
            wchar_t cdBuf[64];
            swprintf_s(cdBuf, L"正在倒计时拾取前台窗口 (%d 秒)... 请立即点击目标窗口", m_countdownSeconds);
            ctx.drawText(cdBuf, Point(radar.left + 46.0f, radar.top + 10.0f), FontToken::Sm, FontWeight::Bold, p.accent);
        } else {
            ctx.drawText(L"点击拾取前台活跃窗口（支持十字准星自动提取进程名与类名）",
                         Point(radar.left + 46.0f, radar.top + 10.0f), FontToken::Sm, FontWeight::SemiBold, p.primary);
        }

        // 选项卡 (运行中应用 vs 预设)
        Rect tabRunning(modal.left + 20.0f, modal.top + 100.0f, modal.left + 140.0f, modal.top + 126.0f);
        ctx.drawText(L"运行中的应用", Point(tabRunning.left, tabRunning.top + 4.0f), FontToken::Sm,
                     m_modalTab == 0 ? FontWeight::Bold : FontWeight::Regular, m_modalTab == 0 ? p.primary : p.textMuted);

        Rect tabPresets(modal.left + 150.0f, modal.top + 100.0f, modal.left + 270.0f, modal.top + 126.0f);
        ctx.drawText(L"常用主流预设", Point(tabPresets.left, tabPresets.top + 4.0f), FontToken::Sm,
                     m_modalTab == 1 ? FontWeight::Bold : FontWeight::Regular, m_modalTab == 1 ? p.primary : p.textMuted);

        // 应用网格展示区 (前 4 项卡片预览)
        float gridY = modal.top + 134.0f;
        for (size_t i = 0; i < std::min<size_t>(3, m_runningApps.size()); ++i) {
            const auto& app = m_runningApps[i];
            Rect card(modal.left + 20.0f, gridY, modal.right - 20.0f, gridY + 34.0f);
            ctx.fillRoundedRect(card, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.03f) : Color(0.0f, 0.0f, 0.0f, 0.03f));
            ctx.drawRoundedRect(card, 4.0f, p.border, 1.0f);

            VectorIconRenderer::drawIcon(ctx, IconType::Monitor, Rect(card.left + 10.0f, card.top + 8.0f, card.left + 26.0f, card.top + 24.0f), p.primary, 1.4f);
            ctx.drawText(app.title, Point(card.left + 34.0f, card.top + 8.0f), FontToken::Sm, FontWeight::Medium, p.text);
            ctx.drawText(app.processName, Point(card.right - 140.0f, card.top + 8.0f), FontToken::Xs, FontWeight::SemiBold, p.primary);
            gridY += 38.0f;
        }

        // 策略选择卡片 (独立手势 vs 禁用)
        float stratY = modal.top + 260.0f;
        Rect cardCustom(modal.left + 20.0f, stratY, modal.left + 265.0f, stratY + 65.0f);
        ctx.fillRoundedRect(cardCustom, 6.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.03f) : Color(0.0f, 0.0f, 0.0f, 0.03f));
        ctx.drawRoundedRect(cardCustom, 6.0f, m_editingRule.effect == 2 ? p.primary : p.border, m_editingRule.effect == 2 ? 1.5f : 1.0f);
        ctx.drawText(L"✦ 自定义独立手势", Point(cardCustom.left + 14.0f, stratY + 12.0f), FontToken::Sm, FontWeight::Bold, p.text);
        ctx.drawText(L"在此应用中重载或扩充全局默认手势动作", Point(cardCustom.left + 14.0f, stratY + 34.0f), FontToken::Xs, FontWeight::Regular, p.textSecondary);

        Rect cardDisable(modal.left + 280.0f, stratY, modal.right - 20.0f, stratY + 65.0f);
        ctx.fillRoundedRect(cardDisable, 6.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.03f) : Color(0.0f, 0.0f, 0.0f, 0.03f));
        ctx.drawRoundedRect(cardDisable, 6.0f, m_editingRule.effect == 1 ? p.danger : p.border, m_editingRule.effect == 1 ? 1.5f : 1.0f);
        ctx.drawText(L"⊘ 禁用手势 (游戏/免打扰)", Point(cardDisable.left + 14.0f, stratY + 12.0f), FontToken::Sm, FontWeight::Bold, p.text);
        ctx.drawText(L"在此前台进程彻底静默并放行右键手势事件", Point(cardDisable.left + 14.0f, stratY + 34.0f), FontToken::Xs, FontWeight::Regular, p.textSecondary);

        // 底部保存与取消按钮
        Rect saveBtn(modal.right - 100.0f, modal.bottom - 44.0f, modal.right - 20.0f, modal.bottom - 12.0f);
        ctx.fillRoundedRect(saveBtn, 4.0f, p.primary);
        ctx.drawText(L"保存规则", Point(saveBtn.left + 14.0f, saveBtn.top + 7.0f), FontToken::Sm, FontWeight::SemiBold, Color(1.0f, 1.0f, 1.0f, 1.0f));

        Rect cancelBtn(modal.right - 180.0f, modal.bottom - 44.0f, modal.right - 110.0f, modal.bottom - 12.0f);
        ctx.fillRoundedRect(cancelBtn, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.06f) : Color(0.0f, 0.0f, 0.0f, 0.06f));
        ctx.drawText(L"取消", Point(cancelBtn.left + 20.0f, cancelBtn.top + 7.0f), FontToken::Sm, FontWeight::Regular, p.textSecondary);
    }
}

} // namespace tools3000::ui::native
