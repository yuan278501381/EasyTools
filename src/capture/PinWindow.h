#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// PinWindow — 贴图窗口
//
// 职责:
//   1. 将截图固定在屏幕最前面（Always-on-top）
//   2. 支持拖拽移动、鼠标滚轮缩放
//   3. 双击关闭
//   4. 右键菜单：复制/保存/关闭/透明度调节
//   5. 多个贴图窗口共存
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_PINWINDOW_H
#define EASYTOOLS_CAPTURE_PINWINDOW_H

#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <opencv2/core.hpp>
#include <vector>
#include <memory>
#include <cstdint>

namespace easy::capture {

class PinWindow : public std::enable_shared_from_this<PinWindow> {
public:
    /// 创建一个新的贴图窗口
    static std::shared_ptr<PinWindow> create(const cv::Mat& image, int x, int y);

    /// 将剪贴板内容（图像 / 文本 / #颜色）直接贴成浮空贴图，置于当前光标处。
    static std::shared_ptr<PinWindow> createFromClipboard();

    /// 关闭此贴图窗口
    void close();

    /// 设置透明度 (0.0~1.0)
    void setOpacity(float opacity);

    /// 设置缩放比例
    void setScale(float scale);

    /// 设置鼠标穿透（点透）。开启后窗口忽略所有鼠标事件，透传到下层窗口。
    void setClickThrough(bool enable);

    /// 切换光标所在贴图的鼠标穿透状态。供全局快捷键调用——
    /// 穿透窗口收不到右键，必须靠快捷键切回；且 WS_EX_TRANSPARENT 会被 WindowFromPoint 跳过，
    /// 故此处用窗口矩形包含判断定位光标下的贴图。
    static bool toggleClickThroughUnderCursor();

    /// 是否存活
    bool isAlive() const { return m_hwnd != nullptr; }

    ~PinWindow();

    // ── 全局管理 ─────────────────────────────────────────────────────────

    /// 关闭所有贴图窗口
    static void closeAll();

    /// 隐藏/显示所有贴图（在屏幕贴满时一键看桌面；隐藏后靠快捷键恢复）
    static void toggleHideAll();

    /// 整理所有贴图：归拢为从主屏左上角开始的整齐层叠堆，并恢复可见
    static void arrangeAll();

    /// 当前贴图数量
    static size_t count() { return s_instances.size(); }

private:
    PinWindow() = default;
    PinWindow(const PinWindow&) = delete;
    PinWindow& operator=(const PinWindow&) = delete;

    bool initWindow(HINSTANCE hInstance, int x, int y, int w, int h);
    bool createRenderResources(const cv::Mat& image);
    void render();
    void applyLayeredOpacity();  // 按 opacity × 穿透提示系数 应用分层透明度

    static LRESULT CALLBACK pinWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    float m_opacity = 1.0f;
    float m_scale = 1.0f;
    int m_origWidth = 0;
    int m_origHeight = 0;
    bool m_isDragging = false;
    POINT m_dragOffset{};
    bool m_clickThrough = false;
    bool m_focused = false;  // 选中态（键盘焦点）：显示高亮边框，可按 Esc 隐藏
    
    // Hover Toolbar
    bool m_isHovering = false;
    float m_hoverAlpha = 0.0f;
    uint64_t m_hoverTime = 0;
    D2D1_RECT_F m_toolbarRect = {};
    D2D1_RECT_F m_btnSaveRect = {};
    D2D1_RECT_F m_btnCloseRect = {};
    bool m_hoverSave = false;
    bool m_hoverClose = false;
    
    void updateHoverAnimation();
    void drawHoverToolbar();

    cv::Mat m_sourceImage;  // 原图副本，供"复制到剪贴板"

    // D2D
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bitmap;

    // 全局实例管理
    static std::vector<std::shared_ptr<PinWindow>> s_instances;
    static bool s_classRegistered;
    static bool s_allHidden;  // toggleHideAll 的隐藏态
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_PINWINDOW_H
