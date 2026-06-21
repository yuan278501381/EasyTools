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
#include <iomanip>
#include <sstream>
#include <filesystem>

// FFmpeg C 头文件
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace easy::capture {

ScreenRecorder& ScreenRecorder::instance() {
    static ScreenRecorder inst;
    return inst;
}

bool ScreenRecorder::initialize() {
    LOG_INFO("屏幕录制引擎已初始化");
    return true;
}

void ScreenRecorder::shutdown() {
    if (m_state != RecordState::Idle) {
        stopRecording();
    }
    LOG_INFO("屏幕录制引擎已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 录制控制
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenRecorder::startRecording(const RecordOptions& options) {
    if (m_state != RecordState::Idle) {
        LOG_WARN("已在录制中, 请先停止当前录制");
        return false;
    }

    easy::core::TraceId::Scope scope;
    m_options = options;
    m_stats = {};
    m_frameIndex = 0;

    // 自动检测屏幕尺寸
    if (options.fullScreen || options.width == 0) {
        m_options.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        m_options.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        m_options.regionX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        m_options.regionY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    }

    // 确保宽高为偶数（编码器要求）
    m_options.width &= ~1;
    m_options.height &= ~1;

    // 生成输出文件路径
    m_outputPath = options.outputPath.empty()
        ? generateOutputPath(options.format)
        : options.outputPath;

    // 确保输出目录存在
    auto dir = std::filesystem::path(m_outputPath).parent_path();
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

    if (m_stateCallback) {
        m_stateCallback(RecordState::Recording, m_stats);
    }

    return true;
}

void ScreenRecorder::pauseRecording() {
    if (m_state == RecordState::Recording) {
        m_state = RecordState::Paused;
        LOG_INFO("屏幕录制已暂停, frames={}", m_stats.frameCount);
        if (m_stateCallback) m_stateCallback(RecordState::Paused, m_stats);
    }
}

void ScreenRecorder::resumeRecording() {
    if (m_state == RecordState::Paused) {
        m_state = RecordState::Recording;
        LOG_INFO("屏幕录制已恢复");
        if (m_stateCallback) m_stateCallback(RecordState::Recording, m_stats);
    }
}

std::string ScreenRecorder::stopRecording() {
    if (m_state == RecordState::Idle) return "";

    easy::core::TraceId::Scope scope;
    m_state = RecordState::Idle;

    // 等待录制线程结束
    if (m_recordThread.joinable()) {
        m_recordThread.join();
    }

    // 写入文件尾
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    if (fmtCtx) {
        av_write_trailer(fmtCtx);
    }

    cleanupEncoder();

    LOG_INFO("屏幕录制已停止: frames={}, duration={:.1f}s, output={}",
             m_stats.frameCount, m_stats.durationSec, m_outputPath);

    if (m_stateCallback) m_stateCallback(RecordState::Idle, m_stats);

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
        while (m_state != RecordState::Idle) {
            auto frameStart = clock::now();

            if (m_state == RecordState::Recording) {
                if (!captureAndEncodeFrame()) {
                    LOG_ERROR("帧捕获/编码失败, 停止录制");
                    m_state = RecordState::Idle;
                    break;
                }
                m_stats.frameCount++;
                m_stats.durationSec = static_cast<double>(m_stats.frameCount) / m_options.fps;
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
}

// ─────────────────────────────────────────────────────────────────────────────
// FFmpeg 编码器
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenRecorder::initializeEncoder(const RecordOptions& options) {
    // 查找编码器
    const AVCodec* codec = nullptr;
    const char* formatName = nullptr;

    switch (options.format) {
        case RecordFormat::MP4_H264:
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            formatName = "mp4";
            break;
        case RecordFormat::MP4_H265:
            codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
            formatName = "mp4";
            break;
        case RecordFormat::WebM_VP9:
            codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
            formatName = "webm";
            break;
        case RecordFormat::GIF:
            codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
            formatName = "gif";
            break;
    }

    if (!codec) {
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

    // 创建流
    AVStream* stream = avformat_new_stream(fmtCtx, codec);
    if (!stream) {
        LOG_ERROR("avformat_new_stream 失败");
        return false;
    }
    m_stream = stream;

    // 编码器上下文
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        LOG_ERROR("avcodec_alloc_context3 失败");
        return false;
    }
    m_codecCtx = codecCtx;

    codecCtx->width = options.width;
    codecCtx->height = options.height;
    codecCtx->time_base = {1, options.fps};
    codecCtx->framerate = {options.fps, 1};
    codecCtx->gop_size = options.fps * 2;  // 关键帧间隔 = 2 秒
    codecCtx->max_b_frames = (options.format == RecordFormat::GIF) ? 0 : 2;
    codecCtx->bit_rate = static_cast<int64_t>(options.bitrateMbps) * 1000000;

    if (options.format == RecordFormat::GIF) {
        codecCtx->pix_fmt = AV_PIX_FMT_PAL8;
    } else {
        codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    // 编码质量预设
    if (options.format == RecordFormat::MP4_H264) {
        av_opt_set(codecCtx->priv_data, "preset", "veryfast", 0);
        av_opt_set(codecCtx->priv_data, "tune", "zerolatency", 0);
    } else if (options.format == RecordFormat::MP4_H265) {
        av_opt_set(codecCtx->priv_data, "preset", "fast", 0);
    }

    if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 打开编码器
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        LOG_ERROR("avcodec_open2 失败, ret={}", ret);
        return false;
    }

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

    // 分配帧
    AVFrame* frame = av_frame_alloc();
    frame->format = codecCtx->pix_fmt;
    frame->width = codecCtx->width;
    frame->height = codecCtx->height;
    av_frame_get_buffer(frame, 0);
    m_frame = frame;

    // 创建颜色空间转换器
    SwsContext* swsCtx = sws_getContext(
        options.width, options.height, AV_PIX_FMT_BGR24,
        options.width, options.height, codecCtx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    m_swsCtx = swsCtx;

    LOG_DEBUG("FFmpeg 编码器初始化成功: codec={}, {}x{} @ {}fps",
              codec->name, options.width, options.height, options.fps);
    return true;
}

bool ScreenRecorder::captureAndEncodeFrame() {
    auto* codecCtx = static_cast<AVCodecContext*>(m_codecCtx);
    auto* fmtCtx = static_cast<AVFormatContext*>(m_fmtCtx);
    auto* stream = static_cast<AVStream*>(m_stream);
    auto* frame = static_cast<AVFrame*>(m_frame);
    auto* swsCtx = static_cast<SwsContext*>(m_swsCtx);

    if (!codecCtx || !fmtCtx || !frame || !swsCtx) return false;

    // 截屏（BitBlt）
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, m_options.width, m_options.height);
    SelectObject(memDC, hBitmap);

    BitBlt(memDC, 0, 0, m_options.width, m_options.height,
           screenDC, m_options.regionX, m_options.regionY, SRCCOPY | CAPTUREBLT);

    // 读取位图数据
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = m_options.width;
    bi.biHeight = -m_options.height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(m_options.width) * m_options.height * 3);
    GetDIBits(memDC, hBitmap, 0, m_options.height, pixels.data(),
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    // 颜色空间转换 BGR24 → YUV420P
    const uint8_t* srcSlice[] = { pixels.data() };
    int srcStride[] = { m_options.width * 3 };

    av_frame_make_writable(frame);
    sws_scale(swsCtx, srcSlice, srcStride, 0, m_options.height,
              frame->data, frame->linesize);

    frame->pts = m_frameIndex++;

    // 编码
    int ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) return false;

    AVPacket* pkt = av_packet_alloc();
    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            av_packet_free(&pkt);
            return false;
        }

        av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmtCtx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    return true;
}

void ScreenRecorder::cleanupEncoder() {
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
    return (dir / oss.str()).string();
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
