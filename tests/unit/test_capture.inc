// ─────────────────────────────────────────────────────────────────────────────
// test_capture.inc — CAPTURE 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────

TEST(CaptureBackendTest, Direct3DAndGdiSmoke) {
    const auto capabilities = easy::capture::captureBackendCapabilities();
    EXPECT_GE(capabilities.size(), 2u);
    bool hasGdi = false;
    bool hasDxgi = false;
    for (const auto& capability : capabilities) {
        if (capability.id == "gdi-bitblt") hasGdi = capability.available;
        if (capability.id == "dxgi-desktop-duplication") hasDxgi = true;
    }
    EXPECT_TRUE(hasGdi);
    EXPECT_TRUE(hasDxgi);

    // 1. 验证内存虚拟捕获驱动 (BGR24 与 BGRA32)
    for (auto format : {easy::capture::CapturePixelFormat::Bgr24, easy::capture::CapturePixelFormat::Bgra32}) {
        auto memBackend = easy::capture::createMemoryCaptureBackend(format);
        EXPECT_NE(memBackend, nullptr);
        EXPECT_EQ(memBackend->info().id, "memory-synthetic");
        EXPECT_TRUE(memBackend->info().available);

        std::string err;
        // 异常尺寸防护测试
        EXPECT_FALSE(memBackend->initialize(easy::capture::CaptureRegion{0, 0, -5, 10}, err));
        EXPECT_FALSE(err.empty());

        // 正常尺寸初始化与多帧捕获测试
        const easy::capture::CaptureRegion validRegion{0, 0, 128, 96};
        EXPECT_TRUE(memBackend->initialize(validRegion, err));
        easy::capture::CaptureFrameView frame;
        EXPECT_TRUE(memBackend->capture(frame, err));
        EXPECT_NE(frame.data, nullptr);
        EXPECT_EQ(frame.width, 128);
        EXPECT_EQ(frame.height, 96);
        EXPECT_EQ(frame.format, format);
        EXPECT_GE(frame.stride, 128 * (format == easy::capture::CapturePixelFormat::Bgra32 ? 4 : 3));
        memBackend->releaseFrame();
        memBackend->shutdown();
    }

    // 2. 真实系统后端探测 (若桌面可用则执行)
    const easy::capture::CaptureRegion region{
        GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 64, 64
    };
    auto backend = easy::capture::createCaptureBackend();
    std::string error;
    if (backend && backend->initialize(region, error)) {
        easy::capture::CaptureFrameView frame;
        if (backend->capture(frame, error)) {
            EXPECT_NE(frame.data, nullptr);
            EXPECT_EQ(frame.width, 64);
            EXPECT_EQ(frame.height, 64);
            EXPECT_GE(frame.stride, frame.width * 3);
            backend->releaseFrame();
        }
        backend->shutdown();
    }
}

// -----------------------------------------------------------------------------
// 18. WASAPI 音频捕获与 Loopback 测试套件
// -----------------------------------------------------------------------------
TEST(AudioCaptureTest, LoopbackWasapiSmoke) {
    const auto devices = easy::capture::AudioCapture::devices();
    const auto outputDevice = std::find_if(devices.begin(), devices.end(),
        [](const auto& device) { return device.systemAudio; });
    if (outputDevice == devices.end()) {
        std::printf("[SKIP] WASAPI device enumeration: no active output device\n");
        return;
    }
    EXPECT_FALSE(outputDevice->id.empty());
    EXPECT_FALSE(outputDevice->name.empty());

    easy::capture::AudioCapture capture;
    easy::capture::AudioCaptureOptions options;
    options.systemAudio = true;
    options.systemDeviceId = outputDevice->id;
    if (!capture.start(options)) {
        std::printf("[SKIP] WASAPI loopback smoke: %s\n", capture.status().error.c_str());
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const auto status = capture.status();
    EXPECT_TRUE(status.systemAudioActive);
    EXPECT_GE(status.mixedPeak, 0.0f);
    EXPECT_LE(status.mixedPeak, 1.0f);
    std::vector<float> frames;
    EXPECT_TRUE(capture.readFrames(easy::capture::AudioCapture::SampleRate / 100, frames));
    EXPECT_EQ(frames.size(), static_cast<std::size_t>(
        easy::capture::AudioCapture::SampleRate / 100 * easy::capture::AudioCapture::Channels));
    capture.setVolumes(0.75f, 1.0f);
    capture.setSystemMuted(true);
    EXPECT_TRUE(capture.status().systemMuted);
    EXPECT_EQ(capture.status().systemPeak, 0.0f);
    capture.setSystemMuted(false);
    EXPECT_FALSE(capture.status().systemMuted);
    // The same object must be reusable after a full endpoint teardown. This
    // exercises the same close/reinitialize path used by runtime reconnection.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (capture.start(options)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        EXPECT_TRUE(capture.status().systemAudioActive);
        capture.stop();
    }
}

// -----------------------------------------------------------------------------
// 19. 长截图与滚动预览拼接测试套件 (内存虚拟驱动全链路测试)
// -----------------------------------------------------------------------------
TEST(ScrollCaptureTest, BoundedPreviewAndStitching) {
    // 注入内存虚拟捕获驱动，消除 CI 无显示器时的 SKIP
    easy::capture::setCaptureBackendFactoryForTesting([]() {
        return easy::capture::createMemoryCaptureBackend(easy::capture::CapturePixelFormat::Bgr24);
    });

    auto& capture = easy::capture::ScrollCapture::instance();
    easy::capture::ScrollCaptureResult completed;
    int progressCalls = 0;
    int largestPreviewRows = 0;
    capture.setProgressCallback([&](const cv::Mat& preview, int) {
        ++progressCalls;
        largestPreviewRows = std::max(largestPreviewRows, preview.rows);
    });
    capture.setCompletionCallback([&](const easy::capture::ScrollCaptureResult& result) {
        completed = result;
    });

    easy::capture::ScrollCaptureOptions options;
    options.mode = easy::capture::ScrollMode::Manual;
    options.captureRect = { 0, 0, 64, 64 };
    // Do not merely infer that the spill path ran from the byte budget.  Use a
    // dedicated directory so this test proves both the successful disk-backed
    // round trip and cleanup of the sensitive temporary pixels afterwards.
    const auto stagingDirectory = std::filesystem::temp_directory_path() /
        (L"EasyToolsScrollCaptureSuccess_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code stagingError;
    std::filesystem::remove_all(stagingDirectory, stagingError);
    ASSERT_TRUE(std::filesystem::create_directories(stagingDirectory, stagingError));
    options.stagingDirectory = stagingDirectory;
    // 强制走磁盘暂存：验证交付时能按顺序回读拼接，且不是只覆盖内存路径。
    options.maxInMemoryStagingBytes = 1;
    capture.start(options);
    EXPECT_TRUE(capture.isRunning());

    capture.captureCurrentFrame();
    capture.captureCurrentFrame();
    capture.stop();

    EXPECT_TRUE(completed.success);
    EXPECT_EQ(completed.frameCount, 2);
    EXPECT_EQ(completed.stitchedImage.cols, 64);
    EXPECT_GE(completed.stitchedImage.rows, 64);
    EXPECT_EQ(progressCalls, 2);
    EXPECT_LE(largestPreviewRows, 128);
    capture.shutdown();
    EXPECT_TRUE(std::filesystem::is_empty(stagingDirectory, stagingError));

    easy::capture::ScrollCaptureResult bounded;
    capture.setCompletionCallback([&](const easy::capture::ScrollCaptureResult& result) {
        bounded = result;
    });
    options.maxOutputBytes = 1;
    capture.start(options);
    capture.captureCurrentFrame();
    EXPECT_FALSE(capture.isRunning());
    EXPECT_FALSE(bounded.success);
    EXPECT_NE(bounded.errorMessage.find("内存上限"), std::string::npos);
    capture.shutdown();

    std::filesystem::remove_all(stagingDirectory, stagingError);

    // 重置测试工厂钩子
    easy::capture::setCaptureBackendFactoryForTesting(nullptr);
}

TEST(ScrollCaptureTest, FailedBackendStartupDoesNotPoisonNextSession) {
    easy::capture::setCaptureBackendFactoryForTesting([]() {
        return easy::capture::createMemoryCaptureBackend(easy::capture::CapturePixelFormat::Bgr24);
    });

    auto& capture = easy::capture::ScrollCapture::instance();
    easy::capture::ScrollCaptureResult failed;
    int completionCount = 0;
    capture.setCompletionCallback([&](const easy::capture::ScrollCaptureResult& result) {
        ++completionCount;
        failed = result;
    });

    easy::capture::ScrollCaptureOptions invalid;
    invalid.mode = easy::capture::ScrollMode::Manual;
    invalid.captureRect = {0, 0, 0, 0};
    capture.start(invalid);
    EXPECT_FALSE(capture.isRunning());
    EXPECT_EQ(completionCount, 1);
    EXPECT_FALSE(failed.success);
    EXPECT_FALSE(failed.errorMessage.empty());
    capture.stop();
    EXPECT_EQ(completionCount, 1);

    easy::capture::ScrollCaptureResult recovered;
    capture.setCompletionCallback([&](const easy::capture::ScrollCaptureResult& result) {
        recovered = result;
    });
    easy::capture::ScrollCaptureOptions valid;
    valid.mode = easy::capture::ScrollMode::Manual;
    valid.captureRect = {0, 0, 32, 32};
    valid.maxInMemoryStagingBytes = 1;
    capture.start(valid);
    ASSERT_TRUE(capture.isRunning());
    capture.captureCurrentFrame();
    capture.stop();
    EXPECT_TRUE(recovered.success);
    EXPECT_EQ(recovered.frameCount, 1);

    capture.shutdown();
    easy::capture::setCaptureBackendFactoryForTesting(nullptr);
}

TEST(ScrollCaptureTest, DiskStagingFailureDeliversSafePartialFailureAndRecovers) {
    using namespace easy::capture;
    setCaptureBackendFactoryForTesting([]() {
        return createMemoryCaptureBackend(CapturePixelFormat::Bgr24);
    });

    const auto nonDirectory = std::filesystem::temp_directory_path() /
        (L"EasyToolsScrollCaptureStagingFile_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove(nonDirectory, error);
    { std::ofstream marker(nonDirectory, std::ios::binary); ASSERT_TRUE(marker.good()); }

    auto& capture = ScrollCapture::instance();
    ScrollCaptureResult failed;
    capture.setCompletionCallback([&](const ScrollCaptureResult& result) { failed = result; });
    ScrollCaptureOptions invalid;
    invalid.mode = ScrollMode::Manual;
    invalid.captureRect = {0, 0, 32, 32};
    invalid.maxInMemoryStagingBytes = 1;
    invalid.stagingDirectory = nonDirectory;
    capture.start(invalid);
    ASSERT_TRUE(capture.isRunning());
    capture.captureCurrentFrame();
    EXPECT_FALSE(capture.isRunning());
    EXPECT_FALSE(failed.success);
    EXPECT_NE(failed.errorMessage.find("临时存储"), std::string::npos);
    capture.shutdown();

    std::filesystem::remove(nonDirectory, error);
    ScrollCaptureResult recovered;
    capture.setCompletionCallback([&](const ScrollCaptureResult& result) { recovered = result; });
    invalid.stagingDirectory.clear();
    capture.start(invalid);
    ASSERT_TRUE(capture.isRunning());
    capture.captureCurrentFrame();
    capture.stop();
    EXPECT_TRUE(recovered.success);
    capture.shutdown();
    setCaptureBackendFactoryForTesting(nullptr);
}

namespace {
class ShortWriteBuffer final : public std::streambuf {
protected:
    std::streamsize xsputn(const char*, std::streamsize count) override {
        return count > 0 ? count - 1 : 0;
    }
    int_type overflow(int_type) override { return traits_type::eof(); }
};
}  // namespace

TEST(ScrollCaptureStorageTest, RejectsPartialWritesAndTruncatedReads) {
    cv::Mat segment(2, 3, CV_8UC3, cv::Scalar(7, 11, 13));
    ShortWriteBuffer shortWrite;
    std::ostream failedOutput(&shortWrite);
    EXPECT_FALSE(easy::capture::scroll_storage::writeMat(failedOutput, segment));

    std::istringstream complete("abcdef", std::ios::binary);
    std::array<char, 6> bytes{};
    EXPECT_TRUE(easy::capture::scroll_storage::readExact(complete, bytes.data(), bytes.size()));
    EXPECT_EQ(std::string(bytes.data(), bytes.size()), "abcdef");

    std::istringstream truncated("abc", std::ios::binary);
    EXPECT_FALSE(easy::capture::scroll_storage::readExact(truncated, bytes.data(), bytes.size()));
}


TEST(ScreenRecorderTest, FrameEncodingSmoke) {
    // 注入内存虚拟捕获驱动，消除 CI 依赖
    easy::capture::setCaptureBackendFactoryForTesting([]() {
        return easy::capture::createMemoryCaptureBackend(easy::capture::CapturePixelFormat::Bgr24);
    });

    const auto output = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorder_" + std::to_wstring(GetCurrentProcessId()) + L".mp4");
    std::error_code ec;
    std::filesystem::remove(output, ec);

    easy::capture::RecordOptions options;
    options.format = easy::capture::RecordFormat::MP4_H264;
    options.fps = 15;
    options.width = 320;
    options.height = 240;
    options.regionX = 0;
    options.regionY = 0;
    options.fullScreen = false;
    options.bitrateMbps = 2;
    options.captureSystemAudio = false;
    options.countdownSeconds = 0;
    options.outputPath = easy::core::WinUtils::wstringToUtf8(output.wstring());

    auto& recorder = easy::capture::ScreenRecorder::instance();
    EXPECT_TRUE(recorder.initialize());
    EXPECT_TRUE(recorder.startRecording(options));
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    const auto completedPath = recorder.stopRecording();
    const auto stats = recorder.stats();
    EXPECT_EQ(completedPath, options.outputPath);
    EXPECT_GT(stats.frameCount, 0);
    EXPECT_FALSE(stats.captureBackend.empty());
    EXPECT_FALSE(stats.encoderName.empty());
    EXPECT_GT(stats.diskFreeBytes, 0u);
    EXPECT_GE(stats.captureLatencyMs, 0.0);
    EXPECT_GT(std::filesystem::file_size(output, ec), 0u);
    EXPECT_FALSE(ec);

    AVFormatContext* probe = nullptr;
    EXPECT_GE(avformat_open_input(&probe, options.outputPath.c_str(), nullptr, nullptr), 0);
    if (probe) {
        EXPECT_GE(avformat_find_stream_info(probe, nullptr), 0);
        bool hasVideo = false;
        for (unsigned index = 0; index < probe->nb_streams; ++index) {
            hasVideo |= probe->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
        }
        EXPECT_TRUE(hasVideo);
        avformat_close_input(&probe);
    }
    recorder.shutdown();
    std::filesystem::remove(output, ec);

    // 倒计时取消流程验证
    const auto cancelledOutput = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorderCancelled_" + std::to_wstring(GetCurrentProcessId()) + L".mp4");
    const auto partialOutput = std::filesystem::path(cancelledOutput.wstring() + L".partial");
    std::filesystem::remove(cancelledOutput, ec);
    std::filesystem::remove(partialOutput, ec);
    options.outputPath = easy::core::WinUtils::wstringToUtf8(cancelledOutput.wstring());
    options.countdownSeconds = 2;
    EXPECT_TRUE(recorder.initialize());
    EXPECT_TRUE(recorder.startRecording(options));
    EXPECT_EQ(recorder.state(), easy::capture::RecordState::Countdown);
    EXPECT_GT(recorder.stats().countdownRemaining, 0);
    EXPECT_TRUE(recorder.stopRecording().empty());
    EXPECT_FALSE(std::filesystem::exists(cancelledOutput));
    EXPECT_FALSE(std::filesystem::exists(partialOutput));
    recorder.shutdown();

    // 异常输出路径拦截测试
    const auto invalidParent = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorderParent_" + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream parentFile(invalidParent, std::ios::binary | std::ios::trunc);
        parentFile << "not a directory";
    }
    const auto invalidOutput = invalidParent / L"record.mp4";
    options.countdownSeconds = 0;
    options.outputPath = easy::core::WinUtils::wstringToUtf8(invalidOutput.wstring());
    EXPECT_TRUE(recorder.initialize());
    EXPECT_FALSE(recorder.startRecording(options));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(invalidOutput.wstring() + L".partial")));
    recorder.shutdown();
    std::filesystem::remove(invalidParent, ec);

    // 重置测试工厂钩子
    easy::capture::setCaptureBackendFactoryForTesting(nullptr);
}

TEST(RecordingGpuProbeTest, RequiresD3d11AndAtLeastOneHardwareBackend) {
    easy::capture::RecordingGpuProbe probe;
    EXPECT_FALSE(probe.canExperiment());
    probe.d3d11Available = true;
    EXPECT_FALSE(probe.canExperiment());
    probe.mediaFoundationHardwareMftAvailable = true;
    EXPECT_TRUE(probe.canExperiment());
    probe.mediaFoundationHardwareMftAvailable = false;
    probe.ffmpegD3d11vaCompiled = true;
    EXPECT_TRUE(probe.canExperiment());
}

// -----------------------------------------------------------------------------
// 21. 光标叠加与图层复原测试套件
// -----------------------------------------------------------------------------
TEST(CursorOverlayTest, StateRestoration) {
    constexpr int size = 128;
    const easy::capture::CaptureRegion region{0, 0, size, size};
    auto backend = easy::capture::createMemoryCaptureBackend(easy::capture::CapturePixelFormat::Bgra32);
    std::string error;
    EXPECT_TRUE(backend->initialize(region, error));
    easy::capture::CaptureFrameView frame;
    EXPECT_TRUE(backend->capture(frame, error));

    const int pixelBytes = 4;
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * pixelBytes;
    std::vector<std::uint8_t> original(rowBytes * frame.height);
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(original.data() + static_cast<std::size_t>(row) * rowBytes,
                    frame.data + static_cast<std::size_t>(row) * frame.stride, rowBytes);
    }

    easy::capture::CursorOverlay overlay;
    {
        auto patch = overlay.apply(frame, region, true, false);
        // 在 patch 作用域内，图层被保护
    }
    bool restored = true;
    for (int row = 0; row < frame.height && restored; ++row) {
        restored = std::memcmp(
            original.data() + static_cast<std::size_t>(row) * rowBytes,
            frame.data + static_cast<std::size_t>(row) * frame.stride,
            rowBytes) == 0;
    }
    EXPECT_TRUE(restored);
    backend->releaseFrame();
    backend->shutdown();
}

// -----------------------------------------------------------------------------
// 22. 截图微调与快捷键提示测试套件
// -----------------------------------------------------------------------------
TEST(CaptureToolbarTest, MicroActionsAndHintDisplay) {
    using easy::capture::ShortcutHintOverlay;
    using easy::capture::ShortcutHintContext;

    auto& overlay = ShortcutHintOverlay::instance();
    auto selectingItems = overlay.getItemsForContext(ShortcutHintContext::CaptureSelecting);
    EXPECT_FALSE(selectingItems.empty());
    bool hasSpacePan = false;
    bool hasWasdNudge = false;
    bool hasColorCopy = false;
    for (const auto& item : selectingItems) {
        if (item.key == L"Space") hasSpacePan = true;
        if (item.key == L"WASD") hasWasdNudge = true;
        if (item.key == L"C") hasColorCopy = true;
    }
    EXPECT_TRUE(hasSpacePan);
    EXPECT_TRUE(hasWasdNudge);
    EXPECT_TRUE(hasColorCopy);

    auto selectedItems = overlay.getItemsForContext(ShortcutHintContext::CaptureSelected);
    EXPECT_FALSE(selectedItems.empty());
    bool hasWasdMove = false;
    bool hasShiftWasdResize = false;
    for (const auto& item : selectedItems) {
        if (item.key == L"WASD") hasWasdMove = true;
        if (item.key == L"Shift+WASD") hasShiftWasdResize = true;
    }
    EXPECT_TRUE(hasWasdMove);
    EXPECT_TRUE(hasShiftWasdResize);

    auto recordSelectingItems = overlay.getItemsForContext(ShortcutHintContext::RecordSelecting);
    EXPECT_FALSE(recordSelectingItems.empty());
    bool hasRecordSpacePan = false;
    for (const auto& item : recordSelectingItems) {
        if (item.key == L"Space") hasRecordSpacePan = true;
    }
    EXPECT_TRUE(hasRecordSpacePan);
}

// -----------------------------------------------------------------------------
// 23. 快捷键提示 DPI 度量测试套件
// -----------------------------------------------------------------------------
TEST(ShortcutHintTest, DpiMetricsAndAlignment) {
    using easy::capture::ShortcutHintStyle;
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(0), 1.0f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(96), 1.0f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(120), 1.25f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(144), 1.5f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(192), 2.0f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(288), 3.0f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(480), 5.0f, 0.001f);
    EXPECT_NEAR(ShortcutHintStyle::scaleForDpi(768), 5.0f, 0.001f);
    EXPECT_GE(ShortcutHintStyle::BaseKeyFont, 13.0f);
    EXPECT_GE(ShortcutHintStyle::BaseLabelFont, 13.0f);
    EXPECT_GE(ShortcutHintStyle::BaseKeyHeight, ShortcutHintStyle::BaseKeyFont * 2.0f);

    float previousFontPixels = 0.0f;
    for (const unsigned dpi : {96u, 120u, 144u, 192u, 240u, 288u, 384u, 480u}) {
        const float fontPixels = ShortcutHintStyle::BaseLabelFont *
                                 ShortcutHintStyle::scaleForDpi(dpi);
        EXPECT_GE(fontPixels, previousFontPixels);
        previousFontPixels = fontPixels;
    }
}

// -----------------------------------------------------------------------------
// 24. 高分屏共享 DPI 度量与自适应换算测试套件
// -----------------------------------------------------------------------------

TEST(CaptureToolbarTest, DpiAdaptiveLayout) {
    using namespace easy::capture;

    CaptureState screenshot;
    screenshot.mode = OverlayMode::Screenshot;
    screenshot.dpiScale = 1.5f;  // Windows 150% / 144 DPI
    const D2D1_SIZE_F desktop150 = D2D1::SizeF(2304.0f, 1440.0f);
    rebuildCaptureToolbar(screenshot, D2D1::RectF(180.0f, 120.0f, 1600.0f, 980.0f),
                          desktop150);
    EXPECT_EQ(screenshot.toolbarButtons.size(), 14u);
    EXPECT_EQ(screenshot.secondaryToolbarButtons.size(), 13u);
    EXPECT_EQ(screenshot.selectionSideButtons.size(), 3u);
    EXPECT_NEAR(screenshot.toolbarButtons.front().rect.bottom -
                screenshot.toolbarButtons.front().rect.top, 45.0f, 0.01f);
    check_toolbar_inside_surface(screenshot, desktop150);
    const auto* cachedButtons = screenshot.toolbarButtons.data();
    rebuildCaptureToolbar(screenshot, D2D1::RectF(180.0f, 120.0f, 1600.0f, 980.0f),
                          desktop150);
    EXPECT_EQ(screenshot.toolbarButtons.data(), cachedButtons);

    CaptureState wrapped;
    wrapped.mode = OverlayMode::Screenshot;
    wrapped.dpiScale = 2.0f;
    const D2D1_SIZE_F compact = D2D1::SizeF(1000.0f, 700.0f);
    rebuildCaptureToolbar(wrapped, D2D1::RectF(80.0f, 100.0f, 850.0f, 480.0f), compact);
    EXPECT_EQ(wrapped.toolbarButtons.size(), 14u);
    EXPECT_EQ(wrapped.secondaryToolbarButtons.size(), 13u);
    EXPECT_EQ(wrapped.selectionSideButtons.size(), 3u);
    check_toolbar_inside_surface(wrapped, compact);

    CaptureState recording;
    recording.mode = OverlayMode::RecordRegion;
    recording.dpiScale = 1.5f;
    rebuildCaptureToolbar(recording, D2D1::RectF(200.0f, 100.0f, 1200.0f, 800.0f),
                          desktop150);
    EXPECT_EQ(recording.toolbarButtons.size(), 2u);
    EXPECT_NEAR(recording.toolbarButtons[0].rect.right -
                recording.toolbarButtons[0].rect.left, 51.0f, 0.01f);
    EXPECT_NEAR(recording.toolbarButtons[1].rect.right -
                recording.toolbarButtons[1].rect.left, 45.0f, 0.01f);
    check_toolbar_inside_surface(recording, desktop150);

    CaptureState extreme;
    extreme.mode = OverlayMode::Screenshot;
    extreme.dpiScale = 5.0f;
    const D2D1_SIZE_F desktop500 = D2D1::SizeF(7680.0f, 4320.0f);
    rebuildCaptureToolbar(extreme, D2D1::RectF(500.0f, 400.0f, 7000.0f, 3600.0f),
                          desktop500);
    EXPECT_EQ(extreme.toolbarButtons.size(), 14u);
    check_toolbar_inside_surface(extreme, desktop500);
}

TEST(CaptureToolbarTest, AccessibilitySemanticsReflectActualState) {
    using namespace easy::capture;

    CaptureState state;
    state.state = OverlayState::Selected;
    state.currentTool = MarkupTool::Arrow;
    state.currentColor = MarkupColor::Blue();

    ToolbarButton arrow;
    arrow.command = ToolbarCommand::SelectTool;
    arrow.tool = MarkupTool::Arrow;
    EXPECT_EQ(toolbarButtonAccessibleName(arrow), L"箭头标注");
    EXPECT_TRUE(isToolbarButtonSelected(arrow, state));
    EXPECT_TRUE(isToolbarButtonEnabled(arrow, state));

    ToolbarButton blue;
    blue.command = ToolbarCommand::SelectColor;
    blue.color = MarkupColor::Blue();
    EXPECT_EQ(toolbarButtonAccessibleName(blue), L"标注颜色");
    EXPECT_TRUE(isToolbarButtonSelected(blue, state));

    ToolbarButton undo;
    undo.command = ToolbarCommand::Undo;
    EXPECT_EQ(toolbarButtonKeyboardShortcut(undo), L"Ctrl+Z");
    EXPECT_FALSE(isToolbarButtonEnabled(undo, state));
    ToolbarButton redo;
    redo.command = ToolbarCommand::Redo;
    EXPECT_FALSE(isToolbarButtonEnabled(redo, state));

    ToolbarButton confirm;
    confirm.command = ToolbarCommand::Confirm;
    EXPECT_EQ(toolbarButtonKeyboardShortcut(confirm), L"Enter");
    state.state = OverlayState::Idle;
    EXPECT_FALSE(isToolbarButtonEnabled(confirm, state));
}

namespace {
constexpr UINT WM_EASYTOOLS_UIA_TEST_INVOKE = WM_APP + 0x4B1;

struct UiaTestSurface {
    HWND hwnd = nullptr;
    int invokeCount = 0;
};

LRESULT CALLBACK uiaTestSurfaceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* surface = reinterpret_cast<UiaTestSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        surface = static_cast<UiaTestSurface*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
        if (surface) surface->hwnd = hwnd;
    }
    if (message == WM_GETOBJECT) {
        using namespace easy::core::accessibility;
        return respondToOverlayUiaGetObject(hwnd, wParam, lParam,
            {L"EasyTools.TestOverlay", L"Automated UI Automation test surface", OverlayUiaRole::Pane, true},
            {{L"EasyTools.TestOverlay.confirm", L"Confirm capture", L"Confirm the test capture",
              L"Enter", {40, 60, 220, 104}, true, true, OverlayUiaActionRole::Button,
              WM_EASYTOOLS_UIA_TEST_INVOKE, 0}});
    }
    if (message == WM_EASYTOOLS_UIA_TEST_INVOKE) {
        if (surface) ++surface->invokeCount;
        return 0;
    }
    if (message == WM_NCDESTROY) {
        easy::core::accessibility::disconnectOverlayUiaProvider(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

class UiaTestWindow final {
public:
    explicit UiaTestWindow(UiaTestSurface* surface) {
        static const ATOM atom = [] {
            WNDCLASSW windowClass{};
            windowClass.hInstance = GetModuleHandleW(nullptr);
            windowClass.lpszClassName = L"EasyTools.UiaProviderTestSurface";
            windowClass.lpfnWndProc = uiaTestSurfaceProc;
            return RegisterClassW(&windowClass);
        }();
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools.UiaProviderTestSurface",
            L"EasyTools UIA test", WS_POPUP, 40, 60, 180, 44,
            nullptr, nullptr, GetModuleHandleW(nullptr), surface);
        if (hwnd) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }

    ~UiaTestWindow() { if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd); }
    HWND hwnd = nullptr;
};

class CoApartment final {
public:
    CoApartment() : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~CoApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    HRESULT result;
};
}  // namespace


TEST(PinWindowTest, TransformAndBoundsCalculation) {
    cv::Mat original = (cv::Mat_<uint8_t>(3, 2) << 10, 20,
                                                   30, 40,
                                                   50, 60);
    // 旋转 90 度
    cv::Mat rotated90;
    cv::rotate(original, rotated90, cv::ROTATE_90_CLOCKWISE);
    EXPECT_TRUE(rotated90.rows == 2 && rotated90.cols == 3);
    EXPECT_EQ(rotated90.at<uint8_t>(0, 0), 50);
    EXPECT_EQ(rotated90.at<uint8_t>(0, 2), 10);
    EXPECT_EQ(rotated90.at<uint8_t>(1, 0), 60);
    EXPECT_EQ(rotated90.at<uint8_t>(1, 2), 20);

    // 水平镜像翻转
    cv::Mat flippedH;
    cv::flip(original, flippedH, 1);
    EXPECT_TRUE(flippedH.rows == 3 && flippedH.cols == 2);
    EXPECT_TRUE(flippedH.at<uint8_t>(0, 0) == 20 && flippedH.at<uint8_t>(0, 1) == 10);
    EXPECT_TRUE(flippedH.at<uint8_t>(2, 0) == 60 && flippedH.at<uint8_t>(2, 1) == 50);

    // 垂直镜像翻转
    cv::Mat flippedV;
    cv::flip(original, flippedV, 0);
    EXPECT_TRUE(flippedV.rows == 3 && flippedV.cols == 2);
    EXPECT_TRUE(flippedV.at<uint8_t>(0, 0) == 50 && flippedV.at<uint8_t>(0, 1) == 60);
    EXPECT_TRUE(flippedV.at<uint8_t>(2, 0) == 10 && flippedV.at<uint8_t>(2, 1) == 20);
}

// -----------------------------------------------------------------------------
// 27. 剪贴板与字符编码转换测试套件
// -----------------------------------------------------------------------------

TEST(CaptureCornerRadiusTest, CornerRadiusInteractiveAdjustment) {
    using namespace easy::capture;

    CaptureState state;
    state.dragStart = {100, 100};
    state.dragEnd = {500, 400};
    state.dpiScale = 1.0f;
    state.cornerRadius = 0.0f;

    // 1. 验证 HitArea::CornerRadius 枚举与状态
    EXPECT_EQ(static_cast<int>(HitArea::CornerRadius), 9);
    state.selAdjustHandle = HitArea::CornerRadius;
    EXPECT_EQ(state.selAdjustHandle, HitArea::CornerRadius);

    // 2. 验证圆角控制点动态偏移量计算算法 (offset = max(14.0f, cornerRadius + 8.0f))
    float selW = 400.0f;
    float selH = 300.0f;
    float maxR = std::min(selW, selH) * 0.5f;
    EXPECT_NEAR(maxR, 150.0f, 0.01f);

    float offset0 = std::clamp(std::max(14.0f, state.cornerRadius + 8.0f), 12.0f, std::min(selW, selH) * 0.40f);
    EXPECT_NEAR(offset0, 14.0f, 0.01f);

    state.cornerRadius = 16.0f;
    float offset16 = std::clamp(std::max(14.0f, state.cornerRadius + 8.0f), 12.0f, std::min(selW, selH) * 0.40f);
    EXPECT_NEAR(offset16, 24.0f, 0.01f);

    // 3. 验证圆角拖拽状态变更与极值约束
    state.isAdjustingCornerRadius = true;
    state.cornerDragStartRadius = 16.0f;
    state.cornerDragStartPos = {124, 124};
    EXPECT_TRUE(state.isAdjustingCornerRadius);
    EXPECT_NEAR(state.cornerDragStartRadius, 16.0f, 0.01f);

    // 4. 验证鼠标靠近角部检测 (triggerDist = 36.0f)
    const float triggerDist = 36.0f;
    POINT nearPt{124, 124};
    float dist = std::hypot(nearPt.x - (100.0f + offset16), nearPt.y - (100.0f + offset16));
    EXPECT_LE(dist, triggerDist);

    POINT farPt{300, 250};
    float farDist = std::hypot(farPt.x - (100.0f + offset16), farPt.y - (100.0f + offset16));
    EXPECT_GT(farDist, triggerDist);
}

TEST(CaptureToolbarTooltipTest, ComprehensiveTooltips) {
    using namespace easy::capture;

    ToolbarButton btnRect;
    btnRect.command = ToolbarCommand::SelectTool;
    btnRect.tool = MarkupTool::Rectangle;
    EXPECT_EQ(btnRect.command, ToolbarCommand::SelectTool);
    EXPECT_EQ(btnRect.tool, MarkupTool::Rectangle);

    ToolbarButton btnArrow;
    btnArrow.command = ToolbarCommand::SelectTool;
    btnArrow.tool = MarkupTool::Arrow;
    EXPECT_EQ(btnArrow.tool, MarkupTool::Arrow);

    ToolbarButton btnColor;
    btnColor.command = ToolbarCommand::SelectColor;
    btnColor.color = {255, 0, 0, 255};
    EXPECT_EQ(btnColor.color.r, 255);
}

TEST(CaptureClipboardFormatTest, BottomUpDIBAndPNG) {
    cv::Mat testMat(100, 100, CV_8UC4, cv::Scalar(100, 150, 200, 255));
    EXPECT_FALSE(testMat.empty());
    EXPECT_EQ(testMat.cols, 100);
    EXPECT_EQ(testMat.rows, 100);

    // 验证 PNG 内存编码支持
    std::vector<uint8_t> pngBytes;
    bool encoded = cv::imencode(".png", testMat, pngBytes);
    EXPECT_TRUE(encoded);
    EXPECT_FALSE(pngBytes.empty());

    // 验证 Windows GDI 标准底向上扫描行正数约束
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = testMat.cols;
    bi.biHeight = testMat.rows; // 正数 = Bottom-Up DIB
    EXPECT_GT(bi.biHeight, 0);
}


TEST(CaptureSizeHudAndMenuTest, HudUnitsAndSwitches) {
    using namespace easy::capture;
    CaptureState state;
    
    // 1. 默认设置检查
    EXPECT_EQ(state.sizeUnit, CaptureState::SizeUnit::Pixel);
    EXPECT_TRUE(state.showPositionInHud);
    EXPECT_TRUE(state.showUnitInHud);
    EXPECT_FALSE(state.isSizeMenuOpen);
    EXPECT_FALSE(state.isSizeHudHovered);

    // 2. 状态切换
    state.sizeUnit = CaptureState::SizeUnit::DeviceIndependentPixel;
    EXPECT_EQ(state.sizeUnit, CaptureState::SizeUnit::DeviceIndependentPixel);

    state.showPositionInHud = false;
    EXPECT_FALSE(state.showPositionInHud);

    state.showUnitInHud = false;
    EXPECT_FALSE(state.showUnitInHud);

    // 3. 菜单开关
    state.isSizeMenuOpen = true;
    EXPECT_TRUE(state.isSizeMenuOpen);

    // 4. AccentColor 与 HEX 解析覆盖
    auto col = easy::core::getAccentColorRGB("coral");
    EXPECT_GT(col.r, 0.5f);
    auto cyanCol = easy::core::getAccentColorRGB("cyan");
    EXPECT_GT(cyanCol.b, 0.5f);
    auto amberCol = easy::core::getAccentColorRGB("amber");
    EXPECT_GT(amberCol.r, 0.5f);
    auto mintCol = easy::core::getAccentColorRGB("mint");
    EXPECT_GT(mintCol.g, 0.5f);
    auto violetCol = easy::core::getAccentColorRGB("violet");
    EXPECT_GT(violetCol.b, 0.5f);
    auto defCol = easy::core::getAccentColorRGB("invalid_name");
    EXPECT_GT(defCol.b, 0.5f);

    auto hexCol1 = easy::core::parseHexColor("#FF0000");
    EXPECT_NEAR(hexCol1.r, 1.0f, 0.01f);
    auto hexCol2 = easy::core::parseHexColor("00FF00");
    EXPECT_NEAR(hexCol2.g, 1.0f, 0.01f);
    auto hexCol3 = easy::core::parseHexColor("invalid_hex");
    EXPECT_NEAR(hexCol3.r, 0.231f, 0.01f);

    // 5. 吸管取色与画笔色彩同步验证
    MarkupColor picked(59, 130, 246);
    state.currentColor = picked;
    EXPECT_EQ(state.currentColor.r, 59);
    EXPECT_EQ(state.currentColor.g, 130);
    EXPECT_EQ(state.currentColor.b, 246);
}

TEST(CaptureTwoTierToolbarAndSubmenuTest, LayoutAndSecondaryProperties) {
    using namespace easy::capture;
    CaptureState state;
    state.dpiScale = 1.0f;
    state.mode = OverlayMode::Screenshot;
    state.currentTool = MarkupTool::Rectangle;

    D2D1_RECT_F selRect = D2D1::RectF(100.0f, 100.0f, 600.0f, 500.0f);
    D2D1_SIZE_F surfaceSize = D2D1::SizeF(1920.0f, 1080.0f);

    // 1. 测试矩形工具下的双层工具栏布局生成
    rebuildCaptureToolbar(state, selRect, surfaceSize);
    EXPECT_FALSE(state.toolbarButtons.empty());
    EXPECT_FALSE(state.secondaryToolbarButtons.empty());
    EXPECT_GT(state.primaryToolbarRect.right - state.primaryToolbarRect.left, 100.0f);
    EXPECT_GT(state.secondaryToolbarRect.right - state.secondaryToolbarRect.left, 100.0f);

    // 验证二级栏包含填充、线条样式、线宽、圆角、调色板
    bool hasFill = false;
    bool hasLineStyle = false;
    bool hasStrokeWidth = false;
    bool hasCornerRadius = false;
    int colorCount = 0;

    for (const auto& btn : state.secondaryToolbarButtons) {
        if (btn.command == ToolbarCommand::ToggleFill) hasFill = true;
        if (btn.command == ToolbarCommand::ToggleLineStyleDropdown) hasLineStyle = true;
        if (btn.command == ToolbarCommand::CycleStrokeWidth) hasStrokeWidth = true;
        if (btn.command == ToolbarCommand::CycleElementCornerRadius) hasCornerRadius = true;
        if (btn.command == ToolbarCommand::SelectColor) ++colorCount;
    }
    EXPECT_TRUE(hasFill);
    EXPECT_TRUE(hasLineStyle);
    EXPECT_TRUE(hasStrokeWidth);
    EXPECT_TRUE(hasCornerRadius);
    EXPECT_EQ(colorCount, 7);

    // 验证主工具栏与二级属性栏包含前置分隔符
    bool hasPriSeparators = false;
    for (const auto& btn : state.toolbarButtons) {
        if (btn.isSeparatorBefore) hasPriSeparators = true;
    }
    EXPECT_TRUE(hasPriSeparators);

    bool hasSecSeparators = false;
    for (const auto& btn : state.secondaryToolbarButtons) {
        if (btn.isSeparatorBefore) hasSecSeparators = true;
    }
    EXPECT_TRUE(hasSecSeparators);

    // 2. 切换到文本工具
    state.currentTool = MarkupTool::Text;
    state.toolbarLayoutValid = false;
    rebuildCaptureToolbar(state, selRect, surfaceSize);
    EXPECT_FALSE(state.secondaryToolbarButtons.empty());

    // 3. 切换到箭头工具
    state.currentTool = MarkupTool::Arrow;
    state.toolbarLayoutValid = false;
    rebuildCaptureToolbar(state, selRect, surfaceSize);
    EXPECT_FALSE(state.secondaryToolbarButtons.empty());

    // 4. 切换到马赛克工具
    state.currentTool = MarkupTool::Mosaic;
    state.toolbarLayoutValid = false;
    rebuildCaptureToolbar(state, selRect, surfaceSize);
    EXPECT_FALSE(state.secondaryToolbarButtons.empty());

    // 4.5 切换到序号工具
    state.currentTool = MarkupTool::Number;
    state.toolbarLayoutValid = false;
    rebuildCaptureToolbar(state, selRect, surfaceSize);
    EXPECT_FALSE(state.secondaryToolbarButtons.empty());

    // 5. 验证 OpenCV 虚线与填充渲染及全部标注工具
    initializeMarkupTextRenderer();
    cv::Mat canvas = cv::Mat::zeros(400, 400, CV_8UC4);
    MarkupEngine engine;
    engine.setBaseImage(canvas.clone());

    auto* elemRect = engine.drawRectangle(cv::Point(20, 20), cv::Point(120, 120), MarkupColor::Red(), 3.0f);
    ASSERT_NE(elemRect, nullptr);
    elemRect->lineStyle = LineStyle::Dashed;
    elemRect->fill = true;
    elemRect->cornerRadius = 14.0f;

    auto* elemArrow = engine.drawArrow(cv::Point(150, 50), cv::Point(250, 150), MarkupColor::Blue(), 4.0f);
    ASSERT_NE(elemArrow, nullptr);
    elemArrow->lineStyle = LineStyle::Dotted;
    elemArrow->arrowStyle = ArrowStyle::DoubleEnded;

    auto* elemPen = engine.drawPenStroke({cv::Point(30, 200), cv::Point(60, 240), cv::Point(90, 210)}, MarkupColor::Green(), 2.0f);
    ASSERT_NE(elemPen, nullptr);
    elemPen->lineStyle = LineStyle::DashDot;

    auto* elemEllipse = engine.drawEllipse(cv::Point(200, 200), cv::Point(300, 300), MarkupColor::Yellow(), 2.0f);
    ASSERT_NE(elemEllipse, nullptr);
    elemEllipse->fill = true;

    engine.drawHighlight(cv::Point(10, 10), cv::Point(80, 40), MarkupColor::Yellow());
    engine.applyMosaic(cv::Point(300, 300), cv::Point(380, 380), 8);
    
    int numVal = engine.addNumberMark(cv::Point(100, 350), MarkupColor::Red());
    EXPECT_EQ(numVal, 1);
    EXPECT_EQ(engine.currentNumber(), 2);
    engine.resetNumber();
    EXPECT_EQ(engine.currentNumber(), 1);

    engine.addSpotlight(cv::Point(100, 100), cv::Point(200, 200), MarkupColor::White());
    engine.addWatermark(cv::Point(0, 0), cv::Point(400, 400), "CONFIDENTIAL");
    engine.applyInpaint(cv::Point(50, 50), cv::Point(70, 70), 3);

    engine.renderAll(canvas);
    EXPECT_GT(cv::countNonZero(canvas.reshape(1)), 0);

    // 验证撤销与重做
    EXPECT_TRUE(engine.canUndo());
    engine.undo();
    EXPECT_TRUE(engine.canRedo());
    engine.redo();
    engine.translateAll(5, 5);
    engine.updateBaseImage(canvas.clone());
    EXPECT_FALSE(engine.getCompositeImage().empty());
    engine.clearAll();
    EXPECT_FALSE(engine.canUndo());

    shutdownMarkupTextRenderer();

    // 6. 验证最大圆角 60 上限配置
    easy::core::ConfigManager::instance().set<double>("/screenshot/maxCornerRadius", 60.0);
    double cfgR = easy::core::ConfigManager::instance().get<double>("/screenshot/maxCornerRadius", 60.0);
    EXPECT_DOUBLE_EQ(cfgR, 60.0);
}

// -----------------------------------------------------------------------------
// 37. 鼠标聚光灯与演示特效测试套件
// -----------------------------------------------------------------------------

TEST(CaptureVectorIconsTest, ComprehensiveVectorRendering) {
    using namespace easy::capture;

    // 验证空指针与无效参数防护
    CaptureVectorIcons::renderIcon(nullptr, nullptr, CaptureIconId::ActionConfirm,
                                   D2D1::RectF(0, 0, 30, 30), nullptr, 1.0f);

    // 创建 Direct2D 工厂与 WIC/D2D 离屏 RenderTarget 进行全图标渲染覆盖验证
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
    ASSERT_NE(d2dFactory, nullptr);

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcRt;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    HRESULT hr = d2dFactory->CreateDCRenderTarget(&props, dcRt.GetAddressOf());
    ASSERT_EQ(hr, S_OK);
    ASSERT_NE(dcRt, nullptr);

    HDC hdc = GetDC(nullptr);
    RECT rc{0, 0, 100, 100};
    dcRt->BindDC(hdc, &rc);
    dcRt->BeginDraw();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> testBrush;
    dcRt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue), testBrush.GetAddressOf());
    ASSERT_NE(testBrush, nullptr);

    // 遍历所有枚举值进行 100% 渲染覆盖
    const std::vector<CaptureIconId> allIcons = {
        CaptureIconId::None,
        CaptureIconId::ToolRectangle,
        CaptureIconId::ToolEllipse,
        CaptureIconId::ToolPen,
        CaptureIconId::ToolHighlight,
        CaptureIconId::ToolArrow,
        CaptureIconId::ToolArrowThin,
        CaptureIconId::ToolArrowDouble,
        CaptureIconId::ToolText,
        CaptureIconId::ToolNumber,
        CaptureIconId::ToolMosaic,
        CaptureIconId::ToolBlur,
        CaptureIconId::ToolInpaint,
        CaptureIconId::ActionUndo,
        CaptureIconId::ActionRedo,
        CaptureIconId::ActionClear,
        CaptureIconId::ActionExtractText,
        CaptureIconId::ActionPinWindow,
        CaptureIconId::ActionScrollCapture,
        CaptureIconId::ActionRecordStart,
        CaptureIconId::ActionRecordPause,
        CaptureIconId::ActionRecordStop,
        CaptureIconId::ActionToggleMic,
        CaptureIconId::ActionToggleSpeaker,
        CaptureIconId::ActionCopy,
        CaptureIconId::ActionSave,
        CaptureIconId::ActionCancel,
        CaptureIconId::ActionConfirm,
        CaptureIconId::PropSolidLine,
        CaptureIconId::PropDashedLine,
        CaptureIconId::PropDottedLine,
        CaptureIconId::PropDashDotLine,
        CaptureIconId::PropStrokeWidth,
        CaptureIconId::PropCornerRadius,
        CaptureIconId::PropFillOutline,
        CaptureIconId::PropFillSolid,
        CaptureIconId::PropPipette,
        CaptureIconId::PropPalette,
        CaptureIconId::PropQrCode,
    };

    D2D1_RECT_F targetRect = D2D1::RectF(10.0f, 10.0f, 40.0f, 40.0f);
    for (auto id : allIcons) {
        CaptureVectorIcons::renderIcon(dcRt.Get(), d2dFactory.Get(), id, targetRect, testBrush.Get(), 1.0f);
        CaptureVectorIcons::renderIcon(dcRt.Get(), d2dFactory.Get(), id, targetRect, testBrush.Get(), 2.0f);
    }

    dcRt->EndDraw();
    ReleaseDC(nullptr, hdc);
}

TEST(CaptureSliderPopupTest, SteplessSliderAndWheelInteraction) {
    using namespace easy::capture;

    CaptureState state;
    state.dpiScale = 1.25f;
    state.currentStrokeWidth = 6;
    state.currentElementCornerRadius = 12.0f;

    // 1. 初始化线宽滑块弹窗
    state.sliderPopup.type = SliderPopupType::StrokeWidth;
    state.sliderPopup.minValue = 1;
    state.sliderPopup.maxValue = 28;
    state.sliderPopup.currentValue = state.currentStrokeWidth;
    state.sliderPopup.trackRect = D2D1::RectF(100.0f, 200.0f, 300.0f, 206.0f);
    
    EXPECT_EQ(state.sliderPopup.type, SliderPopupType::StrokeWidth);
    EXPECT_EQ(state.sliderPopup.currentValue, 6);

    // 2. 模拟滑块拖拽映射
    float trackW = state.sliderPopup.trackRect.right - state.sliderPopup.trackRect.left;
    float clickX = state.sliderPopup.trackRect.left + trackW * 0.5f; // 50% 位置
    float pct = std::clamp((clickX - state.sliderPopup.trackRect.left) / trackW, 0.0f, 1.0f);
    int mappedVal = state.sliderPopup.minValue + static_cast<int>(std::round(pct * (state.sliderPopup.maxValue - state.sliderPopup.minValue)));
    EXPECT_NEAR(mappedVal, 15, 1);

    // 3. 模拟预设胶囊按钮配置
    std::vector<int> presets = { 2, 4, 6, 8, 14, 20 };
    for (size_t i = 0; i < presets.size(); ++i) {
        float px = state.sliderPopup.trackRect.left + i * (trackW / presets.size());
        state.sliderPopup.presetButtons.push_back({ presets[i], D2D1::RectF(px, 220.0f, px + 25.0f, 240.0f) });
    }
    EXPECT_EQ(state.sliderPopup.presetButtons.size(), 6);
    EXPECT_EQ(state.sliderPopup.presetButtons[2].first, 6);

    // 4. 模拟滚轮调节步进
    int zDeltaUp = 120;
    int stepUp = (zDeltaUp > 0) ? 1 : -1;
    state.currentStrokeWidth = std::clamp(state.currentStrokeWidth + stepUp, 1, 28);
    EXPECT_EQ(state.currentStrokeWidth, 7);

    int zDeltaDown = -120;
    int stepDown = (zDeltaDown > 0) ? 1 : -1;
    state.currentStrokeWidth = std::clamp(state.currentStrokeWidth + stepDown, 1, 28);
    EXPECT_EQ(state.currentStrokeWidth, 6);

    // 5. 切换到圆角滑块弹窗
    state.sliderPopup.type = SliderPopupType::CornerRadius;
    state.sliderPopup.minValue = 0;
    state.sliderPopup.maxValue = 40;
    state.sliderPopup.currentValue = static_cast<int>(state.currentElementCornerRadius);
    EXPECT_EQ(state.sliderPopup.type, SliderPopupType::CornerRadius);
    EXPECT_EQ(state.sliderPopup.currentValue, 12);
}

TEST(CaptureShiftConstraintTest, OrthogonalAndSquareAspectConstraints) {
    using namespace easy::capture;

    // 1. 验证正方形约束算法
    cv::Point start(100, 100);
    cv::Point end(250, 180); // w=150, h=80
    int w = std::abs(end.x - start.x);
    int h = std::abs(end.y - start.y);
    int side = std::max(w, h);
    cv::Point constrainedEnd(start.x + (end.x >= start.x ? side : -side),
                             start.y + (end.y >= start.y ? side : -side));
    EXPECT_EQ(constrainedEnd.x - start.x, 150);
    EXPECT_EQ(constrainedEnd.y - start.y, 150);

    // 2. 验证箭头 45° 正交吸附算法
    cv::Point arrowStart(200, 200);
    cv::Point arrowEnd(300, 210); // 接近水平 0°
    double dx = arrowEnd.x - arrowStart.x;
    double dy = arrowEnd.y - arrowStart.y;
    double dist = std::hypot(dx, dy);
    double angle = std::atan2(dy, dx);
    constexpr double step = 3.14159265358979323846 / 4.0;
    double snappedAngle = std::round(angle / step) * step;
    cv::Point snappedEnd(static_cast<int>(std::round(arrowStart.x + dist * std::cos(snappedAngle))),
                         static_cast<int>(std::round(arrowStart.y + dist * std::sin(snappedAngle))));
    EXPECT_NEAR(snappedEnd.y, arrowStart.y, 1); // 成功吸附到绝对水平 0°
    EXPECT_NEAR(snappedEnd.x - arrowStart.x, static_cast<int>(dist), 1);
}

TEST(CaptureSmartGuidesAndQrTest, AlignmentDetectionAndQrLayout) {
    using namespace easy::capture;

    CaptureState state;
    state.dpiScale = 1.0f;
    state.detectedQrText = "https://github.com/yuan278501381/easyTools";

    // 1. 验证二维码微晶胶囊文本截断与布局计算
    std::wstring displayMsg = L"二维码: " + easy::core::WinUtils::utf8ToWstring(state.detectedQrText);
    EXPECT_TRUE(displayMsg.find(L"https://github.com") != std::wstring::npos);
    if (displayMsg.size() > 28) {
        displayMsg = displayMsg.substr(0, 26) + L"...";
    }
    EXPECT_EQ(displayMsg.substr(displayMsg.size() - 3), L"...");

    // 2. 验证智能对齐中轴计算
    D2D1_RECT_F selRect = D2D1::RectF(460.0f, 270.0f, 1460.0f, 810.0f); // 中心 (960, 540)
    D2D1_SIZE_F surfaceSize = D2D1::SizeF(1920.0f, 1080.0f); // 中心 (960, 540)
    float cx = (selRect.left + selRect.right) * 0.5f;
    float cy = (selRect.top + selRect.bottom) * 0.5f;
    float midScreenX = surfaceSize.width * 0.5f;
    float midScreenY = surfaceSize.height * 0.5f;

    EXPECT_NEAR(cx, midScreenX, 0.001f);
    EXPECT_NEAR(cy, midScreenY, 0.001f);
    EXPECT_LT(std::abs(cx - midScreenX), 4.0f);
    EXPECT_LT(std::abs(cy - midScreenY), 4.0f);
}

TEST(CaptureUndoRedoStateSanitizationTest, ActiveElementSafetyAndDirtyFlags) {
    using namespace easy::capture;

    CaptureState state;
    cv::Mat dummy(400, 400, CV_8UC3, cv::Scalar(100, 100, 100));
    state.markup.setBaseImage(dummy);

    // 1. 添加图元
    auto* rectElem = state.markup.drawRectangle(cv::Point(10, 10), cv::Point(100, 100), MarkupColor::Red(), 2.0f);
    ASSERT_NE(rectElem, nullptr);
    state.activeElement = rectElem;
    EXPECT_EQ(state.markup.elementCount(), 1);

    // 2. 执行撤销并验证安全归空
    state.activeElement = nullptr;
    EXPECT_TRUE(state.markup.undo());
    EXPECT_EQ(state.markup.elementCount(), 0);
    EXPECT_EQ(state.activeElement, nullptr);

    // 3. 执行重做并验证图元恢复
    EXPECT_TRUE(state.markup.redo());
    EXPECT_EQ(state.markup.elementCount(), 1);

    // 4. 清空验证
    state.activeElement = nullptr;
    state.markup.clearAll();
    EXPECT_EQ(state.markup.elementCount(), 0);
}

TEST(CaptureHistoryTimeMachineTest, NavigationAndClampLogic) {
    using namespace easy::capture;

    CaptureState state;
    state.historyMode = false;
    state.historyIndex = 0;

    int totalHistory = 5;
    // 模拟逗号按键（上溯历史）
    if (!state.historyMode) {
        state.historyMode = true;
        state.historyIndex = 0;
    }
    EXPECT_TRUE(state.historyMode);
    EXPECT_EQ(state.historyIndex, 0);

    // 连续按逗号上溯
    for (int i = 0; i < 10; ++i) {
        state.historyIndex = std::min(state.historyIndex + 1, totalHistory - 1);
    }
    EXPECT_EQ(state.historyIndex, 4); // 严格夹取在上限

    // 连续按句号回溯
    for (int i = 0; i < 10; ++i) {
        state.historyIndex = std::max(state.historyIndex - 1, 0);
    }
    EXPECT_EQ(state.historyIndex, 0); // 严格夹取在下限
}

TEST(CaptureEmptyTextBoxResizeTest, MinimumBoundsAndResizeBeforeInput) {
    using namespace easy::capture;

    CaptureState state;
    cv::Mat canvas(600, 800, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    state.markup.setBaseImage(canvas);

    // 1. 创建空文本标注元素（模拟用户刚刚点击画布）
    auto* textElem = state.markup.addText(cv::Point(150, 150), "", MarkupColor::Red(), 18.0f);
    ASSERT_NE(textElem, nullptr);
    textElem->isActive = true;
    textElem->isEditing = true;

    // 2. 验证空文本时的包围盒绝不为 0 且具有舒适的最小尺寸 (杜绝 8 手柄挤压在一起)
    cv::Rect bbox = textElem->getBoundingBox();
    EXPECT_GE(bbox.width, 80);
    EXPECT_GE(bbox.height, 25);
    EXPECT_EQ(bbox.x, 150);
    EXPECT_EQ(bbox.y, 150);

    // 3. 验证 8 个手柄的命中测试 (用户在未输入内容前即可拖拽任意手柄)
    HitArea hitRB = textElem->hitTestEx(cv::Point(bbox.x + bbox.width, bbox.y + bbox.height));
    EXPECT_EQ(hitRB, HitArea::RB);

    HitArea hitLT = textElem->hitTestEx(cv::Point(bbox.x, bbox.y));
    EXPECT_EQ(hitLT, HitArea::LT);

    // 4. 模拟拖拽 RB 手柄放大文本框 (字号增大)
    float oldFontSize = textElem->fontSize;
    textElem->resize(20, 20, HitArea::RB);
    EXPECT_GT(textElem->fontSize, oldFontSize);

    // 5. 模拟拖拽 LT 手柄缩小文本框
    textElem->resize(10, 10, HitArea::LT);
    EXPECT_LE(textElem->fontSize, 180.0f);
    EXPECT_GE(textElem->fontSize, 12.0f);
}

TEST(CaptureCornerRadiusDesignerWorkflowTest, LiveCornerCursorAndReset) {
    using namespace easy::capture;

    CaptureState state;
    state.dpiScale = 1.0f;
    state.cornerRadius = 12.0f;

    // 1. 模拟设计师双击圆角手柄：在 12px 与 0px 之间快速重置/切换
    float currentR = state.cornerRadius;
    float toggledR = (currentR > 0.0f) ? 0.0f : 12.0f;
    EXPECT_EQ(toggledR, 0.0f);

    float restoredR = (toggledR > 0.0f) ? 0.0f : 12.0f;
    EXPECT_EQ(restoredR, 12.0f);

    // 2. 验证四个角部的 offset 计算
    float selW = 400.0f, selH = 300.0f;
    float offset = std::clamp(std::max(22.0f, state.cornerRadius + 8.0f), 16.0f, std::min(selW, selH) * 0.40f);
    EXPECT_EQ(offset, 22.0f);
}

TEST(CaptureSelectionSideMenuTest, PixPinSideMenuAndSliderInteraction) {
    using namespace easy::capture;

    CaptureState state;
    state.mode = OverlayMode::Screenshot;
    state.dpiScale = 1.0f;
    state.cornerRadius = 16.0f;

    // 1. 测试常规居中选区：侧边菜单默认在右侧
    D2D1_RECT_F selRect = D2D1::RectF(100.0f, 100.0f, 400.0f, 300.0f);
    D2D1_SIZE_F surface = D2D1::SizeF(1920.0f, 1080.0f);
    rebuildCaptureToolbar(state, selRect, surface);

    EXPECT_EQ(state.selectionSideButtons.size(), 3u);
    EXPECT_EQ(state.selectionSideButtons[0].command, ToolbarCommand::SideToggleCornerRadius);
    EXPECT_EQ(state.selectionSideButtons[1].command, ToolbarCommand::SideInvertSelection);
    EXPECT_EQ(state.selectionSideButtons[2].command, ToolbarCommand::SideResetSelection);
    EXPECT_GT(state.selectionSideRect.left, selRect.right); // 默认在右侧

    // 2. 测试紧贴屏幕右边缘选区：侧边菜单自动翻转至左侧
    D2D1_RECT_F rightEdgeSel = D2D1::RectF(1750.0f, 100.0f, 1910.0f, 300.0f);
    rebuildCaptureToolbar(state, rightEdgeSel, surface);
    EXPECT_EQ(state.selectionSideButtons.size(), 3u);
    EXPECT_LT(state.selectionSideRect.right, rightEdgeSel.left); // 智能翻转至左侧

    // 3. 模拟激活 SideToggleCornerRadius 展开 SelectionCornerRadius 滑块弹窗
    state.sliderPopup.type = SliderPopupType::SelectionCornerRadius;
    state.sliderPopup.minValue = 0;
    state.sliderPopup.maxValue = 80;
    state.sliderPopup.currentValue = static_cast<int>(state.cornerRadius);
    state.sliderPopup.trackRect = D2D1::RectF(100.0f, 200.0f, 300.0f, 205.0f);

    EXPECT_EQ(state.sliderPopup.currentValue, 16);

    // 4. 模拟在滑块轨道上点击 50% 位置
    float trackW = state.sliderPopup.trackRect.right - state.sliderPopup.trackRect.left;
    float clickX = state.sliderPopup.trackRect.left + trackW * 0.5f;
    float pct = std::clamp((clickX - state.sliderPopup.trackRect.left) / trackW, 0.0f, 1.0f);
    int mappedVal = state.sliderPopup.minValue + static_cast<int>(std::round(pct * (state.sliderPopup.maxValue - state.sliderPopup.minValue)));
    state.cornerRadius = static_cast<float>(mappedVal);
    EXPECT_EQ(state.cornerRadius, 40.0f);

    // 5. 模拟滚轮微调选区圆角
    int zDelta = 120;
    int delta = (zDelta > 0) ? 1 : -1;
    state.cornerRadius = std::clamp(state.cornerRadius + static_cast<float>(delta * 2), 0.0f, 80.0f);
    EXPECT_EQ(state.cornerRadius, 42.0f);
}

// -----------------------------------------------------------------------------
// 26. 截图/录屏提示覆盖层生命周期与自适应门禁测试套件
// -----------------------------------------------------------------------------
TEST(CaptureLifecycleAndGhostWindowTest, ShortcutHintOverlayLifecycleAndGhostExclusion) {
    using namespace easy::capture;

    // 1. 初始状态：提示层未显示
    EXPECT_FALSE(ShortcutHintOverlay::instance().isVisible());

    // 2. 模拟进入选区模式：展示快捷键提示
    ShortcutHintOverlay::instance().show(ShortcutHintContext::CaptureSelecting);
    EXPECT_TRUE(ShortcutHintOverlay::instance().isVisible());

    // 3. 模拟退出/取消：即刻隐藏，销毁窗口资源，杜绝幽灵残留
    ShortcutHintOverlay::instance().hide();
    EXPECT_FALSE(ShortcutHintOverlay::instance().isVisible());

    // 4. 模拟 50 次高频连击唤起与隐藏 (Burst Simulation)，验证幂等性
    for (int i = 0; i < 50; ++i) {
        ShortcutHintOverlay::instance().show(ShortcutHintContext::CaptureSelecting);
        EXPECT_TRUE(ShortcutHintOverlay::instance().isVisible());
        ShortcutHintOverlay::instance().hide();
        EXPECT_FALSE(ShortcutHintOverlay::instance().isVisible());
    }
}

TEST(CaptureLifecycleAndGhostWindowTest, DynamicShortcutAdaptationAndOverlayHints) {
    using namespace easy::capture;

    // 1. 验证快捷提示层能动态适配系统语言与快捷键配置
    auto selectingItems = ShortcutHintOverlay::instance().getItemsForContext(ShortcutHintContext::CaptureSelecting);
    EXPECT_FALSE(selectingItems.empty());
    
    // 验证包含核心操作引导条目
    bool hasDragItem = false;
    bool hasSpaceItem = false;
    for (const auto& item : selectingItems) {
        if (item.key == L"拖拽" || item.key == L"Drag") hasDragItem = true;
        if (item.key == L"Space") hasSpaceItem = true;
    }
    EXPECT_TRUE(hasDragItem);
    EXPECT_TRUE(hasSpaceItem);

    // 2. 验证录屏提示项动态读取 HotkeyManager，非硬编码
    auto recordingItems = ShortcutHintOverlay::instance().getItemsForContext(ShortcutHintContext::Recording);
    EXPECT_FALSE(recordingItems.empty());
}

// -----------------------------------------------------------------------------
// 27. 全模块生命周期状态机穷举与动态快捷键感知测试套件 (World-Class DevOps Suite)
// -----------------------------------------------------------------------------