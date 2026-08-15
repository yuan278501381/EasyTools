// ─────────────────────────────────────────────────────────────────────────────
// ScreenCapture.cpp — 屏幕截图核心引擎实现
//
// 截图方案:
//   - 主方案: BitBlt GDI 截图（兼容性最好）
//   - 使用 OpenCV 编码/解码图像
//   - 剪贴板: CF_DIB + CF_BITMAP 双格式写入
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/ScreenCapture.h"
#include "capture/CaptureBackend.h"
#include "core/events/EventBus.h"
#include "core/events/MainThreadDispatcher.h"
#include "capture/CaptureOverlay.h"
#include "core/logger/Logger.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "ocr/OcrEngine.h"
#include "ocr/OcrResultWindow.h"
#include "capture/ScrollCapture.h"
#include "capture/ScrollCaptureOverlay.h"
#include "capture/CaptureHistory.h"
#include "capture/ShortcutHintOverlay.h"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <thread>

namespace easy::capture {

ScreenCapture& ScreenCapture::instance() {
    static ScreenCapture inst;
    return inst;
}

bool ScreenCapture::initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    // 初始化截图覆盖层
    auto& overlay = CaptureOverlay::instance();
    overlay.initialize(hInstance);
    overlay.setClosedCallback([this]() { m_capturing.store(false); });
    overlay.setCallback([this](const CaptureRegion& region, const cv::Mat& markedImage,
                               const CaptureCompletion& completion) {
        easy::core::TraceId::Scope scope;
        CaptureResult result;
        result.region = region;
        result.imageWidth = markedImage.cols;
        result.imageHeight = markedImage.rows;

        // 复制到剪贴板
        if ((m_activeOptions.copyToClipboard ||
             completion.action == CaptureCompletionAction::Copy) &&
            !markedImage.empty()) {
            copyToClipboard(markedImage);
            easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"截图已复制到剪贴板"});
        }

        const bool explicitSave = completion.action == CaptureCompletionAction::SaveAs &&
                                  !completion.filePath.empty();
        if ((explicitSave || m_activeOptions.saveToFile) && !markedImage.empty()) {
            const auto format = explicitSave ? completion.format : m_activeOptions.format;
            auto encoded = encodeImage(markedImage, format, m_activeOptions.quality);
            if (!encoded.empty()) {
                const std::string path = explicitSave
                    ? completion.filePath
                    : m_activeOptions.savePath.empty()
                        ? generateSavePath(format)
                        : m_activeOptions.savePath;
                result.filePath = saveToFile(encoded, path, format);
                if (explicitSave) {
                    easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{
                        result.filePath.empty() ? L"截图保存失败" : L"截图已保存"});
                }
            }
        }

        result.success = true;
        LOG_INFO("截图完成: {}x{}", result.imageWidth, result.imageHeight);

        // 将截图保存到历史
        CaptureHistory::instance().push(markedImage, region, result.filePath);

        if (m_callback) m_callback(result);
    });

    overlay.setOcrCallback([this]([[maybe_unused]] const CaptureRegion& region, const cv::Mat& cropped) {
        if (cropped.empty()) return;
        if (m_ocrRunning.exchange(true)) {
            easy::core::EventBus::instance().publish(
                easy::core::ShowToastEvent{L"OCR 正在识别，请稍候"});
            return;
        }
        
        const bool showResultWindow = easy::core::ConfigManager::instance().get<bool>(
            "/ocr/showResultWindow", true);
        if (showResultWindow) {
            easy::ocr::OcrResultWindow::instance().showResult("识别中... (Recognizing...)");
        }

        // OCR(WinRT .get()) 放后台线程: 避免在主 STA 线程阻塞/冻结 UI; 全程 try/catch 兜底。
        std::string traceId = easy::core::TraceId::current();
        cv::Mat img = cropped.clone();
        if (m_ocrWorker.joinable()) m_ocrWorker.join();
        m_ocrWorker = std::jthread([this, img = std::move(img), traceId]() {
            easy::core::TraceId::setCurrent(traceId);
            std::string displayText;
            try {
                auto results = easy::ocr::OcrEngine::instance().extractText(img);
                std::string fullText;
                for (const auto& r : results) fullText += r.text + "\r\n";
                if (!fullText.empty()) {
                    if (easy::core::ConfigManager::instance().get<bool>("/ocr/copyResult", true)) {
                        easy::core::WinUtils::copyToClipboard(fullText);
                    }
                    LOG_INFO("OCR 提取完成, 行数={}", results.size());
                    displayText = std::move(fullText);
                } else {
                    LOG_WARN("OCR 未提取到文字");
                    displayText = "未识别到文字 (No text recognized)";
                }
            } catch (const std::exception& e) {
                LOG_ERROR("OCR(覆盖层) 异常: {}", e.what());
                displayText = "识别失败 (Recognition failed)";
            } catch (...) {
                LOG_ERROR("OCR(覆盖层) 未知异常");
                displayText = "识别失败 (Unknown error)";
            }
            m_ocrRunning.store(false);
            if (easy::core::ConfigManager::instance().get<bool>("/ocr/showResultWindow", true)) {
                easy::core::MainThreadDispatcher::instance().post([text = std::move(displayText)]() {
                    easy::ocr::OcrResultWindow::instance().showResult(text);
                });
            } else {
                easy::core::EventBus::instance().publish(
                    easy::core::ShowToastEvent{L"OCR 识别完成"});
            }
        });
    });

    ScrollCapture::instance().setProgressCallback([](const cv::Mat& preview, int frameCount) {
        ScrollCaptureOverlay::instance().updatePreview(preview, frameCount);
    });
    ScrollCapture::instance().setCompletionCallback([this](const ScrollCaptureResult& result) {
        easy::core::TraceId::Scope scope;
        ShortcutHintOverlay::instance().hide();
        easy::core::MainThreadDispatcher::instance().post([] {
            ScrollCaptureOverlay::instance().hide();
        });
        if (!result.success || result.stitchedImage.empty()) {
            LOG_ERROR("长截图失败: {}", result.errorMessage);
            easy::core::EventBus::instance().publish(
                easy::core::ShowToastEvent{L"长截图失败，请重试"});
            return;
        }

        bool copied = false;
        if (m_activeOptions.copyToClipboard) {
            copied = copyToClipboard(result.stitchedImage);
        }

        std::string savedPath;
        if (m_activeOptions.saveToFile) {
            auto encoded = encodeImage(result.stitchedImage, m_activeOptions.format, m_activeOptions.quality);
            if (!encoded.empty()) {
                std::string path = m_activeOptions.savePath.empty()
                    ? generateSavePath(m_activeOptions.format)
                    : m_activeOptions.savePath;
                savedPath = saveToFile(encoded, path, m_activeOptions.format);
            }
        }
        CaptureRegion historyRegion{};
        historyRegion.width = result.stitchedImage.cols;
        historyRegion.height = result.stitchedImage.rows;
        CaptureHistory::instance().push(result.stitchedImage, historyRegion, savedPath);
        easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{
            savedPath.empty()
                ? (copied ? L"长截图已复制到剪贴板" : L"长截图已完成")
                : L"长截图已保存"});
        LOG_INFO("长截图已保存，共 {} 帧", result.frameCount);
    });

    LOG_INFO("截图引擎已初始化");
    return true;
}

void ScreenCapture::shutdown() {
    ScrollCapture::instance().shutdown();
    ScrollCaptureOverlay::instance().hide();
    if (m_ocrWorker.joinable()) m_ocrWorker.join();
    m_ocrRunning.store(false);
    CaptureOverlay::instance().shutdown();
    m_capturing.store(false);
    LOG_INFO("截图引擎已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 截图入口
// ─────────────────────────────────────────────────────────────────────────────

void ScreenCapture::startCapture(const CaptureOptions& options) {
    bool expected = false;
    if (!m_capturing.compare_exchange_strong(expected, true)) {
        LOG_WARN("截图已在进行中");
        return;
    }

    easy::core::TraceId::Scope scope;
    m_activeOptions = options;

    // 启动区域选择覆盖层
    CaptureOverlay::instance().startSelection(options);

}

CaptureResult ScreenCapture::captureFullScreen(const CaptureOptions& options) {
    easy::core::TraceId::Scope scope;
    LOG_INFO("执行全屏截图");

    // 获取虚拟屏幕区域（多显示器）
    CaptureRegion fullRegion;
    fullRegion.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    fullRegion.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    fullRegion.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    fullRegion.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    return captureRegion(fullRegion, options);
}

CaptureResult ScreenCapture::captureWindow(HWND hwnd, const CaptureOptions& options) {
    easy::core::TraceId::Scope scope;

    if (!hwnd || !IsWindow(hwnd)) {
        return { false, "", {}, 0, 0, "无效的窗口句柄" };
    }

    RECT rc;
    GetWindowRect(hwnd, &rc);

    CaptureRegion region;
    region.x = rc.left;
    region.y = rc.top;
    region.width = rc.right - rc.left;
    region.height = rc.bottom - rc.top;

    LOG_INFO("截取窗口: hwnd={}, region=({},{})x{}x{}", 
             reinterpret_cast<uintptr_t>(hwnd), region.x, region.y, region.width, region.height);

    return captureRegion(region, options);
}

CaptureResult ScreenCapture::captureRegion(const CaptureRegion& region, const CaptureOptions& options) {
    easy::core::TraceId::Scope scope;
    CaptureResult result;

    if (!region.isValid() || region.width > 32768 || region.height > 32768) {
        result.errorMessage = "无效的截图区域";
        LOG_ERROR("截图失败: {}", result.errorMessage);
        return result;
    }

    // 截取屏幕
    easy::core::PerfTimer captureTimer("screenshot");
    auto image = captureScreen(region);
    captureTimer.stop();
    if (!image || image->empty()) {
        result.errorMessage = "屏幕截图失败";
        LOG_ERROR("截图失败: {}", result.errorMessage);
        return result;
    }

    result.region = region;
    result.imageWidth = image->cols;
    result.imageHeight = image->rows;

    // 复制到剪贴板
    if (options.copyToClipboard) {
        if (copyToClipboard(*image)) {
            LOG_DEBUG("截图已复制到剪贴板");
        } else {
            LOG_WARN("复制到剪贴板失败");
        }
    }

    // 保存到文件
    if (options.saveToFile) {
        auto encoded = encodeImage(*image, options.format, options.quality);
        if (!encoded.empty()) {
            std::string path = options.savePath.empty() 
                ? generateSavePath(options.format) 
                : options.savePath;
            result.filePath = saveToFile(encoded, path, options.format);
            if (!result.filePath.empty()) {
                LOG_INFO("截图已保存: path={}, size={}KB", result.filePath, encoded.size() / 1024);
            }
        }
    }

    result.success = true;
    LOG_INFO("截图完成: {}x{}, format={}", result.imageWidth, result.imageHeight,
             formatExtension(options.format));

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 统一屏幕截取策略（加速后端 + 降级方案）
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<cv::Mat> ScreenCapture::captureScreen(const CaptureRegion& region) {
    auto backend = createCaptureBackend();
    std::string backendError;
    if (backend && backend->initialize(region, backendError)) {
        CaptureFrameView frame;
        if (backend->capture(frame, backendError) && frame.data && frame.width > 0 && frame.height > 0) {
            auto mat = std::make_unique<cv::Mat>();
            if (frame.format == CapturePixelFormat::Bgr24) {
                *mat = cv::Mat(frame.height, frame.width, CV_8UC3, frame.data, frame.stride).clone();
            } else {
                cv::Mat bgra(frame.height, frame.width, CV_8UC4, frame.data, frame.stride);
                cv::cvtColor(bgra, *mat, cv::COLOR_BGRA2BGR);
            }
            backend->releaseFrame();
            backend->shutdown();
            return mat;
        }
        backend->releaseFrame();
        backend->shutdown();
    }
    return captureScreenBitBlt(region);
}

// ─────────────────────────────────────────────────────────────────────────────
// BitBlt 屏幕截取
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<cv::Mat> ScreenCapture::captureScreenBitBlt(const CaptureRegion& region) {
    struct GdiCaptureGuard {
        HDC screen = nullptr;
        HDC memory = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        ~GdiCaptureGuard() {
            if (memory && previous && previous != HGDI_ERROR) SelectObject(memory, previous);
            if (bitmap) DeleteObject(bitmap);
            if (memory) DeleteDC(memory);
            if (screen) ReleaseDC(nullptr, screen);
        }
    } resources;

    resources.screen = GetDC(nullptr);
    if (!resources.screen) {
        LOG_ERROR("获取屏幕 DC 失败, error={}", GetLastError());
        return nullptr;
    }
    resources.memory = CreateCompatibleDC(resources.screen);
    if (!resources.memory) {
        LOG_ERROR("创建截图内存 DC 失败, error={}", GetLastError());
        return nullptr;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = region.width;
    bitmapInfo.bmiHeader.biHeight = -region.height; // top-down，避免后续翻转
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 24;           // 直接得到 OpenCV BGR
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    resources.bitmap = CreateDIBSection(
        resources.screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!resources.bitmap || !pixels) {
        LOG_ERROR("创建截图 DIBSection 失败, error={}", GetLastError());
        return nullptr;
    }
    resources.previous = SelectObject(resources.memory, resources.bitmap);
    if (!resources.previous || resources.previous == HGDI_ERROR) {
        LOG_ERROR("选择截图位图失败, error={}", GetLastError());
        return nullptr;
    }

    // BitBlt 截取屏幕区域
    BOOL ok = BitBlt(resources.memory, 0, 0, region.width, region.height,
                     resources.screen, region.x, region.y, SRCCOPY | CAPTUREBLT);

    if (!ok) {
        LOG_ERROR("BitBlt 失败, error={}", GetLastError());
        return nullptr;
    }

    // DIBSection 已经是顶向 BGR。由于位图内存随 HBITMAP 销毁，返回前仅需
    // 一次紧凑 clone；相比旧路径省去 32 位临时图和 BGRA→BGR 颜色转换。
    const size_t stride = (static_cast<size_t>(region.width) * 3U + 3U) & ~size_t{3U};
    const cv::Mat dibView(region.height, region.width, CV_8UC3, pixels, stride);
    auto image = std::make_unique<cv::Mat>(dibView.clone());

    return image;
}

// ─────────────────────────────────────────────────────────────────────────────
// 图像编码
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> ScreenCapture::encodeImage(const cv::Mat& image, ImageFormat format, int quality) {
    std::vector<uint8_t> buffer;
    std::vector<int> params;

    switch (format) {
        case ImageFormat::PNG:
            params = { cv::IMWRITE_PNG_COMPRESSION, 6 };
            cv::imencode(".png", image, buffer, params);
            break;

        case ImageFormat::JPEG:
            params = { cv::IMWRITE_JPEG_QUALITY, quality };
            cv::imencode(".jpg", image, buffer, params);
            break;

        case ImageFormat::WebP:
            params = { cv::IMWRITE_WEBP_QUALITY, quality };
            cv::imencode(".webp", image, buffer, params);
            break;

        case ImageFormat::BMP:
            cv::imencode(".bmp", image, buffer);
            break;
    }

    return buffer;
}

// ─────────────────────────────────────────────────────────────────────────────
// 剪贴板
// ─────────────────────────────────────────────────────────────────────────────

bool ScreenCapture::copyToClipboard(const cv::Mat& image) {
    // 编码为 BMP 格式（剪贴板原生支持）
    if (!OpenClipboard(nullptr)) {
        LOG_WARN("无法打开剪贴板, error={}", GetLastError());
        return false;
    }

    EmptyClipboard();

    // 创建 DIB
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = image.cols;
    bi.biHeight = -image.rows;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;         // BGR
    bi.biCompression = BI_RGB;
    int rowBytes = image.cols * 3;
    int stride = (rowBytes + 3) & ~3;  // 4 字节对齐
    bi.biSizeImage = stride * image.rows;

    size_t totalSize = sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlobal) {
        CloseClipboard();
        return false;
    }

    auto* pMem = static_cast<uint8_t*>(GlobalLock(hGlobal));
    memcpy(pMem, &bi, sizeof(bi));

    // 确保图像是连续的 BGR
    cv::Mat continuous;
    if (image.channels() == 4) {
        cv::cvtColor(image, continuous, cv::COLOR_BGRA2BGR);
    } else {
        continuous = image.isContinuous() ? image : image.clone();
    }

    // 复制像素数据（注意行对齐）
    for (int y = 0; y < continuous.rows; ++y) {
        memcpy(pMem + sizeof(bi) + y * stride, continuous.ptr(y), rowBytes);
    }

    GlobalUnlock(hGlobal);
    SetClipboardData(CF_DIB, hGlobal);
    CloseClipboard();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 文件操作
// ─────────────────────────────────────────────────────────────────────────────

std::string ScreenCapture::saveToFile(const std::vector<uint8_t>& data, 
                                       const std::string& path,
                                       [[maybe_unused]] ImageFormat format) {
    try {
        // 确保目录存在
        const auto widePath = easy::core::WinUtils::utf8ToWstring(path);
        auto dir = std::filesystem::path(widePath).parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }

        std::ofstream file(std::filesystem::path(widePath), std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("无法打开文件: {}", path);
            return "";
        }

        file.write(reinterpret_cast<const char*>(data.data()), 
                   static_cast<std::streamsize>(data.size()));
        return path;
    } catch (const std::exception& e) {
        LOG_ERROR("保存截图失败: {}", e.what());
        return "";
    }
}

std::string ScreenCapture::generateSavePath(ImageFormat format) const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm localTime{};
    localtime_s(&localTime, &time_t);

    std::ostringstream oss;
    oss << "EasyTools_" 
        << std::put_time(&localTime, "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(3) << ms.count()
        << "." << formatExtension(format);

    // 保存到用户图片目录
    auto picturesDir = easy::core::WinUtils::getAppDataDirectory() / L"Screenshots";
    return easy::core::WinUtils::wstringToUtf8((picturesDir / oss.str()).wstring());
}

std::string ScreenCapture::formatExtension(ImageFormat format) {
    switch (format) {
        case ImageFormat::PNG:  return "png";
        case ImageFormat::JPEG: return "jpg";
        case ImageFormat::WebP: return "webp";
        case ImageFormat::BMP:  return "bmp";
        default: return "png";
    }
}

}  // namespace easy::capture

