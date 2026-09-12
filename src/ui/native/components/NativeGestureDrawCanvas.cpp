#include "ui/native/components/NativeGestureDrawCanvas.h"
#include "ui/native/common/UITheme.h"
#include "ui/native/graphics/GlassmorphismRenderer.h"
#include "ui/native/graphics/VectorIconRenderer.h"
#include <cmath>
#include <algorithm>

namespace tools3000::ui::native {

NativeGestureDrawCanvas::NativeGestureDrawCanvas() {
    layoutParams().minHeight = 320.0f;
    layoutParams().flexGrow = 1.0f;

    m_presets = {
        { "DR", L"关闭窗口", L"↘" },
        { "U",  L"滚动顶部", L"↑" },
        { "D",  L"滚动底部", L"↓" },
        { "LR", L"下一标签", L"←→" },
        { "RL", L"上一标签", L"→←" },
        { "R",  L"前进",     L"→" },
        { "L",  L"后退",     L"←" }
    };
}

NativeGestureDrawCanvas::~NativeGestureDrawCanvas() = default;

void NativeGestureDrawCanvas::setGestureCode(const std::string& code) {
    m_gestureCode = code;
    markNeedsPaint();
    if (m_onGestureChanged) {
        m_onGestureChanged(m_gestureCode);
    }
}

void NativeGestureDrawCanvas::clearCanvas() {
    m_points.clear();
    m_isDrawing = false;
    m_gestureCode.clear();
    m_detectedEdge = "none";
    markNeedsPaint();
}

std::wstring NativeGestureDrawCanvas::codeToArrows(const std::string& code) {
    std::wstring arrows;
    for (size_t i = 0; i < code.size(); ) {
        if (i + 1 < code.size()) {
            std::string pair = code.substr(i, 2);
            if (pair == "UR") { arrows += L"↗"; i += 2; continue; }
            if (pair == "UL") { arrows += L"↖"; i += 2; continue; }
            if (pair == "DR") { arrows += L"↘"; i += 2; continue; }
            if (pair == "DL") { arrows += L"↙"; i += 2; continue; }
        }
        char c = code[i];
        if (c == 'U') arrows += L"↑";
        else if (c == 'D') arrows += L"↓";
        else if (c == 'L') arrows += L"←";
        else if (c == 'R') arrows += L"→";
        else arrows += static_cast<wchar_t>(c);
        i++;
    }
    return arrows;
}

Rect NativeGestureDrawCanvas::getCockpitRect() const {
    return Rect(m_bounds.left, m_bounds.top, m_bounds.right, m_bounds.top + 38.0f);
}

Rect NativeGestureDrawCanvas::getCanvasPadRect() const {
    return Rect(m_bounds.left, m_bounds.top + 42.0f, m_bounds.right, m_bounds.bottom - 44.0f);
}

Rect NativeGestureDrawCanvas::getPresetTrayRect() const {
    return Rect(m_bounds.left, m_bounds.bottom - 40.0f, m_bounds.right, m_bounds.bottom);
}

Size NativeGestureDrawCanvas::measure(float availableWidth, float availableHeight, UIRenderContext& ctx) {
    (void)ctx;
    (void)availableHeight;
    float w = availableWidth > 0 ? availableWidth : 680.0f;
    m_desiredSize = Size(w, 340.0f);
    return m_desiredSize;
}

void NativeGestureDrawCanvas::layout(const Rect& bounds, UIRenderContext& ctx) {
    UIElement::layout(bounds, ctx);
}

bool NativeGestureDrawCanvas::update(float dt) {
    m_animTime += dt;

    // 持续同步物理键盘修饰键状态
    m_modCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    m_modShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    m_modAlt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    return m_isDrawing;
}

void NativeGestureDrawCanvas::recognizeStroke() {
    if (m_points.size() < 2) return;

    std::string directions;
    Point last = m_points[0];
    const float minThreshold = 18.0f;

    for (size_t i = 1; i < m_points.size(); ++i) {
        Point curr = m_points[i];
        float dx = curr.x - last.x;
        float dy = curr.y - last.y;
        float dist = std::hypot(dx, dy);

        if (dist >= minThreshold) {
            float angle = std::atan2(dy, dx) * 180.0f / 3.14159265f;
            std::string dir;

            if (angle >= -22.5f && angle < 22.5f) dir = "R";
            else if (angle >= 22.5f && angle < 67.5f) dir = "DR";
            else if (angle >= 67.5f && angle < 112.5f) dir = "D";
            else if (angle >= 112.5f && angle < 157.5f) dir = "DL";
            else if (angle >= -67.5f && angle < -22.5f) dir = "UR";
            else if (angle >= -112.5f && angle < -67.5f) dir = "U";
            else if (angle >= -157.5f && angle < -112.5f) dir = "UL";
            else dir = "L";

            if (directions.empty() || directions.substr(directions.size() - dir.size()) != dir) {
                directions += dir;
            }
            last = curr;
        }
    }

    if (!directions.empty()) {
        m_gestureCode = directions;
        if (m_onGestureChanged) {
            m_onGestureChanged(m_gestureCode);
        }
    }
}

bool NativeGestureDrawCanvas::onMouseDown(const UIMouseEvent& e) {
    Rect pad = getCanvasPadRect();
    Rect cockpit = getCockpitRect();
    Rect presets = getPresetTrayRect();

    // 检查是否点击了清空按钮
    Rect clearBtn(cockpit.right - 70.0f, cockpit.top + 4.0f, cockpit.right - 4.0f, cockpit.bottom - 4.0f);
    if (clearBtn.contains(e.position)) {
        clearCanvas();
        return true;
    }

    // 检查是否点击了预设芯片
    if (presets.contains(e.position)) {
        float x = presets.left + 70.0f;
        for (const auto& p : m_presets) {
            Rect chip(x, presets.top + 4.0f, x + 72.0f, presets.bottom - 4.0f);
            if (chip.contains(e.position)) {
                setGestureCode(p.code);
                return true;
            }
            x += 78.0f;
        }
        return true;
    }

    if (pad.contains(e.position)) {
        m_points.clear();
        m_isDrawing = true;
        m_points.push_back(e.position);

        if (e.button == MouseButton::Right) m_detectedTrigger = "right";
        else if (e.button == MouseButton::Middle) m_detectedTrigger = "middle";
        else if (e.button == MouseButton::Left) m_detectedTrigger = "left";

        // 判断感应区
        if (e.position.y < pad.top + 28.0f) {
            m_detectedEdge = "top";
        } else if (e.position.y > pad.bottom - 28.0f) {
            m_detectedEdge = "bottom";
        } else {
            m_detectedEdge = "none";
        }

        markNeedsPaint();
        return true;
    }

    return false;
}

bool NativeGestureDrawCanvas::onMouseMove(const UIMouseEvent& e) {
    if (m_isDrawing) {
        Rect pad = getCanvasPadRect();
        Point pt(
            std::clamp(e.position.x, pad.left + 2.0f, pad.right - 2.0f),
            std::clamp(e.position.y, pad.top + 2.0f, pad.bottom - 2.0f)
        );
        m_points.push_back(pt);
        markNeedsPaint();
        return true;
    }
    return false;
}

bool NativeGestureDrawCanvas::onMouseUp(const UIMouseEvent& e) {
    (void)e;
    if (m_isDrawing) {
        m_isDrawing = false;
        recognizeStroke();
        markNeedsPaint();
        return true;
    }
    return false;
}

void NativeGestureDrawCanvas::render(UIRenderContext& ctx) {
    const auto& theme = UITheme::instance();
    const auto& p = theme.palette();
    bool isDark = theme.isEffectiveDark();

    // ── 1. 顶部感应驾驶舱 (Cockpit Bar) ──────────────────────────────────────
    Rect cockpit = getCockpitRect();
    ctx.drawText(L"触发按键:", Point(cockpit.left + 4.0f, cockpit.top + 8.0f),
                 FontToken::Xs, FontWeight::SemiBold, p.textMuted);

    auto drawPill = [&](const std::wstring& text, float x, float y, float w, float h, bool active, Color activeCol) {
        Rect r(x, y, x + w, y + h);
        if (active) {
            ctx.fillRoundedRect(r, 4.0f, Color(activeCol.r, activeCol.g, activeCol.b, isDark ? 0.25f : 0.15f));
            ctx.drawRoundedRect(r, 4.0f, activeCol, 1.2f);
            ctx.drawText(text, Point(x + 6.0f, y + 4.0f), FontToken::Xs, FontWeight::SemiBold, activeCol);
        } else {
            ctx.fillRoundedRect(r, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.04f) : Color(0.0f, 0.0f, 0.0f, 0.04f));
            ctx.drawRoundedRect(r, 4.0f, p.border, 1.0f);
            ctx.drawText(text, Point(x + 6.0f, y + 4.0f), FontToken::Xs, FontWeight::Regular, p.textSecondary);
        }
    };

    float px = cockpit.left + 65.0f;
    drawPill(L"右键", px, cockpit.top + 4.0f, 40.0f, 24.0f, m_detectedTrigger == "right", p.primary); px += 46.0f;
    drawPill(L"中键", px, cockpit.top + 4.0f, 40.0f, 24.0f, m_detectedTrigger == "middle", p.primary); px += 46.0f;
    drawPill(L"侧键1", px, cockpit.top + 4.0f, 46.0f, 24.0f, m_detectedTrigger == "x1", p.primary); px += 52.0f;
    drawPill(L"侧键2", px, cockpit.top + 4.0f, 46.0f, 24.0f, m_detectedTrigger == "x2", p.primary); px += 60.0f;

    ctx.drawText(L"感应区:", Point(px, cockpit.top + 8.0f), FontToken::Xs, FontWeight::SemiBold, p.textMuted);
    px += 48.0f;
    drawPill(L"全局", px, cockpit.top + 4.0f, 40.0f, 24.0f, m_detectedEdge == "none", p.accent); px += 46.0f;
    drawPill(L"顶边缘", px, cockpit.top + 4.0f, 50.0f, 24.0f, m_detectedEdge == "top", p.accent); px += 56.0f;
    drawPill(L"底边缘", px, cockpit.top + 4.0f, 50.0f, 24.0f, m_detectedEdge == "bottom", p.accent); px += 65.0f;

    // 修饰键 (Ctrl / Shift / Alt)
    drawPill(L"Ctrl", px, cockpit.top + 4.0f, 38.0f, 24.0f, m_modCtrl, p.success); px += 44.0f;
    drawPill(L"Shift", px, cockpit.top + 4.0f, 42.0f, 24.0f, m_modShift, p.success); px += 48.0f;
    drawPill(L"Alt", px, cockpit.top + 4.0f, 36.0f, 24.0f, m_modAlt, p.success);

    // 清空画板按钮
    Rect clearBtn(cockpit.right - 68.0f, cockpit.top + 4.0f, cockpit.right - 4.0f, cockpit.bottom - 6.0f);
    ctx.fillRoundedRect(clearBtn, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.05f) : Color(0.0f, 0.0f, 0.0f, 0.05f));
    ctx.drawRoundedRect(clearBtn, 4.0f, p.border, 1.0f);
    VectorIconRenderer::drawIcon(ctx, IconType::RefreshCw, Rect(clearBtn.left + 6.0f, clearBtn.top + 5.0f, clearBtn.left + 20.0f, clearBtn.top + 19.0f), p.textSecondary, 1.5f);
    ctx.drawText(L"清空", Point(clearBtn.left + 24.0f, clearBtn.top + 4.0f), FontToken::Xs, FontWeight::Medium, p.textSecondary);

    // ── 2. 手势主画板 (Drawing Pad) ──────────────────────────────────────────
    Rect pad = getCanvasPadRect();
    Color padBg = isDark ? Color(0.06f, 0.07f, 0.10f, 0.85f) : Color(0.96f, 0.97f, 0.99f, 0.85f);
    ctx.fillRoundedRect(pad, 8.0f, padBg);
    ctx.drawRoundedRect(pad, 8.0f, m_isDrawing ? p.primary : p.border, m_isDrawing ? 1.5f : 1.0f);

    // 顶部与底部边缘感应带指示
    Rect topSensor(pad.left + 2.0f, pad.top + 2.0f, pad.right - 2.0f, pad.top + 24.0f);
    if (m_detectedEdge == "top") {
        ctx.fillRoundedRect(topSensor, 6.0f, Color(p.accent.r, p.accent.g, p.accent.b, 0.18f));
        ctx.drawText(L"◰ 屏幕顶边缘手势激活中", Point(pad.left + 12.0f, pad.top + 5.0f), FontToken::Xs, FontWeight::SemiBold, p.accent);
    }

    Rect bottomSensor(pad.left + 2.0f, pad.bottom - 24.0f, pad.right - 2.0f, pad.bottom - 2.0f);
    if (m_detectedEdge == "bottom") {
        ctx.fillRoundedRect(bottomSensor, 6.0f, Color(p.accent.r, p.accent.g, p.accent.b, 0.18f));
        ctx.drawText(L"◲ 屏幕底边缘手势激活中", Point(pad.left + 12.0f, pad.bottom - 20.0f), FontToken::Xs, FontWeight::SemiBold, p.accent);
    }

    // 画板空闲水印与使用说明
    if (m_points.empty() && m_gestureCode.empty()) {
        float cx = pad.left + pad.width() * 0.5f;
        float cy = pad.top + pad.height() * 0.5f - 15.0f;
        VectorIconRenderer::drawIcon(ctx, IconType::Hand, Rect(cx - 18.0f, cy - 26.0f, cx + 18.0f, cy + 10.0f), p.textMuted, 1.5f);
        ctx.drawText(L"在此画板直接拖曳鼠标绘制手势 · 按键、起始位置与轨迹自动识别",
                     Point(cx - 180.0f, cy + 16.0f), FontToken::Sm, FontWeight::Medium, p.textSecondary);
        ctx.drawText(L"顶边缘起笔=顶边缘手势 · 底边缘起笔=底边缘手势 · 中间起笔=全局手势",
                     Point(cx - 170.0f, cy + 36.0f), FontToken::Xs, FontWeight::Regular, p.textMuted);
    }

    // 实时平滑轨迹绘制
    if (m_points.size() >= 2) {
        // 1) 柔和微晶光晕外发光 (Glow trail)
        Color glowCol(p.primary.r, p.primary.g, p.primary.b, 0.30f);
        for (size_t i = 1; i < m_points.size(); ++i) {
            ctx.drawLine(m_points[i - 1], m_points[i], glowCol, 7.0f);
        }

        // 2) 锐利核心轨迹 (Core stroke)
        Color coreCol(1.0f, 1.0f, 1.0f, 0.95f);
        for (size_t i = 1; i < m_points.size(); ++i) {
            ctx.drawLine(m_points[i - 1], m_points[i], coreCol, 2.5f);
        }

        // 3) 轨迹尾端脉冲微晶光珠
        Point tip = m_points.back();
        ctx.drawCircle(tip, 9.0f, glowCol, 2.0f);
        ctx.fillCircle(tip, 4.5f, p.primary);
        ctx.fillCircle(tip, 2.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // 识别结果悬浮卡片
    if (!m_gestureCode.empty() && !m_isDrawing) {
        std::wstring arrowStr = codeToArrows(m_gestureCode);
        Rect badge(pad.right - 140.0f, pad.bottom - 44.0f, pad.right - 12.0f, pad.bottom - 12.0f);
        ctx.fillRoundedRect(badge, 6.0f, isDark ? Color(0.12f, 0.14f, 0.20f, 0.95f) : Color(1.0f, 1.0f, 1.0f, 0.95f));
        ctx.drawRoundedRect(badge, 6.0f, p.success, 1.2f);
        VectorIconRenderer::drawIcon(ctx, IconType::CheckCircle, Rect(badge.left + 8.0f, badge.top + 8.0f, badge.left + 24.0f, badge.top + 24.0f), p.success, 1.6f);
        ctx.drawText(arrowStr, Point(badge.left + 30.0f, badge.top + 6.0f), FontToken::Lg, FontWeight::Bold, p.text);
    }

    // ── 3. 常用手势快捷预设托盘 (Preset Tray) ────────────────────────────────
    Rect presets = getPresetTrayRect();
    ctx.drawText(L"快捷预设:", Point(presets.left + 4.0f, presets.top + 10.0f),
                 FontToken::Xs, FontWeight::SemiBold, p.textMuted);

    float chX = presets.left + 70.0f;
    for (const auto& pr : m_presets) {
        bool isActive = (m_gestureCode == pr.code);
        Rect chip(chX, presets.top + 4.0f, chX + 72.0f, presets.bottom - 6.0f);
        if (isActive) {
            ctx.fillRoundedRect(chip, 4.0f, Color(p.primary.r, p.primary.g, p.primary.b, 0.25f));
            ctx.drawRoundedRect(chip, 4.0f, p.primary, 1.2f);
        } else {
            ctx.fillRoundedRect(chip, 4.0f, isDark ? Color(1.0f, 1.0f, 1.0f, 0.04f) : Color(0.0f, 0.0f, 0.0f, 0.04f));
            ctx.drawRoundedRect(chip, 4.0f, p.border, 1.0f);
        }
        std::wstring chipText = pr.arrows + L" " + pr.label;
        ctx.drawText(chipText, Point(chX + 6.0f, presets.top + 7.0f), FontToken::Xs, FontWeight::Medium,
                     isActive ? p.primary : p.textSecondary);
        chX += 78.0f;
    }
}

} // namespace tools3000::ui::native
