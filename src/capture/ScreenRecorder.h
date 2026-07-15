#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ScreenRecorder — 屏幕录制引擎
//
// 职责:
//   1. 通过 FFmpeg 编码录制屏幕
//   2. 支持 H.264 / H.265 / VP9 / GIF 编码
//   3. 区域录制 / 全屏录制
//   4. 录制状态管理（开始/暂停/恢复/停止）
//   5. 帧率控制
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_SCREENRECORDER_H
#define EASYTOOLS_CAPTURE_SCREENRECORDER_H

#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>
#include <mutex>
#include <vector>

namespace easy::capture {

/// 录制格式
enum class RecordFormat {
    MP4_H264,   // MP4 + H.264 编码
    MP4_H265,   // MP4 + H.265/HEVC 编码
    WebM_VP9,   // WebM + VP9 编码
    GIF,        // GIF 动图
};

/// 录制状态
enum class RecordState {
    Idle,       // 空闲
    Recording,  // 录制中
    Paused,     // 暂停
};

/// 录制选项
struct RecordOptions {
    RecordFormat format = RecordFormat::MP4_H264;
    int fps = 30;                       // 帧率
    int width = 0;                      // 0 = 自动检测
    int height = 0;
    int regionX = 0;                    // 录制区域
    int regionY = 0;
    bool fullScreen = true;             // 全屏录制
    std::string outputPath;             // 输出文件路径
    int bitrateMbps = 8;               // 视频码率 (Mbps)
};

/// 录制统计
struct RecordStats {
    int frameCount = 0;
    double durationSec = 0.0;
    int64_t fileSizeBytes = 0;
};

/// 状态变化回调
using RecordStateCallback = std::function<void(RecordState state, const RecordStats& stats)>;

class ScreenRecorder {
public:
    static ScreenRecorder& instance();

    /// 初始化
    bool initialize();

    /// 关闭
    void shutdown();

    /// 开始录制
    bool startRecording(const RecordOptions& options);

    /// 暂停录制
    void pauseRecording();

    /// 恢复录制
    void resumeRecording();

    /// 停止录制
    std::string stopRecording();

    /// 当前状态
    RecordState state() const { return m_state.load(); }

    /// 录制统计
    RecordStats stats() const;

    /// 设置状态回调
    void setStateCallback(RecordStateCallback callback);

private:
    ScreenRecorder() = default;
    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    /// 录制线程主循环
    void recordingLoop();

    /// 初始化 FFmpeg 编码器
    bool initializeEncoder(const RecordOptions& options);

    /// 清理 FFmpeg 资源
    void cleanupEncoder();

    /// 捕获一帧并编码
    bool captureAndEncodeFrame();
    bool initializeCaptureResources();
    void cleanupCaptureResources();
    bool writeAvailablePackets();

    void finalizeEncoder();
    void notifyState(RecordState state);

    /// 生成输出文件路径
    std::string generateOutputPath(RecordFormat format) const;

    /// 格式对应的文件扩展名
    static std::string formatExtension(RecordFormat format);

    std::atomic<RecordState> m_state{RecordState::Idle};
    std::thread m_recordThread;
    RecordOptions m_options;
    RecordStats m_stats;
    RecordStateCallback m_stateCallback;
    std::string m_outputPath;

    // FFmpeg 上下文（使用 void* 避免头文件污染）
    void* m_fmtCtx = nullptr;      // AVFormatContext*
    void* m_codecCtx = nullptr;    // AVCodecContext*
    void* m_stream = nullptr;      // AVStream*
    void* m_frame = nullptr;       // AVFrame*
    void* m_swsCtx = nullptr;      // SwsContext*
    void* m_packet = nullptr;      // AVPacket*
    bool m_headerWritten = false;
    int64_t m_frameIndex = 0;

    // Reused GDI capture resources. Creating a DC, bitmap, and multi-megabyte
    // pixel vector for every frame caused avoidable latency and allocation churn.
    HDC m_screenDc = nullptr;
    HDC m_memoryDc = nullptr;
    HBITMAP m_captureBitmap = nullptr;
    HGDIOBJ m_previousBitmap = nullptr;
    void* m_captureBits = nullptr;
    int m_captureStride = 0;
    mutable std::mutex m_statsMutex;
    mutable std::mutex m_callbackMutex;
    std::mutex m_controlMutex;
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_SCREENRECORDER_H
