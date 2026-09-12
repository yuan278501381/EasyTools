#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NativeKeyboardHeatmap.h — Tools3000 纯原生 104 键机械键盘热力学全景组件
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEKEYBOARDHEATMAP_H
#define TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEKEYBOARDHEATMAP_H

#include "ui/native/core/UIElement.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace tools3000::ui::native {

enum class KeyboardLayoutMode {
    Full104 = 0,   // 104 键全尺寸机械键盘
    TKL87,         // 87 键竞技无小键盘
    Compact60      // 60 键极简便携
};

struct KeyCapDef {
    std::string id;
    std::wstring label;
    std::wstring fullName;
    int vkCode = 0;
    float widthU = 1.0f;
    float heightU = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t hits = 0;
};

class NativeKeyboardHeatmap : public UIElement {
public:
    NativeKeyboardHeatmap();
    virtual ~NativeKeyboardHeatmap() override;

    /// 设置键盘规格布局
    void setLayoutMode(KeyboardLayoutMode mode);
    KeyboardLayoutMode getLayoutMode() const { return m_layoutMode; }

    /// 注入按键击键频次统计
    void setKeyHits(int vkCode, uint32_t hits);
    void setAllKeyHits(const std::unordered_map<int, uint32_t>& hitMap);
    void clearHits();

    /// 诊断与单测接口
    uint32_t getKeyHits(int vkCode) const {
        auto it = m_hits.find(vkCode);
        return it != m_hits.end() ? it->second : 0;
    }
    uint32_t getMaxHits() const { return m_maxHits; }
    uint32_t getTotalHits() const { return m_totalHits; }

    // ── UIElement 生命周期 ──────────────────────────────────────────────────
    virtual Size measure(float availableWidth, float availableHeight, UIRenderContext& ctx) override;
    virtual void layout(const Rect& bounds, UIRenderContext& ctx) override;
    virtual void render(UIRenderContext& ctx) override;
    virtual bool update(float dt) override;

    // ── 交互事件 ────────────────────────────────────────────────────────────
    virtual bool onMouseMove(const UIMouseEvent& e) override;
    virtual void onMouseLeave() override;
    virtual bool onMouseDown(const UIMouseEvent& e) override;

private:
    void initKeycaps();
    Color calculateHeatColor(uint32_t hits, uint32_t maxHits, bool isDark) const;
    Rect getKeyRect(const KeyCapDef& k, const Rect& chassisRect, float unitSize) const;

private:
    KeyboardLayoutMode m_layoutMode = KeyboardLayoutMode::Full104;
    std::vector<KeyCapDef> m_keycaps;
    std::unordered_map<int, uint32_t> m_hits;
    uint32_t m_maxHits = 1;
    uint32_t m_totalHits = 0;

    // 悬浮键位指示
    int m_hoveredVk = -1;
    Point m_mousePos;

    // LED 指示灯硬件状态
    bool m_numLock = false;
    bool m_capsLock = false;
    bool m_scrollLock = false;
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_COMPONENTS_NATIVEKEYBOARDHEATMAP_H
