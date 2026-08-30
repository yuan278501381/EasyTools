// ─────────────────────────────────────────────────────────────────────────────
// OcrEngine.cpp — 基于 Windows.Media.Ocr (WinRT) 的离线文字识别
//
// 选型理由 (相较 PaddleOCR Lite 外部进程方案):
//   • 完全离线、随系统内置，无需打包 ~30MB 模型与 exe;
//   • 官方 API，支持 25+ 语言 (依赖已安装的语言包);
//   • 与需求最低版本 Windows 10 1903 完全吻合。
//
// cv::Mat (BGR/BGRA/灰度) → SoftwareBitmap(Bgra8) → OcrEngine.RecognizeAsync。
// ─────────────────────────────────────────────────────────────────────────────

#include "ocr/OcrEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>

#include <unknwn.h>
#include <windows.h>
#include <MemoryBuffer.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <fstream>
#include <iterator>

namespace easy::ocr {

namespace wf  = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmo = winrt::Windows::Media::Ocr;
namespace wgl = winrt::Windows::Globalization;

namespace {

/// 确保当前线程已进入 COM/WinRT 公寓 (OCR 可能在工作线程上被调用)。
void ensureApartment() {
    static thread_local bool inited = false;
    if (inited) return;
    inited = true;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (winrt::hresult_error const&) {
        // 线程已用其它模型初始化亦可正常调用 OCR，忽略。
    }
}

/// cv::Mat → SoftwareBitmap(Bgra8)。失败返回 nullptr。
wgi::SoftwareBitmap matToBitmap(const cv::Mat& src) {
    cv::Mat bgra;
    switch (src.channels()) {
        case 4: bgra = src; break;
        case 3: cv::cvtColor(src, bgra, cv::COLOR_BGR2BGRA); break;
        case 1: cv::cvtColor(src, bgra, cv::COLOR_GRAY2BGRA); break;
        default: return nullptr;
    }
    if (!bgra.isContinuous()) bgra = bgra.clone();

    wgi::SoftwareBitmap bitmap(wgi::BitmapPixelFormat::Bgra8, bgra.cols, bgra.rows,
                               wgi::BitmapAlphaMode::Premultiplied);
    {
        wgi::BitmapBuffer buffer = bitmap.LockBuffer(wgi::BitmapBufferAccessMode::Write);
        wf::IMemoryBufferReference ref = buffer.CreateReference();
        auto byteAccess = ref.as<::Windows::Foundation::IMemoryBufferByteAccess>();

        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        winrt::check_hresult(byteAccess->GetBuffer(&data, &capacity));

        wgi::BitmapPlaneDescription desc = buffer.GetPlaneDescription(0);
        const size_t rowBytes = static_cast<size_t>(bgra.cols) * 4;
        for (int y = 0; y < bgra.rows; ++y) {
            std::memcpy(data + desc.StartIndex + static_cast<size_t>(desc.Stride) * y,
                        bgra.ptr(y), rowBytes);
        }
    }
    return bitmap;
}

/// 创建一个 OCR 引擎: 优先用户语言, 回退英文。
wmo::OcrEngine createEngine() {
    if (auto e = wmo::OcrEngine::TryCreateFromUserProfileLanguages()) return e;
    try {
        return wmo::OcrEngine::TryCreateFromLanguage(wgl::Language(L"en-US"));
    } catch (winrt::hresult_error const&) {
        return nullptr;
    }
}

}  // namespace

OcrEngine& OcrEngine::instance() {
    static OcrEngine inst;
    return inst;
}

bool OcrEngine::initialize() {
    try {
        ensureApartment();
        m_available = static_cast<bool>(createEngine());
        if (m_available) {
            LOG_INFO("OCR 引擎已就绪 (Windows.Media.Ocr)");
        } else {
            LOG_WARN("未检测到 OCR 语言包，OCR 功能不可用。可在系统设置中添加语言的“可选功能 → 光学字符识别”");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OCR 引擎初始化异常: {}", e.what());
        m_available = false;
    }
    return m_available;
}

void OcrEngine::shutdown() {
    LOG_INFO("OCR 引擎已关闭");
}

std::vector<OcrResult> OcrEngine::extractText(const cv::Mat& image) {
    easy::core::TraceId::Scope scope;
    std::vector<OcrResult> results;
    if (image.empty()) {
        LOG_WARN("OCR 输入图像为空");
        return results;
    }

    try {
        ensureApartment();
        wmo::OcrEngine engine = createEngine();
        if (!engine) {
            LOG_ERROR("OCR 不可用: 无语言包");
            return results;
        }

        wgi::SoftwareBitmap bitmap = matToBitmap(image);
        if (!bitmap) {
            LOG_ERROR("OCR: 不支持的图像通道数: {}", image.channels());
            return results;
        }

        wmo::OcrResult result = engine.RecognizeAsync(bitmap).get();

        for (auto const& line : result.Lines()) {
            OcrResult r;
            r.text = winrt::to_string(line.Text());
            r.confidence = 1.0f;  // WinRT OCR 不提供置信度

            float minX = FLT_MAX, minY = FLT_MAX, maxX = 0.0f, maxY = 0.0f;
            bool any = false;
            for (auto const& w : line.Words()) {
                wf::Rect br = w.BoundingRect();
                minX = std::min(minX, br.X);
                minY = std::min(minY, br.Y);
                maxX = std::max(maxX, br.X + br.Width);
                maxY = std::max(maxY, br.Y + br.Height);
                any = true;
            }
            if (any) {
                r.box = cv::Rect(static_cast<int>(minX), static_cast<int>(minY),
                                 static_cast<int>(maxX - minX), static_cast<int>(maxY - minY));
            }
            results.push_back(std::move(r));
        }

        LOG_INFO("OCR 完成, 识别行数: {}", results.size());
    } catch (winrt::hresult_error const& e) {
        LOG_ERROR("OCR WinRT 错误: 0x{:08X} {}",
                  static_cast<uint32_t>(e.code()),
                  easy::core::WinUtils::wstringToUtf8(e.message().c_str()));
    } catch (const std::exception& e) {
        LOG_ERROR("OCR 异常: {}", e.what());
    }
    return results;
}

std::string OcrEngine::recognizeToText(const cv::Mat& image) {
    std::string text;
    for (auto const& r : extractText(image)) {
        if (!text.empty()) text += '\n';
        text += r.text;
    }
    return text;
}

std::string OcrEngine::recognizeImageFile(const std::string& utf8Path) {
    cv::Mat img;
    try {
        // OpenCV imread 需要本地编码路径; 用宽字符读文件再解码以支持非 ASCII 路径。
        std::wstring wpath = easy::core::WinUtils::utf8ToWstring(utf8Path);
        std::ifstream f(wpath.c_str(), std::ios::binary);
        if (f) {
            std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
            if (!buf.empty()) img = cv::imdecode(buf, cv::IMREAD_COLOR);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OCR 读取图片失败: {} ({})", utf8Path, e.what());
    }
    if (img.empty()) {
        LOG_WARN("OCR: 无法加载图片 {}", utf8Path);
        return {};
    }
    return recognizeToText(img);
}

}  // namespace easy::ocr
