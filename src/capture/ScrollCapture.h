#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ScrollCapture — 长截图（滚动截图 + 图像拼接）
//
// 职责:
//   1. 自动模拟滚动并连续截图
//   2. 使用 OpenCV 特征匹配拼接相邻帧
//   3. 检测滚动到底部时自动停止
//   4. 支持手动模式（每次滚动一屏后手动确认）
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_SCROLLCAPTURE_H
#define EASYTOOLS_CAPTURE_SCROLLCAPTURE_H

#include <windows.h>
#include <opencv2/core.hpp>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <cstddef>
#include <thread>
#include <memory>
#include <filesystem>

#include "capture/CaptureBackend.h"

namespace easy::capture {

/// 长截图模式
enum class ScrollMode {
    Auto,       // 自动滚动 + 自动截图
    Manual,     // 手动滚动，每次按键截图
};

/// 长截图选项
struct ScrollCaptureOptions {
    ScrollMode mode = ScrollMode::Auto;
    int scrollDelayMs = 300;        // 每次滚动后等待（毫秒）
    int scrollAmount = 3;           // 每次滚动行数（WM_MOUSEWHEEL delta 倍数）
    int maxFrames = 50;             // 最大帧数（防止无限滚动）
    std::size_t maxOutputBytes = 256ull * 1024ull * 1024ull; // 最终图像字节预算
    // 采集分段在内存中的上限。超过后以原始像素分段写入当前用户的临时目录，
    // 完成时顺序回读，避免同时持有整套分段和最终拼接图。
    std::size_t maxInMemoryStagingBytes = 32ull * 1024ull * 1024ull;
    // Empty selects the current user's Windows temp directory. Tests and
    // managed hosts may supply an existing private directory to make disk-full
    // and permission failures deterministic; it is never created implicitly.
    std::filesystem::path stagingDirectory;
    int overlapPercent = 20;        // 相邻帧重叠区域百分比
    RECT captureRect{};             // 截图区域（屏幕坐标）
};

/// 长截图结果
struct ScrollCaptureResult {
    bool success = false;
    cv::Mat stitchedImage;          // 拼接后的完整图像
    int frameCount = 0;             // 实际捕获帧数
    std::string errorMessage;
};

/// 进度回调仅传递最近的有界预览，避免每帧重建整张长图。
using ScrollProgressCallback = std::function<void(const cv::Mat& recentPreview, int currentFrame)>;

class ScrollCapture {
public:
    static ScrollCapture& instance();

    /// 开始长截图
    void start(const ScrollCaptureOptions& options);

    /// 停止（手动停止或自动检测完成）
    void stop();

    /// 应用退出时取消并回收，不触发完成回调。
    void shutdown();

    /// 手动模式下，用户按键触发截取当前帧
    void captureCurrentFrame();

    /// 设置进度回调
    void setProgressCallback(ScrollProgressCallback cb) { m_progressCb = std::move(cb); }

    /// 设置完成回调
    void setCompletionCallback(std::function<void(const ScrollCaptureResult&)> cb) { m_completionCb = std::move(cb); }

    /// 是否正在运行
    bool isRunning() const { return m_running.load(); }

private:
    ScrollCapture() = default;
    ~ScrollCapture() {
        // 退出时回收滚动线程: 单例析构时若 m_scrollThread 仍 joinable 会触发 std::terminate。
        // 先置 false 让 autoScrollLoop 尽快跳出循环, 再 join 等其收尾(UAF 安全)。
        m_running = false;
        if (m_scrollThread.joinable()) m_scrollThread.join();
    }

    /// 自动滚动线程
    void autoScrollLoop();

    /// 截取当前区域
    cv::Mat captureRegion(const RECT& rect);

    /// 检测是否到达底部（与上一帧几乎相同）
    bool isAtBottom(const cv::Mat& current, const cv::Mat& previous) const;

    /// 拼接所有帧
    cv::Mat stitchFrames(const std::vector<cv::Mat>& frames, int overlapPercent) const;

    /// 找两帧之间的最佳拼接线（特征匹配）
    int findStitchOffset(const cv::Mat& upper, const cv::Mat& lower) const;

    void appendFrame(const cv::Mat& frame);
    cv::Mat buildStitchedImage() const;
    cv::Mat buildRecentPreview(int maxRows) const;
    void deliverCompletion(const std::string& error = {});
    bool spillResidentSegments();
    bool appendSpilledSegment(const cv::Mat& segment);
    void clearStorage();

    struct SpilledSegment {
        std::uint64_t offset = 0;
        int rows = 0;
    };

    std::atomic<bool> m_running{false};
    std::thread m_scrollThread;
    ScrollCaptureOptions m_options;
    std::vector<cv::Mat> m_segments;
    std::size_t m_segmentBytes = 0;
    std::size_t m_residentSegmentBytes = 0;
    std::vector<SpilledSegment> m_spilledSegments;
    std::filesystem::path m_spillPath;
    std::uint64_t m_spillWriteOffset = 0;
    int m_segmentWidth = 0;
    int m_segmentType = -1;
    std::string m_terminalError;
    cv::Mat m_lastFrame;
    std::unique_ptr<ICaptureBackend> m_captureBackend;
    int m_frameCount = 0;
    std::atomic<bool> m_completionDelivered{false};
    std::atomic<bool> m_suppressCompletion{false};
    ScrollProgressCallback m_progressCb;
    std::function<void(const ScrollCaptureResult&)> m_completionCb;
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_SCROLLCAPTURE_H
