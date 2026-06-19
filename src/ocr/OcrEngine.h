#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OcrEngine — OCR 引擎抽象
//
// 职责:
//   1. 封装 PaddleOCR Lite 等 OCR 引擎的调用逻辑
//   2. 支持通过外部进程通信（JSON）或内嵌库实现文字提取
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_OCR_OCRENGINE_H
#define EASYTOOLS_OCR_OCRENGINE_H

#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace easy::ocr {

struct OcrResult {
    std::string text;
    float confidence = 0.0f;
    cv::Rect box;
};

class OcrEngine {
public:
    static OcrEngine& instance();

    /// 初始化引擎 (例如启动外部进程)
    bool initialize();

    /// 关闭引擎
    void shutdown();

    /// 提取文字
    std::vector<OcrResult> extractText(const cv::Mat& image);

private:
    OcrEngine() = default;
    ~OcrEngine() = default;
    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    // TODO: 管理外部进程句柄或 IPC 管道
};

} // namespace easy::ocr

#endif // EASYTOOLS_OCR_OCRENGINE_H
