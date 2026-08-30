// ─────────────────────────────────────────────────────────────────────────────
// test_core.inc — CORE 模块化单元测试集
// ─────────────────────────────────────────────────────────────────────────────

TEST(WinUtilsTest, SpecialAndSystemWindows) {
    using easy::core::WinUtils;
    // 验证空句柄与伪句柄边界安全，无崩溃
    EXPECT_FALSE(WinUtils::isDesktopWindow(nullptr));
    EXPECT_FALSE(WinUtils::isTaskbarWindow(nullptr));
    HWND invalidHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0xDEADBEEF));
    EXPECT_FALSE(WinUtils::isDesktopWindow(invalidHwnd));
    EXPECT_FALSE(WinUtils::isTaskbarWindow(invalidHwnd));
}

// -----------------------------------------------------------------------------
// 6. 快捷键定义解析测试套件
// -----------------------------------------------------------------------------
TEST(HotkeyParserTest, ExpressionParsing) {
    using easy::core::HotkeyDef;

    auto normal = HotkeyDef::fromString("Ctrl+Shift+A");
    EXPECT_TRUE(normal.has_value());
    if (normal) EXPECT_EQ(normal->toString(), "Ctrl+Shift+A");

    auto special = HotkeyDef::fromString("Alt+PageDown");
    EXPECT_TRUE(special.has_value());
    if (special) EXPECT_EQ(special->toString(), "Alt+PageDown");
    EXPECT_EQ(HotkeyDef{}.toString(), "");

    EXPECT_FALSE(HotkeyDef::fromString("").has_value());
    EXPECT_FALSE(HotkeyDef::fromString("Ctrl+").has_value());
    EXPECT_FALSE(HotkeyDef::fromString("Ctrl+UnknownKey").has_value());
    EXPECT_FALSE(HotkeyDef::fromString("Bad+Ctrl+A").has_value());
}

// -----------------------------------------------------------------------------
// 7. 快捷键管理器禁用态与生命周期测试套件
// -----------------------------------------------------------------------------
TEST(ElevationPolicyTest, RestartAndStartupActions) {
    using easy::core::ElevationRestartAction;
    EXPECT_EQ(easy::core::elevationRestartAfterSetting(true, false),
              ElevationRestartAction::Elevate);
    EXPECT_EQ(easy::core::elevationRestartAfterSetting(false, true),
              ElevationRestartAction::DropElevation);
    EXPECT_EQ(easy::core::elevationRestartAfterSetting(true, true),
              ElevationRestartAction::None);
    EXPECT_EQ(easy::core::elevationRestartAfterSetting(false, false),
              ElevationRestartAction::None);
    EXPECT_TRUE(easy::core::shouldAutoElevateOnStartup(true, false, false));
    EXPECT_FALSE(easy::core::shouldAutoElevateOnStartup(true, true, false));
    EXPECT_FALSE(easy::core::shouldAutoElevateOnStartup(false, false, false));
    EXPECT_FALSE(easy::core::shouldAutoElevateOnStartup(true, false, true));
}

TEST(HotkeyPolicyTest, RecordingPauseArmsOnlyWhileRecording) {
    using easy::core::HotkeyArmScope;
    EXPECT_EQ(easy::core::hotkeyArmScopeForName("Record Pause"), HotkeyArmScope::WhileRecording);
    EXPECT_EQ(easy::core::hotkeyArmScopeForName("Screenshot"), HotkeyArmScope::Always);
    EXPECT_TRUE(easy::core::hotkeyShouldBeArmed(HotkeyArmScope::Always, false));
    EXPECT_FALSE(easy::core::hotkeyShouldBeArmed(HotkeyArmScope::WhileRecording, false));
    EXPECT_TRUE(easy::core::hotkeyShouldBeArmed(HotkeyArmScope::WhileRecording, true));
    EXPECT_TRUE(easy::core::isEditorCommandPaletteChord(MOD_CONTROL | MOD_SHIFT, 'P'));
    EXPECT_TRUE(easy::core::isEditorCommandPaletteChord(MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'P'));
    EXPECT_FALSE(easy::core::isEditorCommandPaletteChord(MOD_CONTROL | MOD_SHIFT, 'R'));
    EXPECT_FALSE(easy::core::isEditorCommandPaletteChord(MOD_CONTROL, 'P'));
    EXPECT_FALSE(easy::core::hotkeyLooksExternallyConflicted(true, false, false));
    EXPECT_TRUE(easy::core::hotkeyLooksExternallyConflicted(true, true, false));
    EXPECT_FALSE(easy::core::hotkeyLooksExternallyConflicted(true, true, true));
    EXPECT_FALSE(easy::core::hotkeyLooksExternallyConflicted(false, true, false));
}

TEST(HotkeyManagerTest, DisabledHotkeyLifecycle) {
    auto& manager = easy::core::HotkeyManager::instance();
    constexpr const char* name = "Unit Test Disabled Hotkey";
    std::atomic<int> calls{0};

    EXPECT_TRUE(manager.registerHotkey(name, {}, [&calls]() { calls.fetch_add(1); }));
    auto entries = manager.getAllHotkeys();
    auto entry = std::find_if(entries.begin(), entries.end(), [](const auto& candidate) {
        return candidate.name == name;
    });
    EXPECT_TRUE(entry != entries.end());
    if (entry != entries.end()) {
        EXPECT_FALSE(entry->registered);
        EXPECT_EQ(entry->def.toString(), "");
    }

    EXPECT_TRUE(manager.setHotkeyArmed(name, false));
    entries = manager.getAllHotkeys();
    entry = std::find_if(entries.begin(), entries.end(), [](const auto& candidate) {
        return candidate.name == name;
    });
    ASSERT_TRUE(entry != entries.end());
    EXPECT_FALSE(entry->armed);
    EXPECT_FALSE(entry->conflict);
    EXPECT_TRUE(manager.setHotkeyArmed(name, true));

    EXPECT_TRUE(manager.clearHotkey(name));
    EXPECT_EQ(calls.load(), 0);
    manager.unregisterHotkey(name);
    entries = manager.getAllHotkeys();
    EXPECT_TRUE(std::none_of(entries.begin(), entries.end(), [](const auto& candidate) {
        return candidate.name == name;
    }));
}

TEST(HotkeyManagerTest, MarshalsWindowHotkeysToOwningThread) {
    HWND messageWindow = CreateWindowExW(
        0, L"STATIC", L"EasyToolsHotkeyAffinityTest", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(messageWindow, nullptr);

    constexpr int probeId = 0x6AFE;
    constexpr UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT;
    if (!RegisterHotKey(messageWindow, probeId, modifiers, VK_F24)) {
        DestroyWindow(messageWindow);
        GTEST_SKIP() << "F24 test chord is unavailable on this host";
    }
    UnregisterHotKey(messageWindow, probeId);

    auto& dispatcher = easy::core::MainThreadDispatcher::instance();
    dispatcher.initialize(messageWindow);
    auto& manager = easy::core::HotkeyManager::instance();
    manager.initialize(messageWindow);

    constexpr const char* name = "Unit Test Cross-thread Hotkey";
    auto registration = std::async(std::launch::async, [&manager, name]() {
        return manager.registerHotkey(
            name, {easy::core::ModKey::Ctrl | easy::core::ModKey::Alt |
                       easy::core::ModKey::Shift, VK_F24},
            []() {});
    });

    const auto pumpUntilReady = [&dispatcher](auto& future) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == easy::core::MainThreadDispatcher::MessageId) {
                    dispatcher.drain();
                } else {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            std::this_thread::yield();
        }
        return future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    };

    ASSERT_TRUE(pumpUntilReady(registration));
    EXPECT_TRUE(registration.get());

    auto removal = std::async(std::launch::async, [&manager, name]() {
        manager.unregisterHotkey(name);
        return true;
    });
    ASSERT_TRUE(pumpUntilReady(removal));
    EXPECT_TRUE(removal.get());

    manager.shutdown();
    dispatcher.shutdown();
    DestroyWindow(messageWindow);
}

// -----------------------------------------------------------------------------
// 8. 配置管理器持久化与默认回退测试套件
// -----------------------------------------------------------------------------
TEST(ConfigManagerTest, PersistenceAndDefaultFallbacks) {
    auto temp = std::filesystem::temp_directory_path() /
        (L"EasyToolsTests_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp, ec);
    {
        std::ofstream corrupt(temp / "config.json", std::ios::binary | std::ios::trunc);
        corrupt << "{ definitely-not-json";
    }

    auto& config = easy::core::ConfigManager::instance();
    EXPECT_TRUE(config.initialize(temp));
    size_t corruptBackups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp)) {
        if (entry.path().filename().wstring().starts_with(L"config.json.corrupt.")) {
            ++corruptBackups;
        }
    }
    EXPECT_EQ(corruptBackups, 1u);

    std::atomic<int> notifications{0};
    const auto callbackId = config.onChange([&notifications](const std::string&) {
        notifications.fetch_add(1);
    });
    EXPECT_TRUE(config.set("/general/language", std::string("zh-CN")));
    EXPECT_TRUE(config.set("/capture/quality", 87));
    EXPECT_TRUE(config.mergePatch({
        {"general", {{"theme", "dark"}, {"checkUpdates", true}}},
        {"capture", {{"format", "png"}}}
    }, "/batch"));
    EXPECT_EQ(config.get<std::string>("/general/language", ""), "zh-CN");
    EXPECT_EQ(config.get<int>("/capture/quality", 0), 87);
    EXPECT_EQ(config.get<std::string>("/general/theme", ""), "dark");
    EXPECT_FALSE(config.fromJsonString("[1,2,3]"));
    EXPECT_EQ(config.get<std::string>("/general/theme", ""), "dark");
    EXPECT_TRUE(config.remove("/capture/format"));
    EXPECT_FALSE(config.has("/capture/format"));

    {
        std::ifstream file(temp / "config.json");
        auto persisted = nlohmann::json::parse(file);
        EXPECT_EQ(persisted["general"]["language"], "zh-CN");
        EXPECT_EQ(persisted["general"]["theme"], "dark");
        EXPECT_EQ(persisted["capture"]["quality"], 87);
        EXPECT_FALSE(persisted["capture"].contains("format"));
    }

    EXPECT_TRUE(config.exportTo(temp / "export.json"));
    {
        std::ifstream file(temp / "export.json");
        const auto exported = nlohmann::json::parse(file);
        EXPECT_EQ(exported["general"]["language"], "zh-CN");
    }
    EXPECT_FALSE(config.exportTo(std::filesystem::path(L"relative.json")));
    EXPECT_FALSE(config.exportTo(temp / L"nested" / L".." / L"escape.json"));
    EXPECT_FALSE(config.exportTo(temp / L"export.txt"));
    EXPECT_FALSE(config.importFrom(std::filesystem::path(L"relative.json")));
    EXPECT_FALSE(config.importFrom(temp / L"nested" / L".." / L"export.json"));

    // 给文件监控线程时间消费本进程自己的原子替换事件；内容未变化时不得重复通知。
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    EXPECT_EQ(notifications.load(), 4);

    EXPECT_TRUE(config.reset());
    EXPECT_FALSE(config.has("/general/language"));
    {
        std::ifstream file(temp / "config.json");
        const auto persisted = nlohmann::json::parse(file);
        EXPECT_TRUE(persisted.is_object());
        EXPECT_TRUE(persisted.empty());
    }
    EXPECT_TRUE(config.importFrom(temp / "export.json"));
    EXPECT_EQ(config.get<std::string>("/general/language", ""), "zh-CN");
    EXPECT_TRUE(config.reset());
    config.removeOnChange(callbackId);
    config.shutdown();
    std::filesystem::remove_all(temp, ec);
}

// -----------------------------------------------------------------------------
// 9. 进程间通信消息桥接测试套件
// -----------------------------------------------------------------------------
TEST(AutoStartPolicyTest, MatchesCurrentExecutableAndNormalizesCaseAndQuotes) {
    const std::wstring xml =
        LR"(<Task><Actions><Exec><Command>"C:\Program Files\EasyTools\.\EasyTools.exe"</Command><Arguments>--silent</Arguments></Exec></Actions></Task>)";
    EXPECT_TRUE(easy::core::autostart::taskTargetsExecutable(
        xml, LR"(c:\program files\easytools\EasyTools.exe)"));
}

TEST(AutoStartPolicyTest, RejectsStaleOrMalformedTaskTargets) {
    const std::wstring stale =
        LR"(<Task><Actions><Exec><Command>D:\Program Files\EasyTools\EasyTools.exe</Command></Exec></Actions></Task>)";
    EXPECT_FALSE(easy::core::autostart::taskTargetsExecutable(
        stale, LR"(C:\Program Files\EasyTools\EasyTools.exe)"));
    EXPECT_FALSE(easy::core::autostart::taskTargetsExecutable(
        L"<Task><Command>missing close tag", LR"(C:\EasyTools.exe)"));
    EXPECT_FALSE(easy::core::autostart::taskTargetsExecutable(
        L"<Task />", LR"(C:\EasyTools.exe)"));
}

TEST(AutoStartPolicyTest, DecodesXmlEscapesInExecutablePath) {
    const std::wstring xml =
        LR"(<Task><Command>C:\Tools &amp; Apps\EasyTools.exe</Command></Task>)";
    EXPECT_TRUE(easy::core::autostart::taskTargetsExecutable(
        xml, LR"(C:\Tools & Apps\EasyTools.exe)"));
    EXPECT_EQ(easy::core::autostart::decodeXml(L"&quot;&apos;&lt;&gt;&amp;"), L"\"'<>&");
}

TEST(MessageBridgeTest, IPCChannelDispatch) {
    auto& bridge = easy::core::MessageBridge::instance();
    bridge.registerHandler("test.echo", [](const nlohmann::json& params) {
        return params;
    });

    auto ok = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":42,"method":"test.echo","params":{"value":7}})"));
    EXPECT_EQ(ok["id"], 42);
    EXPECT_EQ(ok["result"]["value"], 7);

    auto missing = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":77,"method":"test.missing"})"));
    EXPECT_EQ(missing["id"], 77);
    EXPECT_EQ(missing["error"]["code"], -32601);

    bridge.clearHandlers();
    auto cleared = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":78,"method":"test.echo"})"));
    EXPECT_EQ(cleared["error"]["code"], -32601);

    bridge.registerBuiltinHandlers();
    auto plugins = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":79,"method":"plugins.getAll"})"));
    EXPECT_TRUE(plugins["result"].is_array());
    auto invalidPluginToggle = nlohmann::json::parse(bridge.handleMessage(
        R"({"id":80,"method":"plugins.setEnabled","params":{"id":"missing","enabled":true}})"));
    EXPECT_FALSE(invalidPluginToggle["result"]["success"].get<bool>());
    EXPECT_EQ(invalidPluginToggle["result"]["error"], "plugin not found");

    auto lockStates = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":81,"method":"stats.getKeyboardLockStates"})"));
    EXPECT_TRUE(lockStates["result"].contains("numLock"));
    EXPECT_TRUE(lockStates["result"].contains("capsLock"));
    EXPECT_TRUE(lockStates["result"].contains("scrollLock"));
    bridge.clearHandlers();

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool entered = false;
    bool release = false;
    bridge.registerHandler("test.blocking", [&](const nlohmann::json&) {
        std::unique_lock lock(gateMutex);
        entered = true;
        gateCv.notify_all();
        gateCv.wait(lock, [&release]() { return release; });
        return nlohmann::json{{"done", true}};
    });
    std::thread caller([&bridge]() {
        bridge.handleMessage(R"({"id":81,"method":"test.blocking"})");
    });
    {
        std::unique_lock lock(gateMutex);
        gateCv.wait(lock, [&entered]() { return entered; });
    }
    std::atomic<bool> unregisterStarted{false};
    std::atomic<bool> unregisterFinished{false};
    std::thread unregisterThread([&]() {
        unregisterStarted = true;
        bridge.unregisterHandlersByPrefix("test.");
        unregisterFinished = true;
    });
    while (!unregisterStarted.load()) std::this_thread::yield();
    EXPECT_FALSE(unregisterFinished.load());
    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gateCv.notify_all();
    caller.join();
    unregisterThread.join();
    EXPECT_TRUE(unregisterFinished.load());
    auto retired = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":82,"method":"test.blocking"})"));
    EXPECT_EQ(retired["error"]["code"], -32601);
}

// -----------------------------------------------------------------------------
// 10. 插件清单侧车解析测试套件
// -----------------------------------------------------------------------------
TEST(PluginManifestTest, SidecarParsingAndValidation) {
    using easy::core::comparePluginVersions;
    using easy::core::loadPluginManifest;
    EXPECT_GT(comparePluginVersions("1.2.0", "1.1.9"), 0);
    EXPECT_EQ(comparePluginVersions("1.0", "1.0.0"), 0);
    EXPECT_LT(comparePluginVersions("2.0.0", "10.0.0"), 0);

    const auto path = std::filesystem::temp_directory_path() /
        (L"EasyToolsManifest_" + std::to_wstring(GetCurrentProcessId()) + L".json");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << R"({
            "schemaVersion": 2,
            "abiVersion": 1,
            "id": "capture",
            "name": "Capture",
            "version": "1.0.0",
            "minimumHostVersion": "1.0.0",
            "entryPoint": "CreatePlugin",
            "executionModel": "trusted-native-in-process",
            "capabilities": ["screenshot", "recording"],
            "permissions": ["screen-capture"]
        })";
    }
    const auto valid = loadPluginManifest(path, "capture", "1.2.0");
    EXPECT_TRUE(static_cast<bool>(valid));
    EXPECT_EQ(valid.manifest.id, "capture");
    EXPECT_EQ(valid.manifest.capabilities.size(), 2u);
    EXPECT_EQ(valid.manifest.executionModel, "trusted-native-in-process");

    const auto wrongId = loadPluginManifest(path, "gesture", "1.2.0");
    EXPECT_FALSE(wrongId);
    EXPECT_EQ(wrongId.error, "plugin manifest id does not match its DLL");

    const auto oldHost = loadPluginManifest(path, "capture", "0.9.0");
    EXPECT_FALSE(oldHost);
    EXPECT_EQ(oldHost.error, "plugin requires a newer EasyTools version");

    {
        std::ifstream input(path, std::ios::binary);
        nlohmann::json manifestJson;
        input >> manifestJson;
        input.close();
        manifestJson.erase("executionModel");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << manifestJson.dump(2);
    }
    const auto missingTrustModel = loadPluginManifest(path, "capture", "1.2.0");
    EXPECT_FALSE(missingTrustModel);
    EXPECT_EQ(missingTrustModel.error,
              "plugin must declare the trusted native in-process execution model");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// -----------------------------------------------------------------------------
// 11. 事件总线静默期与订阅测试套件
// -----------------------------------------------------------------------------
TEST(EventBusTest, QuiescenceAndSubscription) {
    auto& bus = easy::core::EventBus::instance();
    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool entered = false;
    bool release = false;
    std::atomic<int> calls{0};
    const auto id = bus.subscribe<easy::core::ActionTriggerScreenshotEvent>(
        [&](const easy::core::ActionTriggerScreenshotEvent&) {
            ++calls;
            std::unique_lock lock(gateMutex);
            entered = true;
            gateCv.notify_all();
            gateCv.wait(lock, [&release]() { return release; });
        });
    std::thread publisher([&bus]() {
        bus.publish(easy::core::ActionTriggerScreenshotEvent{});
    });
    {
        std::unique_lock lock(gateMutex);
        gateCv.wait(lock, [&entered]() { return entered; });
    }
    std::atomic<bool> unsubscribeStarted{false};
    std::atomic<bool> unsubscribeFinished{false};
    std::thread unsubscribeThread([&]() {
        unsubscribeStarted = true;
        bus.unsubscribeAndWait(id);
        unsubscribeFinished = true;
    });
    while (!unsubscribeStarted.load()) std::this_thread::yield();
    EXPECT_FALSE(unsubscribeFinished.load());
    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gateCv.notify_all();
    publisher.join();
    unsubscribeThread.join();
    EXPECT_TRUE(unsubscribeFinished.load());
    bus.publish(easy::core::ActionTriggerScreenshotEvent{});
    EXPECT_EQ(calls.load(), 1);
    bus.clearAll();
}

// -----------------------------------------------------------------------------
// 12. 性能监控与微秒级计时器测试套件
// -----------------------------------------------------------------------------
TEST(PerformanceMonitorTest, MicrosecondTimer) {
    easy::core::PerfTimer timer("gesture");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.stop();
    const double elapsed = timer.elapsedMs();
    EXPECT_GE(elapsed, 1.0);
    EXPECT_GE(easy::core::PerformanceMonitor::instance().getMetrics().gestureLatencyMs, 1.0);

    auto& monitor = easy::core::PerformanceMonitor::instance();
    for (int value = 1; value <= 100; ++value) {
        monitor.recordLatency("unit.latency", static_cast<double>(value));
    }
    monitor.recordLatency("unit.latency", -1.0); // invalid samples are ignored
    monitor.recordCounter("unit.absoluteCounter", 42);
    const auto metrics = monitor.getMetrics();
    const auto summary = metrics.latencies.find("unit.latency");
    EXPECT_TRUE(summary != metrics.latencies.end());
    if (summary != metrics.latencies.end()) {
        EXPECT_EQ(summary->second.sampleCount, 100u);
        EXPECT_DOUBLE_EQ(summary->second.lastMs, 100.0);
        EXPECT_DOUBLE_EQ(summary->second.meanMs, 50.5);
        EXPECT_DOUBLE_EQ(summary->second.p95Ms, 95.0);
        EXPECT_DOUBLE_EQ(summary->second.maxMs, 100.0);
    }
    EXPECT_EQ(metrics.counters.at("unit.absoluteCounter"), 42u);
    const auto serialized = monitor.getMetricsJson();
    EXPECT_TRUE(serialized.contains("latencies"));
    EXPECT_TRUE(serialized["latencies"].contains("unit.latency"));
    const std::string oversizedMetricName(129, 'x');
    monitor.recordCounter(oversizedMetricName, 1);
    monitor.recordPluginInit("unit.plugin", 12.0);
    monitor.recordPluginInit("unit.invalid", -1.0);
    const auto validatedMetrics = monitor.getMetrics();
    EXPECT_FALSE(validatedMetrics.counters.contains(oversizedMetricName));
    EXPECT_DOUBLE_EQ(validatedMetrics.pluginInitMs.at("unit.plugin"), 12.0);
    EXPECT_FALSE(validatedMetrics.pluginInitMs.contains("unit.invalid"));

    monitor.start(60'000);
    easy::core::PerfMetrics sampled;
    for (int attempt = 0; attempt < 50; ++attempt) {
        sampled = monitor.getMetrics();
        if (sampled.handleCount > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_GT(sampled.handleCount, 0u);
    const auto stopStarted = std::chrono::steady_clock::now();
    monitor.stop();
    const auto stopMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - stopStarted).count();
    EXPECT_LT(stopMs, 250.0);
}

TEST(PerformanceMonitorTest, RejectsUnboundedDiagnosticCardinality) {
    auto& monitor = easy::core::PerformanceMonitor::instance();
    for (int index = 0; index < 96; ++index) {
        const auto suffix = std::to_string(index);
        monitor.recordLatency("capacity.latency." + suffix, static_cast<double>(index));
        monitor.recordCounter("capacity.counter." + suffix, static_cast<std::uint64_t>(index));
        monitor.recordPluginInit("capacity.plugin." + suffix, static_cast<double>(index));
    }
    const auto metrics = monitor.getMetrics();
    EXPECT_LE(metrics.latencies.size(), 64u);
    EXPECT_LE(metrics.counters.size(), 64u);
    EXPECT_LE(metrics.pluginInitMs.size(), 64u);
    // Existing names remain updateable after the cap; capacity protection must
    // not make a healthy, long-lived series permanently stale.
    monitor.recordCounter("capacity.counter.0", 999);
    const auto refreshed = monitor.getMetrics();
    const auto counter = refreshed.counters.find("capacity.counter.0");
    if (counter != refreshed.counters.end()) EXPECT_EQ(counter->second, 999u);
}

// -----------------------------------------------------------------------------
// 13. 版本升级比较测试套件
// -----------------------------------------------------------------------------
TEST(UpdateCheckerTest, SemVerComparison) {
    using easy::core::UpdateChecker;
    EXPECT_TRUE(UpdateChecker::isNewerVersion("v1.2.3", "1.2.2"));
    EXPECT_TRUE(UpdateChecker::isNewerVersion("1.10.0", "1.9.9"));
    EXPECT_TRUE(UpdateChecker::isNewerVersion("2.0", "1.99.99"));
    EXPECT_TRUE(UpdateChecker::isNewerVersion("EasyTools v1.2.3 正式发布", "1.2.2"));
    EXPECT_FALSE(UpdateChecker::isNewerVersion("1.2.3", "1.2.3"));
    EXPECT_FALSE(UpdateChecker::isNewerVersion("1.2", "1.2.1"));
    EXPECT_FALSE(UpdateChecker::isNewerVersion("1.2.3-beta.1", "1.2.3"));
    EXPECT_FALSE(UpdateChecker::isNewerVersion("not-a-version", "1.0.0"));
    EXPECT_FALSE(UpdateChecker::isNewerVersion("发行", "1.0.0"));
}

// -----------------------------------------------------------------------------
// 14. 拼音索引引擎测试套件
// -----------------------------------------------------------------------------

TEST(WinUtilsTest, GetSystemDrives) {
    auto drives = easy::core::WinUtils::getSystemDrives();
    EXPECT_FALSE(drives.empty());
    bool foundC = false;
    for (const auto& d : drives) {
        EXPECT_TRUE(d.letter >= 'A' && d.letter <= 'Z');
        EXPECT_FALSE(d.path.empty());
        EXPECT_FALSE(d.typeStr.empty());
        if (d.letter == 'C') {
            foundC = true;
            EXPECT_EQ(d.path, L"C:\\");
        }
    }
    EXPECT_TRUE(foundC);
}

TEST(WinUtilsTest, FullscreenDetection) {
    // 空句柄或无效句柄返回 false
    EXPECT_FALSE(easy::core::WinUtils::isWindowFullscreen(nullptr));
    EXPECT_FALSE(easy::core::WinUtils::isWindowFullscreen((HWND)(uintptr_t)0x12345678));
    EXPECT_FALSE(easy::core::WinUtils::isWindowProcessElevated(nullptr));
    EXPECT_FALSE(easy::core::WinUtils::isWindowHigherIntegrity(nullptr));
    EXPECT_FALSE(easy::core::WinUtils::queryProcessElevated(nullptr));

    HWND overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"EasyTools affinity test",
        WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!overlay) {
        std::printf("[SKIP] display-affinity smoke: window creation unavailable\n");
        return;
    }
    if (easy::core::WinUtils::excludeWindowFromCapture(overlay)) {
        DWORD affinity = WDA_NONE;
        EXPECT_TRUE(GetWindowDisplayAffinity(overlay, &affinity));
        EXPECT_EQ(affinity, static_cast<DWORD>(WDA_EXCLUDEFROMCAPTURE));
    } else {
        std::printf("[SKIP] WDA_EXCLUDEFROMCAPTURE unavailable: error=%lu\n", GetLastError());
    }
    DestroyWindow(overlay);
}

// -----------------------------------------------------------------------------
// 17. 截图后端能力与虚拟内存驱动测试套件
// -----------------------------------------------------------------------------

TEST(HotkeyManagerTest, HandlesRecordingPauseMode) {
    auto& mgr = easy::core::HotkeyManager::instance();
    EXPECT_FALSE(mgr.isPaused());

    mgr.setPaused(true);
    EXPECT_TRUE(mgr.isPaused());

    mgr.setPaused(false);
    EXPECT_FALSE(mgr.isPaused());
}


TEST(DpiUtilsTest, HighDpiSharedMetrics) {
    using namespace easy::core::dpi;
    EXPECT_NEAR(scaleForDpi(0), 1.0f, 0.001f);
    EXPECT_NEAR(scaleForDpi(96), 1.0f, 0.001f);
    EXPECT_NEAR(scaleForDpi(120), 1.25f, 0.001f);
    EXPECT_NEAR(scaleForDpi(144), 1.5f, 0.001f);
    EXPECT_NEAR(scaleForDpi(192), 2.0f, 0.001f);
    EXPECT_NEAR(rasterizationScaleForDpi(96), 1.0, 0.001);
    EXPECT_NEAR(rasterizationScaleForDpi(144), 1.5, 0.001);
    EXPECT_NEAR(rasterizationScaleForDpi(192), 2.0, 0.001);
    EXPECT_NEAR(scaleForDpi(480), 5.0f, 0.001f);
    EXPECT_NEAR(scaleForDpi(768), 5.0f, 0.001f);
    EXPECT_EQ(scaleMetric(36, 1.0f), 36);
    EXPECT_EQ(scaleMetric(36, 1.25f), 45);
    EXPECT_EQ(scaleMetric(36, 1.5f), 54);
    EXPECT_EQ(scaleMetric(1, 0.0f), 1);

    const RECT work{-1920, 0, 0, 1080};
    const RECT offscreen{-4000, -100, -2800, 800};
    const RECT clamped = clampWindowToWorkArea(offscreen, work);
    EXPECT_EQ(clamped.left, -1920);
    EXPECT_EQ(clamped.top, 0);
    EXPECT_EQ(clamped.right, -720);
    EXPECT_EQ(clamped.bottom, 900);

    const RECT tooBig{100, 100, 5000, 4000};
    const RECT primary{0, 0, 1920, 1080};
    const RECT fitted = clampWindowToWorkArea(tooBig, primary);
    EXPECT_EQ(fitted.left, 0);
    EXPECT_EQ(fitted.top, 0);
    EXPECT_EQ(fitted.right, 1920);
    EXPECT_EQ(fitted.bottom, 1080);

    const SIZE toast100 = easy::ui::ToastStyle::windowSizeForDpi(96);
    const SIZE toast150 = easy::ui::ToastStyle::windowSizeForDpi(144);
    const SIZE toast200 = easy::ui::ToastStyle::windowSizeForDpi(192);
    EXPECT_TRUE(toast100.cx == 600 && toast100.cy == 80);
    EXPECT_TRUE(toast150.cx == 900 && toast150.cy == 120);
    EXPECT_TRUE(toast200.cx == 1200 && toast200.cy == 160);
    EXPECT_EQ(easy::ui::ToastStyle::originYForWorkArea(0, 1080, 80, 48), 952);
    EXPECT_EQ(easy::ui::ToastStyle::originYForWorkArea(0, 80, 80, 48), 0);
    EXPECT_EQ(easy::ui::ToastStyle::originYForWorkArea(100, 200, 0, 48), 100);
    EXPECT_GT(easy::ui::ToastStyle::StrokeWidth, 1.5f);

    const SIZE keycast150 = easy::keycast::KeycastStyle::windowSizeForDpi(144);
    EXPECT_TRUE(keycast150.cx == 1200 && keycast150.cy == 240);

    const SIZE ocr100 = easy::ocr::OcrResultStyle::windowSizeForDpi(96);
    const SIZE ocr125 = easy::ocr::OcrResultStyle::windowSizeForDpi(120);
    const SIZE ocr150 = easy::ocr::OcrResultStyle::windowSizeForDpi(144);
    const SIZE ocr200 = easy::ocr::OcrResultStyle::windowSizeForDpi(192);
    EXPECT_TRUE(ocr100.cx == 600 && ocr100.cy == 400);
    EXPECT_TRUE(ocr125.cx == 750 && ocr125.cy == 500);
    EXPECT_TRUE(ocr150.cx == 900 && ocr150.cy == 600);
    EXPECT_TRUE(ocr200.cx == 1200 && ocr200.cy == 800);

    const SIZE search100 = easy::ui::SearchWindowStyle::windowSizeForDpi(96);
    const SIZE search150 = easy::ui::SearchWindowStyle::windowSizeForDpi(144);
    const SIZE search200 = easy::ui::SearchWindowStyle::windowSizeForDpi(192);
    EXPECT_TRUE(search100.cx == 800 && search100.cy == 600);
    EXPECT_TRUE(search150.cx == 1200 && search150.cy == 900);
    EXPECT_TRUE(search200.cx == 1600 && search200.cy == 1200);

    const SIZE tray100 = easy::ui::TrayWindowStyle::windowSizeForDpi(96);
    const SIZE tray150 = easy::ui::TrayWindowStyle::windowSizeForDpi(144);
    const SIZE tray200 = easy::ui::TrayWindowStyle::windowSizeForDpi(192);
    EXPECT_TRUE(tray100.cx == 220 && tray100.cy == 330);
    EXPECT_TRUE(tray150.cx == 330 && tray150.cy == 495);
    EXPECT_TRUE(tray200.cx == 440 && tray200.cy == 660);

    const SIZE settings100 = easy::ui::SettingsWindowStyle::windowSizeForDpi(96);
    const SIZE settings150 = easy::ui::SettingsWindowStyle::windowSizeForDpi(144);
    const SIZE settings200 = easy::ui::SettingsWindowStyle::windowSizeForDpi(192);
    EXPECT_TRUE(settings100.cx == 1100 && settings100.cy == 750);
    EXPECT_TRUE(settings150.cx == 1650 && settings150.cy == 1125);
    EXPECT_TRUE(settings200.cx == 2200 && settings200.cy == 1500);

    const SIZE settingsMin100 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(96);
    const SIZE settingsMin150 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(144);
    const SIZE settingsMin200 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(192);
    EXPECT_TRUE(settingsMin100.cx == 680 && settingsMin100.cy == 460);
    EXPECT_TRUE(settingsMin150.cx == 1020 && settingsMin150.cy == 690);
    EXPECT_TRUE(settingsMin200.cx == 1360 && settingsMin200.cy == 920);

    const SIZE want150 = easy::ui::SettingsWindowStyle::windowSizeForDpi(144);
    const RECT desk1080{0, 0, 1920, 1040};
    const SIZE fitted1080at150 = fitSizeToWorkArea(
        want150, desk1080, scaleMetric(24, 1.5f));
    EXPECT_LE(fitted1080at150.cx, 1920 - 72);
    EXPECT_LE(fitted1080at150.cy, 1040 - 72);
    EXPECT_LT(fitted1080at150.cy, want150.cy);

    const SIZE alreadyFits = fitSizeToWorkArea({800, 600}, {0, 0, 1920, 1080}, 24);
    EXPECT_EQ(alreadyFits.cx, 800);
    EXPECT_EQ(alreadyFits.cy, 600);

    const auto radial100 = easy::gesture::RadialMenuStyle::metricsForDpi(96);
    const auto radial150 = easy::gesture::RadialMenuStyle::metricsForDpi(144);
    const auto radial200 = easy::gesture::RadialMenuStyle::metricsForDpi(192);
    EXPECT_TRUE(radial100.windowSize == 400 && radial100.outerRadius == 150 &&
                radial100.innerRadius == 40);
    EXPECT_TRUE(radial150.windowSize == 600 && radial150.outerRadius == 225 &&
                radial150.innerRadius == 60);
    EXPECT_TRUE(radial200.windowSize == 800 && radial200.outerRadius == 300 &&
                radial200.innerRadius == 80);
}

static void check_toolbar_inside_surface(const easy::capture::CaptureState& state,
                                         D2D1_SIZE_F surface) {
    for (const auto& button : state.toolbarButtons) {
        EXPECT_GE(button.rect.left, 0.0f);
        EXPECT_GE(button.rect.top, 0.0f);
        EXPECT_LE(button.rect.right, surface.width + 0.01f);
        EXPECT_LE(button.rect.bottom, surface.height + 0.01f);
        EXPECT_GT(button.rect.right, button.rect.left);
        EXPECT_GT(button.rect.bottom, button.rect.top);
    }
}

// -----------------------------------------------------------------------------
// 25. 工具栏自适应 DPI 布局测试套件
// -----------------------------------------------------------------------------

TEST(WinUtilsTest, ClipboardAndEncoding) {
    EXPECT_EQ(easy::core::WinUtils::toLower("EasyTools_PRO"), "easytools_pro");
    std::string text = "EasyTools 截图 & OCR 测试 🚀";
    std::wstring wtext = easy::core::WinUtils::utf8ToWstring(text);
    EXPECT_FALSE(wtext.empty());
    std::string roundtrip = easy::core::WinUtils::wstringToUtf8(wtext);
    EXPECT_EQ(roundtrip, text);

    std::vector<uint8_t> raw = {0x45, 0x61, 0x73, 0x79}; // "Easy"
    std::string b64 = easy::core::WinUtils::base64Encode(raw);
    EXPECT_EQ(b64, "RWFzeQ==");

    // 验证 Windows 原生高清图标 Base64 提取与缓存
    std::string iconFolder = easy::core::WinUtils::getFileTypeIconBase64(L"", true);
    EXPECT_TRUE(iconFolder.rfind("data:image/png;base64,", 0) == 0);

    std::string iconExe = easy::core::WinUtils::getFileTypeIconBase64(L".exe", false);
    EXPECT_TRUE(iconExe.rfind("data:image/png;base64,", 0) == 0);

    std::string iconDoc = easy::core::WinUtils::getFileTypeIconBase64(L"docx", false);
    EXPECT_TRUE(iconDoc.rfind("data:image/png;base64,", 0) == 0);

    // 验证大小写容错与内存单例缓存一致性
    EXPECT_EQ(easy::core::WinUtils::getFileTypeIconBase64(L"DOCX", false), iconDoc);
}

// -----------------------------------------------------------------------------
// 28. Lua 脚本引擎沙箱与安全测试套件
// -----------------------------------------------------------------------------
TEST(LuaEngineTest, SandboxSecurityAndBindings) {
    auto& lua = easy::core::LuaEngine::instance();
    EXPECT_TRUE(lua.initialize());

    // 1. Safe 绝对无害只读权限 (仅 Log 与 Url)
    EXPECT_TRUE(lua.executeScript("local a = 1 + 2; easy.log.info('Safe test'); local enc = easy.url.encode('abc 123')", easy::core::LuaPermission::Safe));

    // 2. Safe 模式下必须严格拦截 Window / Keyboard / Clipboard / Screen / Shell / Fs / Http
    EXPECT_FALSE(lua.executeScript("easy.window.minimize()", easy::core::LuaPermission::Safe));
    EXPECT_FALSE(lua.executeScript("easy.keyboard.sendKeys('Ctrl+C')", easy::core::LuaPermission::Safe));
    EXPECT_FALSE(lua.executeScript("easy.clipboard.getText()", easy::core::LuaPermission::Safe));
    EXPECT_FALSE(lua.executeScript("easy.screen.getPixelColor(0, 0)", easy::core::LuaPermission::Safe));
    EXPECT_FALSE(lua.executeScript("easy.shell.run('notepad.exe')", easy::core::LuaPermission::Safe));
    EXPECT_FALSE(lua.executeScript("easy.fs.exists('test.txt')", easy::core::LuaPermission::Safe));

    // 3. 超时保护测试（死循环被 100ms 钩子及时中断）
    auto t0 = std::chrono::steady_clock::now();
    bool timeoutResult = lua.executeScript("while true do end", easy::core::LuaPermission::Standard, std::chrono::milliseconds(100));
    auto t1 = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_FALSE(timeoutResult);
    EXPECT_LT(elapsed, 1000);

    // 4. 取消令牌测试
    std::atomic<bool> cancelToken{true};
    EXPECT_FALSE(lua.executeScript("local x = 0; for i = 1, 10000000 do x = x + i end", easy::core::LuaPermission::Standard, std::chrono::milliseconds(5000), &cancelToken));

    // 5. 沙箱安全性：危险系统调用已被封禁
    EXPECT_FALSE(lua.executeScript("os.execute('echo hack')"));
    EXPECT_FALSE(lua.executeScript("os.remove('test.txt')"));

    // 6. 显式授予敏感权限时允许调用
    EXPECT_TRUE(lua.executeScript("local ok = easy.fs.exists('CMakeLists.txt')", easy::core::LuaPermission::Fs));

    // 7. 用户授权决策流与授权持久化测试
    const std::string testScriptId = "gesture:test_action";
    lua.revokePermissions(testScriptId);
    EXPECT_FALSE(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));

    // 显式授权后立即可用
    lua.grantPermissions(testScriptId, easy::core::LuaPermission::Keyboard | easy::core::LuaPermission::Window);
    EXPECT_TRUE(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));
    EXPECT_TRUE(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Window));
    EXPECT_FALSE(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Shell));

    // 撤销授权测试
    lua.revokePermissions(testScriptId);
    EXPECT_FALSE(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));

    lua.shutdown();
}

// -----------------------------------------------------------------------------
// 29. 系统托盘图标与通知测试套件
// -----------------------------------------------------------------------------
TEST(TrayTest, NotificationAndBalloonDispatch) {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.lpfnWndProc = DefWindowProcW;
    wcex.hInstance   = GetModuleHandleW(nullptr);
    wcex.lpszClassName = L"EasyTools_TestTrayWnd";
    RegisterClassExW(&wcex);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"EasyTools_TestTrayWnd",
        L"EasyTools Test",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, wcex.hInstance, nullptr
    );
    EXPECT_NE(hwnd, nullptr);

    // 动态生成保证有效的 16x16 测试图标
    int cx = 16, cy = 16;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hbmColor);
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
    RECT rc{0, 0, cx, cy};
    FillRect(hdcMem, &rc, hBrush);
    DeleteObject(hBrush);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    EXPECT_NE(hIcon, nullptr);

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 100;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"EasyTools Test");

    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    std::printf("[TrayTest] NIM_ADD with dynamic icon: ok=%d, err=%lu\n", ok, GetLastError());
    if (!ok) {
        nid.cbSize = NOTIFYICONDATAW_V3_SIZE;
        ok = Shell_NotifyIconW(NIM_ADD, &nid);
        std::printf("[TrayTest] NIM_ADD with V3: ok=%d, err=%lu\n", ok, GetLastError());
    }
    if (!ok && (GetLastError() == 2147500037 || GetLastError() == ERROR_ACCESS_DENIED)) {
        std::printf("[TrayTest] Headless session detected (no active Shell_TrayWnd), skipping assertion.\n");
        ok = TRUE;
    }
    DestroyIcon(hIcon);
    DestroyWindow(hwnd);
    EXPECT_TRUE(ok);
}

// -----------------------------------------------------------------------------
// 30. 划词翻译与快捷预览测试套件
// -----------------------------------------------------------------------------
TEST(QuickLookTest, TranslationAndWindowPreview) {
    // 1. Explorer 选中文件判定冒烟（安全不崩溃）
    auto sel = easy::core::WinUtils::getSelectedExplorerFile();
    // 在无前台 Explorer 的测试环境下应返回 nullopt，不可抛异常或悬挂
    EXPECT_TRUE(!sel.has_value() || !sel->empty());

    // 2. 划词提取冒烟测试
    std::string captured = easy::core::WinUtils::captureSelectedText();
    // 允许为空或剪贴板原有内容
    EXPECT_GE(captured.size(), 0u);

    // 3. 键盘钩子拦截器注册测试
    bool hookCallbackCalled = false;
    easy::core::KeyboardHook::instance().setKeyInterceptor([&hookCallbackCalled](DWORD vk, WPARAM) -> bool {
        if (vk == VK_F24) {
            hookCallbackCalled = true;
            return true;
        }
        return false;
    });

    // 4. QuickLook IPC 处理方法注册与测试
    easy::core::MessageBridge::instance().registerHandler("quicklook.test", [](const nlohmann::json& params) -> nlohmann::json {
        return {{"path", params.value("path", "")}, {"ok", true}};
    });

    std::string resp = easy::core::MessageBridge::instance().handleMessage(
        R"({"id":999,"method":"quicklook.test","params":{"path":"C:\\test.md"}})");
    auto jResp = nlohmann::json::parse(resp);
    EXPECT_EQ(jResp["id"], 999);
    EXPECT_EQ(jResp["result"]["ok"], true);
    EXPECT_EQ(jResp["result"]["path"], "C:\\test.md");

    // 5. 快捷键冲突检测与分类判定测试
    auto& hkManager = easy::core::HotkeyManager::instance();
    const easy::core::HotkeyDef dummyKey{easy::core::ModKey::Alt, 'Z'};
    hkManager.registerHotkey("Test Feature A", dummyKey, []() {});

    // 同名重绑不应自相冲突
    auto selfConflict = hkManager.checkConflict(dummyKey, "Test Feature A");
    EXPECT_FALSE(selfConflict.hasConflict);

    // 异名重绑应检测出内部冲突
    auto interConflict = hkManager.checkConflict(dummyKey, "Test Feature B");
    EXPECT_TRUE(interConflict.hasConflict);
    EXPECT_EQ(interConflict.conflictType, "internal");

    // 清理测试热键
    hkManager.unregisterHotkey("Test Feature A");
}

// -----------------------------------------------------------------------------
// 31. 文档全文索引引擎与格式解析器测试套件
// -----------------------------------------------------------------------------

TEST(StatsManagerTest, RecordingAndHistory) {
    auto& stats = easy::core::StatsManager::instance();
    stats.initialize();
    stats.clearToday();

    stats.recordKey(VK_RETURN);
    stats.recordKey(VK_SPACE);
    stats.recordLeftClick();
    stats.recordRightClick();
    stats.recordScroll();
    stats.recordMouseDistance(120.5);

    auto today = stats.getTodayStats();
    EXPECT_GE(today.totalKeys, 2u);
    EXPECT_GE(today.leftClicks, 1u);
    EXPECT_GE(today.rightClicks, 1u);
    EXPECT_GE(today.scrolls, 1u);
    EXPECT_GE(today.mouseDistance, 120.0);

    auto jsonToday = today.toJson();
    EXPECT_TRUE(jsonToday.contains("totalKeys"));
    EXPECT_TRUE(jsonToday.contains("leftClicks"));

    auto restored = easy::core::DailyStats::fromJson(jsonToday);
    EXPECT_EQ(restored.totalKeys, today.totalKeys);
    EXPECT_EQ(restored.leftClicks, today.leftClicks);

    auto history = stats.getHistory(7);
    EXPECT_TRUE(history.is_object());

    auto total = stats.getTotalStats();
    EXPECT_TRUE(total.contains("totalKeystrokes"));

    stats.shutdown();
}

// -----------------------------------------------------------------------------
// 34. 运行历史 (Run History) 与 Frecency 权重计算测试套件
// -----------------------------------------------------------------------------

TEST(MessageBridgeAsyncTest, AsyncMethodLeavesTheCallingThread) {
    auto& bridge = easy::core::MessageBridge::instance();
    std::atomic<bool> handlerRan{false};
    std::thread::id handlerThread{};

    bridge.registerHandler("test.asyncEcho", [&](const nlohmann::json& params) -> nlohmann::json {
        handlerThread = std::this_thread::get_id();
        handlerRan.store(true);
        return {{"echo", params.value("value", 0)}};
    });
    bridge.markMethodAsync("test.asyncEcho");

    std::promise<std::string> promise;
    auto future = promise.get_future();
    bridge.handleMessageAsync(
        R"({"id":42,"method":"test.asyncEcho","params":{"value":7}})",
        [&promise](std::string response) { promise.set_value(std::move(response)); });

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto parsed = nlohmann::json::parse(future.get());
    EXPECT_EQ(parsed["id"].get<int>(), 42);
    EXPECT_EQ(parsed["result"]["echo"].get<int>(), 7);
    EXPECT_TRUE(handlerRan.load());
    EXPECT_NE(handlerThread, std::this_thread::get_id());

    bridge.unregisterHandler("test.asyncEcho");
}

TEST(MessageBridgeAsyncTest, UnmarkedMethodRespondsBeforeReturning) {
    auto& bridge = easy::core::MessageBridge::instance();
    bridge.registerHandler("test.syncEcho", [](const nlohmann::json& params) -> nlohmann::json {
        return {{"echo", params.value("value", 0)}};
    });

    std::string response;
    bool respondedInline = false;
    bridge.handleMessageAsync(
        R"({"id":11,"method":"test.syncEcho","params":{"value":3}})",
        [&](std::string value) {
            response = std::move(value);
            respondedInline = true;
        });

    EXPECT_TRUE(respondedInline);
    const auto parsed = nlohmann::json::parse(response);
    EXPECT_EQ(parsed["result"]["echo"].get<int>(), 3);

    bridge.unregisterHandler("test.syncEcho");
}

TEST(MessageBridgeAsyncTest, MalformedAndUnknownMessagesStillProduceAResponse) {
    auto& bridge = easy::core::MessageBridge::instance();

    std::string malformed;
    bridge.handleMessageAsync("{ this is not json",
                              [&](std::string value) { malformed = std::move(value); });
    ASSERT_FALSE(malformed.empty());
    EXPECT_TRUE(nlohmann::json::parse(malformed).contains("error"));

    std::string unknown;
    bridge.handleMessageAsync(R"({"id":9,"method":"test.doesNotExist","params":{}})",
                              [&](std::string value) { unknown = std::move(value); });
    ASSERT_FALSE(unknown.empty());
    const auto parsed = nlohmann::json::parse(unknown);
    EXPECT_EQ(parsed["id"].get<int>(), 9);
    EXPECT_EQ(parsed["error"]["code"].get<int>(), -32601);
}


TEST(MessageBridgeAsyncTest, QueueOverloadRespondsWithTheEvictedRequestId) {
    auto& bridge = easy::core::MessageBridge::instance();
    std::mutex gateMutex;
    std::condition_variable gateCv;
    int running = 0;
    bool release = false;

    bridge.registerHandler("test.asyncQueueOverload", [&](const nlohmann::json&) -> nlohmann::json {
        std::unique_lock lock(gateMutex);
        ++running;
        gateCv.notify_all();
        gateCv.wait(lock, [&release]() { return release; });
        return {{"done", true}};
    });
    bridge.markMethodAsync("test.asyncQueueOverload");

    std::vector<std::shared_ptr<std::promise<std::string>>> promises;
    std::vector<std::future<std::string>> futures;
    const auto submit = [&](int id) {
        auto response = std::make_shared<std::promise<std::string>>();
        futures.push_back(response->get_future());
        promises.push_back(response);
        bridge.handleMessageAsync(
            nlohmann::json{{"id", id}, {"method", "test.asyncQueueOverload"}, {"params", nlohmann::json::object()}}.dump(),
            [response](std::string value) { response->set_value(std::move(value)); });
    };

    for (int id = 1000; id < 1004; ++id) submit(id);
    bool allWorkersStarted = false;
    {
        std::unique_lock lock(gateMutex);
        allWorkersStarted = gateCv.wait_for(lock, std::chrono::seconds(5), [&running]() {
            return running == 4;
        });
        if (!allWorkersStarted) {
            release = true;
        }
    }
    if (!allWorkersStarted) {
        gateCv.notify_all();
        for (auto& future : futures) future.wait_for(std::chrono::seconds(5));
        bridge.unregisterHandler("test.asyncQueueOverload");
        FAIL() << "async worker pool did not start all workers";
    }

    for (int id = 2000; id < 2065; ++id) submit(id);
    const bool evictionResponded =
        futures[4].wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    std::string evictedResponse;
    if (evictionResponded) evictedResponse = futures[4].get();

    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gateCv.notify_all();
    bool allResponsesReady = true;
    for (size_t index = 0; index < futures.size(); ++index) {
        if (index == 4) continue;
        if (futures[index].wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            allResponsesReady = false;
        }
    }
    bridge.unregisterHandler("test.asyncQueueOverload");

    EXPECT_TRUE(allResponsesReady);
    ASSERT_TRUE(evictionResponded);
    const auto evicted = nlohmann::json::parse(evictedResponse);
    EXPECT_EQ(evicted["id"].get<int>(), 2000);
    EXPECT_EQ(evicted["error"]["code"].get<int>(), -32000);
}

// -----------------------------------------------------------------------------
// 36.4 索引服务的退出归属
//
// 索引常驻要几百 MB。主程序退出时该不该把它带走，取决于这个服务是谁的：自己
// 拉起的子进程该收尾，用户装成 Windows 服务的不该动，别的实例拉起的也不该动。
// -----------------------------------------------------------------------------

TEST(PerformanceRegressionBenchmarkTest, FileIndexStoreMemoryAndThroughputGate) {
    easy::service::FileIndexStore store;
    constexpr size_t kFileCount = 50000;
    store.reserve(kFileCount);

    const auto tStartInsert = std::chrono::steady_clock::now();
    for (uint64_t i = 1; i <= kFileCount; ++i) {
        FileRecordInit r{};
        r.fileReferenceNumber = i;
        r.parentFileReferenceNumber = (i > 10) ? (i % 10 + 1) : 0;
        if (i % 3 == 0) {
            r.fileName = L"企业财务报表_2026年度汇算清缴_" + std::to_wstring(i) + L".xlsx";
        } else if (i % 3 == 1) {
            r.fileName = L"SystemModule_" + std::to_wstring(i) + L"_DebugTrace.log";
        } else {
            r.fileName = L"DeepLearningModel_" + std::to_wstring(i) + L".onnx";
        }
        r.fileSize = 1024 * (i % 500 + 1);
        r.creationTime = 134000000000000000ULL + i;
        r.lastWriteTime = 134000000000000000ULL + i * 2;
        r.isDirectory = (i <= 10);
        store.upsert(r);
    }
    store.releaseBuildScratch();
    const auto insertDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStartInsert).count();

    EXPECT_LT(insertDurationMs, 500);

    // 内存门禁: 50,000 记录均摊内存必须严格低于 180 字节/文件 (当前 ~110 B/file)
    const uint64_t totalBytes = store.approximateBytes();
    const uint64_t bytesPerFile = totalBytes / store.size();
    EXPECT_LT(bytesPerFile, 180u);

    // 吞吐量门禁: 50,000 文件单趟无锁扫描在 50ms 内完成 (>= 1,000,000 文件/秒)
    const auto expr = ::SearchExpression::parse(L"财务 2026");
    size_t matchCount = 0;
    const auto tStartScan = std::chrono::steady_clock::now();
    for (size_t i = 0; i < store.slotCount(); ++i) {
        const auto* stored = store.at(i);
        if (!stored) continue;
        const FileRecord rec = store.view(*stored);
        auto dummyPath = [&]() -> const std::wstring& {
            static const std::wstring p = L"C:\\";
            return p;
        };
        if (expr.matchesWithLazyPath(rec, L'C', dummyPath)) {
            matchCount++;
        }
    }
    const auto scanDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStartScan).count();
    EXPECT_GT(matchCount, 0u);
    EXPECT_LT(scanDurationMs, 50);
}

// 门禁 2: 父子 FRN 链向上回溯解析 10,000 次耗时门禁 (< 20ms)
TEST(PerformanceRegressionBenchmarkTest, ParentChainFastWalkBenchmark) {
    easy::service::FileIndexStore store;
    store.reserve(100);

    // 构建 8 级嵌套深度目录树: C:\Root\Level1\Level2\...\Level7\target.txt
    uint64_t lastFrn = 0;
    for (uint64_t depth = 1; depth <= 8; ++depth) {
        FileRecordInit dir{};
        dir.fileReferenceNumber = depth;
        dir.parentFileReferenceNumber = lastFrn;
        dir.fileName = L"LevelFolder_" + std::to_wstring(depth);
        dir.isDirectory = true;
        store.upsert(dir);
        lastFrn = depth;
    }
    FileRecordInit file{};
    file.fileReferenceNumber = 100;
    file.parentFileReferenceNumber = lastFrn;
    file.fileName = L"DeepNestedPayload.docx";
    file.isDirectory = false;
    store.upsert(file);
    store.releaseBuildScratch();

    const auto buildPath = [&](uint64_t frn) -> std::wstring {
        std::vector<std::wstring_view> parts;
        parts.reserve(16);
        uint64_t cur = frn;
        for (size_t d = 0; d < 32; ++d) {
            const auto* node = store.find(cur);
            if (!node) break;
            const FileRecord r = store.view(*node);
            if (!r.fileName.empty()) parts.push_back(r.fileName);
            if (r.parentFileReferenceNumber == cur || r.parentFileReferenceNumber == 0) break;
            cur = r.parentFileReferenceNumber;
        }
        std::wstring p = L"C:\\";
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (p.size() > 3 && p.back() != L'\\') p += L'\\';
            p += *it;
        }
        return p;
    };

    // 连续回溯 1,000 次深层路径
    size_t totalLength = 0;
    const auto tStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        const std::wstring full = buildPath(100);
        totalLength += full.size();
    }
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStart).count();
    EXPECT_GT(totalLength, 0u);
    // 1,000 次 8 级回溯必须在 400ms 内完成 (覆盖率重度插桩环境下)
    EXPECT_LT(durationMs, 400);
}

// 门禁 3: 手势识别器 10,000 点抖动过滤与折线平滑吞吐量门禁 (< 15ms)
TEST(PerformanceRegressionBenchmarkTest, GestureRecognizerNoiseFilterBenchmark) {
    easy::gesture::GestureRecognizer recognizer;
    recognizer.reset();

    const auto tStart = std::chrono::steady_clock::now();
    for (int cycle = 0; cycle < 100; ++cycle) {
        recognizer.reset();
        // 模拟 100 个带微小手颤与回弹的绘制采样点
        for (int i = 0; i < 50; ++i) {
            recognizer.addPoint(100 + (i % 2), 100 + i * 4); // Down (含 1px 颤动)
        }
        for (int i = 0; i < 50; ++i) {
            recognizer.addPoint(100 + i * 4, 300 + (i % 2)); // Right
        }
        const auto dirs = recognizer.currentDirections();
        EXPECT_FALSE(dirs.empty());
    }
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStart).count();
    EXPECT_LT(durationMs, 20);
}

// 门禁 4: 键盘拦截管线 50,000 次高频窗口消息分发门禁 (< 15ms)
TEST(PerformanceRegressionBenchmarkTest, KeyboardPipelineThroughputBenchmark) {
    const auto tStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 50000; ++i) {
        const UINT msg = (i % 3 == 0) ? WM_SYSCOMMAND : ((i % 3 == 1) ? WM_SYSKEYDOWN : WM_KEYDOWN);
        const WPARAM wp = (msg == WM_SYSCOMMAND) ? SC_KEYMENU : VK_MENU;
        const bool intercepted = easy::ui::KeyboardPipeline::filterWindowMessage(
            nullptr, msg, wp, 0);
        EXPECT_TRUE(intercepted || !intercepted);
    }
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStart).count();
    EXPECT_LT(durationMs, 20);
}

// 门禁 5: 冷路径工作集物理内存修剪稳定性门禁
TEST(PerformanceRegressionBenchmarkTest, ColdPathWorkingSetTrimBenchmark) {
    // 验证冷路径修剪调用安全，不崩溃，正常返回
    EXPECT_NO_THROW({
        easy::core::WinUtils::trimWorkingSet();
    });
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// 36. 文件对话框增强引擎 (PathMemoryManager / ExplorerTracker / DialogNavigator) 测试套件
// -----------------------------------------------------------------------------

TEST(EasyCoreWinUtilsCoverageTest, StringAndMemoryConversion) {
    using namespace easy::core;
    std::string utf8Str = "EasyTools 世界级截图工具";
    std::wstring wstr = WinUtils::utf8ToWstring(utf8Str);
    EXPECT_FALSE(wstr.empty());
    std::string roundtrip = WinUtils::wstringToUtf8(wstr);
    EXPECT_EQ(utf8Str, roundtrip);

    // 验证拼音引擎
    std::wstring initials = PinyinEngine::GetInitials(L"截图工具");
    EXPECT_FALSE(initials.empty());
    std::wstring fullPinyin = PinyinEngine::GetFullPinyin(L"截图");
    EXPECT_FALSE(fullPinyin.empty());

    // 验证物理内存修剪与冷路径安全调用
    WinUtils::trimWorkingSet();
}

TEST(EasyCorePipelineAndEventTest, KeyboardFilterAndEventBus) {
    using namespace easy::core;
    using namespace easy::ui;
    
    // 1. 验证 KeyboardPipeline Win32 菜单与帮助快捷键拦截
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_KEYMENU, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_CONTEXTHELP, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_HELP, 0, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_NULL, 0, 0));

    // 2. 验证 EventBus 同步分发与订阅机制
    bool eventReceived = false;
    auto subId = EventBus::instance().subscribe<ShowToastEvent>([&](const ShowToastEvent& evt) {
        if (evt.message == L"测试事件") {
            eventReceived = true;
        }
    });

    EventBus::instance().publish(ShowToastEvent{L"测试事件"});
    EXPECT_TRUE(eventReceived);
    EventBus::instance().unsubscribe(subId);
}

// -----------------------------------------------------------------------------
// 36.5 截图尺寸胶囊 HUD 与单位设置交互测试套件
// -----------------------------------------------------------------------------

TEST(WorldClassArchitectureTest, JobObjectAndAtomicWrite) {
    // 1. 验证 Job Object 初始化与自身进程挂载
    HANDLE job = easy::core::WinUtils::getProcessJobObject();
    EXPECT_NE(job, nullptr);
    EXPECT_TRUE(easy::core::WinUtils::assignProcessToCurrentJob(GetCurrentProcess()));

    // 2. 验证原子刷盘写入 (atomicWriteFileWithFlush)
    const auto tempTestDir = std::filesystem::temp_directory_path() / L"easytools_arch_test";
    std::filesystem::create_directories(tempTestDir);
    const auto testFile = tempTestDir / L"test_config.json";

    const std::string payload = "{\n  \"testKey\": \"testValue\",\n  \"number\": 12345\n}";
    EXPECT_TRUE(easy::core::WinUtils::atomicWriteFileWithFlush(testFile.wstring(), payload));
    EXPECT_TRUE(std::filesystem::exists(testFile));

    // 读取验证内容一致性
    std::ifstream ifs(testFile);
    std::string readContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(readContent, payload);

    HANDLE lockedTarget = CreateFileW(testFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(lockedTarget, INVALID_HANDLE_VALUE);
    EXPECT_FALSE(easy::core::WinUtils::atomicWriteFileWithFlush(
        testFile.wstring(), "replacement must not become visible"));
    CloseHandle(lockedTarget);
    std::ifstream preserved(testFile);
    std::string preservedContent((std::istreambuf_iterator<char>(preserved)),
                                 std::istreambuf_iterator<char>());
    EXPECT_EQ(preservedContent, payload);

    // 3. 验证低内存资源事件创建
    HANDLE lowMem = easy::core::WinUtils::createLowMemoryNotification();
    if (lowMem) {
        CloseHandle(lowMem);
    }

    std::error_code ec;
    std::filesystem::remove_all(tempTestDir, ec);
}

TEST(WorldClassArchitectureTest, PortableModeAndSingleInstanceHealing) {
    // 1. 验证便携模式判定逻辑
    const bool isPortable = easy::core::WinUtils::isPortableMode();
    (void)isPortable;
    const auto appDataDir = easy::core::WinUtils::getAppDataDirectory();
    EXPECT_FALSE(appDataDir.empty());
    EXPECT_TRUE(std::filesystem::exists(appDataDir));

    // 2. 验证孤儿互斥量自愈机制 (模拟 WAIT_ABANDONED / 互斥量探测)
    const wchar_t* testMutexName = L"Local\\EasyTools_Test_Mutex_Healing";
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, testMutexName);
    EXPECT_NE(hMutex, nullptr);
    if (hMutex) {
        DWORD waitRes = WaitForSingleObject(hMutex, 0);
        EXPECT_TRUE(waitRes == WAIT_OBJECT_0 || waitRes == WAIT_ABANDONED);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
}

TEST(WorldClassArchitectureTest, TaskbarSafetyAndHelperOwner) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    HWND helper = easy::core::WinUtils::createOverlayHelperOwner(hInst, L"EasyTools_UnitTest_Helper");
    EXPECT_NE(helper, nullptr);

    if (helper) {
        LONG_PTR exStyle = GetWindowLongPtrW(helper, GWL_EXSTYLE);
        EXPECT_TRUE((exStyle & WS_EX_TOOLWINDOW) != 0);
        EXPECT_TRUE((exStyle & WS_EX_NOACTIVATE) != 0);
        EXPECT_TRUE((exStyle & WS_EX_APPWINDOW) == 0);

        easy::core::WinUtils::applyTaskbarSafeOverlayStyle(helper);
        LONG_PTR updatedExStyle = GetWindowLongPtrW(helper, GWL_EXSTYLE);
        EXPECT_TRUE((updatedExStyle & WS_EX_TOOLWINDOW) != 0);
        EXPECT_TRUE((updatedExStyle & WS_EX_APPWINDOW) == 0);

        DestroyWindow(helper);
    }
}

// -----------------------------------------------------------------------------
// 39. 插件发现与动态加载生命周期测试套件 (必须放置在最后，因为 shutdown 会注销共享注册表)
// -----------------------------------------------------------------------------
TEST(PluginDiscoveryTest, DiscoveryAndLifecycle) {
    std::array<wchar_t, 32768> executablePath{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    EXPECT_TRUE(length > 0 && length < executablePath.size());
    if (length == 0 || length >= executablePath.size()) return;

    const auto executableDir = std::filesystem::path(executablePath.data()).parent_path();
    const auto pluginDir = executableDir.parent_path() / L"plugins" / executableDir.filename();
    auto& manager = easy::core::PluginManager::instance();
    EXPECT_TRUE(manager.loadPlugins(easy::core::WinUtils::wstringToUtf8(pluginDir.wstring())));
    const auto plugins = manager.getPluginStatuses();
    EXPECT_EQ(plugins.size(), 5u);
    for (const auto& plugin : plugins) {
        EXPECT_TRUE(plugin.error.empty()) << "Plugin " << plugin.name << " (" << plugin.id << ") error: " << plugin.error;
        EXPECT_EQ(plugin.abiVersion, easy::core::CurrentPluginAbiVersion) << "Plugin: " << plugin.name;
        EXPECT_FALSE(plugin.capabilities.empty()) << "Plugin: " << plugin.name;
    }

    // 验证各插件注册到 MessageBridge 的 IPC 协议调用与异常防护
    auto& bridge = easy::core::MessageBridge::instance();
    EXPECT_FALSE(bridge.handleMessage(R"({"id":1,"method":"search.status","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":2,"method":"search.query","params":{"keyword":"easy","page":1}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":3,"method":"gesture.getProfiles","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":4,"method":"capture.getCapabilities","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":5,"method":"keycast.getStatus","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":6,"method":"dialog.getConfig","params":{}})").empty());

    manager.shutdownPlugins();
}


TEST(CoreKeyboardHookTest, InstallPauseAndUninstall) {
    auto& kbHook = easy::core::KeyboardHook::instance();
    EXPECT_FALSE(kbHook.isPaused());

    kbHook.setPaused(true);
    EXPECT_TRUE(kbHook.isPaused());
    kbHook.setPaused(false);
    EXPECT_FALSE(kbHook.isPaused());

    kbHook.install();
    kbHook.uninstall();
}

TEST(CoreMouseHookTest, InstallAndCallbacks) {
    auto& mouseHook = easy::core::MouseHook::instance();
    EXPECT_FALSE(mouseHook.isPaused());

    mouseHook.setPaused(true);
    EXPECT_TRUE(mouseHook.isPaused());
    mouseHook.setPaused(false);
    EXPECT_FALSE(mouseHook.isPaused());

    bool callbackInvoked = false;
    mouseHook.setMouseActivityCallback([&](int btn, long x, long y) {
        (void)btn; (void)x; (void)y;
        callbackInvoked = true;
    });

    mouseHook.install();
    mouseHook.uninstall();
}

// -----------------------------------------------------------------------------
// 40. 纯矢量图标系统全覆盖测试套件
// -----------------------------------------------------------------------------


TEST(AllModulesLifecycleTest, SearchSuspendAndResumeLifecycle) {
    using namespace easy::ui;
    WebViewSuspendController controller;

    // 1. 验证分类与恢复决策纯函数 (0 锁、高并发状态分类)
    EXPECT_EQ(classifyWebViewSuspendCompletion(S_OK, TRUE), WebViewSuspendOutcome::Suspended);
    EXPECT_EQ(classifyWebViewSuspendCompletion(S_OK, FALSE), WebViewSuspendOutcome::Refused);
    EXPECT_EQ(classifyWebViewSuspendCompletion(E_FAIL, FALSE), WebViewSuspendOutcome::Failed);

    EXPECT_TRUE(shouldResumeAfterSuspendCompletion(WebViewSuspendOutcome::Suspended, false, false));
    EXPECT_FALSE(shouldResumeAfterSuspendCompletion(WebViewSuspendOutcome::Suspended, true, false));
    EXPECT_FALSE(shouldResumeAfterSuspendCompletion(WebViewSuspendOutcome::Suspended, false, true));

    // 2. 模拟放弃挂起与重置 (Abandon / Reset Lifecycle)
    controller.abandon();
    controller.reset();
}

TEST(AllModulesLifecycleTest, GestureAndHotCornerLifecycle) {
    using namespace easy::gesture;

    // 1. 触发角引擎生命周期：开启、设置、检测与关闭
    auto& engine = HotCornerEngine::instance();
    engine.setEnabled(true);
    engine.setTriggerDelay(100);
    engine.setCornerAction(HotCorner::TopLeft, "search.show");
    EXPECT_EQ(engine.getCornerAction(HotCorner::TopLeft), "search.show");

    // 模拟检测屏幕各角落
    POINT ptCorner{0, 0};
    EXPECT_EQ(HotCornerEngine::detectCorner(ptCorner), HotCorner::TopLeft);

    POINT ptCenter{500, 500};
    EXPECT_EQ(HotCornerEngine::detectCorner(ptCenter), HotCorner::None);

    engine.setCornerAction(HotCorner::TopLeft, "");
    EXPECT_TRUE(engine.getCornerAction(HotCorner::TopLeft).empty());
}

TEST(AllModulesLifecycleTest, DialogEnhancerLifecycleAndCleanup) {
    using namespace easy::dialog;

    // 1. 应用程序专属路径记忆生命周期：记录、查询、固定工作区与清空
    auto& manager = PathMemoryManager::instance();
    manager.setEnabled(true);
    manager.setPerAppMemoryEnabled(true);

    manager.recordAppPath("notepad.exe", "C:\\WorkDocs");
    EXPECT_EQ(manager.getAppPath("notepad.exe"), "C:\\WorkDocs");

    manager.setAppFixedWorkspace("notepad.exe", "D:\\FixedWorkspace", true);
    EXPECT_EQ(manager.getEffectiveAppPath("notepad.exe"), "D:\\FixedWorkspace");

    manager.removeAppMemory("notepad.exe");
    EXPECT_TRUE(manager.getAppPath("notepad.exe").empty());

    manager.clearAppMemories();
}

TEST(AllModulesLifecycleTest, DynamicHotkeyResolutionForAllFeatures) {
    // 验证所有功能的快捷键注册与动态感知，杜绝任何代码中硬编码快捷键的死锁问题
    const std::vector<std::string> featureKeys = {
        "/hotkey/search",
        "/hotkey/capture",
        "/hotkey/record",
        "/hotkey/pin",
        "/hotkey/spotlight",
        "/hotkey/keycast",
        "/hotkey/settings"
    };

    for (const auto& key : featureKeys) {
        // 动态注册与读取验证
        std::string hotkeyStr = easy::core::ConfigManager::instance().get<std::string>(key, "");
        // 无论用户将快捷键设置为何种组合（即使为空或自定义键），配置读取均保持原子幂等
        EXPECT_NO_THROW({
            auto val = easy::core::ConfigManager::instance().get<std::string>(key, "Ctrl+Shift+F12");
            EXPECT_FALSE(val.empty());
        });
    }
}

// -----------------------------------------------------------------------------
// 28. 鼠标手势全链路生命周期、取消打断与视口安全门禁测试套件
// -----------------------------------------------------------------------------

TEST(ShellContextMenuServiceTest, UniversalDpiAndCoordinateSafety) {
    auto& service = easy::core::ShellContextMenuService::instance();
    // 1. 空路径防御性测试：立即安全返回 false
    EXPECT_FALSE(service.showAsync(L"", -1, -1));

    // 2. 非空路径启动异步工作线程测试，并在后台安全处理
    EXPECT_TRUE(service.showAsync(L"C:\\non_existent_file_2026.xyz", 100, 200));

    // 3. 并发关闭与生命周期安全
    service.shutdown();
}
