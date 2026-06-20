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

    /// 关闭此贴图窗口
    void close();

    /// 设置透明度 (0.0~1.0)
    void setOpacity(float opacity);

    /// 设置缩放比例
    void setScale(float scale);

    /// 是否存活
    bool isAlive() const { return m_hwnd != nullptr; }

    ~PinWindow();

    // ── 全局管理 ─────────────────────────────────────────────────────────

    /// 关闭所有贴图窗口
    static void closeAll();

    /// 当前贴图数量
    static size_t count() { return s_instances.size(); }

private:
    PinWindow() = default;
    PinWindow(const PinWindow&) = delete;
    PinWindow& operator=(const PinWindow&) = delete;

    bool initWindow(HINSTANCE hInstance, int x, int y, int w, int h);
    bool createRenderResources(const cv::Mat& image);
    void render();

    static LRESULT CALLBACK pinWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    float m_opacity = 1.0f;
    float m_scale = 1.0f;
    int m_origWidth = 0;
    int m_origHeight = 0;
    bool m_isDragging = false;
    POINT m_dragOffset{};

    // D2D
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_bitmap;

    // 全局实例管理
    static std::vector<std::shared_ptr<PinWindow>> s_instances;
    static bool s_classRegistered;
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_PINWINDOW_H
