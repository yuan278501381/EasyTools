// ─────────────────────────────────────────────────────────────────────────────
// ScreenCapture.cpp — 屏幕截图核心引擎实现
//
// 截图方案:
//   - 主方案: BitBlt GDI 截图（兼容性最好）
//   - 使用 OpenCV 编码/解码图像
//   - 剪贴板: CF_DIB + CF_BITMAP 双格式写入
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/ScreenCapture.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace easy::capture {

ScreenCapture& ScreenCapture::instance() {
    static ScreenCapture inst;
    return inst;
}

bool ScreenCapture::initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    LOG_INFO("截图引擎已初始化");
    return true;
}

void ScreenCapture::shutdown() {
    LOG_INFO("截图引擎已关闭");
}

// ─────────────────────────────────────────────────────────────────────────────
// 截图入口
// ─────────────────────────────────────────────────────────────────────────────

void ScreenCapture::startCapture(const CaptureOptions& options) {
    if (m_capturing) {
        LOG_WARN("截图已在进行中");
        return;
    }

    easy::core::TraceId::Scope scope;
    m_capturing = true;

    // TODO: Phase 2.2 实现区域选择覆盖层
    // 暂时使用全屏截图
    auto result = captureFullScreen(options);

    m_capturing = false;

    if (m_callback) {
        m_callback(result);
    }
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

    if (!region.isValid()) {
        result.errorMessage = "无效的截图区域";
        LOG_ERROR("截图失败: {}", result.errorMessage);
        return result;
    }

    // 截取屏幕
    auto image = captureScreenBitBlt(region);
    if (!image || image->empty()) {
        result.errorMessage = "BitBlt 截图失败";
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
// BitBlt 屏幕截取
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<cv::Mat> ScreenCapture::captureScreenBitBlt(const CaptureRegion& region) {
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(screenDC, region.width, region.height);
    HGDIOBJ oldBitmap = SelectObject(memDC, hBitmap);

    // BitBlt 截取屏幕区域
    BOOL ok = BitBlt(memDC, 0, 0, region.width, region.height,
                     screenDC, region.x, region.y, SRCCOPY | CAPTUREBLT);

    if (!ok) {
        SelectObject(memDC, oldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        LOG_ERROR("BitBlt 失败, error={}", GetLastError());
        return nullptr;
    }

    // 将 HBITMAP 数据读入 cv::Mat
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = region.width;
    bi.biHeight = -region.height;  // 负值 = top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;  // BGRA
    bi.biCompression = BI_RGB;

    auto image = std::make_unique<cv::Mat>(region.height, region.width, CV_8UC4);

    GetDIBits(memDC, hBitmap, 0, region.height, image->data,
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    // 清理 GDI 资源
    SelectObject(memDC, oldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    // 转为 BGR（去掉 Alpha 通道，OpenCV 标准格式）
    cv::Mat bgr;
    cv::cvtColor(*image, bgr, cv::COLOR_BGRA2BGR);
    *image = bgr;

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
    bi.biSizeImage = image.cols * image.rows * 3;

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
    int rowBytes = image.cols * 3;
    int stride = (rowBytes + 3) & ~3;  // 4 字节对齐

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
        auto dir = std::filesystem::path(path).parent_path();
        if (!dir.empty()) {
            std::filesystem::create_directories(dir);
        }

        std::ofstream file(path, std::ios::binary);
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
    return (picturesDir / oss.str()).string();
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
