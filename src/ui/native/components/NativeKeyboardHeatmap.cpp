#include "ui/native/components/NativeKeyboardHeatmap.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <algorithm>
#include <cmath>

namespace tools3000::ui::native {

NativeKeyboardHeatmap::NativeKeyboardHeatmap() {
    layoutParams().minHeight = 310.0f;
    layoutParams().flexGrow = 1.0f;

    initKeycaps();

    // 默认注入日常模拟热力学数据基线 (如 Space, Enter, Backspace, Ctrl, C, V 较高)
    m_hits[VK_SPACE] = 4320;
    m_hits[VK_RETURN] = 2150;
    m_hits[VK_BACK] = 1840;
    m_hits[VK_LCONTROL] = 1680;
    m_hits[VK_CONTROL] = 1680;
    m_hits['E'] = 1420;
    m_hits['T'] = 1380;
    m_hits['A'] = 1250;
    m_hits['O'] = 1180;
    m_hits['I'] = 1120;
    m_hits['N'] = 1050;
    m_hits['S'] = 980;
    m_hits['C'] = 890;
    m_hits['V'] = 870;

    m_totalHits = 0;
    m_maxHits = 1;
    for (const auto& [vk, count] : m_hits) {
        m_totalHits += count;
        if (count > m_maxHits) m_maxHits = count;
    }
}

NativeKeyboardHeatmap::~NativeKeyboardHeatmap() = default;

void NativeKeyboardHeatmap::initKeycaps() {
    m_keycaps.clear();

    // ── 1. 主键区 (Main Cluster 15u) ──────────────────────────────────────────
    // Row 0: F 功能键行 (y = 0.0)
    m_keycaps.push_back({ "esc", L"Esc", L"Escape 退出键", VK_ESCAPE, 1.0f, 1.0f, 0.0f, 0.0f });
    m_keycaps.push_back({ "f1", L"F1", L"F1 帮助", VK_F1, 1.0f, 1.0f, 2.0f, 0.0f });
    m_keycaps.push_back({ "f2", L"F2", L"F2 重命名", VK_F2, 1.0f, 1.0f, 3.0f, 0.0f });
    m_keycaps.push_back({ "f3", L"F3", L"F3 查找", VK_F3, 1.0f, 1.0f, 4.0f, 0.0f });
    m_keycaps.push_back({ "f4", L"F4", L"F4", VK_F4, 1.0f, 1.0f, 5.0f, 0.0f });
    m_keycaps.push_back({ "f5", L"F5", L"F5 刷新", VK_F5, 1.0f, 1.0f, 6.5f, 0.0f });
    m_keycaps.push_back({ "f6", L"F6", L"F6", VK_F6, 1.0f, 1.0f, 7.5f, 0.0f });
    m_keycaps.push_back({ "f7", L"F7", L"F7", VK_F7, 1.0f, 1.0f, 8.5f, 0.0f });
    m_keycaps.push_back({ "f8", L"F8", L"F8", VK_F8, 1.0f, 1.0f, 9.5f, 0.0f });
    m_keycaps.push_back({ "f9", L"F9", L"F9", VK_F9, 1.0f, 1.0f, 11.0f, 0.0f });
    m_keycaps.push_back({ "f10", L"F10", L"F10", VK_F10, 1.0f, 1.0f, 12.0f, 0.0f });
    m_keycaps.push_back({ "f11", L"F11", L"F11 全屏", VK_F11, 1.0f, 1.0f, 13.0f, 0.0f });
    m_keycaps.push_back({ "f12", L"F12", L"F12 开发者", VK_F12, 1.0f, 1.0f, 14.0f, 0.0f });

    // Row 1: 数字行 (y = 1.35)
    static const wchar_t* numLabels[] = { L"`", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0", L"-", L"=" };
    static const int numVks[] = { VK_OEM_3, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', VK_OEM_MINUS, VK_OEM_PLUS };
    for (int i = 0; i < 13; ++i) {
        m_keycaps.push_back({ "num_" + std::to_string(i), numLabels[i], numLabels[i], numVks[i], 1.0f, 1.0f, static_cast<float>(i), 1.35f });
    }
    m_keycaps.push_back({ "backspace", L"⌫ Back", L"退格删除键", VK_BACK, 2.0f, 1.0f, 13.0f, 1.35f });

    // Row 2: Tab 行 (y = 2.45)
    m_keycaps.push_back({ "tab", L"Tab", L"Tab 制表键", VK_TAB, 1.5f, 1.0f, 0.0f, 2.45f });
    static const wchar_t* qLabels[] = { L"Q", L"W", L"E", L"R", L"T", L"Y", L"U", L"I", L"O", L"P", L"[", L"]" };
    static const int qVks[] = { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', VK_OEM_4, VK_OEM_6 };
    for (int i = 0; i < 12; ++i) {
        m_keycaps.push_back({ "q_" + std::to_string(i), qLabels[i], qLabels[i], qVks[i], 1.0f, 1.0f, 1.5f + i, 2.45f });
    }
    m_keycaps.push_back({ "slash_back", L"\\", L"反斜杠", VK_OEM_5, 1.5f, 1.0f, 13.5f, 2.45f });

    // Row 3: Caps 行 (y = 3.55)
    m_keycaps.push_back({ "caps", L"Caps", L"CapsLock 大小写锁定", VK_CAPITAL, 1.75f, 1.0f, 0.0f, 3.55f });
    static const wchar_t* aLabels[] = { L"A", L"S", L"D", L"F", L"G", L"H", L"J", L"K", L"L", L";", L"'" };
    static const int aVks[] = { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', VK_OEM_1, VK_OEM_7 };
    for (int i = 0; i < 11; ++i) {
        m_keycaps.push_back({ "a_" + std::to_string(i), aLabels[i], aLabels[i], aVks[i], 1.0f, 1.0f, 1.75f + i, 3.55f });
    }
    m_keycaps.push_back({ "enter", L"↵ Enter", L"Enter 回车键", VK_RETURN, 2.25f, 1.0f, 12.75f, 3.55f });

    // Row 4: Shift 行 (y = 4.65)
    m_keycaps.push_back({ "lshift", L"Shift", L"Left Shift 左换档", VK_SHIFT, 2.25f, 1.0f, 0.0f, 4.65f });
    static const wchar_t* zLabels[] = { L"Z", L"X", L"C", L"V", L"B", L"N", L"M", L",", L".", L"/" };
    static const int zVks[] = { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD, VK_OEM_2 };
    for (int i = 0; i < 10; ++i) {
        m_keycaps.push_back({ "z_" + std::to_string(i), zLabels[i], zLabels[i], zVks[i], 1.0f, 1.0f, 2.25f + i, 4.65f });
    }
    m_keycaps.push_back({ "rshift", L"Shift", L"Right Shift 右换档", VK_SHIFT, 2.75f, 1.0f, 12.25f, 4.65f });

    // Row 5: 底栏 (y = 5.75)
    m_keycaps.push_back({ "lctrl", L"Ctrl", L"Left Ctrl 控制键", VK_CONTROL, 1.25f, 1.0f, 0.0f, 5.75f });
    m_keycaps.push_back({ "lwin", L"Win", L"Windows 徽标键", VK_LWIN, 1.25f, 1.0f, 1.25f, 5.75f });
    m_keycaps.push_back({ "lalt", L"Alt", L"Left Alt 换档键", VK_MENU, 1.25f, 1.0f, 2.5f, 5.75f });
    m_keycaps.push_back({ "space", L"Space", L"Space 空格键", VK_SPACE, 6.25f, 1.0f, 3.75f, 5.75f });
    m_keycaps.push_back({ "ralt", L"Alt", L"Right Alt", VK_MENU, 1.25f, 1.0f, 10.0f, 5.75f });
    m_keycaps.push_back({ "rwin", L"Win", L"Windows 徽标键", VK_RWIN, 1.25f, 1.0f, 11.25f, 5.75f });
    m_keycaps.push_back({ "menu", L"Menu", L"上下文菜单键", VK_APPS, 1.25f, 1.0f, 12.5f, 5.75f });
    m_keycaps.push_back({ "rctrl", L"Ctrl", L"Right Ctrl 控制键", VK_CONTROL, 1.25f, 1.0f, 13.75f, 5.75f });

    // ── 2. 编辑与方向键区 (Nav Cluster 3u, offset x = 15.5) ───────────────────
    const float navX = 15.5f;
    m_keycaps.push_back({ "prtsc", L"PS", L"Print Screen 截图", VK_SNAPSHOT, 1.0f, 1.0f, navX + 0.0f, 0.0f });
    m_keycaps.push_back({ "scrlk", L"SL", L"Scroll Lock 滚动锁", VK_SCROLL, 1.0f, 1.0f, navX + 1.0f, 0.0f });
    m_keycaps.push_back({ "pause", L"PB", L"Pause Break 暂停", VK_PAUSE, 1.0f, 1.0f, navX + 2.0f, 0.0f });

    m_keycaps.push_back({ "ins", L"Ins", L"Insert 插入键", VK_INSERT, 1.0f, 1.0f, navX + 0.0f, 1.35f });
    m_keycaps.push_back({ "home", L"Home", L"Home 行首", VK_HOME, 1.0f, 1.0f, navX + 1.0f, 1.35f });
    m_keycaps.push_back({ "pgup", L"PgUp", L"Page Up 上一页", VK_PRIOR, 1.0f, 1.0f, navX + 2.0f, 1.35f });

    m_keycaps.push_back({ "del", L"Del", L"Delete 删除键", VK_DELETE, 1.0f, 1.0f, navX + 0.0f, 2.45f });
    m_keycaps.push_back({ "end", L"End", L"End 行尾", VK_END, 1.0f, 1.0f, navX + 1.0f, 2.45f });
    m_keycaps.push_back({ "pgdn", L"PgDn", L"Page Down 下一页", VK_NEXT, 1.0f, 1.0f, navX + 2.0f, 2.45f });

    m_keycaps.push_back({ "up", L"↑", L"方向键 上", VK_UP, 1.0f, 1.0f, navX + 1.0f, 4.65f });
    m_keycaps.push_back({ "left", L"←", L"方向键 左", VK_LEFT, 1.0f, 1.0f, navX + 0.0f, 5.75f });
    m_keycaps.push_back({ "down", L"↓", L"方向键 下", VK_DOWN, 1.0f, 1.0f, navX + 1.0f, 5.75f });
    m_keycaps.push_back({ "right", L"→", L"方向键 右", VK_RIGHT, 1.0f, 1.0f, navX + 2.0f, 5.75f });

    // ── 3. 数字小键盘区 (Numpad Cluster 4u, offset x = 19.0) ──────────────────
    const float padX = 19.0f;
    m_keycaps.push_back({ "numlock", L"Num", L"NumLock 小键盘锁", VK_NUMLOCK, 1.0f, 1.0f, padX + 0.0f, 1.35f });
    m_keycaps.push_back({ "np_div", L"/", L"小键盘 除号", VK_DIVIDE, 1.0f, 1.0f, padX + 1.0f, 1.35f });
    m_keycaps.push_back({ "np_mul", L"*", L"小键盘 乘号", VK_MULTIPLY, 1.0f, 1.0f, padX + 2.0f, 1.35f });
    m_keycaps.push_back({ "np_sub", L"-", L"小键盘 减号", VK_SUBTRACT, 1.0f, 1.0f, padX + 3.0f, 1.35f });

    m_keycaps.push_back({ "np_7", L"7", L"数字 7", VK_NUMPAD7, 1.0f, 1.0f, padX + 0.0f, 2.45f });
    m_keycaps.push_back({ "np_8", L"8", L"数字 8", VK_NUMPAD8, 1.0f, 1.0f, padX + 1.0f, 2.45f });
    m_keycaps.push_back({ "np_9", L"9", L"数字 9", VK_NUMPAD9, 1.0f, 1.0f, padX + 2.0f, 2.45f });
    m_keycaps.push_back({ "np_add", L"+", L"小键盘 加号", VK_ADD, 1.0f, 2.1f, padX + 3.0f, 2.45f });

    m_keycaps.push_back({ "np_4", L"4", L"数字 4", VK_NUMPAD4, 1.0f, 1.0f, padX + 0.0f, 3.55f });
    m_keycaps.push_back({ "np_5", L"5", L"数字 5", VK_NUMPAD5, 1.0f, 1.0f, padX + 1.0f, 3.55f });
    m_keycaps.push_back({ "np_6", L"6", L"数字 6", VK_NUMPAD6, 1.0f, 1.0f, padX + 2.0f, 3.55f });

    m_keycaps.push_back({ "np_1", L"1", L"数字 1", VK_NUMPAD1, 1.0f, 1.0f, padX + 0.0f, 4.65f });
    m_keycaps.push_back({ "np_2", L"2", L"数字 2", VK_NUMPAD2, 1.0f, 1.0f, padX + 1.0f, 4.65f });
    m_keycaps.push_back({ "np_3", L"3", L"数字 3", VK_NUMPAD3, 1.0f, 1.0f, padX + 2.0f, 4.65f });
    m_keycaps.push_back({ "np_enter", L"↵", L"小键盘 回车", VK_RETURN, 1.0f, 2.1f, padX + 3.0f, 4.65f });

    m_keycaps.push_back({ "np_0", L"0", L"数字 0", VK_NUMPAD0, 2.0f, 1.0f, padX + 0.0f, 5.75f });
    m_keycaps.push_back({ "np_dot", L".", L"小数点", VK_DECIMAL, 1.0f, 1.0f, padX + 2.0f, 5.75f });
}

void NativeKeyboardHeatmap::setLayoutMode(KeyboardLayoutMode mode) {
    m_layoutMode = mode;
    markNeedsLayout();
    markNeedsPaint();
}

void NativeKeyboardHeatmap::setKeyHits(int vkCode, uint32_t hits) {
    m_hits[vkCode] = hits;
    m_totalHits = 0;
    m_maxHits = 1;
    for (const auto& [vk, count] : m_hits) {
        m_totalHits += count;
        if (count > m_maxHits) m_maxHits = count;
    }
    markNeedsPaint();
}

void NativeKeyboardHeatmap::setAllKeyHits(const std::unordered_map<int, uint32_t>& hitMap) {
    m_hits = hitMap;
    m_totalHits = 0;
    m_maxHits = 1;
    for (const auto& [vk, count] : m_hits) {
        m_totalHits += count;
        if (count > m_maxHits) m_maxHits = count;
    }
    markNeedsPaint();
}

void NativeKeyboardHeatmap::clearHits() {
    m_hits.clear();
    m_totalHits = 0;
    m_maxHits = 1;
    markNeedsPaint();
}

Size NativeKeyboardHeatmap::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    (void)availableHeight;
    float w = availableWidth > 0 ? availableWidth : 780.0f;
    float totalU = (m_layoutMode == KeyboardLayoutMode::Full104) ? 23.0f :
                   (m_layoutMode == KeyboardLayoutMode::TKL87)   ? 18.5f : 15.0f;
    float unitSize = (w - 28.0f) / totalU;
    unitSize = std::clamp(unitSize, 26.0f, 38.0f);
    float chassisH = 24.0f + 6.75f * unitSize;
    float totalH = 38.0f + chassisH + 12.0f + 32.0f;
    m_desiredSize = Size(w, totalH);
    return m_desiredSize;
}

void NativeKeyboardHeatmap::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeKeyboardHeatmap::update(float dt) {
    (void)dt;
    // 硬件 LED 指示灯状态实时刷新
    m_numLock = (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
    m_capsLock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    m_scrollLock = (GetKeyState(VK_SCROLL) & 0x0001) != 0;
    return false;
}

Rect NativeKeyboardHeatmap::getKeyRect(const KeyCapDef& k, const Rect& chassisRect, float unitSize) const {
    const float gap = std::max(2.0f, unitSize * 0.07f);
    float rx = chassisRect.left + 14.0f + k.x * unitSize;
    float ry = chassisRect.top + 12.0f + k.y * unitSize;
    float rw = k.widthU * unitSize - gap;
    float rh = k.heightU * unitSize - gap;
    return Rect(rx, ry, rx + rw, ry + rh);
}

Color NativeKeyboardHeatmap::calculateHeatColor(uint32_t hits, uint32_t maxHits, bool isDark) const {
    if (hits == 0) {
        return isDark ? Color(0.12f, 0.13f, 0.18f, 0.85f) : Color(0.92f, 0.94f, 0.97f, 0.85f);
    }
    float t = std::clamp(static_cast<float>(hits) / static_cast<float>(std::max(1u, maxHits)), 0.0f, 1.0f);

    // 高斯热力学渐变: Idle(深青) -> Warm(橙黄) -> Peak(炽烈红)
    if (t < 0.35f) {
        float f = t / 0.35f;
        return Color(0.1f + 0.1f * f, 0.4f + 0.4f * f, 0.7f + 0.2f * f, 0.9f);
    } else if (t < 0.75f) {
        float f = (t - 0.35f) / 0.40f;
        return Color(0.85f + 0.15f * f, 0.65f - 0.25f * f, 0.15f, 0.92f);
    } else {
        float f = (t - 0.75f) / 0.25f;
        return Color(0.95f + 0.05f * f, 0.25f - 0.15f * f, 0.20f - 0.10f * f, 0.95f);
    }
}

bool NativeKeyboardHeatmap::onMouseMove(const UIMouseEvent& e) {
    m_mousePos = e.position;
    int prevHover = m_hoveredVk;
    m_hoveredVk = -1;

    float totalU = (m_layoutMode == KeyboardLayoutMode::Full104) ? 23.0f :
                   (m_layoutMode == KeyboardLayoutMode::TKL87)   ? 18.5f : 15.0f;
    float unitSize = (m_bounds.width() - 28.0f) / totalU;
    unitSize = std::clamp(unitSize, 26.0f, 38.0f);
    float chassisW = totalU * unitSize + 28.0f;
    float chassisLeft = m_bounds.left + std::max(0.0f, (m_bounds.width() - chassisW) * 0.5f);
    float chassisH = 24.0f + 6.75f * unitSize;
    Rect chassis(chassisLeft, m_bounds.top + 38.0f, chassisLeft + chassisW, m_bounds.top + 38.0f + chassisH);

    if (chassis.contains(e.position)) {
        for (const auto& k : m_keycaps) {
            if (m_layoutMode == KeyboardLayoutMode::Compact60 && k.x >= 15.0f) continue;
            if (m_layoutMode == KeyboardLayoutMode::TKL87 && k.x >= 18.5f) continue;

            Rect r = getKeyRect(k, chassis, unitSize);
            if (r.contains(e.position)) {
                m_hoveredVk = k.vkCode;
                break;
            }
        }
    }

    if (m_hoveredVk != prevHover) {
        markNeedsPaint();
    }
    return true;
}

void NativeKeyboardHeatmap::onMouseLeave() {
    if (m_hoveredVk != -1) {
        m_hoveredVk = -1;
        markNeedsPaint();
    }
}

bool NativeKeyboardHeatmap::onMouseDown(const UIMouseEvent& e) {
    // 检查布局规格分段切换按钮
    Rect header(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 30.0f);
    float btnX = header.right - 220.0f;
    Rect btn104(btnX, header.top, btnX + 70.0f, header.bottom);
    Rect btn87(btnX + 74.0f, header.top, btnX + 144.0f, header.bottom);
    Rect btn60(btnX + 148.0f, header.top, btnX + 218.0f, header.bottom);

    if (btn104.contains(e.position)) { setLayoutMode(KeyboardLayoutMode::Full104); return true; }
    if (btn87.contains(e.position)) { setLayoutMode(KeyboardLayoutMode::TKL87); return true; }
    if (btn60.contains(e.position)) { setLayoutMode(KeyboardLayoutMode::Compact60); return true; }

    return false;
}

void NativeKeyboardHeatmap::render(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 1. 顶部标题栏与布局规格切换胶囊 ──────────────────────────────────────
    Rect header(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 30.0f);
    VectorIconRenderer::drawIcon(ctx, IconType::Keyboard, Rect(header.left + 2.0f, header.top + 6.0f, header.left + 18.0f, header.top + 22.0f), p.primary, 1.5f);

    std::wstring modeTitle = (m_layoutMode == KeyboardLayoutMode::Full104) ? L"键盘热力分布 (104 键全尺寸)" :
                             (m_layoutMode == KeyboardLayoutMode::TKL87)   ? L"键盘热力分布 (87 键 TKL)" :
                                                                             L"键盘热力分布 (60 键紧凑)";
    ctx.drawText(modeTitle, Point(header.left + 24.0f, header.top + 6.0f), FontToken::Sm, FontWeight::SemiBold, p.text);

    float btnX = header.right - 220.0f;
    auto drawSegBtn = [&](const std::wstring& text, float x, bool active) {
        Rect r(x, header.top, x + 68.0f, header.bottom);
        if (active) {
            ctx.fillRoundedRect(r, 4.0f, Color(p.primary.r, p.primary.g, p.primary.b, isDark ? 0.25f : 0.15f));
            ctx.drawRoundedRect(r, 4.0f, p.primary, 1.2f);
            ctx.drawText(text, Point(x + 10.0f, header.top + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.primary);
        } else {
            ctx.fillRoundedRect(r, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.04f) : Color(0.0f, 0.0f, 0.0f, 0.04f));
            ctx.drawRoundedRect(r, 4.0f, p.border, 1.0f);
            ctx.drawText(text, Point(x + 10.0f, header.top + 5.0f), FontToken::Xs, FontWeight::Regular, p.textMuted);
        }
    };
    drawSegBtn(L"104 全键", btnX, m_layoutMode == KeyboardLayoutMode::Full104);
    drawSegBtn(L"87 TKL", btnX + 74.0f, m_layoutMode == KeyboardLayoutMode::TKL87);
    drawSegBtn(L"60 极简", btnX + 148.0f, m_layoutMode == KeyboardLayoutMode::Compact60);

    // ── 2. 机械键盘底盘盘体 (Keyboard Chassis) ──────────────────────────────
    float totalU = (m_layoutMode == KeyboardLayoutMode::Full104) ? 23.0f :
                   (m_layoutMode == KeyboardLayoutMode::TKL87)   ? 18.5f : 15.0f;
    float unitSize = (m_bounds.width() - 28.0f) / totalU;
    unitSize = std::clamp(unitSize, 26.0f, 38.0f);
    float chassisW = totalU * unitSize + 28.0f;
    float chassisLeft = m_bounds.left + std::max(0.0f, (m_bounds.width() - chassisW) * 0.5f);
    float chassisH = 24.0f + 6.75f * unitSize;

    Rect chassis(chassisLeft, m_bounds.top + 38.0f, chassisLeft + chassisW, m_bounds.top + 38.0f + chassisH);
    Color chassisBg = isDark ? Color(0.08f, 0.09f, 0.13f, 0.95f) : Color(0.95f, 0.96f, 0.98f, 0.95f);
    ctx.fillRoundedRect(chassis, 8.0f, chassisBg);
    ctx.drawRoundedRect(chassis, 8.0f, p.border, 1.0f);

    // 小键盘状态 LED 灯 (104 模式下放置在 Row 0 上方数字小键盘区上方)
    if (m_layoutMode == KeyboardLayoutMode::Full104) {
        float ledX = chassis.left + 14.0f + 19.0f * unitSize + 4.0f;
        float ledY = chassis.top + 16.0f;
        auto drawLed = [&](const wchar_t* name, bool on, float lx) {
            Color ledCol = on ? Color(0.2f, 0.85f, 0.4f, 1.0f) : (isDark ? Color(0.25f, 0.28f, 0.35f, 0.6f) : Color(0.7f, 0.75f, 0.8f, 0.6f));
            ctx.fillCircle(Point(lx + 4.0f, ledY + 4.0f), 3.0f, ledCol);
            ctx.drawText(name, Point(lx + 10.0f, ledY - 2.0f), FontToken::Xs, FontWeight::Medium, p.textMuted);
        };
        drawLed(L"NUM", m_numLock, ledX);
        drawLed(L"CAPS", m_capsLock, ledX + 44.0f);
        drawLed(L"SCRL", m_scrollLock, ledX + 88.0f);
    }

    // ── 3. 键帽阵列渲染 ──────────────────────────────────────────────────────
    const KeyCapDef* hoveredDef = nullptr;
    Rect hoveredRect;

    for (const auto& k : m_keycaps) {
        if (m_layoutMode == KeyboardLayoutMode::Compact60 && k.x >= 15.0f) continue;
        if (m_layoutMode == KeyboardLayoutMode::TKL87 && k.x >= 18.5f) continue;

        Rect r = getKeyRect(k, chassis, unitSize);
        auto it = m_hits.find(k.vkCode);
        uint32_t hits = (it != m_hits.end()) ? it->second : 0;

        Color keyBg = calculateHeatColor(hits, m_maxHits, isDark);
        bool isHov = (m_hoveredVk == k.vkCode);

        if (isHov) {
            hoveredDef = &k;
            hoveredRect = r;
        }

        ctx.fillRoundedRect(r, 3.5f, keyBg);
        ctx.drawRoundedRect(r, 3.5f, isHov ? p.primary : p.border, isHov ? 2.0f : 1.0f);

        // 键帽字符渲染：统一通过带边界裁剪与居中对齐的 DirectWrite 渲染，杜绝任何字符溢出碰撞
        Color textCol = (hits > 0 || isHov) ? Color(1.0f, 1.0f, 1.0f, 0.95f) : p.textSecondary;
        std::wstring dispLabel = k.label;
        if (k.id == "np_enter") {
            dispLabel = L"↵";
        }
        ctx.drawText(dispLabel, r, textCol, FontToken::Xs, FontWeight::SemiBold,
                     FontFamilyType::Sans, TextAlignmentH::Center, TextAlignmentV::Center,
                     false, true);
    }

    // ── 4. 底部热力能谱图例栏 & 响应式状态微晶胶囊 ──────────────────────────
    Rect footer(chassis.left, chassis.bottom + 12.0f, chassis.right, chassis.bottom + 44.0f);
    ctx.drawText(L"空闲 0", Point(footer.left + 4.0f, footer.top + 6.0f), FontToken::Xs, FontWeight::Regular, p.textMuted);

    // 色谱条
    Rect specBar(footer.left + 48.0f, footer.top + 8.0f, footer.left + 160.0f, footer.top + 18.0f);
    ctx.drawRoundedRect(specBar, 3.0f, p.border, 1.0f);
    for (int step = 0; step < 10; ++step) {
        float x1 = specBar.left + step * 11.2f;
        float x2 = x1 + 11.2f;
        float val = static_cast<float>(step) / 10.0f;
        Color c = calculateHeatColor(static_cast<uint32_t>(val * 100), 100, isDark);
        ctx.fillRect(Rect(x1, specBar.top, x2, specBar.bottom), c);
    }
    ctx.drawText(L"峰值 Peak", Point(specBar.right + 8.0f, footer.top + 6.0f), FontToken::Xs, FontWeight::Regular, p.textMuted);

    // 右侧高频 / 悬浮键位响应式微晶胶囊 (自适应宽度，杜绝多标签挤压碰撞)
    std::wstring topKeyName = L"Space";
    uint32_t topCount = 0;
    for (const auto& [vk, count] : m_hits) {
        if (count > topCount) {
            topCount = count;
            for (const auto& k : m_keycaps) {
                if (k.vkCode == vk) {
                    topKeyName = k.label;
                    break;
                }
            }
        }
    }

    wchar_t statusBuf[128];
    bool isHoverActive = false;
    if (hoveredDef) {
        auto it = m_hits.find(hoveredDef->vkCode);
        uint32_t count = (it != m_hits.end()) ? it->second : 0;
        float hPct = m_totalHits > 0 ? (static_cast<float>(count) / static_cast<float>(m_totalHits) * 100.0f) : 0.0f;
        swprintf_s(statusBuf, L"%s · %u 次 (占比 %.1f%%)",
                   hoveredDef->fullName.empty() ? hoveredDef->label.c_str() : hoveredDef->fullName.c_str(),
                   count, hPct);
        isHoverActive = true;
    } else {
        float pct = m_totalHits > 0 ? (static_cast<float>(topCount) / static_cast<float>(m_totalHits) * 100.0f) : 0.0f;
        swprintf_s(statusBuf, L"今日最高频: %s (%u 次 · %.1f%%)", topKeyName.c_str(), topCount, pct);
    }

    float maxPillW = std::max(0.0f, footer.right - (specBar.right + 70.0f));
    float pillW = std::min(300.0f, std::max(200.0f, maxPillW));
    if (pillW >= 160.0f && footer.right - pillW > specBar.right + 20.0f) {
        Rect pillRect(footer.right - pillW, footer.top + 2.0f, footer.right, footer.bottom - 2.0f);
        Color pillBg = isHoverActive
            ? (isDark ? p.primary.withAlpha(0.22f) : p.primary.withAlpha(0.12f))
            : (isDark ? p.accent.withAlpha(0.22f) : p.accent.withAlpha(0.12f));
        Color pillBorder = isHoverActive ? p.primary : p.accent;

        ctx.fillRoundedRect(pillRect, 4.0f, pillBg);
        ctx.drawRoundedRect(pillRect, 4.0f, pillBorder, 1.0f);

        IconType icon = isHoverActive ? IconType::Crosshair : IconType::Flame;
        VectorIconRenderer::drawIcon(ctx, icon,
            Rect(pillRect.left + 8.0f, pillRect.top + 5.0f, pillRect.left + 22.0f, pillRect.top + 19.0f),
            pillBorder, 1.5f);

        ctx.drawText(statusBuf,
            Rect(pillRect.left + 26.0f, pillRect.top, pillRect.right - 8.0f, pillRect.bottom),
            p.text, FontToken::Xs, FontWeight::SemiBold, FontFamilyType::Sans,
            TextAlignmentH::Center, TextAlignmentV::Center, false, true);
    }
}

} // namespace tools3000::ui::native
