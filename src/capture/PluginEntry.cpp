#include "core/plugin/IPlugin.h"
#include "EasyToolsVersion.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/events/EventBus.h"
#include "core/events/MainThreadDispatcher.h"
#include "capture/ScreenCapture.h"
#include "capture/ScreenRecorder.h"
#include "capture/RecordingIndicator.h"
#include "capture/PinWindow.h"
#include "capture/CaptureOverlay.h"
#include "capture/CaptureHistory.h"
#include <opencv2/imgcodecs.hpp>
#include "ocr/OcrEngine.h"
#include "ocr/OcrResultWindow.h"
#include <thread>
#include <filesystem>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <fstream>
#include <iterator>
#include <windows.h>

namespace easy::capture {

namespace {

easy::core::HotkeyDef configuredHotkey(const std::string& name,
                                       const easy::core::HotkeyDef& fallback) {
    const auto text = easy::core::ConfigManager::instance().get<std::string>(
        "/hotkeys/" + name, fallback.toString());
    return easy::core::HotkeyDef::fromString(text).value_or(fallback);
}

std::string timestampedPath(const std::string& directory, const char* prefix,
                            const char* extension) {
    if (directory.empty()) return {};
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream name;
    name << prefix << std::put_time(&local, "%Y%m%d_%H%M%S") << '_'
         << std::setfill('0') << std::setw(3) << ms.count() << extension;
    const auto dir = easy::core::WinUtils::utf8ToWstring(directory);
    return easy::core::WinUtils::wstringToUtf8((std::filesystem::path(dir) / name.str()).wstring());
}

ImageFormat imageFormatFromConfig(const std::string& value) {
    if (value == "jpg" || value == "jpeg") return ImageFormat::JPEG;
    if (value == "webp") return ImageFormat::WebP;
    if (value == "bmp") return ImageFormat::BMP;
    return ImageFormat::PNG;
}

const char* imageExtension(ImageFormat format) {
    switch (format) {
        case ImageFormat::JPEG: return ".jpg";
        case ImageFormat::WebP: return ".webp";
        case ImageFormat::BMP: return ".bmp";
        default: return ".png";
    }
}

CaptureOptions configuredCaptureOptions() {
    auto& config = easy::core::ConfigManager::instance();
    CaptureOptions options;
    options.format = imageFormatFromConfig(config.get<std::string>("/capture/format", "png"));
    options.quality = std::clamp(config.get<int>("/capture/quality", 90), 1, 100);
    options.saveToFile = config.get<bool>("/capture/saveToFile", true);
    options.copyToClipboard = config.get<bool>("/capture/copyToClipboard", true);
    options.showCrosshair = config.get<bool>("/capture/showCrosshair", true);
    options.autoDetectWindow = config.get<bool>("/capture/autoDetectWindow", true);
    const auto directory = config.get<std::string>(
        "/capture/saveDirectory", config.get<std::string>("/capture/savePath", ""));
    options.savePath = timestampedPath(directory, "EasyTools_", imageExtension(options.format));
    return options;
}

RecordFormat recordFormatFromConfig(const std::string& value) {
    if (value == "mp4_h265") return RecordFormat::MP4_H265;
    if (value == "webm_vp9") return RecordFormat::WebM_VP9;
    if (value == "gif") return RecordFormat::GIF;
    return RecordFormat::MP4_H264;
}

const char* recordExtension(RecordFormat format) {
    switch (format) {
        case RecordFormat::WebM_VP9: return ".webm";
        case RecordFormat::GIF: return ".gif";
        default: return ".mp4";
    }
}

RecordOptions configuredRecordOptions(const CaptureRegion& region) {
    auto& config = easy::core::ConfigManager::instance();
    RecordOptions options;
    options.regionX = region.x;
    options.regionY = region.y;
    options.width = region.width;
    options.height = region.height;
    options.fullScreen = false;
    options.format = recordFormatFromConfig(config.get<std::string>("/recording/format", "mp4_h264"));
    options.fps = std::clamp(config.get<int>("/recording/fps", 30), 1, 120);
    options.bitrateMbps = std::clamp(config.get<int>("/recording/bitrate", 8), 1, 100);
    const auto directory = config.get<std::string>(
        "/recording/saveDirectory", config.get<std::string>("/recording/savePath", ""));
    options.outputPath = timestampedPath(directory, "record_", recordExtension(options.format));
    return options;
}

void configureRecorderStateCallback() {
    ScreenRecorder::instance().setStateCallback([](RecordState state, const RecordStats& stats) {
        easy::core::MainThreadDispatcher::instance().post([state, stats]() {
            auto& indicator = RecordingIndicator::instance();
            if (state == RecordState::Idle) indicator.hide();
            else {
                indicator.setPaused(state == RecordState::Paused);
                indicator.update(stats.durationSec, stats.frameCount);
            }
        });
    });
}

void stopRecordingWithFeedback() {
    auto& indicator = RecordingIndicator::instance();
    indicator.hide();
    const auto path = ScreenRecorder::instance().stopRecording();
    if (!path.empty()) {
        LOG_INFO("录屏已保存: {}", path);
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"录屏已保存"});
    }
}

void toggleRecording() {
    auto& recorder = ScreenRecorder::instance();
    if (recorder.state() == RecordState::Idle) {
        auto& overlay = CaptureOverlay::instance();
        overlay.setRecordCallback([](const CaptureRegion& region) {
            configureRecorderStateCallback();
            if (ScreenRecorder::instance().startRecording(configuredRecordOptions(region))) {
                RecordingIndicator::instance().show();
            } else {
                easy::core::EventBus::instance().publish(
                    easy::core::ShowToastEvent{L"无法开始录屏，请查看日志"});
            }
        });
        overlay.startSelection(configuredCaptureOptions(), OverlayMode::RecordRegion);
    } else {
        // Global hotkey and tray action are start/stop. Pause/resume remains an
        // explicit control on the always-visible recording indicator.
        stopRecordingWithFeedback();
    }
}

}  // namespace

static std::atomic<bool> g_ocrPending{false};
static std::atomic<bool> g_ocrRunning{false};
static std::jthread g_ocrWorker;

static void onCaptureCompletedForOcr(const easy::capture::CaptureResult& result) {
    if (!g_ocrPending.exchange(false)) return;
    if (!result.success || result.filePath.empty()) return;

    std::string path = result.filePath;
    if (g_ocrRunning.exchange(true)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        easy::core::EventBus::instance().publish(
            easy::core::ShowToastEvent{L"OCR 正在识别，请稍候"});
        return;
    }
    if (g_ocrWorker.joinable()) g_ocrWorker.join();
    g_ocrWorker = std::jthread([path]() {
        try {
            std::string text = easy::ocr::OcrEngine::instance().recognizeImageFile(path);
            if (text.empty()) {
                easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"未识别到文字"});
            } else {
                const bool copy = easy::core::ConfigManager::instance().get<bool>("/ocr/copyResult", true);
                const bool showResult = easy::core::ConfigManager::instance().get<bool>("/ocr/showResultWindow", true);
                if (copy) easy::core::WinUtils::copyToClipboard(text);
                if (showResult) {
                    easy::core::MainThreadDispatcher::instance().post([text = std::move(text)]() {
                        easy::ocr::OcrResultWindow::instance().showResult(text);
                    });
                } else {
                    easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{
                        copy ? L"OCR 识别完成，已复制到剪贴板" : L"OCR 识别完成"});
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("OCR 后台线程异常: {}", e.what());
            easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"OCR 识别失败"});
        } catch (...) {
            LOG_ERROR("OCR 后台线程未知异常");
            easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"OCR 识别失败"});
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        g_ocrRunning.store(false);
    });
}


static void triggerOcrCapture() {
    auto& ocr = easy::ocr::OcrEngine::instance();
    if (!ocr.isAvailable()) {
        easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"未找到 OCR 语言包"});
        return;
    }
    LOG_INFO("OCR Capture Triggered");
    auto opts = configuredCaptureOptions();
    opts.copyToClipboard = false;
    opts.saveToFile = true;
    auto tempPath = easy::core::WinUtils::getAppDataDirectory() / L"temp";
    std::filesystem::create_directories(tempPath);
    opts.savePath = easy::core::WinUtils::wstringToUtf8(
        (tempPath / (L"ocr_" + std::to_wstring(GetTickCount64()) + L".png")).wstring());
    opts.format = ImageFormat::PNG;
    g_ocrPending.store(true);
    easy::capture::ScreenCapture::instance().startCapture(opts);
}

class CapturePlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Capture"; }
    const char* getVersion() const override { return easy::version::String; }

    bool initialize() override {
        LOG_INFO("CapturePlugin: 初始化截图/录屏引擎");

        if (!initializeMarkupTextRenderer()) return false;

        HMODULE hMod = GetModuleHandleW(nullptr);

        ScreenCapture::instance().initialize(hMod);
        ScreenCapture::instance().setCallback(onCaptureCompletedForOcr);
        ScreenRecorder::instance().initialize();

        auto& indicator = RecordingIndicator::instance();
        indicator.initialize(hMod);
        indicator.onPause([]() {
            auto& recorder = ScreenRecorder::instance();
            if (recorder.state() == RecordState::Recording) recorder.pauseRecording();
            else if (recorder.state() == RecordState::Paused) recorder.resumeRecording();
        });
        indicator.onStop([]() {
            stopRecordingWithFeedback();
        });

        easy::ocr::OcrEngine::instance().initialize();

        
        auto& mb = easy::core::MessageBridge::instance();
        auto& bus = easy::core::EventBus::instance();

        // 订阅 EventBus 上的托盘/快捷键触发事件
        bus.subscribe<easy::core::ActionTriggerScreenshotEvent>([](const easy::core::ActionTriggerScreenshotEvent&) {
            g_ocrPending.store(false);
            easy::capture::ScreenCapture::instance().startCapture(configuredCaptureOptions());
        });

        bus.subscribe<easy::core::ActionToggleRecordingEvent>([](const easy::core::ActionToggleRecordingEvent&) {
            toggleRecording();
        });
        
        mb.registerHandler("capture.triggerScreenshot", [](const nlohmann::json&) -> nlohmann::json {
            g_ocrPending.store(false);
            easy::capture::ScreenCapture::instance().startCapture(configuredCaptureOptions());
            return {{"success", true}};
        });

        mb.registerHandler("capture.toggleRecording", [](const nlohmann::json&) -> nlohmann::json {
            toggleRecording();
            return {{"success", true}};
        });

        auto& hotkeys = easy::core::HotkeyManager::instance();
        hotkeys.registerHotkey("Screenshot", configuredHotkey("Screenshot", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'A'}), []() {
            g_ocrPending.store(false);
            easy::capture::ScreenCapture::instance().startCapture(configuredCaptureOptions());
        });
        hotkeys.registerHotkey("Record", configuredHotkey("Record", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'R'}), []() {
            toggleRecording();
        });
        hotkeys.registerHotkey("OCR", configuredHotkey("OCR", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'O'}), []() {
            triggerOcrCapture();
        });
        hotkeys.registerHotkey("Pin Toggle", configuredHotkey("Pin Toggle", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'X'}), []() {
            easy::capture::PinWindow::toggleClickThroughUnderCursor();
        });
        hotkeys.registerHotkey("Pin Paste", configuredHotkey("Pin Paste", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'V'}), []() {
            easy::capture::PinWindow::createFromClipboard();
        });
        hotkeys.registerHotkey("Pin Hide All", configuredHotkey("Pin Hide All", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'H'}), []() {
            easy::capture::PinWindow::toggleHideAll();
        });
        hotkeys.registerHotkey("Pin Arrange", configuredHotkey("Pin Arrange", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'G'}), []() {
            easy::capture::PinWindow::arrangeAll();
        });

        mb.registerHandler("capture.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"format", config.get<std::string>("/capture/format", "png")},
                {"quality", config.get<int>("/capture/quality", 90)},
                {"saveToFile", config.get<bool>("/capture/saveToFile", true)},
                {"copyToClipboard", config.get<bool>("/capture/copyToClipboard", true)},
                {"saveDirectory", config.get<std::string>(
                    "/capture/saveDirectory", config.get<std::string>("/capture/savePath", ""))},
                {"showCrosshair", config.get<bool>("/capture/showCrosshair", true)},
                {"autoDetectWindow", config.get<bool>("/capture/autoDetectWindow", true)}
            };
        });

        mb.registerHandler("capture.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            static const std::unordered_set<std::string> formats = {"png", "jpg", "jpeg", "webp", "bmp"};
            static const std::unordered_set<std::string> boolKeys = {
                "saveToFile", "copyToClipboard", "showCrosshair", "autoDetectWindow"
            };
            if (!params.is_object() || params.empty()) return {{"success", false}, {"error", "no settings supplied"}};
            for (const auto& [key, value] : params.items()) {
                if (key == "format" && (!value.is_string() || !formats.contains(value.get<std::string>())))
                    return {{"success", false}, {"error", "invalid image format"}};
                if (key == "quality" && (!value.is_number_integer() || value.get<int>() < 1 || value.get<int>() > 100))
                    return {{"success", false}, {"error", "quality must be between 1 and 100"}};
                if (key == "saveDirectory" && (!value.is_string() || value.get_ref<const std::string&>().size() > 32767))
                    return {{"success", false}, {"error", "invalid save directory"}};
                if (boolKeys.contains(key) && !value.is_boolean())
                    return {{"success", false}, {"error", key + " must be boolean"}};
                if (key != "format" && key != "quality" && key != "saveDirectory" && !boolKeys.contains(key))
                    return {{"success", false}, {"error", "unsupported setting: " + key}};
            }
            const bool saved = easy::core::ConfigManager::instance().mergePatch(
                {{"capture", params}}, "/capture");
            return {{"success", saved}, {"error", saved ? "" : "failed to persist settings"}};
        });

        mb.registerHandler("recording.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"format", config.get<std::string>("/recording/format", "mp4_h264")},
                {"fps", config.get<int>("/recording/fps", 30)},
                {"bitrate", config.get<int>("/recording/bitrate", 8)},
                {"saveDirectory", config.get<std::string>(
                    "/recording/saveDirectory", config.get<std::string>("/recording/savePath", ""))}
            };
        });

        mb.registerHandler("recording.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            static const std::unordered_set<std::string> formats = {"mp4_h264", "mp4_h265", "webm_vp9", "gif"};
            if (!params.is_object() || params.empty()) return {{"success", false}, {"error", "no settings supplied"}};
            for (const auto& [key, value] : params.items()) {
                if (key == "format" && (!value.is_string() || !formats.contains(value.get<std::string>())))
                    return {{"success", false}, {"error", "invalid recording format"}};
                if (key == "fps" && (!value.is_number_integer() || value.get<int>() < 1 || value.get<int>() > 120))
                    return {{"success", false}, {"error", "fps must be between 1 and 120"}};
                if (key == "bitrate" && (!value.is_number_integer() || value.get<int>() < 1 || value.get<int>() > 100))
                    return {{"success", false}, {"error", "bitrate must be between 1 and 100"}};
                if (key == "saveDirectory" && (!value.is_string() || value.get_ref<const std::string&>().size() > 32767))
                    return {{"success", false}, {"error", "invalid save directory"}};
                if (key != "format" && key != "fps" && key != "bitrate" && key != "saveDirectory")
                    return {{"success", false}, {"error", "unsupported setting: " + key}};
            }
            const bool saved = easy::core::ConfigManager::instance().mergePatch(
                {{"recording", params}}, "/recording");
            return {{"success", saved}, {"error", saved ? "" : "failed to persist settings"}};
        });

        mb.registerHandler("ocr.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"engine", "windows"},
                {"language", "system"},
                {"copyResult", config.get<bool>("/ocr/copyResult", true)},
                {"showResultWindow", config.get<bool>("/ocr/showResultWindow", true)}
            };
        });

        mb.registerHandler("ocr.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            if (!params.is_object() || params.empty()) return {{"success", false}, {"error", "no settings supplied"}};
            for (const auto& [key, value] : params.items()) {
                if ((key != "copyResult" && key != "showResultWindow") || !value.is_boolean())
                    return {{"success", false}, {"error", "unsupported OCR setting: " + key}};
            }
            const bool saved = easy::core::ConfigManager::instance().mergePatch(
                {{"ocr", params}}, "/ocr");
            return {{"success", saved}, {"error", saved ? "" : "failed to persist settings"}};
        });

        mb.registerHandler("ocr.getStatus", [](const nlohmann::json&) -> nlohmann::json {
            return {{"available", easy::ocr::OcrEngine::instance().isAvailable()}};
        });

        mb.registerHandler("ocr.recognizeImageFile", [](const nlohmann::json& params) -> nlohmann::json {
            std::string path = params.value("path", "");
            if (path.empty()) return {{"success", false}, {"error", "path is required"}};
            std::string text = easy::ocr::OcrEngine::instance().recognizeImageFile(path);
            bool copy = params.value("copyToClipboard", easy::core::ConfigManager::instance().get<bool>("/ocr/copyResult", true));
            if (copy && !text.empty()) easy::core::WinUtils::copyToClipboard(text);
            return {{"success", true}, {"text", text}, {"copied", copy && !text.empty()}};
        });

        mb.registerHandler("capture.pinImageFile", [](const nlohmann::json& params) -> nlohmann::json {
            std::string path = params.value("path", "");
            if (path.empty()) return {{"success", false}, {"error", "path is required"}};
            try {
                const auto filePath = std::filesystem::path(easy::core::WinUtils::utf8ToWstring(path));
                std::ifstream stream(filePath, std::ios::binary);
                if (!stream) return {{"success", false}, {"error", "file not found"}};
                std::vector<unsigned char> bytes(
                    (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                cv::Mat image = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
                if (!image.empty()) {
                    POINT pt;
                    GetCursorPos(&pt);
                    easy::capture::PinWindow::create(image, pt.x, pt.y);
                    return {{"success", true}};
                }
            } catch (const std::exception& e) {
                LOG_ERROR("贴图文件加载失败: {}", e.what());
                return {{"success", false}, {"error", e.what()}};
            }
            return {{"success", false}, {"error", "unsupported or invalid image"}};
        });

        mb.registerHandler("capture.pasteAsPin", [](const nlohmann::json&) -> nlohmann::json {
            auto pin = easy::capture::PinWindow::createFromClipboard();
            return {{"success", static_cast<bool>(pin)}};
        });

        mb.registerHandler("history.getAll", [](const nlohmann::json& params) -> nlohmann::json {
            auto result = nlohmann::json::parse(
                easy::capture::CaptureHistory::instance().toMetadataJson());
            if (!params.value("includeThumbnails", false)) return result;

            for (auto& metadata : result) {
                const auto entry = easy::capture::CaptureHistory::instance().get(
                    metadata.value("index", -1));
                if (!entry || entry->thumbnail.empty()) continue;
                std::vector<uint8_t> buffer;
                if (cv::imencode(".png", entry->thumbnail, buffer)) {
                    metadata["base64"] = easy::core::WinUtils::base64Encode(buffer);
                }
            }
            return result;
        });

        mb.registerHandler("history.getThumbnail", [](const nlohmann::json& params) -> nlohmann::json {
            int index = params.value("index", -1);
            if (index < 0) return {{"success", false}};
            const auto entry = easy::capture::CaptureHistory::instance().get(index);
            if (!entry || entry->thumbnail.empty()) return {{"success", false}};
            
            std::vector<uint8_t> buffer;
            cv::imencode(".png", entry->thumbnail, buffer);
            std::string base64 = easy::core::WinUtils::base64Encode(buffer);
            return {{"success", true}, {"base64", base64}};
        });

        mb.registerHandler("history.clear", [](const nlohmann::json&) -> nlohmann::json {
            easy::capture::CaptureHistory::instance().clear();
            return {{"success", true}};
        });

        mb.registerHandler("history.open", [](const nlohmann::json& params) -> nlohmann::json {
            const int index = params.value("index", -1);
            const auto entry = easy::capture::CaptureHistory::instance().get(index);
            if (!entry || entry->image.empty()) {
                return {{"success", false}, {"error", "history entry not found"}};
            }
            POINT cursor{};
            GetCursorPos(&cursor);
            const auto pin = easy::capture::PinWindow::create(entry->image, cursor.x, cursor.y);
            return {{"success", static_cast<bool>(pin)}};
        });

        return true;
    }

    void shutdown() override {
        LOG_INFO("CapturePlugin: 卸载截图/录屏引擎");
        g_ocrPending.store(false);
        if (g_ocrWorker.joinable()) g_ocrWorker.join();
        g_ocrRunning.store(false);
        PinWindow::closeAll();
        RecordingIndicator::instance().shutdown();
        ScreenRecorder::instance().shutdown();
        ScreenCapture::instance().shutdown();
        easy::ocr::OcrResultWindow::instance().cleanup();
        easy::ocr::OcrEngine::instance().shutdown();
        shutdownMarkupTextRenderer();
    }
};

} // namespace easy::capture

PLUGIN_API easy::core::IPlugin* CreatePlugin() {
    static easy::capture::CapturePlugin instance;
    return &instance;
}
