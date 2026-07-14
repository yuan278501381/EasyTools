#include "core/plugin/IPlugin.h"
#include "core/logger/Logger.h"
#include "core/ipc/MessageBridge.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/events/EventBus.h"
#include "capture/ScreenCapture.h"
#include "capture/ScreenRecorder.h"
#include "capture/RecordingIndicator.h"
#include "capture/PinWindow.h"
#include "capture/CaptureOverlay.h"
#include "capture/CaptureHistory.h"
#include <opencv2/imgcodecs.hpp>
#include "ocr/OcrEngine.h"
#include <thread>
#include <filesystem>
#include <atomic>
#include <windows.h>

namespace easy::capture {

static std::atomic<bool> g_ocrPending{false};

static void onCaptureCompletedForOcr(const easy::capture::CaptureResult& result) {
    if (!g_ocrPending.exchange(false)) return;
    if (!result.success || result.filePath.empty()) return;

    std::string path = result.filePath;
    std::thread([path]() {
        try {
            std::string text = easy::ocr::OcrEngine::instance().recognizeImageFile(path);
            std::error_code ec;
            std::filesystem::remove(path, ec);

            if (text.empty()) {
                easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"未识别到文字"});
                return;
            }
            easy::core::WinUtils::copyToClipboard(text);
            easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"OCR 识别完成，已复制到剪贴板"});
        } catch (const std::exception& e) {
            LOG_ERROR("OCR 后台线程异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("OCR 后台线程未知异常");
        }
    }).detach();
}


static void triggerOcrCapture() {
    auto& ocr = easy::ocr::OcrEngine::instance();
    if (!ocr.isAvailable()) {
        easy::core::EventBus::instance().publish(easy::core::ShowToastEvent{L"未找到 OCR 语言包"});
        return;
    }
    LOG_INFO("OCR Capture Triggered");
    easy::capture::CaptureOptions opts;
    g_ocrPending.store(true);
    easy::capture::ScreenCapture::instance().startCapture(opts);
}

class CapturePlugin : public easy::core::IPlugin {
public:
    const char* getName() const override { return "Capture"; }
    const char* getVersion() const override { return "1.0.0"; }

    bool initialize() override {
        LOG_INFO("CapturePlugin: 初始化截图/录屏引擎");

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
            RecordingIndicator::instance().hide();
            auto path = ScreenRecorder::instance().stopRecording();
            LOG_INFO("录屏已保存: {}", path);
        });

        easy::ocr::OcrEngine::instance().initialize();

        
        auto& mb = easy::core::MessageBridge::instance();
        auto& bus = easy::core::EventBus::instance();

        // 订阅 EventBus 上的托盘/快捷键触发事件
        bus.subscribe<easy::core::ActionTriggerScreenshotEvent>([](const easy::core::ActionTriggerScreenshotEvent&) {
            easy::capture::CaptureOptions opts;
            easy::capture::ScreenCapture::instance().startCapture(opts);
        });

        bus.subscribe<easy::core::ActionToggleRecordingEvent>([](const easy::core::ActionToggleRecordingEvent&) {
            auto& recorder = easy::capture::ScreenRecorder::instance();
            if (recorder.state() == easy::capture::RecordState::Idle) {
                easy::capture::CaptureOptions opts;
                auto& overlay = easy::capture::CaptureOverlay::instance();
                overlay.setRecordCallback([&recorder](const easy::capture::CaptureRegion& region) {
                    easy::capture::RecordOptions recOpts;
                    recOpts.regionX = region.x;
                    recOpts.regionY = region.y;
                    recOpts.width = region.width;
                    recOpts.height = region.height;
                    recOpts.fullScreen = false;
                    
                    auto tempPath = easy::core::WinUtils::getAppDataDirectory() / L"temp";
                    std::filesystem::create_directories(tempPath);
                    auto file = tempPath / (L"record_" + std::to_wstring(GetTickCount64()) + L".mp4");
                    recOpts.outputPath = easy::core::WinUtils::wstringToUtf8(file.wstring());

                    recorder.setStateCallback([](easy::capture::RecordState state, const easy::capture::RecordStats& stats) {
                        easy::capture::RecordingIndicator::instance().setPaused(state == easy::capture::RecordState::Paused);
                    });
                    recorder.startRecording(recOpts);
                    easy::capture::RecordingIndicator::instance().show();
                });
                overlay.startSelection(opts, easy::capture::OverlayMode::RecordRegion);
            } else if (recorder.state() == easy::capture::RecordState::Recording) {
                recorder.pauseRecording();
            } else if (recorder.state() == easy::capture::RecordState::Paused) {
                recorder.resumeRecording();
            }
        });
        
        mb.registerHandler("capture.triggerScreenshot", [](const nlohmann::json&) -> nlohmann::json {
            easy::capture::CaptureOptions opts;
            easy::capture::ScreenCapture::instance().startCapture(opts);
            return {{"success", true}};
        });

        mb.registerHandler("capture.toggleRecording", [](const nlohmann::json&) -> nlohmann::json {
            auto& recorder = easy::capture::ScreenRecorder::instance();
            if (recorder.state() == easy::capture::RecordState::Idle) {
                easy::capture::CaptureOptions opts;
                auto& overlay = easy::capture::CaptureOverlay::instance();
                overlay.setRecordCallback([&recorder](const easy::capture::CaptureRegion& region) {
                    easy::capture::RecordOptions recOpts;
                    recOpts.regionX = region.x;
                    recOpts.regionY = region.y;
                    recOpts.width = region.width;
                    recOpts.height = region.height;
                    recOpts.fullScreen = false;
                    
                    auto tempPath = easy::core::WinUtils::getAppDataDirectory() / L"temp";
                    std::filesystem::create_directories(tempPath);
                    auto file = tempPath / (L"record_" + std::to_wstring(GetTickCount64()) + L".mp4");
                    recOpts.outputPath = easy::core::WinUtils::wstringToUtf8(file.wstring());

                    recorder.setStateCallback([](easy::capture::RecordState state, const easy::capture::RecordStats& stats) {
                        easy::capture::RecordingIndicator::instance().setPaused(state == easy::capture::RecordState::Paused);
                    });
                    recorder.startRecording(recOpts);
                    easy::capture::RecordingIndicator::instance().show();
                });
                overlay.startSelection(opts, easy::capture::OverlayMode::RecordRegion);
            } else if (recorder.state() == easy::capture::RecordState::Recording) {
                recorder.pauseRecording();
            } else if (recorder.state() == easy::capture::RecordState::Paused) {
                recorder.resumeRecording();
            }
            return {{"success", true}};
        });

        auto& hotkeys = easy::core::HotkeyManager::instance();
        hotkeys.registerHotkey("Screenshot", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'A'}, []() {
            easy::capture::CaptureOptions opts;
            easy::capture::ScreenCapture::instance().startCapture(opts);
        });
        hotkeys.registerHotkey("Record", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'R'}, []() {
            auto& mb = easy::core::MessageBridge::instance();
            mb.handleMessage("{\"method\":\"capture.toggleRecording\"}");
        });
        hotkeys.registerHotkey("OCR", {easy::core::ModKey::Ctrl | easy::core::ModKey::Shift, 'O'}, []() {
            triggerOcrCapture();
        });
        hotkeys.registerHotkey("Pin Toggle", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'X'}, []() {
            easy::capture::PinWindow::toggleClickThroughUnderCursor();
        });
        hotkeys.registerHotkey("Pin Paste", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'V'}, []() {
            easy::capture::PinWindow::createFromClipboard();
        });
        hotkeys.registerHotkey("Pin Hide All", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'H'}, []() {
            easy::capture::PinWindow::toggleHideAll();
        });
        hotkeys.registerHotkey("Pin Arrange", {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt | easy::core::ModKey::Shift, 'G'}, []() {
            easy::capture::PinWindow::arrangeAll();
        });

        mb.registerHandler("capture.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"format", config.get<std::string>("/capture/format", "png")},
                {"quality", config.get<int>("/capture/quality", 90)},
                {"saveToFile", config.get<bool>("/capture/saveToFile", true)},
                {"copyToClipboard", config.get<bool>("/capture/copyToClipboard", true)},
                {"savePath", config.get<std::string>("/capture/savePath", "")},
                {"showCrosshair", config.get<bool>("/capture/showCrosshair", true)},
                {"autoDetectWindow", config.get<bool>("/capture/autoDetectWindow", true)}
            };
        });

        mb.registerHandler("capture.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            for (auto& [key, value] : params.items()) config.set("/capture/" + key, value);
            return {{"success", true}};
        });

        mb.registerHandler("recording.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"format", config.get<std::string>("/recording/format", "mp4_h264")},
                {"fps", config.get<int>("/recording/fps", 30)},
                {"bitrate", config.get<int>("/recording/bitrate", 8)},
                {"includeAudio", config.get<bool>("/recording/includeAudio", false)},
                {"savePath", config.get<std::string>("/recording/savePath", "")}
            };
        });

        mb.registerHandler("recording.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            for (auto& [key, value] : params.items()) config.set("/recording/" + key, value);
            return {{"success", true}};
        });

        mb.registerHandler("ocr.getSettings", [](const nlohmann::json&) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            return {
                {"engine", config.get<std::string>("/ocr/engine", "paddleocr")},
                {"language", config.get<std::string>("/ocr/language", "ch")},
                {"autoOcr", config.get<bool>("/ocr/autoOcr", false)},
                {"copyResult", config.get<bool>("/ocr/copyResult", true)}
            };
        });

        mb.registerHandler("ocr.updateSettings", [](const nlohmann::json& params) -> nlohmann::json {
            auto& config = easy::core::ConfigManager::instance();
            for (auto& [key, value] : params.items()) config.set("/ocr/" + key, value);
            return {{"success", true}};
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
            if (path.empty()) return {{"success", false}};
            try {
                cv::Mat image = cv::imread(path, cv::IMREAD_UNCHANGED);
                if (!image.empty()) {
                    POINT pt;
                    GetCursorPos(&pt);
                    easy::capture::PinWindow::create(image, pt.x, pt.y);
                    return {{"success", true}};
                }
            } catch(...) {}
            return {{"success", false}};
        });

        mb.registerHandler("history.getAll", [](const nlohmann::json&) -> nlohmann::json {
            std::string jsonStr = easy::capture::CaptureHistory::instance().toMetadataJson();
            return nlohmann::json::parse(jsonStr);
        });

        mb.registerHandler("history.getThumbnail", [](const nlohmann::json& params) -> nlohmann::json {
            int index = params.value("index", -1);
            if (index < 0) return {{"success", false}};
            const auto* entry = easy::capture::CaptureHistory::instance().get(index);
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

        return true;
    }

    void shutdown() override {
        LOG_INFO("CapturePlugin: 卸载截图/录屏引擎");
        PinWindow::closeAll();
        RecordingIndicator::instance().shutdown();
        ScreenRecorder::instance().shutdown();
        ScreenCapture::instance().shutdown();
        easy::ocr::OcrEngine::instance().shutdown();
    }
};

} // namespace easy::capture

PLUGIN_API easy::core::IPlugin* CreatePlugin() {
    static easy::capture::CapturePlugin instance;
    return &instance;
}
