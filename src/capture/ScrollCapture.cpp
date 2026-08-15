// ─────────────────────────────────────────────────────────────────────────────
// ScrollCapture.cpp — 长截图实现
//
// 拼接算法:
//   1. 连续截图获取多帧（自动滚动 or 手动触发）
//   2. 相邻帧底部/顶部存在重叠区域
//   3. 使用 cv::matchTemplate 在重叠区域中查找最佳对齐偏移
//   4. 逐帧拼接：上帧保留完整 + 下帧从偏移位置往后拼接
//   5. 检测连续两帧相似度 > 0.99 时判定为到达底部
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/ScrollCapture.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"

#include <opencv2/imgproc.hpp>

namespace easy::capture {

ScrollCapture& ScrollCapture::instance() {
    static ScrollCapture inst;
    return inst;
}

void ScrollCapture::start(const ScrollCaptureOptions& options) {
    if (m_running) {
        LOG_WARN("长截图已在运行中");
        return;
    }

    easy::core::TraceId::Scope scope;
    m_options = options;
    m_segments.clear();
    m_lastFrame.release();
    m_frameCount = 0;
    m_completionDelivered = false;
    m_suppressCompletion = false;
    m_running = true;

    if (options.mode == ScrollMode::Auto) {
        // 回收上一次已结束但尚未 join 的线程: autoScrollLoop 自行完成拼接后只置 m_running=false,
        // 并不 join 自身, 故 m_scrollThread 仍处于 joinable。直接向 joinable 的 std::thread 赋值
        // 会触发 std::terminate。此处 m_running 已为 false(被上方守卫保证), join 会立即返回。
        if (m_scrollThread.joinable()) {
            m_scrollThread.join();
        }
        m_scrollThread = std::thread([this]() { autoScrollLoop(); });
    }

    LOG_INFO("长截图已启动: mode={}, scrollDelay={}ms, maxFrames={}",
             options.mode == ScrollMode::Auto ? "Auto" : "Manual",
             options.scrollDelayMs, options.maxFrames);
}

void ScrollCapture::stop() {
    m_running = false;
    if (m_scrollThread.joinable()) {
        m_scrollThread.join();
    }

    deliverCompletion();
}

void ScrollCapture::shutdown() {
    m_suppressCompletion = true;
    m_running = false;
    if (m_scrollThread.joinable()) m_scrollThread.join();
    m_progressCb = nullptr;
    m_completionCb = nullptr;
    m_segments.clear();
    m_lastFrame.release();
    m_frameCount = 0;
    easy::core::WinUtils::trimWorkingSet();
}

void ScrollCapture::captureCurrentFrame() {
    if (!m_running) return;

    auto frame = captureRegion(m_options.captureRect);
    if (!frame.empty()) {
        appendFrame(frame);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 自动滚动
// ─────────────────────────────────────────────────────────────────────────────

void ScrollCapture::autoScrollLoop() {
    easy::core::TraceId::Scope scope;
    POINT originalCursor{};
    GetCursorPos(&originalCursor);

    // 线程入口兜底: 截屏(cv::Mat 分配)、拼接、回调等可抛异常, 逃逸出线程函数会 std::terminate。
    try {
    for (int i = 0; i < m_options.maxFrames && m_running; ++i) {
        // 截取当前画面
        auto frame = captureRegion(m_options.captureRect);
        if (frame.empty()) continue;

        // 检测是否到达底部
        if (!m_lastFrame.empty() && isAtBottom(frame, m_lastFrame)) {
            LOG_INFO("检测到滚动到底部, frame={}", i);
            break;
        }

        appendFrame(frame);

        // 模拟鼠标滚轮滚动
        RECT& r = m_options.captureRect;
        int centerX = (r.left + r.right) / 2;
        int centerY = (r.top + r.bottom) / 2;

        // 移动鼠标到截图区域中心
        SetCursorPos(centerX, centerY);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 发送滚轮事件
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA * m_options.scrollAmount);
        SendInput(1, &input, sizeof(INPUT));

        // 等待页面渲染
        std::this_thread::sleep_for(std::chrono::milliseconds(m_options.scrollDelayMs));
    }

    m_running = false;
    SetCursorPos(originalCursor.x, originalCursor.y);
    deliverCompletion();
    } catch (const std::exception& e) {
        m_running = false;
        SetCursorPos(originalCursor.x, originalCursor.y);
        LOG_ERROR("长截图线程异常, 已停止: {}", e.what());
        deliverCompletion(e.what());
    } catch (...) {
        m_running = false;
        SetCursorPos(originalCursor.x, originalCursor.y);
        LOG_ERROR("长截图线程未知异常, 已停止");
        deliverCompletion("未知错误");
    }
}

void ScrollCapture::appendFrame(const cv::Mat& frame) {
    if (frame.empty()) return;
    if (m_lastFrame.empty()) {
        m_segments.push_back(frame.clone());
    } else {
        int offset = findStitchOffset(m_lastFrame, frame);
        if (offset <= 0 || offset >= frame.rows) {
            offset = frame.rows * std::clamp(m_options.overlapPercent, 0, 95) / 100;
        }
        const int remaining = frame.rows - offset;
        if (remaining > 0) {
            m_segments.push_back(frame(cv::Rect(0, offset, frame.cols, remaining)).clone());
        }
    }
    m_lastFrame = frame.clone();
    ++m_frameCount;

    if (m_progressCb) {
        m_progressCb(buildStitchedImage(), m_frameCount);
    }
}

cv::Mat ScrollCapture::buildStitchedImage() const {
    if (m_segments.empty()) return {};
    cv::Mat stitched;
    cv::vconcat(m_segments, stitched);
    return stitched;
}

void ScrollCapture::deliverCompletion(const std::string& error) {
    if (m_completionDelivered.exchange(true)) return;
    if (m_suppressCompletion.load()) return;
    ScrollCaptureResult result;
    result.frameCount = m_frameCount;
    result.errorMessage = error;
    if (error.empty()) result.stitchedImage = buildStitchedImage();
    result.success = error.empty() && !result.stitchedImage.empty();
    if (!result.success && result.errorMessage.empty()) {
        result.errorMessage = m_frameCount == 0 ? "没有捕获到任何帧" : "图像拼接失败";
    }
    if (m_completionCb) m_completionCb(result);
    if (result.success) {
        LOG_INFO("长截图拼接完成: {}x{}, 帧数={}", result.stitchedImage.cols,
                 result.stitchedImage.rows, result.frameCount);
    }
    m_segments.clear();
    m_lastFrame.release();
    easy::core::WinUtils::trimWorkingSet();
}

// ─────────────────────────────────────────────────────────────────────────────
// 截取区域
// ─────────────────────────────────────────────────────────────────────────────

cv::Mat ScrollCapture::captureRegion(const RECT& rect) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return {};

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, w, h);
    HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);

    BitBlt(memDC, 0, 0, w, h, screenDC, rect.left, rect.top, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    cv::Mat image(h, w, CV_8UC3);
    GetDIBits(memDC, hBitmap, 0, h, image.data,
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(memDC, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return image;
}

// ─────────────────────────────────────────────────────────────────────────────
// 底部检测
// ─────────────────────────────────────────────────────────────────────────────

bool ScrollCapture::isAtBottom(const cv::Mat& current, const cv::Mat& previous) const {
    if (current.size() != previous.size()) return false;

    cv::Mat diff;
    cv::absdiff(current, previous, diff);
    double meanDiff = cv::mean(diff)[0];

    // 如果平均差异小于 1（几乎相同），则认为到达底部
    return meanDiff < 1.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 图像拼接
// ─────────────────────────────────────────────────────────────────────────────

cv::Mat ScrollCapture::stitchFrames(const std::vector<cv::Mat>& frames, int overlapPercent) const {
    if (frames.empty()) return {};
    if (frames.size() == 1) return frames[0].clone();

    easy::core::TraceId::Scope scope;

    // 逐帧拼接
    cv::Mat result = frames[0].clone();

    for (size_t i = 1; i < frames.size(); ++i) {
        int offset = findStitchOffset(result, frames[i]);

        if (offset <= 0 || offset >= frames[i].rows) {
            // 匹配失败，使用默认重叠
            int defaultOverlap = frames[i].rows * overlapPercent / 100;
            offset = defaultOverlap;
        }

        // 创建拼接结果
        int newHeight = result.rows + frames[i].rows - offset;
        cv::Mat newResult(newHeight, result.cols, result.type());

        // 上半部分 = 当前结果
        result.copyTo(newResult(cv::Rect(0, 0, result.cols, result.rows)));

        // 下半部分 = 新帧的 offset 行之后
        int remainingHeight = frames[i].rows - offset;
        if (remainingHeight > 0) {
            cv::Mat lowerPart = frames[i](cv::Rect(0, offset, frames[i].cols, remainingHeight));
            lowerPart.copyTo(newResult(cv::Rect(0, result.rows, result.cols, remainingHeight)));
        }

        result = newResult;
        LOG_DEBUG("拼接帧 {}/{}: offset={}, 结果高度={}", i, frames.size() - 1, offset, result.rows);
    }

    return result;
}

int ScrollCapture::findStitchOffset(const cv::Mat& upper, const cv::Mat& lower) const {
    // 取上帧底部的一小块作为模板
    int templateHeight = std::min(upper.rows / 4, 100);
    if (templateHeight < 20) return -1;

    cv::Mat templateImg = upper(cv::Rect(0, upper.rows - templateHeight, upper.cols, templateHeight));

    // 在下帧中搜索（只搜上半部分）
    int searchHeight = std::min(lower.rows, templateHeight * 3);
    cv::Mat searchRegion = lower(cv::Rect(0, 0, lower.cols, searchHeight));

    // 图像金字塔加速匹配
    double scale = 0.5;
    cv::Mat smallTemplate, smallSearch, smallResult;
    cv::resize(templateImg, smallTemplate, cv::Size(), scale, scale, cv::INTER_LINEAR);
    cv::resize(searchRegion, smallSearch, cv::Size(), scale, scale, cv::INTER_LINEAR);

    cv::matchTemplate(smallSearch, smallTemplate, smallResult, cv::TM_CCOEFF_NORMED);

    double smallMaxVal;
    cv::Point smallMaxLoc;
    cv::minMaxLoc(smallResult, nullptr, &smallMaxVal, nullptr, &smallMaxLoc);

    if (smallMaxVal < 0.7) {
        LOG_WARN("缩小模板匹配置信度过低: {:.3f}", smallMaxVal);
        return -1;
    }

    // 恢复粗略坐标
    int roughY = static_cast<int>(smallMaxLoc.y / scale);
    int fineSearchTop = std::max(0, roughY - 5);
    int fineSearchBottom = std::min(searchRegion.rows, roughY + templateHeight + 5);

    if (fineSearchBottom - fineSearchTop < templateHeight) return -1;

    cv::Mat fineSearchRegion = searchRegion(cv::Rect(0, fineSearchTop, searchRegion.cols, fineSearchBottom - fineSearchTop));
    
    cv::Mat fineResult;
    cv::matchTemplate(fineSearchRegion, templateImg, fineResult, cv::TM_CCOEFF_NORMED);

    double maxVal;
    cv::Point maxLoc;
    cv::minMaxLoc(fineResult, nullptr, &maxVal, nullptr, &maxLoc);

    if (maxVal < 0.8) {
        LOG_WARN("模板匹配置信度过低: {:.3f}", maxVal);
        return -1;
    }

    // 偏移 = 模板在下帧中的匹配位置 + 模板高度
    int offset = fineSearchTop + maxLoc.y + templateHeight;
    LOG_DEBUG("模板匹配: confidence={:.3f}, offset={}", maxVal, offset);

    return offset;
}

}  // namespace easy::capture
