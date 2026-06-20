#include "ocr/OcrEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include <windows.h>

namespace easy::ocr {

OcrEngine& OcrEngine::instance() {
    static OcrEngine inst;
    return inst;
}

bool OcrEngine::initialize() {
    LOG_INFO("OCR 引擎已初始化 (占位)");
    // TODO: 启动 paddleocr.exe 子进程并建立匿名管道通信
    return true;
}

void OcrEngine::shutdown() {
    LOG_INFO("OCR 引擎已关闭");
    // TODO: 关闭子进程
}

std::vector<OcrResult> OcrEngine::extractText(const cv::Mat& image) {
    easy::core::TraceId::Scope scope;
    LOG_INFO("开始提取文字...");
    std::vector<OcrResult> results;
    
    // TODO:
    // 1. 将 image 编码为 base64 / 写入临时文件 / 通过共享内存传递
    // 2. 将命令写入子进程 stdin
    // 3. 从 stdout 读取 JSON 结果
    
    // 模拟延迟和返回结果
    ::Sleep(500);
    OcrResult r;
    r.text = "示例 OCR 提取结果";
    r.confidence = 0.99f;
    r.box = cv::Rect(0, 0, image.cols, image.rows);
    results.push_back(r);

    LOG_INFO("文字提取完成，行数: {}", results.size());
    return results;
}

} // namespace easy::ocr
