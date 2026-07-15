// ─────────────────────────────────────────────────────────────────────────────
// ScreenRecorder.cpp — 屏幕录制引擎实现
//
// 录制管道:
//   BitBlt 截屏 → cv::Mat → sws_scale 转 YUV420P → avcodec_send_frame
//   → avcodec_receive_packet → av_write_frame → 输出文件
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/ScreenRecorder.h"
#include "core/logger/Logger.h"
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
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace easy::capture {

namespace {

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
            for (const char* name : {"libx264", "libopenh264", "h264_mf", "h264_qsv",
                                     "h264_nvenc", "h264_amf", "mpeg4"}) {
                addEncoderCandidate(codecs, avcodec_find_encoder_by_name(name));
            }
            break;
        case RecordFormat::MP4_H265:
            codecId = AV_CODEC_ID_HEVC;
            for (const char* name : {"libx265", "hevc_mf", "hevc_qsv", "hevc_nvenc",
                                     "hevc_amf", "mpeg4"}) {
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
    }

    easy::core::TraceId::Scope scope;
    m_options = options;
    {
        std::lock_guard lock(m_statsMutex);
        m_stats = {};
    }
    m_frameIndex = 0;

    // 自动检测屏幕尺寸
    if (options.fps < 1 || options.fps > 120 ||
        options.bitrateMbps < 1 || options.bitrateMbps > 100) {
        LOG_ERROR("录屏参数无效: fps={}, bitrate={}", options.fps, options.bitrateMbps);
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

    // 确保输出目录存在
    auto dir = std::filesystem::path(
        easy::core::WinUtils::utf8ToWstring(m_outputPath)).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }

    // 初始化编码器
    if (!initializeEncoder(m_options)) {
        LOG_ERROR("FFmpeg 编码器初始化失败");
        cleanupEncoder();
        return false;
    }

    m_state = RecordState::Recording;

    // 启动录制线程
    m_recordThread = std::thread([this]() {
        recordingLoop();
    });

    LOG_INFO("屏幕录制已开始: {}x{} @ {}fps, format={}, output={}",
             m_options.width, m_options.height, m_options.fps,
             formatExtension(m_options.format), m_outputPath);

    notifyState(RecordState::Recording);

    return true;
}

void ScreenRecorder::pauseRecording() {
    if (m_state == RecordState::Recording) {
        m_state = RecordState::Paused;
        LOG_INFO("屏幕录制已暂停, frames={}", stats().frameCount);
        notifyState(RecordState::Paused);
    }
}

void ScreenRecorder::resumeRecording() {
    if (m_state == RecordState::Paused) {
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

    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(
            std::filesystem::path(easy::core::WinUtils::utf8ToWstring(m_outputPath)), ec);
        if (!ec) {
            std::lock_guard lock(m_statsMutex);
            m_stats.fileSizeBytes = static_cast<int64_t>(size);
        }
    }

    const auto finalStats = stats();

    LOG_INFO("屏幕录制已停止: frames={}, duration={:.1f}s, output={}",
             finalStats.frameCount, finalStats.durationSec, m_outputPath);

    notifyState(RecordState::Idle);

    return m_outputPath;
}

// ─────────────────────────────────────────────────────────────────────────────
// 录制线程
// ─────────────────────────────────────────────────────────────────────────────

void ScreenRecorder::recordingLoop() {
    using clock = std::chrono::high_resolution_clock;
    auto frameDuration = std::chrono::microseconds(1'000'000 / m_options.fps);

    // 线程入口兜底: 任何 std 异常(如全屏帧缓冲 std::bad_alloc、日志 std::format 抛错)若逃逸出
    // 线程函数会直接 std::terminate 整个进程。这里全程兜底, 异常时干净停止录制。
    try {
        if (!initializeCaptureResources()) {
            LOG_ERROR("录屏捕获资源初始化失败");
            m_state = RecordState::Idle;
        }
        while (m_state != RecordState::Idle) {
            auto frameStart = clock::now();

            if (m_state == RecordState::Recording) {
                if (!captureAndEncodeFrame()) {
                    LOG_ERROR("帧捕获/编码失败, 停止录制");
                    m_state = RecordState::Idle;
                    break;
                }
                {
                    std::lock_guard lock(m_statsMutex);
                    m_stats.frameCount++;
                    m_stats.durationSec = static_cast<double>(m_stats.frameCount) / m_options.fps;
                }
            }

            // 帧率控制
            auto elapsed = clock::now() - frameStart;
            if (elapsed < frameDuration) {
                std::this_thread::sleep_for(frameDuration - elapsed);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("录制线程异常, 已停止录制: {}", e.what());
        m_state = RecordState::Idle;
    } catch (...) {
        LOG_ERROR("录制线程未知异常, 已停止录制");
        m_state = RecordState::Idle;
    }

    cleanupCaptureResources();

    if (m_state == RecordState::Idle) notifyState(RecordState::Idle);
}

RecordStats ScreenRecorder::stats() const {
    std::lock_guard lock(m_statsMutex);
    return m_stats;
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
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    if (codecCtx && m_headerWritten) {
        avcodec_send_frame(codecCtx, nullptr);
        writeAvailablePackets();
    }
    if (fmtCtx && m_headerWritten) av_write_trailer(fmtCtx);
    cleanupEncoder();
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
    int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, formatName, m_outputPath.c_str());
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

    // 打开输出文件
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmtCtx->pb, m_outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_ERROR("avio_open 失败: {}", m_outputPath);
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

    // 分配帧
    AVFrame* frame = av_frame_alloc();
    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;
    if (!frame || av_frame_get_buffer(frame, 0) < 0) {
        LOG_ERROR("av_frame_get_buffer 失败");
        if (frame) av_frame_free(&frame);
        return false;
    }
    m_frame = frame;

    // 创建颜色空间转换器
    SwsContext* swsCtx = sws_getContext(
        options.width, options.height, AV_PIX_FMT_BGR24,
        options.width, options.height, codecCtx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    m_swsCtx = swsCtx;
    if (!swsCtx) {
        LOG_ERROR("sws_getContext 失败");
        return false;
    }
    m_packet = av_packet_alloc();
    if (!m_packet) {
        LOG_ERROR("av_packet_alloc 失败");
        return false;
    }

    LOG_DEBUG("FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps",
              codec->name, options.width, options.height, options.fps);
    return true;
}

bool ScreenRecorder::captureAndEncodeFrame() {
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    auto* frame = static_cast<AVFrame*>(m_frame);
    auto* swsCtx = static_cast<SwsContext*>(m_swsCtx);

    if (!codecCtx || !frame || !swsCtx) return false;

    if (!m_screenDc || !m_memoryDc || !m_captureBits) return false;
    if (!BitBlt(m_memoryDc, 0, 0, m_options.width, m_options.height,
                m_screenDc, m_options.regionX, m_options.regionY,
                SRCCOPY | CAPTUREBLT)) {
        LOG_ERROR("BitBlt 录屏捕获失败, error={}", GetLastError());
        return false;
    }

    // 颜色空间转换 BGR24 → YUV420P
    const uint8_t* srcSlice[] = { static_cast<const uint8_t*>(m_captureBits) };
    int srcStride[] = { m_captureStride };

    if (av_frame_make_writable(frame) < 0 ||
        sws_scale(swsCtx, srcSlice, srcStride, 0, m_options.height,
                  frame->data, frame->linesize) <= 0) {
        return false;
    }

    frame->pts = m_frameIndex++;

    // 编码
    int ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) return false;

    return writeAvailablePackets();
}

bool ScreenRecorder::initializeCaptureResources() {
    cleanupCaptureResources();
    m_screenDc = GetDC(nullptr);
    if (!m_screenDc) return false;
    m_memoryDc = CreateCompatibleDC(m_screenDc);
    if (!m_memoryDc) {
        cleanupCaptureResources();
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = m_options.width;
    bitmapInfo.bmiHeader.biHeight = -m_options.height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 24;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    m_captureBitmap = CreateDIBSection(m_screenDc, &bitmapInfo, DIB_RGB_COLORS,
                                       &m_captureBits, nullptr, 0);
    if (!m_captureBitmap || !m_captureBits) {
        cleanupCaptureResources();
        return false;
    }
    m_previousBitmap = SelectObject(m_memoryDc, m_captureBitmap);
    m_captureStride = (m_options.width * 3 + 3) & ~3;
    return true;
}

void ScreenRecorder::cleanupCaptureResources() {
    if (m_memoryDc && m_previousBitmap) {
        SelectObject(m_memoryDc, m_previousBitmap);
        m_previousBitmap = nullptr;
    }
    if (m_captureBitmap) {
        DeleteObject(m_captureBitmap);
        m_captureBitmap = nullptr;
    }
    m_captureBits = nullptr;
    if (m_memoryDc) {
        DeleteDC(m_memoryDc);
        m_memoryDc = nullptr;
    }
    if (m_screenDc) {
        ReleaseDC(nullptr, m_screenDc);
        m_screenDc = nullptr;
    }
    m_captureStride = 0;
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
    if (m_packet) {
        av_packet_free(reinterpret_cast<AVPacket**>(&m_packet));
        m_packet = nullptr;
    }
    if (m_swsCtx) {
        sws_freeContext(static_cast<SwsContext*>(m_swsCtx));
        m_swsCtx = nullptr;
    }
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
