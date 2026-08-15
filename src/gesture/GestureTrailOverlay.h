#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureTrailOverlay — 手势轨迹可视化覆盖层
//
// 职责:
//   1. 创建全屏透明 Layered Window
//   2. 使用 Direct2D 绘制鼠标移动轨迹（彩色渐变线段）
//   3. 手势完成后显示识别结果文字
//   4. 自动淡出消失
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_TRAILOVERLAY_H
#define EASYTOOLS_GESTURE_TRAILOVERLAY_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

namespace easy::gesture {

/// 轨迹点
struct TrailPoint {
    float x;
    float y;
    DWORD timestamp;  // GetTickCount64 时间戳
};

/// 轨迹样式配置
struct TrailStyle {
    float lineWidth     = 3.0f;     // 线条宽度
    float startOpacity  = 0.9f;     // 起点不透明度
    float endOpacity    = 0.2f;     // 终点不透明度（渐隐）
    float fadeOutMs     = 350.0f;   // 淡出时间(毫秒)
    uint32_t lineColor  = 0x7C3AED; // 线条颜色 (RGB, 紫色)
    uint32_t resultBg   = 0x000000; // 结果背景色
    float resultFontSize = 20.0f;   // 结果文字大小 (按键回显质感尺寸)
};

class GestureTrailOverlay {
public:
    static GestureTrailOverlay& instance();

    /// 初始化（在主线程调用，创建窗口和 D2D 资源）
    bool initialize(HINSTANCE hInstance);

    /// 关闭（销毁窗口和资源）
    void shutdown();

    /// 开始新的一次手势轨迹
    void beginTrail();

    /// 添加轨迹点
    void addPoint(float x, float y);

    /// 实时更新当前手势识别到的动作名称（按键回显风格）
    void setLiveAction(const std::string& actionText);

    /// 结束手势轨迹，显示识别结果（如 "← 后退"）
    void endTrail(const std::string& resultText = "");

    /// 创建 Direct2D 资源 (按需初始化)
    bool createD2DResources();

    /// 释放 D2D 资源与大尺寸虚拟屏幕位图 (深度释放内存)
    void releaseD2DResources();

    /// 清空画布 (提交全透明帧)
    void clearCanvas();

    /// 立即隐藏
    void hide();

    /// 设置样式
    void setStyle(const TrailStyle& style);

    /// 是否正在显示
    bool isVisible() const { return m_visible.load(); }

private:
    GestureTrailOverlay() = default;
    ~GestureTrailOverlay() = default;
    GestureTrailOverlay(const GestureTrailOverlay&) = delete;
    GestureTrailOverlay& operator=(const GestureTrailOverlay&) = delete;

    /// 创建 Layered Window
    bool createOverlayWindow(HINSTANCE hInstance);
    bool updateTextFormat(float dpiScale);

    /// 渲染帧
    void render();

    /// 淡出定时器回调
    void startFadeOut();

    /// 窗口过程
    static LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_helperOwnerHwnd = nullptr;
    HWND m_hwnd = nullptr;
    TrailStyle m_style;
    std::atomic<bool> m_visible{false};
    bool m_fading = false;
    float m_fadeAlpha = 1.0f;
    DWORD m_fadeStartTick = 0;
    std::atomic<bool> m_renderTimerActive{false};

    // 虚拟屏幕原点: 轨迹点是绝对屏幕坐标, 覆盖层左上角对应此原点, 绘制时需减去
    int m_originX = 0;
    int m_originY = 0;

    // 轨迹数据
    std::mutex m_trailMutex;
    std::vector<TrailPoint> m_points;
    std::string m_resultText;

    // 缓存每段的贝塞尔曲线 PathGeometry 以提升性能
    std::vector<Microsoft::WRL::ComPtr<ID2D1PathGeometry>> m_pathCache;

    // Direct2D 资源
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;
    HDC m_memoryDC = nullptr;
    HBITMAP m_memoryBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_width = 0;
    int m_height = 0;
    float m_dpiScale = 1.0f;
    float m_textScale = 0.0f;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_lineBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBorderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> m_strokeStyle;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_TRAILOVERLAY_H
