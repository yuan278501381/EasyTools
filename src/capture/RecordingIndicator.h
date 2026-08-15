#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RecordingIndicator — 录制状态指示器（悬浮工具条）
//
// 职责:
//   1. 录制中在屏幕顶部显示悬浮状态条
//   2. 显示录制时间、帧数、暂停/停止按钮
//   3. 红色圆点闪烁动画提示录制中
//   4. 可拖拽移动
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_RECORDINGINDICATOR_H
#define EASYTOOLS_CAPTURE_RECORDINGINDICATOR_H

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <functional>

namespace easy::capture {

/// 指示器按钮事件
using IndicatorAction = std::function<void()>;

class RecordingIndicator {
public:
    static RecordingIndicator& instance();

    /// 初始化
    bool initialize(HINSTANCE hInstance);

    /// 关闭
    void shutdown();

    /// 显示指示器
    void show();

    /// 隐藏指示器
    void hide();

    /// 更新录制时间和帧数
    void update(double durationSec, int frameCount);

    /// 设置暂停状态
    void setPaused(bool paused) { m_paused = paused; }

    /// 设置回调
    void onPause(IndicatorAction action) { m_onPause = std::move(action); }
    void onStop(IndicatorAction action) { m_onStop = std::move(action); }
    void onSystemAudioMute(IndicatorAction action) { m_onSystemAudioMute = std::move(action); }
    void onMicrophoneMute(IndicatorAction action) { m_onMicrophoneMute = std::move(action); }

private:
    RecordingIndicator() = default;

    bool createWindow(HINSTANCE hInstance);
    bool createRenderResources();
    void releaseRenderResources();
    void render();

    /// 格式化时间为 MM:SS
    std::wstring formatDuration(double seconds) const;

    static LRESULT CALLBACK indicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    bool m_paused = false;
    double m_duration = 0.0;
    int m_frames = 0;
    float m_systemAudioPeak = 0.0f;
    float m_microphonePeak = 0.0f;
    bool m_systemAudioActive = false;
    bool m_microphoneActive = false;
    bool m_systemAudioMuted = false;
    bool m_microphoneMuted = false;
    bool m_countingDown = false;
    int m_countdownRemaining = 0;
    bool m_storageWarning = false;
    std::int64_t m_estimatedRemainingSec = -1;
    bool m_performanceLimited = false;
    double m_effectiveFps = 0.0;
    bool m_isDragging = false;
    POINT m_dragOffset{};
    DWORD m_blinkTick = 0;    // 红点闪烁计时
    float m_dpiScale = 1.0f;

    // 按钮区域
    RECT m_pauseBtn{};
    RECT m_stopBtn{};
    RECT m_systemAudioBtn{};
    RECT m_microphoneBtn{};

    IndicatorAction m_onPause;
    IndicatorAction m_onStop;
    IndicatorAction m_onSystemAudioMute;
    IndicatorAction m_onMicrophoneMute;

    // D2D
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_textBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_redDotBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_btnHoverBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_audioBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_textFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_btnTextFormat;
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_RECORDINGINDICATOR_H
