// ─────────────────────────────────────────────────────────────────────────────
// ScreenRecorder.cpp — 屏幕录制引擎实现
//
// 录制管道:
//   可替换捕获后端 → sws_scale 转换 → avcodec_send_frame
//   → avcodec_receive_packet → av_write_frame → 输出文件
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/ScreenRecorder.h"
#include "core/logger/Logger.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"

#include <chrono>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <vector>

// FFmpeg C 头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace easy::capture {

namespace {

constexpr std::uint64_t StorageReserveBytes = 256ULL * 1024 * 1024;
constexpr std::int64_t StorageWarningSeconds = 5 * 60;

std::string ffmpegError(int error) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
    av_strerror(error, buffer.data(), buffer.size());
    return buffer.data();
}

void addEncoderCandidate(std::vector<const AVCodec*>& codecs, const AVCodec* codec) {
    if (!codec) return;
    for (const auto* existing : codecs) {
        if (existing == codec || std::strcmp(existing->name, codec->name) == 0) return;
    }
    codecs.push_back(codec);
}

std::vector<const AVCodec*> encoderCandidates(RecordFormat format) {
    std::vector<const AVCodec*> codecs;
    AVCodecID codecId = AV_CODEC_ID_H264;
    switch (format) {
        case RecordFormat::MP4_H264:
            codecId = AV_CODEC_ID_H264;
            for (const char* name : {"h264_nvenc", "h264_qsv", "h264_amf", "h264_mf",
                                     "libx264", "libopenh264", "mpeg4"}) {
                addEncoderCandidate(codecs, avcodec_find_encoder_by_name(name));
            }
            break;
        case RecordFormat::MP4_H265:
            codecId = AV_CODEC_ID_HEVC;
            for (const char* name : {"hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_mf",
                                     "libx265", "mpeg4"}) {
                addEncoderCandidate(codecs, avcodec_find_encoder_by_name(name));
            }
            break;
        case RecordFormat::WebM_VP9:
            codecId = AV_CODEC_ID_VP9;
            addEncoderCandidate(codecs, avcodec_find_encoder_by_name("libvpx-vp9"));
            break;
        case RecordFormat::GIF:
            codecId = AV_CODEC_ID_GIF;
            break;
    }
    // 对 H.264/H.265 只使用明确知道输入约束的编码器。FFmpeg 的 generic lookup
    // 在某些构建中会返回仅接收 GPU frame 的 d3d12va encoder，软件帧必然失败。
    if (format != RecordFormat::MP4_H264 && format != RecordFormat::MP4_H265) {
        addEncoderCandidate(codecs, avcodec_find_encoder(codecId));
    }
    return codecs;
}

bool isHardwareEncoder(const char* name) {
    if (!name) return false;
    return std::strstr(name, "_nvenc") || std::strstr(name, "_qsv") ||
           std::strstr(name, "_amf") || std::strstr(name, "_mf");
}

const char* formatId(RecordFormat format) {
    switch (format) {
        case RecordFormat::MP4_H264: return "mp4_h264";
        case RecordFormat::MP4_H265: return "mp4_h265";
        case RecordFormat::WebM_VP9: return "webm_vp9";
        case RecordFormat::GIF: return "gif";
    }
    return "unknown";
}

AVPixelFormat choosePixelFormat(const AVCodec* codec, RecordFormat format) {
    const bool mediaFoundation = codec && std::strstr(codec->name, "_mf") != nullptr;
    const void* configs = nullptr;
    int count = 0;
    const int result = avcodec_get_supported_config(
        nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count);
    if (result < 0 || !configs || count <= 0) {
        // FFmpeg 的 Media Foundation wrapper 没有公布静态 pix_fmts 列表；
        // Windows H.264/H.265 MFT 的软件输入格式是 NV12。退回 YUV420P 会在
        // avcodec_open2 中直接返回 EINVAL，表现为用户无法开始录屏。
        if (mediaFoundation) return AV_PIX_FMT_NV12;
        return format == RecordFormat::GIF ? AV_PIX_FMT_PAL8 : AV_PIX_FMT_YUV420P;
    }

    const auto* formats = static_cast<const AVPixelFormat*>(configs);
    const std::array<AVPixelFormat, 7> preferred = format == RecordFormat::GIF
        ? std::array<AVPixelFormat, 7>{AV_PIX_FMT_PAL8, AV_PIX_FMT_RGB8, AV_PIX_FMT_BGR8,
                                      AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0, AV_PIX_FMT_YUV420P,
                                      AV_PIX_FMT_NV12}
        : mediaFoundation
            ? std::array<AVPixelFormat, 7>{AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
                                          AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0, AV_PIX_FMT_YUV444P,
                                          AV_PIX_FMT_P010}
            : std::array<AVPixelFormat, 7>{AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_YUVJ420P,
                                          AV_PIX_FMT_BGRA, AV_PIX_FMT_BGR0, AV_PIX_FMT_YUV444P,
                                          AV_PIX_FMT_P010};
    for (const auto candidate : preferred) {
        for (int index = 0; index < count; ++index) {
            if (formats[index] == candidate) return candidate;
        }
    }
    return formats[0];
}

AVSampleFormat chooseAudioSampleFormat(const AVCodec* codec) {
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                     0, &configs, &count) >= 0 && configs && count > 0) {
        const auto* formats = static_cast<const AVSampleFormat*>(configs);
        for (const auto preferred : {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT}) {
            for (int index = 0; index < count; ++index) {
                if (formats[index] == preferred) return preferred;
            }
        }
        return formats[0];
    }
    return AV_SAMPLE_FMT_FLTP;
}

std::filesystem::path outputDirectory(const std::string& outputPath) {
    std::error_code ec;
    auto directory = std::filesystem::path(
        easy::core::WinUtils::utf8ToWstring(outputPath)).parent_path();
    if (directory.empty()) directory = std::filesystem::current_path(ec);
    return directory;
}

bool probeDirectoryWritable(const std::filesystem::path& directory, DWORD& error) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto probe = directory /
            (L".easytools-write-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt) + L".tmp");
        HANDLE file = CreateFileW(
            probe.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            error = ERROR_SUCCESS;
            return true;
        }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}

}  // namespace

ScreenRecorder& ScreenRecorder::instance() {
    static ScreenRecorder inst;
    return inst;
}

bool ScreenRecorder::initialize() {
    LOG_INFO("屏幕录制引擎已初始化");
    return true;
}

void ScreenRecorder::shutdown() {
    stopRecording();
    LOG_INFO("屏幕录制引擎已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 录制控制
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenRecorder::startRecording(const RecordOptions& options) {
    std::lock_guard controlLock(m_controlMutex);
    if (m_state != RecordState::Idle) {
        LOG_WARN("已在录制中, 请先停止当前录制");
        return false;
    }

    // 上一次若因捕获/编码错误自行退出，线程仍需 join，编码器也需封尾清理。
    if (m_recordThread.joinable()) {
        m_recordThread.join();
        finalizeEncoder();
        commitOutputFile(stats().frameCount > 0);
    }

    easy::core::TraceId::Scope scope;
    m_options = options;
    {
        std::lock_guard lock(m_statsMutex);
        m_stats = {};
    }
    m_frameIndex = 0;
    m_audioFrameIndex = 0;
    m_audioEncodingFailed = false;
    m_lastFrameTimings = {};
    m_adaptiveFrameStep = 1;
    m_overloadScore = 0;
    m_recoveryScore = 0;

    // 自动检测屏幕尺寸
    if (options.fps < 1 || options.fps > 120 ||
        options.bitrateMbps < 1 || options.bitrateMbps > 100 ||
        options.countdownSeconds < 0 || options.countdownSeconds > 10) {
        LOG_ERROR("录屏参数无效: fps={}, bitrate={}, countdown={}",
                  options.fps, options.bitrateMbps, options.countdownSeconds);
        return false;
    }

    if (options.fullScreen || options.width <= 0 || options.height <= 0) {
        m_options.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        m_options.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        m_options.regionX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        m_options.regionY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    }

    // 确保宽高为偶数（编码器要求）
    m_options.width &= ~1;
    m_options.height &= ~1;
    if (m_options.width <= 0 || m_options.height <= 0 ||
        m_options.width > 32768 || m_options.height > 32768) {
        LOG_ERROR("录屏区域无效: {}x{}", m_options.width, m_options.height);
        return false;
    }

    // 生成输出文件路径
    m_outputPath = options.outputPath.empty()
        ? generateOutputPath(options.format)
        : options.outputPath;
    m_workingOutputPath = m_outputPath + ".partial";

    // 确保输出目录存在
    auto dir = outputDirectory(m_outputPath);
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::lock_guard lock(m_statsMutex);
            m_stats.stopReason = "output_directory_unavailable";
            LOG_ERROR("无法创建录屏输出目录: path={}, error={}", m_outputPath, ec.message());
            m_workingOutputPath.clear();
            return false;
        }
    }
    {
        std::error_code ec;
        std::filesystem::remove(
            std::filesystem::path(easy::core::WinUtils::utf8ToWstring(m_workingOutputPath)), ec);
    }
    if (!preflightOutputStorage()) {
        commitOutputFile(false);
        return false;
    }

    // 音频设备失败只关闭对应音轨，视频录制仍然可以开始。
    const bool wantsAudio = m_options.captureSystemAudio || m_options.captureMicrophone;
    if (wantsAudio && m_options.format != RecordFormat::GIF) {
        const AudioCaptureOptions audioOptions{
            m_options.captureSystemAudio, m_options.captureMicrophone,
            m_options.systemAudioDeviceId, m_options.microphoneDeviceId,
            std::clamp(m_options.systemAudioVolume, 0.0f, 2.0f),
            std::clamp(m_options.microphoneVolume, 0.0f, 2.0f)
        };
        m_audioCapture.start(audioOptions);
        const auto audioStatus = m_audioCapture.status();
        m_options.captureSystemAudio = audioStatus.systemAudioActive;
        m_options.captureMicrophone = audioStatus.microphoneActive;
        std::lock_guard lock(m_statsMutex);
        m_stats.systemAudioActive = audioStatus.systemAudioActive;
        m_stats.microphoneActive = audioStatus.microphoneActive;
        m_stats.audioError = audioStatus.error;
    } else if (wantsAudio) {
        std::lock_guard lock(m_statsMutex);
        m_stats.audioError = "GIF does not support an audio track";
        m_options.captureSystemAudio = false;
        m_options.captureMicrophone = false;
    }

    // 初始化编码器
    if (!initializeEncoder(m_options)) {
        LOG_ERROR("FFmpeg 编码器初始化失败");
        m_audioCapture.stop();
        cleanupEncoder();
        commitOutputFile(false);
        return false;
    }
    m_finalizePending = true;

    const bool countingDown = m_options.countdownSeconds > 0;
    if (countingDown) m_audioCapture.setPaused(true);
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.countdownRemaining = countingDown ? m_options.countdownSeconds : 0;
    }
    m_state = countingDown ? RecordState::Countdown : RecordState::Recording;

    // 启动录制线程
    m_recordThread = std::thread([this]() {
        recordingLoop();
    });

    LOG_INFO("屏幕录制已准备: {}x{} @ {}fps, countdown={}s, format={}, output={}",
             m_options.width, m_options.height, m_options.fps,
             m_options.countdownSeconds,
             formatExtension(m_options.format), m_outputPath);

    notifyState(m_state.load());

    return true;
}

void ScreenRecorder::pauseRecording() {
    if (m_state == RecordState::Recording) {
        m_state = RecordState::Paused;
        m_audioCapture.setPaused(true);
        {
            std::lock_guard lock(m_statsMutex);
            m_stats.systemAudioPeak = 0.0f;
            m_stats.microphonePeak = 0.0f;
            m_stats.mixedAudioPeak = 0.0f;
        }
        LOG_INFO("屏幕录制已暂停, frames={}", stats().frameCount);
        notifyState(RecordState::Paused);
    }
}

void ScreenRecorder::resumeRecording() {
    if (m_state == RecordState::Paused) {
        m_audioCapture.setPaused(false);
        m_state = RecordState::Recording;
        LOG_INFO("屏幕录制已恢复");
        notifyState(RecordState::Recording);
    }
}

std::string ScreenRecorder::stopRecording() {
    std::lock_guard controlLock(m_controlMutex);
    if (m_state == RecordState::Idle && !m_recordThread.joinable()) return "";

    easy::core::TraceId::Scope scope;
    m_state = RecordState::Idle;

    // 等待录制线程结束
    if (m_recordThread.joinable()) {
        m_recordThread.join();
    }

    finalizeEncoder();

    const auto finalStats = stats();
    const bool committed = commitOutputFile(finalStats.frameCount > 0);
    std::string completedPath;
    if (committed) completedPath = m_outputPath;
    else if (finalStats.frameCount > 0 && !m_workingOutputPath.empty()) {
        completedPath = m_workingOutputPath;
    }

    LOG_INFO("屏幕录制已停止: frames={}, dropped={}, duration={:.1f}s, output={}",
             finalStats.frameCount, finalStats.droppedFrameCount,
             finalStats.durationSec, completedPath);

    notifyState(RecordState::Idle);

    return completedPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// 录制线程
// ─────────────────────────────────────────────────────────────────────────────

void ScreenRecorder::recordingLoop() {
    using clock = std::chrono::steady_clock;
    const auto frameDuration = std::chrono::nanoseconds(1'000'000'000LL / m_options.fps);
    auto nextFrameAt = clock::now();
    auto nextStorageCheck = clock::now() + std::chrono::seconds(1);
    auto nextTelemetryAt = clock::now() + std::chrono::seconds(1);
    bool wasPaused = false;
    const auto emitPipelineMetrics = [this]() {
        const auto snapshot = stats();
        if (snapshot.frameCount <= 0) return;
        auto& monitor = easy::core::PerformanceMonitor::instance();
        monitor.recordLatency("recording.capture", snapshot.captureLatencyMs);
        monitor.recordLatency("recording.conversion", snapshot.conversionLatencyMs);
        monitor.recordLatency("recording.encode", snapshot.encodeLatencyMs);
        monitor.recordLatency("recording.pipeline", snapshot.pipelineLatencyMs);
    };

    // 线程入口兜底: 任何 std 异常(如全屏帧缓冲 std::bad_alloc、日志 std::format 抛错)若逃逸出
    // 线程函数会直接 std::terminate 整个进程。这里全程兜底, 异常时干净停止录制。
    try {
        if (m_state == RecordState::Countdown) {
            const auto deadline = clock::now() + std::chrono::seconds(m_options.countdownSeconds);
            while (m_state == RecordState::Countdown) {
                const auto now = clock::now();
                if (now >= deadline) break;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now).count();
                {
                    std::lock_guard lock(m_statsMutex);
                    m_stats.countdownRemaining = static_cast<int>((remaining + 999) / 1000);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            if (m_state == RecordState::Countdown) {
                {
                    std::lock_guard lock(m_statsMutex);
                    m_stats.countdownRemaining = 0;
                }
                m_audioCapture.setPaused(false);
                m_state = RecordState::Recording;
                notifyState(RecordState::Recording);
                nextFrameAt = clock::now();
            }
        }
        if (m_state != RecordState::Idle && !initializeCaptureResources()) {
            LOG_ERROR("录屏捕获资源初始化失败");
            {
                std::lock_guard lock(m_statsMutex);
                m_stats.stopReason = "capture_initialization_failed";
            }
            m_state = RecordState::Idle;
        }
        while (m_state != RecordState::Idle) {
            if (m_state == RecordState::Paused) {
                wasPaused = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (wasPaused) {
                // 暂停时间不计入视频时间线，恢复后从“现在”继续。
                nextFrameAt = clock::now();
                wasPaused = false;
            }

            auto now = clock::now();
            if (now < nextFrameAt) {
                std::this_thread::sleep_until(nextFrameAt);
                now = clock::now();
            }

            // 捕获或编码比目标帧周期慢时，跳过已经错过的时间槽并让 PTS 留出
            // 对应间隔。这样不会形成无界帧队列，也不会让输出视频加速播放。
            int skipped = 0;
            if (now > nextFrameAt + frameDuration) {
                skipped = static_cast<int>((now - nextFrameAt) / frameDuration);
                m_frameIndex += skipped;
                nextFrameAt += frameDuration * skipped;
            }

            if (!captureAndEncodeFrame()) {
                LOG_ERROR("帧捕获/编码失败, 停止录制");
                {
                    std::lock_guard lock(m_statsMutex);
                    m_stats.stopReason = "capture_or_encode_failed";
                }
                m_state = RecordState::Idle;
                break;
            }
            if (!encodeAvailableAudio()) {
                LOG_WARN("音频编码失败, 已降级为仅视频录制");
            }
            nextFrameAt += frameDuration;
            {
                std::lock_guard lock(m_statsMutex);
                ++m_stats.frameCount;
                const double budgetMs = 1000.0 / m_options.fps;
                const bool overloadedSample = skipped > 0 ||
                    m_lastFrameTimings.totalMs > budgetMs * 0.90;
                if (overloadedSample) {
                    m_overloadScore = std::min(m_options.fps * 4,
                        m_overloadScore + std::max(1, skipped));
                    m_recoveryScore = 0;
                } else if (m_lastFrameTimings.totalMs < budgetMs * 0.65) {
                    m_overloadScore = std::max(0, m_overloadScore - 1);
                    ++m_recoveryScore;
                } else {
                    m_recoveryScore = 0;
                }

                const int overloadThreshold = std::max(8, m_options.fps / 2);
                if (m_overloadScore >= overloadThreshold && m_adaptiveFrameStep < 4) {
                    m_adaptiveFrameStep *= 2;
                    m_overloadScore = 0;
                    m_recoveryScore = 0;
                    LOG_WARN("录屏持续过载，自适应帧步长调整为 {}", m_adaptiveFrameStep);
                } else {
                    const int recoveryThreshold = std::max(30,
                        m_options.fps * 3 / m_adaptiveFrameStep);
                    if (m_adaptiveFrameStep > 1 && m_recoveryScore >= recoveryThreshold) {
                        m_adaptiveFrameStep /= 2;
                        m_recoveryScore = 0;
                        LOG_INFO("录屏负载已恢复，自适应帧步长调整为 {}", m_adaptiveFrameStep);
                    }
                }

                const int adaptiveSkipped = m_adaptiveFrameStep - 1;
                m_frameIndex += adaptiveSkipped;
                nextFrameAt += frameDuration * adaptiveSkipped;
                m_stats.droppedFrameCount += skipped + adaptiveSkipped;
                m_stats.durationSec = static_cast<double>(m_frameIndex) / m_options.fps;
                const auto smooth = [](double previous, double current) {
                    return previous <= 0.0 ? current : previous * 0.88 + current * 0.12;
                };
                m_stats.captureLatencyMs = smooth(
                    m_stats.captureLatencyMs, m_lastFrameTimings.captureMs);
                m_stats.conversionLatencyMs = smooth(
                    m_stats.conversionLatencyMs, m_lastFrameTimings.conversionMs);
                m_stats.encodeLatencyMs = smooth(
                    m_stats.encodeLatencyMs, m_lastFrameTimings.encodeMs);
                m_stats.pipelineLatencyMs = smooth(
                    m_stats.pipelineLatencyMs, m_lastFrameTimings.totalMs);
                m_stats.effectiveFps = m_stats.durationSec > 0.0
                    ? m_stats.frameCount / m_stats.durationSec : 0.0;
                m_stats.adaptiveFrameStep = m_adaptiveFrameStep;
                m_stats.performanceLimited = m_adaptiveFrameStep > 1 ||
                    m_overloadScore >= overloadThreshold / 2;
            }
            if (clock::now() >= nextStorageCheck) {
                if (!updateOutputStorageStatus()) {
                    LOG_ERROR("录屏因磁盘安全余量不足而停止");
                    m_state = RecordState::Idle;
                    break;
                }
                nextStorageCheck = clock::now() + std::chrono::seconds(1);
            }
            if (clock::now() >= nextTelemetryAt) {
                emitPipelineMetrics();
                nextTelemetryAt = clock::now() + std::chrono::seconds(1);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("录制线程异常, 已停止录制: {}", e.what());
        {
            std::lock_guard lock(m_statsMutex);
            m_stats.stopReason = "recording_thread_exception";
        }
        m_state = RecordState::Idle;
    } catch (...) {
        LOG_ERROR("录制线程未知异常, 已停止录制");
        {
            std::lock_guard lock(m_statsMutex);
            m_stats.stopReason = "recording_thread_exception";
        }
        m_state = RecordState::Idle;
    }

    emitPipelineMetrics();
    cleanupCaptureResources();

    if (m_state == RecordState::Idle) notifyState(RecordState::Idle);
}

void ScreenRecorder::setAudioVolumes(float systemVolume, float microphoneVolume) {
    m_audioCapture.setVolumes(systemVolume, microphoneVolume);
}

bool ScreenRecorder::toggleSystemAudioMuted() {
    bool muted = false;
    {
        std::lock_guard lock(m_statsMutex);
        if (!m_stats.systemAudioActive || m_state == RecordState::Idle) return false;
        muted = !m_stats.systemAudioMuted;
    }
    m_audioCapture.setSystemMuted(muted);
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.systemAudioMuted = muted;
        if (muted) m_stats.systemAudioPeak = 0.0f;
    }
    return true;
}

bool ScreenRecorder::toggleMicrophoneMuted() {
    bool muted = false;
    {
        std::lock_guard lock(m_statsMutex);
        if (!m_stats.microphoneActive || m_state == RecordState::Idle) return false;
        muted = !m_stats.microphoneMuted;
    }
    m_audioCapture.setMicrophoneMuted(muted);
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.microphoneMuted = muted;
        if (muted) m_stats.microphonePeak = 0.0f;
    }
    return true;
}

RecordStats ScreenRecorder::stats() const {
    std::lock_guard lock(m_statsMutex);
    return m_stats;
}

RecordingCapabilities ScreenRecorder::capabilities() {
    RecordingCapabilities result;
    result.captureBackends = captureBackendCapabilities();
    result.audioDevices = AudioCapture::devices();
    for (const auto format : {RecordFormat::MP4_H264, RecordFormat::MP4_H265,
                              RecordFormat::WebM_VP9, RecordFormat::GIF}) {
        for (const auto* codec : encoderCandidates(format)) {
            result.encoders.push_back({formatId(format), codec->name,
                                       isHardwareEncoder(codec->name)});
        }
    }
    return result;
}

void ScreenRecorder::setStateCallback(RecordStateCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_stateCallback = std::move(callback);
}

void ScreenRecorder::notifyState(RecordState state) {
    RecordStateCallback callback;
    {
        std::lock_guard lock(m_callbackMutex);
        callback = m_stateCallback;
    }
    if (callback) callback(state, stats());
}

void ScreenRecorder::finalizeEncoder() {
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    auto* audioCodecCtx = static_cast<AVCodecContext*>(m_audioCodecCtx);
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    m_audioCapture.stop();
    encodeAvailableAudio();
    if (codecCtx && m_headerWritten) {
        avcodec_send_frame(codecCtx, nullptr);
        writeAvailablePackets();
    }
    if (audioCodecCtx && m_headerWritten) {
        avcodec_send_frame(audioCodecCtx, nullptr);
        writeAvailableAudioPackets();
    }
    if (fmtCtx && m_headerWritten) av_write_trailer(fmtCtx);
    cleanupEncoder();
    m_finalizePending = false;
}

bool ScreenRecorder::commitOutputFile(bool keepRecording) {
    if (m_workingOutputPath.empty()) return false;
    const auto working = std::filesystem::path(
        easy::core::WinUtils::utf8ToWstring(m_workingOutputPath));
    const auto destination = std::filesystem::path(
        easy::core::WinUtils::utf8ToWstring(m_outputPath));
    std::error_code ec;
    if (!keepRecording) {
        std::filesystem::remove(working, ec);
        m_workingOutputPath.clear();
        return false;
    }
    if (!std::filesystem::exists(working, ec) || ec) {
        LOG_ERROR("录屏临时文件不存在, 无法提交: {}", m_workingOutputPath);
        return false;
    }
    if (!MoveFileExW(working.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOG_ERROR("录屏文件原子提交失败: error={}, temporary={}",
                  GetLastError(), m_workingOutputPath);
        return false;
    }
    const auto size = std::filesystem::file_size(destination, ec);
    if (!ec) {
        std::lock_guard lock(m_statsMutex);
        m_stats.fileSizeBytes = static_cast<int64_t>(size);
    }
    m_workingOutputPath.clear();
    return true;
}

std::uint64_t ScreenRecorder::estimatedBytesPerSecond() const {
    const std::uint64_t video = static_cast<std::uint64_t>(
        std::clamp(m_options.bitrateMbps, 1, 100)) * 1'000'000ULL / 8;
    const std::uint64_t audio = (m_options.captureSystemAudio || m_options.captureMicrophone)
        ? 192'000ULL / 8 : 0;
    return std::max<std::uint64_t>(video + audio, 1);
}

bool ScreenRecorder::preflightOutputStorage() {
    const auto directory = outputDirectory(m_outputPath);
    DWORD writeError = ERROR_SUCCESS;
    if (directory.empty() || !probeDirectoryWritable(directory, writeError)) {
        std::lock_guard lock(m_statsMutex);
        m_stats.stopReason = "output_directory_not_writable";
        LOG_ERROR("录屏输出目录不可写: path={}, error={}", m_outputPath, writeError);
        return false;
    }

    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr)) {
        std::lock_guard lock(m_statsMutex);
        m_stats.stopReason = "disk_space_query_failed";
        LOG_ERROR("无法查询录屏目录剩余空间: path={}, error={}", m_outputPath, GetLastError());
        return false;
    }
    const auto bytesPerSecond = estimatedBytesPerSecond();
    const auto minimum = StorageReserveBytes + bytesPerSecond * 10;
    const auto remaining = available.QuadPart > StorageReserveBytes
        ? static_cast<std::int64_t>((available.QuadPart - StorageReserveBytes) / bytesPerSecond)
        : 0;
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.diskFreeBytes = available.QuadPart;
        m_stats.estimatedRemainingSec = remaining;
        m_stats.storageWarning = remaining <= StorageWarningSeconds;
        if (available.QuadPart < minimum) m_stats.stopReason = "insufficient_disk_space";
    }
    if (available.QuadPart < minimum) {
        LOG_ERROR("录屏剩余空间不足: available={}, required={}",
                  available.QuadPart, minimum);
        return false;
    }
    return true;
}

bool ScreenRecorder::updateOutputStorageStatus() {
    const auto directory = outputDirectory(m_outputPath);
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr)) {
        std::lock_guard lock(m_statsMutex);
        m_stats.stopReason = "disk_space_query_failed";
        return false;
    }
    const auto bytesPerSecond = estimatedBytesPerSecond();
    const auto remaining = available.QuadPart > StorageReserveBytes
        ? static_cast<std::int64_t>((available.QuadPart - StorageReserveBytes) / bytesPerSecond)
        : 0;
    std::lock_guard lock(m_statsMutex);
    m_stats.diskFreeBytes = available.QuadPart;
    m_stats.estimatedRemainingSec = remaining;
    m_stats.storageWarning = remaining <= StorageWarningSeconds;
    if (available.QuadPart <= StorageReserveBytes) {
        m_stats.stopReason = "low_disk_space";
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// FFmpeg 编码器
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenRecorder::initializeEncoder(const RecordOptions& options) {
    const char* formatName = nullptr;

    switch (options.format) {
        case RecordFormat::MP4_H264:
            formatName = "mp4";
            break;
        case RecordFormat::MP4_H265:
            formatName = "mp4";
            break;
        case RecordFormat::WebM_VP9:
            formatName = "webm";
            break;
        case RecordFormat::GIF:
            formatName = "gif";
            break;
    }

    const auto candidates = encoderCandidates(options.format);
    if (candidates.empty()) {
        LOG_ERROR("未找到编码器: format={}", formatExtension(options.format));
        return false;
    }

    // 创建输出上下文
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_alloc_output_context2(
        &fmtCtx, nullptr, formatName, m_workingOutputPath.c_str());
    if (ret < 0 || !fmtCtx) {
        LOG_ERROR("avformat_alloc_output_context2 失败");
        return false;
    }
    m_fmtCtx = fmtCtx;

    // 不同机器上的 FFmpeg 可能提供 libx264、Media Foundation 或显卡编码器。
    // 逐个尝试，并依据编码器实际能力选择像素格式，避免固定 YUV420P 导致 EINVAL。
    const AVCodec* codec = nullptr;
    AVCodecContext* codecCtx = nullptr;
    for (const auto* candidate : candidates) {
        AVCodecContext* attempt = avcodec_alloc_context3(candidate);
        if (!attempt) continue;
        const bool mediaFoundation = std::strstr(candidate->name, "_mf") != nullptr;
        attempt->width = options.width;
        attempt->height = options.height;
        attempt->time_base = {1, options.fps};
        attempt->framerate = {options.fps, 1};
        // MF wrapper/部分 Windows MFT 不接受显式 GOP 长度；FFmpeg 自身也把
        // MF encoder 的默认 g 设为 0。强行写 60 会让 avcodec_open2 返回 EINVAL。
        attempt->gop_size = mediaFoundation ? 0 : options.fps * 2;
        attempt->max_b_frames = 0;
        attempt->bit_rate = static_cast<int64_t>(options.bitrateMbps) * 1'000'000;
        attempt->pix_fmt = choosePixelFormat(candidate, options.format);
        if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
            attempt->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (std::strcmp(candidate->name, "libx264") == 0) {
            av_opt_set(attempt->priv_data, "preset", "veryfast", 0);
            av_opt_set(attempt->priv_data, "tune", "zerolatency", 0);
        } else if (std::strcmp(candidate->name, "libx265") == 0) {
            av_opt_set(attempt->priv_data, "preset", "fast", 0);
        }

        ret = avcodec_open2(attempt, candidate, nullptr);
        if (ret >= 0) {
            codec = candidate;
            codecCtx = attempt;
            break;
        }
        LOG_WARN("编码器不可用: codec={}, pixelFormat={}, error={} ({})",
                 candidate->name, static_cast<int>(attempt->pix_fmt), ret, ffmpegError(ret));
        avcodec_free_context(&attempt);
    }
    if (!codec || !codecCtx) {
        LOG_ERROR("所有候选编码器均初始化失败: format={}", formatExtension(options.format));
        return false;
    }
    m_codecCtx = codecCtx;
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.encoderName = codec->name;
        m_stats.hardwareEncoder = isHardwareEncoder(codec->name);
    }

    if ((options.format == RecordFormat::MP4_H264 && codecCtx->codec_id != AV_CODEC_ID_H264) ||
        (options.format == RecordFormat::MP4_H265 && codecCtx->codec_id != AV_CODEC_ID_HEVC)) {
        LOG_WARN("请求的编码器在当前系统不可用，已回退兼容编码器: codec={}", codec->name);
    }

    AVStream* stream = avformat_new_stream(fmtCtx, codec);
    if (!stream) {
        LOG_ERROR("avformat_new_stream 失败");
        return false;
    }
    m_stream = stream;

    avcodec_parameters_from_context(stream->codecpar, codecCtx);
    stream->time_base = codecCtx->time_base;

    if ((options.captureSystemAudio || options.captureMicrophone) &&
        !initializeAudioEncoder(options)) {
        LOG_WARN("音频编码器不可用, 已降级为仅视频录制");
        m_audioCapture.stop();
        std::lock_guard lock(m_statsMutex);
        m_stats.systemAudioActive = false;
        m_stats.microphoneActive = false;
        if (m_stats.audioError.empty()) m_stats.audioError = "audio encoder unavailable";
    }

    // 打开输出文件
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx->pb, m_workingOutputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_ERROR("avio_open 失败: {}", m_workingOutputPath);
            return false;
        }
    }

    // 写入文件头
    ret = avformat_write_header(fmtCtx, nullptr);
    if (ret < 0) {
        LOG_ERROR("avformat_write_header 失败");
        return false;
    }
    m_headerWritten = true;

    // 分配编码帧。输入像素格式取决于运行时选择的捕获后端，转换器在首帧创建。
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR("av_frame_alloc 失败");
        return false;
    }
    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;
    if (av_frame_get_buffer(frame, 0) < 0) {
        LOG_ERROR("av_frame_get_buffer 失败");
        av_frame_free(&frame);
        return false;
    }
    m_frame = frame;
    m_packet = av_packet_alloc();
    if (!m_packet) {
        LOG_ERROR("av_packet_alloc 失败");
        return false;
    }

    LOG_DEBUG("FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps",
              codec->name, options.width, options.height, options.fps);
    return true;
}

bool ScreenRecorder::initializeAudioEncoder(const RecordOptions& options) {
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    if (!fmtCtx) return false;
    const AVCodecID codecId = options.format == RecordFormat::WebM_VP9
        ? AV_CODEC_ID_OPUS : AV_CODEC_ID_AAC;
    const AVCodec* codec = avcodec_find_encoder(codecId);
    if (!codec) return false;

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwrContext* resampler = nullptr;
    auto releaseLocals = [&]() {
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        if (resampler) swr_free(&resampler);
        if (codecCtx) avcodec_free_context(&codecCtx);
    };
    if (!codecCtx) return false;

    codecCtx->sample_rate = AudioCapture::SampleRate;
    codecCtx->time_base = {1, AudioCapture::SampleRate};
    codecCtx->bit_rate = 192'000;
    codecCtx->sample_fmt = chooseAudioSampleFormat(codec);
    AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (av_channel_layout_copy(&codecCtx->ch_layout, &stereoLayout) < 0) {
        releaseLocals();
        return false;
    }
    if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    int result = avcodec_open2(codecCtx, codec, nullptr);
    if (result < 0) {
        LOG_WARN("音频编码器初始化失败: codec={}, error={}", codec->name, ffmpegError(result));
        releaseLocals();
        return false;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        releaseLocals();
        return false;
    }
    frame->format = codecCtx->sample_fmt;
    frame->sample_rate = codecCtx->sample_rate;
    frame->nb_samples = codecCtx->frame_size > 0 ? codecCtx->frame_size : 1024;
    if (av_channel_layout_copy(&frame->ch_layout, &codecCtx->ch_layout) < 0 ||
        av_frame_get_buffer(frame, 0) < 0) {
        releaseLocals();
        return false;
    }

    AVChannelLayout inputLayout = AV_CHANNEL_LAYOUT_STEREO;
    result = swr_alloc_set_opts2(
        &resampler, &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
        &inputLayout, AV_SAMPLE_FMT_FLT, AudioCapture::SampleRate, 0, nullptr);
    if (result < 0 || !resampler || swr_init(resampler) < 0) {
        releaseLocals();
        return false;
    }

    AVStream* stream = avformat_new_stream(fmtCtx, codec);
    if (!stream || avcodec_parameters_from_context(stream->codecpar, codecCtx) < 0) {
        releaseLocals();
        return false;
    }
    stream->time_base = codecCtx->time_base;

    m_audioCodecCtx = codecCtx;
    m_audioStream = stream;
    m_audioFrame = frame;
    m_audioSwrCtx = resampler;
    m_audioPacket = packet;
    m_audioInput.reserve(static_cast<std::size_t>(frame->nb_samples) * AudioCapture::Channels);
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.audioEncoderName = codec->name;
    }
    LOG_INFO("录屏音频已启用: codec={}, rate={}, channels={}",
             codec->name, codecCtx->sample_rate, codecCtx->ch_layout.nb_channels);
    return true;
}

bool ScreenRecorder::encodeAvailableAudio() {
    if (!m_audioCodecCtx || m_audioEncodingFailed) return true;
    auto* codecCtx = static_cast<AVCodecContext*>(m_audioCodecCtx);
    auto* frame = static_cast<AVFrame*>(m_audioFrame);
    auto* resampler = static_cast<SwrContext*>(m_audioSwrCtx);
    if (!codecCtx || !frame || !resampler) return true;

    while (m_audioCapture.readFrames(frame->nb_samples, m_audioInput)) {
        if (av_frame_make_writable(frame) < 0) {
            m_audioEncodingFailed = true;
            return false;
        }
        const std::uint8_t* input[]{
            reinterpret_cast<const std::uint8_t*>(m_audioInput.data())
        };
        const int converted = swr_convert(resampler, frame->data, frame->nb_samples,
                                          input, frame->nb_samples);
        if (converted != frame->nb_samples) {
            m_audioEncodingFailed = true;
            return false;
        }
        frame->pts = m_audioFrameIndex;
        frame->duration = converted;
        m_audioFrameIndex += converted;
        if (avcodec_send_frame(codecCtx, frame) < 0 || !writeAvailableAudioPackets()) {
            m_audioEncodingFailed = true;
            return false;
        }
    }

    const auto audioStatus = m_audioCapture.status();
    std::lock_guard lock(m_statsMutex);
    m_stats.systemAudioActive = audioStatus.systemAudioActive;
    m_stats.microphoneActive = audioStatus.microphoneActive;
    m_stats.droppedAudioFrames = audioStatus.droppedFrames;
    m_stats.audioDiscontinuities = audioStatus.discontinuities;
    m_stats.audioReconnectAttempts = audioStatus.reconnectAttempts;
    m_stats.audioReconnectSuccesses = audioStatus.reconnectSuccesses;
    m_stats.systemAudioPeak = audioStatus.systemPeak;
    m_stats.microphonePeak = audioStatus.microphonePeak;
    m_stats.mixedAudioPeak = audioStatus.mixedPeak;
    m_stats.systemAudioMuted = audioStatus.systemMuted;
    m_stats.microphoneMuted = audioStatus.microphoneMuted;
    if (!audioStatus.error.empty()) m_stats.audioError = audioStatus.error;
    return true;
}

bool ScreenRecorder::writeAvailableAudioPackets() {
    auto* codecCtx = static_cast<AVCodecContext*>(m_audioCodecCtx);
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    auto* stream = static_cast<AVStream*>(m_audioStream);
    auto* packet = static_cast<AVPacket*>(m_audioPacket);
    if (!codecCtx || !fmtCtx || !stream || !packet) return false;
    while (true) {
        const int result = avcodec_receive_packet(codecCtx, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return true;
        if (result < 0) return false;
        av_packet_rescale_ts(packet, codecCtx->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const int writeResult = av_interleaved_write_frame(fmtCtx, packet);
        av_packet_unref(packet);
        if (writeResult < 0) return false;
    }
}

bool ScreenRecorder::captureAndEncodeFrame() {
    using clock = std::chrono::steady_clock;
    const auto pipelineStarted = clock::now();
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    auto* frame = static_cast<AVFrame*>(m_frame);

    if (!codecCtx || !frame) return false;

    if (!m_captureBackend) return false;
    CaptureFrameView captured;
    std::string captureError;
    if (!m_captureBackend->capture(captured, captureError)) {
        LOG_ERROR("录屏帧捕获失败: backend={}, error={}",
                  m_captureBackend->info().id, captureError);
        return false;
    }
    const auto capturedAt = clock::now();
    struct FrameReleaseGuard {
        ICaptureBackend* backend;
        ~FrameReleaseGuard() { backend->releaseFrame(); }
    } releaseGuard{m_captureBackend.get()};

    const auto& backendInfo = m_captureBackend->info();
    if (backendInfo.id != m_activeCaptureBackendId) {
        LOG_INFO("录屏捕获后端已切换: {} -> {}", m_activeCaptureBackendId, backendInfo.id);
        m_activeCaptureBackendId = backendInfo.id;
        std::lock_guard lock(m_statsMutex);
        m_stats.captureBackend = backendInfo.id;
    }
    if (!ensureScaler(captured.format)) return false;
    auto* swsCtx = static_cast<SwsContext*>(m_swsCtx);

    [[maybe_unused]] auto cursorPatch = m_cursorOverlay.apply(
        captured,
        {m_options.regionX, m_options.regionY, m_options.width, m_options.height},
        m_options.includeCursor, m_options.showClickEffects);

    const uint8_t* srcSlice[] = { captured.data };
    int srcStride[] = { captured.stride };

    if (av_frame_make_writable(frame) < 0 ||
        sws_scale(swsCtx, srcSlice, srcStride, 0, m_options.height,
                  frame->data, frame->linesize) <= 0) {
        return false;
    }
    const auto convertedAt = clock::now();

    frame->pts = m_frameIndex++;

    // 编码
    int ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) return false;

    const bool written = writeAvailablePackets();
    const auto encodedAt = clock::now();
    const auto milliseconds = [](auto duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    };
    m_lastFrameTimings.captureMs = milliseconds(capturedAt - pipelineStarted);
    m_lastFrameTimings.conversionMs = milliseconds(convertedAt - capturedAt);
    m_lastFrameTimings.encodeMs = milliseconds(encodedAt - convertedAt);
    m_lastFrameTimings.totalMs = milliseconds(encodedAt - pipelineStarted);
    return written;
}

bool ScreenRecorder::ensureScaler(CapturePixelFormat format) {
    if (m_swsCtx && m_scalerInputFormat == format) return true;
    if (m_swsCtx) {
        sws_freeContext(static_cast<SwsContext*>(m_swsCtx));
        m_swsCtx = nullptr;
    }
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    if (!codecCtx) return false;
    const AVPixelFormat sourceFormat = format == CapturePixelFormat::Bgra32
        ? AV_PIX_FMT_BGRA : AV_PIX_FMT_BGR24;
    m_swsCtx = sws_getContext(
        m_options.width, m_options.height, sourceFormat,
        m_options.width, m_options.height, codecCtx->pix_fmt,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        LOG_ERROR("sws_getContext 失败: sourceFormat={}", static_cast<int>(sourceFormat));
        return false;
    }
    m_scalerInputFormat = format;
    return true;
}

bool ScreenRecorder::initializeCaptureResources() {
    cleanupCaptureResources();
    m_captureBackend = createCaptureBackend();
    if (!m_captureBackend) return false;
    std::string error;
    const CaptureRegion region{
        m_options.regionX, m_options.regionY, m_options.width, m_options.height
    };
    if (!m_captureBackend->initialize(region, error)) {
        LOG_ERROR("录屏捕获后端初始化失败: error={}", error);
        m_captureBackend.reset();
        return false;
    }
    {
        std::lock_guard lock(m_statsMutex);
        m_stats.captureBackend = m_captureBackend->info().id;
    }
    m_activeCaptureBackendId = m_captureBackend->info().id;
    LOG_INFO("录屏捕获后端已选择: backend={}, accelerated={}",
             m_captureBackend->info().id, m_captureBackend->info().accelerated);
    return true;
}

void ScreenRecorder::cleanupCaptureResources() {
    if (m_captureBackend) m_captureBackend->shutdown();
    m_captureBackend.reset();
    m_activeCaptureBackendId.clear();
}

bool ScreenRecorder::writeAvailablePackets() {
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    auto* stream = static_cast<AVStream*>(m_stream);
    auto* packet = static_cast<AVPacket*>(m_packet);
    if (!codecCtx || !fmtCtx || !stream || !packet) return false;

    while (true) {
        const int ret = avcodec_receive_packet(codecCtx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) return false;
        av_packet_rescale_ts(packet, codecCtx->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const int writeResult = av_interleaved_write_frame(fmtCtx, packet);
        av_packet_unref(packet);
        if (writeResult < 0) return false;
    }
}

void ScreenRecorder::cleanupEncoder() {
    cleanupCaptureResources();
    m_audioCapture.stop();
    if (m_audioPacket) {
        av_packet_free(reinterpret_cast<AVPacket**>(&m_audioPacket));
        m_audioPacket = nullptr;
    }
    if (m_audioSwrCtx) {
        swr_free(reinterpret_cast<SwrContext**>(&m_audioSwrCtx));
        m_audioSwrCtx = nullptr;
    }
    if (m_audioFrame) {
        av_frame_free(reinterpret_cast<AVFrame**>(&m_audioFrame));
        m_audioFrame = nullptr;
    }
    if (m_audioCodecCtx) {
        avcodec_free_context(reinterpret_cast<AVCodecContext**>(&m_audioCodecCtx));
        m_audioCodecCtx = nullptr;
    }
    if (m_packet) {
        av_packet_free(reinterpret_cast<AVPacket**>(&m_packet));
        m_packet = nullptr;
    }
    if (m_swsCtx) {
        sws_freeContext(static_cast<SwsContext*>(m_swsCtx));
        m_swsCtx = nullptr;
    }
    m_scalerInputFormat = CapturePixelFormat::Bgr24;
    if (m_frame) {
        av_frame_free(reinterpret_cast<AVFrame**>(&m_frame));
        m_frame = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(reinterpret_cast<AVCodecContext**>(&m_codecCtx));
        m_codecCtx = nullptr;
    }
    if (m_fmtCtx) {
        auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
        if (fmtCtx->pb) {
            avio_closep(&fmtCtx->pb);
        }
        avformat_free_context(fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_stream = nullptr;
    m_audioStream = nullptr;
    m_audioInput.clear();
    m_audioEncodingFailed = false;
    m_headerWritten = false;
}

std::string ScreenRecorder::generateOutputPath(RecordFormat format) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time_t);

    std::ostringstream oss;
    oss << "EasyTools_Rec_" << std::put_time(&localTime, "%Y%m%d_%H%M%S")
        << "." << formatExtension(format);

    auto dir = easy::core::WinUtils::getAppDataDirectory() / L"Recordings";
    return easy::core::WinUtils::wstringToUtf8((dir / oss.str()).wstring());
}

std::string ScreenRecorder::formatExtension(RecordFormat format) {
    switch (format) {
        case RecordFormat::MP4_H264: return "mp4";
        case RecordFormat::MP4_H265: return "mp4";
        case RecordFormat::WebM_VP9: return "webm";
        case RecordFormat::GIF:      return "gif";
        default: return "mp4";
    }
}

}  // namespace easy::capture
