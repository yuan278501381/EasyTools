#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeGestureDrawCanvas.h — Tools3000 原生手势平滑画板与笔画识别中枢
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEGESTUREDRAWCANVAS_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEGESTUREDRAWCANVAS_H

#include "ui/native/core/UIElement.h"
#include <vector>
#include <string>
#include <functional>

namespace tools3000::ui::native {

struct GesturePreset {
    std::string code;
    std::wstring label;
    std::wstring arrows;
};

class NativeGestureDrawCanvas : public UIElement {
public:
    NativeGestureDrawCanvas();
    virtual ~NativeGestureDrawCanvas() override;

    /// 获取当前识别得到的手势代码（如 "DR", "U", "LR"）
    const std::string& getGestureCode() const { return m_gestureCode; }

    /// 设置预设手势代码
    void setGestureCode(const std::string& code);

    /// 手势识别或预设选择回调
    void setOnGestureChanged(std::function<void(const std::string& code)> cb) {
        m_onGestureChanged = std::move(cb);
    }

    /// 清空画板
    void clearCanvas();

    /// 诊断与单测接口
    size_t getPointCount() const { return m_points.size(); }
    const std::vector<Point>& getPoints() const { return m_points; }

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 鼠标手势交互 ────────────────────────────────────────────────────────
    virtual bool onMouseDown(const UIMouseEvent& e) override;
    virtual bool onMouseMove(const UIMouseEvent& e) override;
    virtual bool onMouseUp(const UIMouseEvent& e) override;

    static std::wstring codeToArrows(const std::string& code);

private:
    void recognizeStroke();
    void rebuildPresetButtons();
    Rect getCockpitRect() const;
    Rect getCanvasPadRect() const;
    Rect getPresetTrayRect() const;

private:
    std::vector<Point> m_points;
    bool m_isDrawing = false;
    std::string m_gestureCode;
    std::string m_detectedTrigger = "right"; // right, middle, x1, x2, left
    std::string m_detectedEdge = "none";    // none (global), top, bottom

    // 物理键盘修饰键状态
    bool m_modCtrl = false;
    bool m_modShift = false;
    bool m_modAlt = false;

    // 动画脉冲
    float m_animTime = 0.0f;

    std::vector<GesturePreset> m_presets;
    std::function<void(const std::string& code)> m_onGestureChanged;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEGESTUREDRAWCANVAS_H
