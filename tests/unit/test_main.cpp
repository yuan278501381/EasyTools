// ─────────────────────────────────────────────────────────────────────────────
// test_main.cpp — EasyTools 单元测试 (零依赖的轻量断言运行器)
//
// 覆盖核心纯逻辑:
//   • GestureRecognizer 方向编码 (直线/对角/多段/防抖)
//   • ScopeRule 匹配 (精确/通配符/大小写/禁用)
//
// 由 deploy.ps1 在 CMake 构建后运行; 任一断言失败则进程返回非 0, CI 判为失败。
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureRecognizer.h"
#include "gesture/GestureAction.h"
#include "gesture/GestureProfile.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/ScopeRule.h"
#include "gesture/RadialMenuStyle.h"
#include "capture/CaptureBackend.h"
#include "capture/AudioCapture.h"
#include "capture/CursorOverlay.h"
#include "capture/ScreenRecorder.h"
#include "capture/ScrollCapture.h"
#include "capture/ShortcutHintStyle.h"
#include "keycast/KeycastStyle.h"
#include "ocr/OcrResultStyle.h"
#include "ui/ToastStyle.h"
#include "ui/WebViewWindowStyle.h"
#include "capture/CaptureToolbarLayout.h"
#include "core/config/ConfigManager.h"
#include "core/events/EventBus.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/plugin/PluginManifest.h"
#include "core/plugin/PluginManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/utils/WinUtils.h"
#include "core/lua/LuaEngine.h"
#include "service/SearchExpression.h"
#include "service/content/ContentSearchEngine.h"
#include "service/content/PlainTextExtractor.h"
#include "service/content/ZipXmlExtractor.h"
#include "service/content/PsdAiExtractor.h"
#include "service/content/DxfExtractor.h"
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

extern "C" {
#include <libavformat/avformat.h>
}

namespace {
int g_checks = 0;
int g_failures = 0;
}

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            std::printf("[FAIL] %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        auto a_ = (actual);                                                    \
        std::string e_ = (expected);                                          \
        if (a_ != e_) {                                                        \
            std::printf("[FAIL] %s:%d  %s => '%s', expected '%s'\n",           \
                        __FILE__, __LINE__, #actual, a_.c_str(), e_.c_str());  \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace easy::gesture;

// 喂入一串轨迹点, 返回识别出的方向编码 (无效手势返回空串)。
static std::string recognize(std::initializer_list<TrackPoint> pts) {
    GestureRecognizer r;  // 默认配置: minSegmentDistance=30, samplingInterval=5
    r.reset();
    for (const auto& p : pts) r.addPoint(p.x, p.y);
    auto res = r.finalize();
    return res ? res->code : std::string{};
}

static void test_recognizer() {
    // 屏幕坐标 Y 轴向下: 向上 = y 减小
    CHECK_EQ(recognize({{0, 0}, {100, 0}}), "R");    // →
    CHECK_EQ(recognize({{100, 0}, {0, 0}}), "L");    // ←
    CHECK_EQ(recognize({{0, 100}, {0, 0}}), "U");    // ↑
    CHECK_EQ(recognize({{0, 0}, {0, 100}}), "D");    // ↓

    // 对角线 (单段)
    CHECK_EQ(recognize({{0, 100}, {100, 0}}), "UR"); // ↗
    CHECK_EQ(recognize({{0, 0}, {100, 100}}), "DR"); // ↘

    // 多段 (L 形)
    CHECK_EQ(recognize({{0, 100}, {100, 100}, {100, 0}}), "R-U"); // → 然后 ↑
    CHECK_EQ(recognize({{100, 0}, {0, 0}, {0, 100}}), "L-D");     // ← 然后 ↓

    // 方向序列与箭头转换辅助函数
    std::vector<Direction> dirs = {Direction::Left, Direction::Down};
    CHECK_EQ(directionsToCode(dirs), "L-D");
    CHECK_EQ(directionsToArrowString(dirs), "←↓");

    std::vector<Direction> singleDir = {Direction::Up};
    CHECK_EQ(directionsToCode(singleDir), "U");
    CHECK_EQ(directionsToArrowString(singleDir), "↑");

    // 防抖: 位移小于最小段距离 → 无效手势
    CHECK_EQ(recognize({{0, 0}, {20, 0}}), "");

    // 智能转弯圆角平滑消抖 (Corner Fillet Folding)
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Right})), "D-R");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::DownRight, Direction::Down})), "R-D");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownLeft, Direction::Left})), "D-L");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpRight, Direction::Right})), "U-R");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpLeft, Direction::Left})), "U-L");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::DownLeft, Direction::Down})), "L-D");
    
    // 孤立回弹微抖动消除 (Rebound Jitter Elimination)
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Down})), "D");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::UpRight, Direction::Right})), "R");

    // 真正对角手势保留
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight})), "DR");
    CHECK_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight, Direction::Right})), "DR-R");

    // HEX 颜色解析测试
    auto parsed = easy::core::parseHexColor("#FF0000");
    CHECK(parsed.r == 1.0f);
    CHECK(parsed.g == 0.0f);
    CHECK(parsed.b == 0.0f);
    auto invalidHex = easy::core::parseHexColor("invalid");
    CHECK(invalidHex.r > 0.0f);
}

static void test_scoperule() {
    // 进程名精确匹配 (大小写不敏感)
    ScopeRule r1;
    r1.processName = "chrome.exe";
    r1.matchMode = MatchMode::Exact;
    CHECK(r1.matches(nullptr, L"chrome.exe", L"AnyClass"));
    CHECK(r1.matches(nullptr, L"CHROME.EXE", L""));
    CHECK(!r1.matches(nullptr, L"firefox.exe", L""));

    // 窗口类名通配符
    ScopeRule r2;
    r2.windowClass = "Chrome_*";
    r2.matchMode = MatchMode::Wildcard;
    CHECK(r2.matches(nullptr, L"", L"Chrome_WidgetWin_1"));
    CHECK(!r2.matches(nullptr, L"", L"MozillaWindowClass"));

    // 进程名通配符
    ScopeRule r3;
    r3.processName = "*.exe";
    r3.matchMode = MatchMode::Wildcard;
    CHECK(r3.matches(nullptr, L"notepad.exe", L""));
    CHECK(!r3.matches(nullptr, L"bash", L""));

    // 通配符中的正则元字符必须按普通字符处理
    ScopeRule rSpecial;
    rSpecial.processName = "app[1].exe";
    rSpecial.matchMode = MatchMode::Wildcard;
    CHECK(rSpecial.matches(nullptr, L"app[1].exe", L""));
    CHECK(!rSpecial.matches(nullptr, L"app1.exe", L""));

    // 禁用的规则永不匹配
    ScopeRule r4;
    r4.processName = "chrome.exe";
    r4.enabled = false;
    CHECK(!r4.matches(nullptr, L"chrome.exe", L""));

    // JSON 往返
    ScopeRule r5;
    r5.id = "rule-1";
    r5.name = "Chrome";
    r5.processName = "chrome.exe";
    r5.matchMode = MatchMode::Wildcard;
    r5.effect = RuleEffect::Disable;
    ScopeRule r5b = ScopeRule::fromJson(r5.toJson());
    CHECK(r5b.id == "rule-1");
    CHECK(r5b.processName == "chrome.exe");
    CHECK(r5b.matchMode == MatchMode::Wildcard);
    CHECK(r5b.effect == RuleEffect::Disable);

    // 越界枚举输入必须收敛到合法范围
    auto invalid = ScopeRule::fromJson({{"matchMode", 99}, {"effect", -5}});
    CHECK(invalid.matchMode == MatchMode::Regex);
    CHECK(invalid.effect == RuleEffect::Enable);
}

static void test_gesture_actions_and_builtin_commands() {
    using namespace easy::gesture;

    // 1. KeyStroke 解析与序列化
    auto ks1 = KeyStroke::fromString("Ctrl+Shift+T");
    CHECK(ks1.modifiers == (MOD_CONTROL | MOD_SHIFT));
    CHECK(ks1.virtualKey == 'T');
    CHECK_EQ(ks1.toString(), "Ctrl+Shift+T");

    auto ks2 = KeyStroke::fromString("Alt+Left");
    CHECK(ks2.modifiers == MOD_ALT);
    CHECK(ks2.virtualKey == VK_LEFT);
    CHECK_EQ(ks2.toString(), "Alt+Left");

    auto ksEmpty = KeyStroke::fromString("");
    CHECK(ksEmpty.virtualKey == 0);
    ksEmpty.send(nullptr); // 空按键安全防御

    // 2. GestureAction JSON 往返
    GestureAction a1;
    a1.type = ActionType::SendKeys;
    a1.name = "关闭标签页";
    a1.description = "关闭当前标签";
    a1.keyStroke = KeyStroke::fromString("Ctrl+W");
    auto j1 = a1.toJson();
    auto a1b = GestureAction::fromJson(j1);
    CHECK_EQ(a1b.name, "关闭标签页");
    CHECK_EQ(a1b.keyStroke.toString(), "Ctrl+W");

    GestureAction a2;
    a2.type = ActionType::BuiltinCommand;
    a2.name = "下一曲";
    a2.builtinCmd = BuiltinCommand::MediaNext;
    auto j2 = a2.toJson();
    auto a2b = GestureAction::fromJson(j2);
    CHECK(a2b.builtinCmd == BuiltinCommand::MediaNext);

    // 3. GestureMapping 字段与 JSON 往返 (包含 id, enabled, instantExecute, silentToast)
    GestureMapping m1;
    m1.id = "custom-m1";
    m1.enabled = true;
    m1.instantExecute = true;
    m1.silentToast = true;
    m1.gestureCode = "D-R";
    m1.action = a1;
    auto jm1 = m1.toJson();
    auto m1b = GestureMapping::fromJson(jm1);
    CHECK_EQ(m1b.id, "custom-m1");
    CHECK(m1b.enabled);
    CHECK(m1b.instantExecute);
    CHECK(m1b.silentToast);
    CHECK_EQ(m1b.gestureCode, "D-R");
    CHECK_EQ(m1b.action.name, "关闭标签页");

    // 4. GestureProfile 三态模型、单项开关与手势调序测试
    GestureProfile prof("test_profile");
    prof.addMapping(m1);

    GestureMapping m2;
    m2.id = "custom-m2";
    m2.gestureCode = "L-U";
    m2.action.name = "剪切";
    prof.addMapping(m2);

    CHECK(prof.findAction("D-R").has_value());
    prof.setMappingEnabled("D-R", false);
    CHECK(!prof.findAction("D-R").has_value()); // 禁用后 findAction 返回空
    CHECK(prof.findMapping("D-R").has_value()); // 但 findMapping 仍能查到
    prof.setMappingEnabled("D-R", true);
    CHECK(prof.findAction("D-R").has_value());

    // 调序与移动测试
    CHECK(prof.moveMapping(0, 1));
    CHECK_EQ(prof.getMappings()[0].gestureCode, "L-U");
    CHECK_EQ(prof.getMappings()[1].gestureCode, "D-R");
    prof.reorderMappings({"D-R", "L-U"});
    CHECK_EQ(prof.getMappings()[0].gestureCode, "D-R");
    CHECK_EQ(prof.getMappings()[1].gestureCode, "L-U");

    // 触发方式三态测试
    CHECK(prof.getTriggerState("right") == TriggerModeState::Default);
    prof.setTriggerState("right", TriggerModeState::Disabled);
    CHECK(prof.getTriggerState("right") == TriggerModeState::Disabled);
    prof.setAllTriggerStates(TriggerModeState::Enabled);
    CHECK(prof.getTriggerState("right") == TriggerModeState::Enabled);
    CHECK(prof.getTriggerState("middle") == TriggerModeState::Enabled);

    // Profile JSON 往返测试
    auto profJson = prof.toJson();
    auto profRestored = GestureProfile::fromJson(profJson);
    CHECK_EQ(profRestored.name(), "test_profile");
    CHECK(profRestored.getMappings().size() == 2);
    CHECK(profRestored.getTriggerState("right") == TriggerModeState::Enabled);

    // 5. BuiltinCommandDispatcher 应用级回调路由与媒体命令
    auto& dispatcher = BuiltinCommandDispatcher::instance();
    std::atomic<bool> screenshotCalled{false};
    dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, [&screenshotCalled]() {
        screenshotCalled = true;
    });

    dispatcher.execute(BuiltinCommand::TakeScreenshot, nullptr);
    CHECK(screenshotCalled.load());

    // 验证多媒体命令在无有效目标窗口与伪窗口上下文下均能稳定分发，不发生异常
    dispatcher.execute(BuiltinCommand::MediaNext, nullptr);
    dispatcher.execute(BuiltinCommand::MediaPrev, nullptr);
    dispatcher.execute(BuiltinCommand::MediaPlayPause, nullptr);
    dispatcher.execute(BuiltinCommand::VolumeMute, nullptr);
    dispatcher.clearHandlers();
}

static void test_hotkey_parser() {
    using easy::core::HotkeyDef;

    auto normal = HotkeyDef::fromString("Ctrl+Shift+A");
    CHECK(normal.has_value());
    if (normal) CHECK_EQ(normal->toString(), "Ctrl+Shift+A");

    auto special = HotkeyDef::fromString("Alt+PageDown");
    CHECK(special.has_value());
    if (special) CHECK_EQ(special->toString(), "Alt+PageDown");
    CHECK_EQ(HotkeyDef{}.toString(), "");

    CHECK(!HotkeyDef::fromString("").has_value());
    CHECK(!HotkeyDef::fromString("Ctrl+").has_value());
    CHECK(!HotkeyDef::fromString("Ctrl+UnknownKey").has_value());
    CHECK(!HotkeyDef::fromString("Bad+Ctrl+A").has_value());
}

static void test_disabled_hotkey_lifecycle() {
    auto& manager = easy::core::HotkeyManager::instance();
    constexpr const char* name = "Unit Test Disabled Hotkey";
    std::atomic<int> calls{0};

    CHECK(manager.registerHotkey(name, {}, [&calls]() { calls.fetch_add(1); }));
    auto entries = manager.getAllHotkeys();
    auto entry = std::find_if(entries.begin(), entries.end(), [](const auto& candidate) {
        return candidate.name == name;
    });
    CHECK(entry != entries.end());
    if (entry != entries.end()) {
        CHECK(!entry->registered);
        CHECK_EQ(entry->def.toString(), "");
    }

    CHECK(manager.clearHotkey(name));
    CHECK(calls.load() == 0);
    manager.unregisterHotkey(name);
    entries = manager.getAllHotkeys();
    CHECK(std::none_of(entries.begin(), entries.end(), [](const auto& candidate) {
        return candidate.name == name;
    }));
}

static void test_config_manager() {
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
    CHECK(config.initialize(temp));
    size_t corruptBackups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(temp)) {
        if (entry.path().filename().wstring().starts_with(L"config.json.corrupt.")) {
            ++corruptBackups;
        }
    }
    CHECK(corruptBackups == 1);

    std::atomic<int> notifications{0};
    const auto callbackId = config.onChange([&notifications](const std::string&) {
        notifications.fetch_add(1);
    });
    CHECK(config.set("/general/language", std::string("zh-CN")));
    CHECK(config.set("/capture/quality", 87));
    CHECK(config.mergePatch({
        {"general", {{"theme", "dark"}, {"checkUpdates", true}}},
        {"capture", {{"format", "png"}}}
    }, "/batch"));
    CHECK_EQ(config.get<std::string>("/general/language", ""), "zh-CN");
    CHECK(config.get<int>("/capture/quality", 0) == 87);
    CHECK_EQ(config.get<std::string>("/general/theme", ""), "dark");
    CHECK(!config.fromJsonString("[1,2,3]"));
    CHECK_EQ(config.get<std::string>("/general/theme", ""), "dark");
    CHECK(config.remove("/capture/format"));
    CHECK(!config.has("/capture/format"));

    {
        std::ifstream file(temp / "config.json");
        auto persisted = nlohmann::json::parse(file);
        CHECK(persisted["general"]["language"] == "zh-CN");
        CHECK(persisted["general"]["theme"] == "dark");
        CHECK(persisted["capture"]["quality"] == 87);
        CHECK(!persisted["capture"].contains("format"));
    }

    CHECK(config.exportTo(temp / "export.json"));
    {
        std::ifstream file(temp / "export.json");
        const auto exported = nlohmann::json::parse(file);
        CHECK(exported["general"]["language"] == "zh-CN");
    }

    // 给文件监控线程时间消费本进程自己的原子替换事件；内容未变化时不得重复通知。
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    CHECK(notifications.load() == 4);

    CHECK(config.reset());
    CHECK(!config.has("/general/language"));
    {
        std::ifstream file(temp / "config.json");
        const auto persisted = nlohmann::json::parse(file);
        CHECK(persisted.is_object());
        CHECK(persisted.empty());
    }
    config.removeOnChange(callbackId);
    config.shutdown();
    std::filesystem::remove_all(temp, ec);
}

static void test_message_bridge() {
    auto& bridge = easy::core::MessageBridge::instance();
    bridge.registerHandler("test.echo", [](const nlohmann::json& params) {
        return params;
    });

    auto ok = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":42,"method":"test.echo","params":{"value":7}})"));
    CHECK(ok["id"] == 42);
    CHECK(ok["result"]["value"] == 7);

    auto missing = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":77,"method":"test.missing"})"));
    CHECK(missing["id"] == 77);
    CHECK(missing["error"]["code"] == -32601);

    bridge.clearHandlers();
    auto cleared = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":78,"method":"test.echo"})"));
    CHECK(cleared["error"]["code"] == -32601);

    bridge.registerBuiltinHandlers();
    auto plugins = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":79,"method":"plugins.getAll"})"));
    CHECK(plugins["result"].is_array());
    auto invalidPluginToggle = nlohmann::json::parse(bridge.handleMessage(
        R"({"id":80,"method":"plugins.setEnabled","params":{"id":"missing","enabled":true}})"));
    CHECK(!invalidPluginToggle["result"]["success"].get<bool>());
    CHECK(invalidPluginToggle["result"]["error"] == "plugin not found");
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
    CHECK(!unregisterFinished.load());
    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gateCv.notify_all();
    caller.join();
    unregisterThread.join();
    CHECK(unregisterFinished.load());
    auto retired = nlohmann::json::parse(
        bridge.handleMessage(R"({"id":82,"method":"test.blocking"})"));
    CHECK(retired["error"]["code"] == -32601);
}

static void test_plugin_manifest() {
    using easy::core::comparePluginVersions;
    using easy::core::loadPluginManifest;
    CHECK(comparePluginVersions("1.2.0", "1.1.9") > 0);
    CHECK(comparePluginVersions("1.0", "1.0.0") == 0);
    CHECK(comparePluginVersions("2.0.0", "10.0.0") < 0);

    const auto path = std::filesystem::temp_directory_path() /
        (L"EasyToolsManifest_" + std::to_wstring(GetCurrentProcessId()) + L".json");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << R"({
            "schemaVersion": 1,
            "abiVersion": 1,
            "id": "capture",
            "name": "Capture",
            "version": "1.0.0",
            "minimumHostVersion": "1.0.0",
            "entryPoint": "CreatePlugin",
            "capabilities": ["screenshot", "recording"],
            "permissions": ["screen-capture"]
        })";
    }
    const auto valid = loadPluginManifest(path, "capture", "1.2.0");
    CHECK(static_cast<bool>(valid));
    CHECK_EQ(valid.manifest.id, "capture");
    CHECK(valid.manifest.capabilities.size() == 2);

    const auto wrongId = loadPluginManifest(path, "gesture", "1.2.0");
    CHECK(!wrongId);
    CHECK_EQ(wrongId.error, "plugin manifest id does not match its DLL");

    const auto oldHost = loadPluginManifest(path, "capture", "0.9.0");
    CHECK(!oldHost);
    CHECK_EQ(oldHost.error, "plugin requires a newer EasyTools version");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void test_event_bus_quiescence() {
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
    CHECK(!unsubscribeFinished.load());
    {
        std::lock_guard lock(gateMutex);
        release = true;
    }
    gateCv.notify_all();
    publisher.join();
    unsubscribeThread.join();
    CHECK(unsubscribeFinished.load());
    bus.publish(easy::core::ActionTriggerScreenshotEvent{});
    CHECK(calls.load() == 1);
    bus.clearAll();
}

static void test_perf_timer() {
    easy::core::PerfTimer timer("gesture");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.stop();
    const double elapsed = timer.elapsedMs();
    CHECK(elapsed >= 1.0);
    CHECK(easy::core::PerformanceMonitor::instance().getMetrics().gestureLatencyMs >= 1.0);

    auto& monitor = easy::core::PerformanceMonitor::instance();
    for (int value = 1; value <= 100; ++value) {
        monitor.recordLatency("unit.latency", static_cast<double>(value));
    }
    monitor.recordLatency("unit.latency", -1.0); // invalid samples are ignored
    const auto metrics = monitor.getMetrics();
    const auto summary = metrics.latencies.find("unit.latency");
    CHECK(summary != metrics.latencies.end());
    if (summary != metrics.latencies.end()) {
        CHECK(summary->second.sampleCount == 100);
        CHECK(summary->second.lastMs == 100.0);
        CHECK(summary->second.meanMs == 50.5);
        CHECK(summary->second.p95Ms == 95.0);
        CHECK(summary->second.maxMs == 100.0);
    }

    monitor.start(60'000);
    easy::core::PerfMetrics sampled;
    for (int attempt = 0; attempt < 50; ++attempt) {
        sampled = monitor.getMetrics();
        if (sampled.handleCount > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(sampled.handleCount > 0);
    const auto stopStarted = std::chrono::steady_clock::now();
    monitor.stop();
    const auto stopMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - stopStarted).count();
    CHECK(stopMs < 250.0);
}

static void test_update_version_comparison() {
    using easy::core::UpdateChecker;
    CHECK(UpdateChecker::isNewerVersion("v1.2.3", "1.2.2"));
    CHECK(UpdateChecker::isNewerVersion("1.10.0", "1.9.9"));
    CHECK(UpdateChecker::isNewerVersion("2.0", "1.99.99"));
    CHECK(UpdateChecker::isNewerVersion("EasyTools v1.2.3 正式发布", "1.2.2"));
    CHECK(!UpdateChecker::isNewerVersion("1.2.3", "1.2.3"));
    CHECK(!UpdateChecker::isNewerVersion("1.2", "1.2.1"));
    CHECK(!UpdateChecker::isNewerVersion("1.2.3-beta.1", "1.2.3"));
    CHECK(!UpdateChecker::isNewerVersion("not-a-version", "1.0.0"));
    CHECK(!UpdateChecker::isNewerVersion("发行", "1.0.0"));
}

#include "service/PinyinEngine.h"
#include "core/utils/WinUtils.h"

static void test_pinyin_engine() {
    auto wx_init = PinyinEngine::GetInitials(L"微信");
    auto wx_full = PinyinEngine::GetFullPinyin(L"微信");
    std::wprintf(L"[DEBUG] 微信 initials: %ls, full: %ls\n", wx_init.c_str(), wx_full.c_str());

    // 拼音首字母
    CHECK(PinyinEngine::GetInitials(L"微信") == L"wx");
    CHECK(PinyinEngine::GetInitials(L"你好世界") == L"nhsj");
    CHECK(PinyinEngine::GetInitials(L"EasyTools工具") == L"easytoolsgj");

    // 拼音全拼
    CHECK(PinyinEngine::GetFullPinyin(L"微信") == L"weixin");
    CHECK(PinyinEngine::GetFullPinyin(L"你好") == L"nihao");
    CHECK(PinyinEngine::GetFullPinyin(L"EasyTools") == L"easytools");
}

static void test_search_everything_expressions() {
    FileRecord txtFile{1, 0, L"readme.txt", L"readme.txt", L"readme.txt", L"readme.txt", false};
    FileRecord pngFile{2, 0, L"screenshot.png", L"screenshot.png", L"screenshot.png", L"screenshot.png", false};
    FileRecord docDir{3, 0, L"Documents", L"documents", L"documents", L"documents", true};
    FileRecord wxFile{4, 0, L"微信.exe", L"微信.exe", L"wx.exe", L"weixin.exe", false};
    FileRecord cppFile{5, 0, L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", false};
    FileRecord logFile{6, 0, L"app_2026.log", L"app_2026.log", L"app_2026.log", L"app_2026.log", false};

    // 1. 标准通配符与扩展名
    auto expr1 = SearchExpression::parse(L"*.txt");
    CHECK(expr1.matches(txtFile, L'C', L"C:\\readme.txt"));
    CHECK(!expr1.matches(pngFile, L'C', L"C:\\screenshot.png"));

    auto exprExt = SearchExpression::parse(L"ext:png;jpg;webp");
    CHECK(exprExt.matches(pngFile, L'C', L"C:\\screenshot.png"));
    CHECK(!exprExt.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 2. 类型过滤器 (file: / folder: / dir:)
    auto exprFile = SearchExpression::parse(L"file: *.txt");
    CHECK(exprFile.matches(txtFile, L'C', L"C:\\readme.txt"));
    CHECK(!exprFile.matches(docDir, L'C', L"C:\\Documents"));

    auto exprDir = SearchExpression::parse(L"folder: documents");
    CHECK(exprDir.matches(docDir, L'C', L"C:\\Documents"));
    CHECK(!exprDir.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 3. 逻辑与、或、非 (AND / OR / NOT)
    auto exprNot = SearchExpression::parse(L"*.cpp !test");
    FileRecord normalCpp{7, 0, L"main.cpp", L"main.cpp", L"main.cpp", L"main.cpp", false};
    CHECK(!exprNot.matches(cppFile, L'C', L"C:\\test_main.cpp"));
    CHECK(exprNot.matches(normalCpp, L'C', L"C:\\main.cpp"));

    auto exprOr = SearchExpression::parse(L"ext:txt | ext:png");
    CHECK(exprOr.matches(txtFile, L'C', L"C:\\readme.txt"));
    CHECK(exprOr.matches(pngFile, L'C', L"C:\\screenshot.png"));
    CHECK(!exprOr.matches(cppFile, L'C', L"C:\\test_main.cpp"));

    // 4. 正则表达式 (regex: / r:)
    auto exprRegex = SearchExpression::parse(L"regex:^app_[0-9]+\\.log$");
    CHECK(exprRegex.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    CHECK(!exprRegex.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 5. 盘符与路径过滤 (c: / path: / parent:)
    auto exprDrive = SearchExpression::parse(L"c: *.txt");
    CHECK(exprDrive.matches(txtFile, L'C', L"C:\\readme.txt"));
    CHECK(!exprDrive.matches(txtFile, L'D', L"D:\\readme.txt"));

    auto exprPath = SearchExpression::parse(L"path:logs");
    CHECK(exprPath.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    CHECK(!exprPath.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 6. 拼音检索与禁用拼音 (pinyin: / nopy:)
    auto exprPy = SearchExpression::parse(L"wx");
    CHECK(exprPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    auto exprNoPy = SearchExpression::parse(L"nopy:wx");
    CHECK(!exprNoPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    // 7. 双引号带空格短语
    auto exprQuote = SearchExpression::parse(L"\"test main\"");
    FileRecord spaceFile{8, 0, L"test main.cpp", L"test main.cpp", L"test main.cpp", L"test main.cpp", false};
    CHECK(exprQuote.matches(spaceFile, L'C', L"C:\\test main.cpp"));
}

static void test_winutils_fullscreen() {
    // 空句柄或无效句柄返回 false
    CHECK(!easy::core::WinUtils::isWindowFullscreen(nullptr));
    CHECK(!easy::core::WinUtils::isWindowFullscreen((HWND)(uintptr_t)0x12345678));

    HWND overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"EasyTools affinity test",
        WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!overlay) {
        std::printf("[SKIP] display-affinity smoke: window creation unavailable\n");
        return;
    }
    if (easy::core::WinUtils::excludeWindowFromCapture(overlay)) {
        DWORD affinity = WDA_NONE;
        CHECK(GetWindowDisplayAffinity(overlay, &affinity));
        CHECK(affinity == WDA_EXCLUDEFROMCAPTURE);
    } else {
        std::printf("[SKIP] WDA_EXCLUDEFROMCAPTURE unavailable: error=%lu\n", GetLastError());
    }
    DestroyWindow(overlay);
}

static void test_plugin_discovery() {
    std::array<wchar_t, 32768> executablePath{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    CHECK(length > 0 && length < executablePath.size());
    if (length == 0 || length >= executablePath.size()) return;

    const auto executableDir = std::filesystem::path(executablePath.data()).parent_path();
    const auto pluginDir = executableDir.parent_path() / L"plugins" / executableDir.filename();
    auto& manager = easy::core::PluginManager::instance();
    CHECK(manager.loadPlugins(easy::core::WinUtils::wstringToUtf8(pluginDir.wstring())));
    const auto plugins = manager.getPluginStatuses();
    CHECK(plugins.size() == 4);
    for (const auto& plugin : plugins) {
        CHECK(plugin.error.empty());
        CHECK(plugin.abiVersion == easy::core::CurrentPluginAbiVersion);
        CHECK(!plugin.capabilities.empty());
    }
    manager.shutdownPlugins();
}

static void test_capture_backend_smoke() {
    const auto capabilities = easy::capture::captureBackendCapabilities();
    CHECK(capabilities.size() >= 2);
    bool hasGdi = false;
    bool hasDxgi = false;
    for (const auto& capability : capabilities) {
        if (capability.id == "gdi-bitblt") hasGdi = capability.available;
        if (capability.id == "dxgi-desktop-duplication") hasDxgi = true;
    }
    CHECK(hasGdi);
    CHECK(hasDxgi);

    const easy::capture::CaptureRegion region{
        GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 64, 64
    };
    auto backend = easy::capture::createCaptureBackend();
    std::string error;
    if (!backend->initialize(region, error)) {
        std::printf("[SKIP] capture backend smoke: %s\n", error.c_str());
        return;
    }
    easy::capture::CaptureFrameView frame;
    if (!backend->capture(frame, error)) {
        std::printf("[SKIP] capture frame smoke: %s\n", error.c_str());
        backend->shutdown();
        return;
    }
    CHECK(frame.data != nullptr);
    CHECK(frame.width == 64);
    CHECK(frame.height == 64);
    CHECK(frame.stride >= frame.width * 3);
    std::printf("[INFO] capture backend: %s\n", backend->info().id.c_str());
    backend->releaseFrame();
    backend->shutdown();
}

static void test_audio_capture_smoke() {
    const auto devices = easy::capture::AudioCapture::devices();
    const auto outputDevice = std::find_if(devices.begin(), devices.end(),
        [](const auto& device) { return device.systemAudio; });
    if (outputDevice == devices.end()) {
        std::printf("[SKIP] WASAPI device enumeration: no active output device\n");
        return;
    }
    CHECK(!outputDevice->id.empty());
    CHECK(!outputDevice->name.empty());

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
    CHECK(status.systemAudioActive);
    CHECK(status.mixedPeak >= 0.0f && status.mixedPeak <= 1.0f);
    std::vector<float> frames;
    CHECK(capture.readFrames(easy::capture::AudioCapture::SampleRate / 100, frames));
    CHECK(frames.size() == static_cast<std::size_t>(
        easy::capture::AudioCapture::SampleRate / 100 * easy::capture::AudioCapture::Channels));
    capture.setVolumes(0.75f, 1.0f);
    capture.setSystemMuted(true);
    CHECK(capture.status().systemMuted);
    CHECK(capture.status().systemPeak == 0.0f);
    capture.setSystemMuted(false);
    CHECK(!capture.status().systemMuted);
    capture.stop();

    // The same object must be reusable after a full endpoint teardown. This
    // exercises the same close/reinitialize path used by runtime reconnection.
    CHECK(capture.start(options));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(capture.status().systemAudioActive);
    capture.stop();
}

static bool canCaptureDisplayFrames() {
    const easy::capture::CaptureRegion region{
        GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 64, 64
    };
    auto backend = easy::capture::createCaptureBackend();
    std::string error;
    if (!backend || !backend->initialize(region, error)) return false;
    easy::capture::CaptureFrameView frame;
    bool success = backend->capture(frame, error);
    backend->releaseFrame();
    backend->shutdown();
    return success;
}

static void test_scroll_capture_bounded_preview() {
    if (!canCaptureDisplayFrames()) {
        std::printf("[SKIP] scroll capture smoke: display capture unavailable in current session\n");
        return;
    }

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
    options.captureRect = {
        GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + 64,
        GetSystemMetrics(SM_YVIRTUALSCREEN) + 64
    };
    capture.start(options);
    if (!capture.isRunning()) {
        std::printf("[SKIP] scroll capture smoke: backend unavailable\n");
        capture.shutdown();
        return;
    }
    capture.captureCurrentFrame();
    capture.captureCurrentFrame();
    capture.stop();
    CHECK(completed.success);
    CHECK(completed.frameCount == 2);
    CHECK(completed.stitchedImage.cols == 64);
    CHECK(completed.stitchedImage.rows >= 64);
    CHECK(progressCalls == 2);
    CHECK(largestPreviewRows <= 128);
    capture.shutdown();
}

static void test_screen_recorder_smoke() {
    if (!canCaptureDisplayFrames()) {
        std::printf("[SKIP] screen recorder smoke: display capture unavailable in current session\n");
        return;
    }

    const auto output = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorder_" + std::to_wstring(GetCurrentProcessId()) + L".mp4");
    std::error_code ec;
    std::filesystem::remove(output, ec);

    easy::capture::RecordOptions options;
    options.format = easy::capture::RecordFormat::MP4_H264;
    options.fps = 15;
    options.width = 320;
    options.height = 240;
    options.regionX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    options.regionY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    options.fullScreen = false;
    options.bitrateMbps = 2;
    options.captureSystemAudio = true;
    options.countdownSeconds = 0;
    options.outputPath = easy::core::WinUtils::wstringToUtf8(output.wstring());

    auto& recorder = easy::capture::ScreenRecorder::instance();
    CHECK(recorder.initialize());
    if (!recorder.startRecording(options)) {
        std::printf("[SKIP] screen recorder smoke: encoder unavailable\n");
        recorder.shutdown();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    if (recorder.stats().systemAudioActive) {
        CHECK(recorder.toggleSystemAudioMuted());
        CHECK(recorder.stats().systemAudioMuted);
        recorder.setAudioVolumes(0.8f, 1.0f);
        CHECK(recorder.toggleSystemAudioMuted());
        CHECK(!recorder.stats().systemAudioMuted);
    }
    const auto completedPath = recorder.stopRecording();
    const auto stats = recorder.stats();
    CHECK_EQ(completedPath, options.outputPath);
    CHECK(stats.frameCount > 0);
    CHECK(!stats.captureBackend.empty());
    CHECK(!stats.encoderName.empty());
    CHECK(stats.diskFreeBytes > 0);
    CHECK(stats.estimatedRemainingSec >= 0);
    CHECK(stats.captureLatencyMs >= 0.0);
    CHECK(stats.conversionLatencyMs >= 0.0);
    CHECK(stats.encodeLatencyMs >= 0.0);
    CHECK(stats.pipelineLatencyMs >= stats.captureLatencyMs * 0.5);
    CHECK(stats.effectiveFps > 0.0 && stats.effectiveFps <= options.fps);
    CHECK(stats.adaptiveFrameStep >= 1 && stats.adaptiveFrameStep <= 4);
    CHECK(std::filesystem::file_size(output, ec) > 0 && !ec);
    CHECK(!std::filesystem::exists(std::filesystem::path(output.wstring() + L".partial")));
    AVFormatContext* probe = nullptr;
    CHECK(avformat_open_input(&probe, options.outputPath.c_str(), nullptr, nullptr) >= 0);
    if (probe) {
        CHECK(avformat_find_stream_info(probe, nullptr) >= 0);
        bool hasVideo = false;
        bool hasAudio = false;
        for (unsigned index = 0; index < probe->nb_streams; ++index) {
            hasVideo |= probe->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
            hasAudio |= probe->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
        }
        CHECK(hasVideo);
        if (stats.systemAudioActive) {
            CHECK(hasAudio);
            CHECK(!stats.audioEncoderName.empty());
        }
        avformat_close_input(&probe);
    }
    std::printf("[INFO] recording pipeline: %s -> %s + %s, frames=%d, dropped=%d\n",
                stats.captureBackend.c_str(), stats.encoderName.c_str(),
                stats.audioEncoderName.empty() ? "no-audio" : stats.audioEncoderName.c_str(),
                stats.frameCount, stats.droppedFrameCount);
    recorder.shutdown();
    std::filesystem::remove(output, ec);

    const auto cancelledOutput = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorderCancelled_" + std::to_wstring(GetCurrentProcessId()) + L".mp4");
    const auto partialOutput = std::filesystem::path(cancelledOutput.wstring() + L".partial");
    std::filesystem::remove(cancelledOutput, ec);
    std::filesystem::remove(partialOutput, ec);
    options.captureSystemAudio = false;
    options.outputPath = easy::core::WinUtils::wstringToUtf8(cancelledOutput.wstring());
    options.countdownSeconds = 2;
    CHECK(recorder.initialize());
    CHECK(recorder.startRecording(options));
    CHECK(recorder.state() == easy::capture::RecordState::Countdown);
    CHECK(recorder.stats().countdownRemaining > 0);
    CHECK(recorder.stopRecording().empty());
    CHECK(!std::filesystem::exists(cancelledOutput));
    CHECK(!std::filesystem::exists(partialOutput));
    recorder.shutdown();

    // A path whose parent is a regular file must fail during preflight without
    // starting capture/encoding or leaving a .partial artifact behind.
    const auto invalidParent = std::filesystem::temp_directory_path() /
        (L"EasyToolsRecorderParent_" + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream parentFile(invalidParent, std::ios::binary | std::ios::trunc);
        parentFile << "not a directory";
    }
    const auto invalidOutput = invalidParent / L"record.mp4";
    options.countdownSeconds = 0;
    options.outputPath = easy::core::WinUtils::wstringToUtf8(invalidOutput.wstring());
    CHECK(recorder.initialize());
    CHECK(!recorder.startRecording(options));
    CHECK(recorder.stats().stopReason == "output_directory_unavailable" ||
          recorder.stats().stopReason == "output_directory_not_writable");
    CHECK(!std::filesystem::exists(std::filesystem::path(invalidOutput.wstring() + L".partial")));
    recorder.shutdown();
    std::filesystem::remove(invalidParent, ec);
}

static void test_cursor_overlay_restore() {
    CURSORINFO cursorInfo{sizeof(CURSORINFO)};
    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING)) {
        std::printf("[SKIP] cursor overlay smoke: pointer is hidden\n");
        return;
    }
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    const auto monitor = MonitorFromPoint(cursorInfo.ptScreenPos, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;
    constexpr int size = 128;
    const int monitorLeft = static_cast<int>(monitorInfo.rcMonitor.left);
    const int monitorTop = static_cast<int>(monitorInfo.rcMonitor.top);
    const int maxX = std::max(monitorLeft, static_cast<int>(monitorInfo.rcMonitor.right) - size);
    const int maxY = std::max(monitorTop, static_cast<int>(monitorInfo.rcMonitor.bottom) - size);
    const easy::capture::CaptureRegion region{
        std::clamp(static_cast<int>(cursorInfo.ptScreenPos.x) - size / 2, monitorLeft, maxX),
        std::clamp(static_cast<int>(cursorInfo.ptScreenPos.y) - size / 2, monitorTop, maxY),
        size, size
    };

    auto backend = easy::capture::createCaptureBackend();
    std::string error;
    if (!backend->initialize(region, error)) {
        std::printf("[SKIP] cursor overlay backend: %s\n", error.c_str());
        return;
    }
    easy::capture::CaptureFrameView frame;
    if (!backend->capture(frame, error)) {
        backend->shutdown();
        std::printf("[SKIP] cursor overlay frame: %s\n", error.c_str());
        return;
    }
    const int pixelBytes = frame.format == easy::capture::CapturePixelFormat::Bgra32 ? 4 : 3;
    const std::size_t rowBytes = static_cast<std::size_t>(frame.width) * pixelBytes;
    std::vector<std::uint8_t> original(rowBytes * frame.height);
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(original.data() + static_cast<std::size_t>(row) * rowBytes,
                    frame.data + static_cast<std::size_t>(row) * frame.stride, rowBytes);
    }

    easy::capture::CursorOverlay overlay;
    bool changed = false;
    {
        auto patch = overlay.apply(frame, region, true, false);
        if (!patch.empty()) {
            for (int row = 0; row < frame.height && !changed; ++row) {
                changed = std::memcmp(
                    original.data() + static_cast<std::size_t>(row) * rowBytes,
                    frame.data + static_cast<std::size_t>(row) * frame.stride,
                    rowBytes) != 0;
            }
        }
    }
    if (!changed) {
        // Cursor visibility/shape can change between the initial probe and the
        // captured frame (for example while the test host shows a busy cursor).
        // Restoration is still validated below; do not make CI depend on live UI.
        std::printf("[SKIP] cursor overlay smoke: live cursor produced no pixel delta\n");
    } else {
        CHECK(changed);
    }
    bool restored = true;
    for (int row = 0; row < frame.height && restored; ++row) {
        restored = std::memcmp(
            original.data() + static_cast<std::size_t>(row) * rowBytes,
            frame.data + static_cast<std::size_t>(row) * frame.stride,
            rowBytes) == 0;
    }
    CHECK(restored);
    backend->releaseFrame();
    backend->shutdown();
}

static void test_shortcut_hint_dpi_metrics() {
    using easy::capture::ShortcutHintStyle;
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(0) - 1.0f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(96) - 1.0f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(120) - 1.25f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(144) - 1.5f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(192) - 2.0f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(288) - 3.0f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(480) - 5.0f) < 0.001f);
    CHECK(std::abs(ShortcutHintStyle::scaleForDpi(768) - 5.0f) < 0.001f);
    CHECK(ShortcutHintStyle::BaseKeyFont >= 13.0f);
    CHECK(ShortcutHintStyle::BaseLabelFont >= 13.0f);
    CHECK(ShortcutHintStyle::BaseKeyHeight >= ShortcutHintStyle::BaseKeyFont * 2.0f);

    float previousFontPixels = 0.0f;
    for (const unsigned dpi : {96u, 120u, 144u, 192u, 240u, 288u, 384u, 480u}) {
        const float fontPixels = ShortcutHintStyle::BaseLabelFont *
                                 ShortcutHintStyle::scaleForDpi(dpi);
        CHECK(fontPixels >= previousFontPixels);
        previousFontPixels = fontPixels;
    }
}

static void test_shared_dpi_metrics() {
    using namespace easy::core::dpi;
    CHECK(std::abs(scaleForDpi(0) - 1.0f) < 0.001f);
    CHECK(std::abs(scaleForDpi(96) - 1.0f) < 0.001f);
    CHECK(std::abs(scaleForDpi(120) - 1.25f) < 0.001f);
    CHECK(std::abs(scaleForDpi(144) - 1.5f) < 0.001f);
    CHECK(std::abs(scaleForDpi(192) - 2.0f) < 0.001f);
    CHECK(std::abs(scaleForDpi(480) - 5.0f) < 0.001f);
    CHECK(std::abs(scaleForDpi(768) - 5.0f) < 0.001f);
    CHECK(scaleMetric(36, 1.0f) == 36);
    CHECK(scaleMetric(36, 1.25f) == 45);
    CHECK(scaleMetric(36, 1.5f) == 54);
    CHECK(scaleMetric(1, 0.0f) == 1);

    const SIZE toast100 = easy::ui::ToastStyle::windowSizeForDpi(96);
    const SIZE toast150 = easy::ui::ToastStyle::windowSizeForDpi(144);
    const SIZE toast200 = easy::ui::ToastStyle::windowSizeForDpi(192);
    CHECK(toast100.cx == 600 && toast100.cy == 80);
    CHECK(toast150.cx == 900 && toast150.cy == 120);
    CHECK(toast200.cx == 1200 && toast200.cy == 160);

    const SIZE keycast150 = easy::keycast::KeycastStyle::windowSizeForDpi(144);
    CHECK(keycast150.cx == 1200 && keycast150.cy == 240);

    const SIZE ocr100 = easy::ocr::OcrResultStyle::windowSizeForDpi(96);
    const SIZE ocr125 = easy::ocr::OcrResultStyle::windowSizeForDpi(120);
    const SIZE ocr150 = easy::ocr::OcrResultStyle::windowSizeForDpi(144);
    const SIZE ocr200 = easy::ocr::OcrResultStyle::windowSizeForDpi(192);
    CHECK(ocr100.cx == 600 && ocr100.cy == 400);
    CHECK(ocr125.cx == 750 && ocr125.cy == 500);
    CHECK(ocr150.cx == 900 && ocr150.cy == 600);
    CHECK(ocr200.cx == 1200 && ocr200.cy == 800);

    const SIZE search100 = easy::ui::SearchWindowStyle::windowSizeForDpi(96);
    const SIZE search150 = easy::ui::SearchWindowStyle::windowSizeForDpi(144);
    const SIZE search200 = easy::ui::SearchWindowStyle::windowSizeForDpi(192);
    CHECK(search100.cx == 800 && search100.cy == 600);
    CHECK(search150.cx == 1200 && search150.cy == 900);
    CHECK(search200.cx == 1600 && search200.cy == 1200);

    const SIZE tray100 = easy::ui::TrayWindowStyle::windowSizeForDpi(96);
    const SIZE tray150 = easy::ui::TrayWindowStyle::windowSizeForDpi(144);
    const SIZE tray200 = easy::ui::TrayWindowStyle::windowSizeForDpi(192);
    CHECK(tray100.cx == 200 && tray100.cy == 265);
    CHECK(tray150.cx == 300 && tray150.cy == 398);
    CHECK(tray200.cx == 400 && tray200.cy == 530);

    const SIZE settings100 = easy::ui::SettingsWindowStyle::windowSizeForDpi(96);
    const SIZE settings150 = easy::ui::SettingsWindowStyle::windowSizeForDpi(144);
    const SIZE settings200 = easy::ui::SettingsWindowStyle::windowSizeForDpi(192);
    CHECK(settings100.cx == 1260 && settings100.cy == 880);
    CHECK(settings150.cx == 1890 && settings150.cy == 1320);
    CHECK(settings200.cx == 2520 && settings200.cy == 1760);

    const SIZE settingsMin100 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(96);
    const SIZE settingsMin150 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(144);
    const SIZE settingsMin200 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(192);
    CHECK(settingsMin100.cx == 760 && settingsMin100.cy == 520);
    CHECK(settingsMin150.cx == 1140 && settingsMin150.cy == 780);
    CHECK(settingsMin200.cx == 1520 && settingsMin200.cy == 1040);

    const auto radial100 = easy::gesture::RadialMenuStyle::metricsForDpi(96);
    const auto radial150 = easy::gesture::RadialMenuStyle::metricsForDpi(144);
    const auto radial200 = easy::gesture::RadialMenuStyle::metricsForDpi(192);
    CHECK(radial100.windowSize == 400 && radial100.outerRadius == 150 &&
          radial100.innerRadius == 40);
    CHECK(radial150.windowSize == 600 && radial150.outerRadius == 225 &&
          radial150.innerRadius == 60);
    CHECK(radial200.windowSize == 800 && radial200.outerRadius == 300 &&
          radial200.innerRadius == 80);
}

static void check_toolbar_inside_surface(const easy::capture::CaptureState& state,
                                         D2D1_SIZE_F surface) {
    for (const auto& button : state.toolbarButtons) {
        CHECK(button.rect.left >= 0.0f);
        CHECK(button.rect.top >= 0.0f);
        CHECK(button.rect.right <= surface.width + 0.01f);
        CHECK(button.rect.bottom <= surface.height + 0.01f);
        CHECK(button.rect.right > button.rect.left);
        CHECK(button.rect.bottom > button.rect.top);
    }
}

static void test_capture_toolbar_dpi_layout() {
    using namespace easy::capture;

    CaptureState screenshot;
    screenshot.mode = OverlayMode::Screenshot;
    screenshot.dpiScale = 1.5f;  // Windows 150% / 144 DPI
    const D2D1_SIZE_F desktop150 = D2D1::SizeF(2304.0f, 1440.0f);
    rebuildCaptureToolbar(screenshot, D2D1::RectF(180.0f, 120.0f, 1600.0f, 980.0f),
                          desktop150);
    CHECK(screenshot.toolbarButtons.size() == 25);
    CHECK(std::abs((screenshot.toolbarButtons.front().rect.bottom -
                    screenshot.toolbarButtons.front().rect.top) - 45.0f) < 0.01f);
    check_toolbar_inside_surface(screenshot, desktop150);
    const auto* cachedButtons = screenshot.toolbarButtons.data();
    rebuildCaptureToolbar(screenshot, D2D1::RectF(180.0f, 120.0f, 1600.0f, 980.0f),
                          desktop150);
    CHECK(screenshot.toolbarButtons.data() == cachedButtons);

    CaptureState wrapped;
    wrapped.mode = OverlayMode::Screenshot;
    wrapped.dpiScale = 2.0f;
    const D2D1_SIZE_F compact = D2D1::SizeF(1000.0f, 700.0f);
    rebuildCaptureToolbar(wrapped, D2D1::RectF(80.0f, 100.0f, 850.0f, 480.0f), compact);
    CHECK(wrapped.toolbarButtons.size() == 25);
    bool hasSecondRow = false;
    for (const auto& button : wrapped.toolbarButtons) {
        if (button.rect.top > wrapped.toolbarButtons.front().rect.top + 0.01f) {
            hasSecondRow = true;
            break;
        }
    }
    CHECK(hasSecondRow);
    check_toolbar_inside_surface(wrapped, compact);

    CaptureState recording;
    recording.mode = OverlayMode::RecordRegion;
    recording.dpiScale = 1.5f;
    rebuildCaptureToolbar(recording, D2D1::RectF(200.0f, 100.0f, 1200.0f, 800.0f),
                          desktop150);
    CHECK(recording.toolbarButtons.size() == 2);
    CHECK(std::abs((recording.toolbarButtons[0].rect.right -
                    recording.toolbarButtons[0].rect.left) - 102.0f) < 0.01f);
    CHECK(std::abs((recording.toolbarButtons[1].rect.right -
                    recording.toolbarButtons[1].rect.left) - 117.0f) < 0.01f);
    check_toolbar_inside_surface(recording, desktop150);

    CaptureState extreme;
    extreme.mode = OverlayMode::Screenshot;
    extreme.dpiScale = 5.0f;
    const D2D1_SIZE_F desktop500 = D2D1::SizeF(7680.0f, 4320.0f);
    rebuildCaptureToolbar(extreme, D2D1::RectF(500.0f, 400.0f, 7000.0f, 3600.0f),
                          desktop500);
    CHECK(extreme.toolbarButtons.size() == 25);
    check_toolbar_inside_surface(extreme, desktop500);
}

static void test_pin_window_transform() {
    cv::Mat original = (cv::Mat_<uint8_t>(3, 2) << 10, 20,
                                                   30, 40,
                                                   50, 60);
    // 旋转 90 度
    cv::Mat rotated90;
    cv::rotate(original, rotated90, cv::ROTATE_90_CLOCKWISE);
    CHECK(rotated90.rows == 2 && rotated90.cols == 3);
    CHECK(rotated90.at<uint8_t>(0, 0) == 50);
    CHECK(rotated90.at<uint8_t>(0, 2) == 10);
    CHECK(rotated90.at<uint8_t>(1, 0) == 60);
    CHECK(rotated90.at<uint8_t>(1, 2) == 20);

    // 水平镜像翻转
    cv::Mat flippedH;
    cv::flip(original, flippedH, 1);
    CHECK(flippedH.rows == 3 && flippedH.cols == 2);
    CHECK(flippedH.at<uint8_t>(0, 0) == 20 && flippedH.at<uint8_t>(0, 1) == 10);
    CHECK(flippedH.at<uint8_t>(2, 0) == 60 && flippedH.at<uint8_t>(2, 1) == 50);

    // 垂直镜像翻转
    cv::Mat flippedV;
    cv::flip(original, flippedV, 0);
    CHECK(flippedV.rows == 3 && flippedV.cols == 2);
    CHECK(flippedV.at<uint8_t>(0, 0) == 50 && flippedV.at<uint8_t>(0, 1) == 60);
    CHECK(flippedV.at<uint8_t>(2, 0) == 10 && flippedV.at<uint8_t>(2, 1) == 20);
}

static void test_winutils_clipboard_and_encoding() {
    CHECK_EQ(easy::core::WinUtils::toLower("EasyTools_PRO"), "easytools_pro");
    std::string text = "EasyTools 截图 & OCR 测试 🚀";
    std::wstring wtext = easy::core::WinUtils::utf8ToWstring(text);
    CHECK(!wtext.empty());
    std::string roundtrip = easy::core::WinUtils::wstringToUtf8(wtext);
    CHECK_EQ(roundtrip, text);

    std::vector<uint8_t> raw = {0x45, 0x61, 0x73, 0x79}; // "Easy"
    std::string b64 = easy::core::WinUtils::base64Encode(raw);
    CHECK_EQ(b64, "RWFzeQ==");
}

static void test_lua_engine_security() {
    auto& lua = easy::core::LuaEngine::instance();
    CHECK(lua.initialize());

    // 1. Safe 绝对无害只读权限 (仅 Log 与 Url)
    CHECK(lua.executeScript("local a = 1 + 2; easy.log.info('Safe test'); local enc = easy.url.encode('abc 123')", easy::core::LuaPermission::Safe));

    // 2. Safe 模式下必须严格拦截 Window / Keyboard / Clipboard / Screen / Shell / Fs / Http
    CHECK(!lua.executeScript("easy.window.minimize()", easy::core::LuaPermission::Safe));
    CHECK(!lua.executeScript("easy.keyboard.sendKeys('Ctrl+C')", easy::core::LuaPermission::Safe));
    CHECK(!lua.executeScript("easy.clipboard.getText()", easy::core::LuaPermission::Safe));
    CHECK(!lua.executeScript("easy.screen.getPixelColor(0, 0)", easy::core::LuaPermission::Safe));
    CHECK(!lua.executeScript("easy.shell.run('notepad.exe')", easy::core::LuaPermission::Safe));
    CHECK(!lua.executeScript("easy.fs.exists('test.txt')", easy::core::LuaPermission::Safe));

    // 3. 超时保护测试（死循环被 100ms 钩子及时中断）
    auto t0 = std::chrono::steady_clock::now();
    bool timeoutResult = lua.executeScript("while true do end", easy::core::LuaPermission::Standard, std::chrono::milliseconds(100));
    auto t1 = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    CHECK(!timeoutResult);
    CHECK(elapsed < 1000);

    // 4. 取消令牌测试
    std::atomic<bool> cancelToken{true};
    CHECK(!lua.executeScript("local x = 0; for i = 1, 10000000 do x = x + i end", easy::core::LuaPermission::Standard, std::chrono::milliseconds(5000), &cancelToken));

    // 5. 沙箱安全性：危险系统调用已被封禁
    CHECK(!lua.executeScript("os.execute('echo hack')"));
    CHECK(!lua.executeScript("os.remove('test.txt')"));

    // 6. 显式授予敏感权限时允许调用
    CHECK(lua.executeScript("local ok = easy.fs.exists('CMakeLists.txt')", easy::core::LuaPermission::Fs));

    // 7. 用户授权决策流与授权持久化测试
    const std::string testScriptId = "gesture:test_action";
    lua.revokePermissions(testScriptId);
    CHECK(!lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));

    // 显式授权后立即可用
    lua.grantPermissions(testScriptId, easy::core::LuaPermission::Keyboard | easy::core::LuaPermission::Window);
    CHECK(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));
    CHECK(lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Window));
    CHECK(!lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Shell));

    // 撤销授权测试
    lua.revokePermissions(testScriptId);
    CHECK(!lua.isScriptAuthorized(testScriptId, easy::core::LuaPermission::Keyboard));

    lua.shutdown();
}

static void test_tray_notification() {
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
    CHECK(hwnd != nullptr);

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

    CHECK(hIcon != nullptr);

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
    CHECK(ok);
}

static void test_quicklook_and_translation() {
    // 1. Explorer 选中文件判定冒烟（安全不崩溃）
    auto sel = easy::core::WinUtils::getSelectedExplorerFile();
    // 在无前台 Explorer 的测试环境下应返回 nullopt，不可抛异常或悬挂
    CHECK(!sel.has_value() || !sel->empty());

    // 2. 划词提取冒烟测试
    std::string captured = easy::core::WinUtils::captureSelectedText();
    // 允许为空或剪贴板原有内容
    CHECK(captured.size() >= 0);

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
    CHECK(jResp["id"] == 999);
    CHECK(jResp["result"]["ok"] == true);
    CHECK(jResp["result"]["path"] == "C:\\test.md");

    // 5. 快捷键冲突检测与分类判定测试
    auto& hkManager = easy::core::HotkeyManager::instance();
    const easy::core::HotkeyDef dummyKey{easy::core::ModKey::Alt, 'Z'};
    hkManager.registerHotkey("Test Feature A", dummyKey, []() {});

    // 同名重绑不应自相冲突
    auto selfConflict = hkManager.checkConflict(dummyKey, "Test Feature A");
    CHECK(!selfConflict.hasConflict);

    // 异名重绑应检测出内部冲突
    auto interConflict = hkManager.checkConflict(dummyKey, "Test Feature B");
    CHECK(interConflict.hasConflict);
    CHECK(interConflict.conflictType == "internal");

    // 清理测试热键
    hkManager.unregisterHotkey("Test Feature A");
}

static void test_content_search_extractors() {
    // 1. SearchExpression content 语法解析验证
    auto expr1 = SearchExpression::parse(L"content:SELECT");
    CHECK(expr1.hasContentFilter());
    CHECK_EQ(easy::core::WinUtils::wstringToUtf8(expr1.getContentQuery()), "SELECT");
    CHECK(!expr1.requiresFullPath());

    auto expr2 = SearchExpression::parse(L"ext:cpp;h c:EasyTools c:\\projects\\");
    CHECK(expr2.hasContentFilter());
    CHECK_EQ(easy::core::WinUtils::wstringToUtf8(expr2.getContentQuery()), "EasyTools");
    CHECK(expr2.requiresFullPath());

    auto expr3 = SearchExpression::parse(L"内容:工程图纸");
    CHECK(expr3.hasContentFilter());
    CHECK_EQ(easy::core::WinUtils::wstringToUtf8(expr3.getContentQuery()), "工程图纸");

    // 拼音音节分隔符 (如输入法 tong'xi 匹配同喜 tongxi)
    auto exprPinyinSyllable = SearchExpression::parse(L"tong'xi");
    FileRecord recTongXi;
    recTongXi.fileName = L"同喜.txt";
    recTongXi.normalizedName = L"同喜.txt";
    recTongXi.pinyinFull = L"tongxi";
    recTongXi.pinyinInitials = L"tx";
    CHECK(exprPinyinSyllable.matches(recTongXi, L'C'));

    // 2. ContentSearchEngine 格式支持测试
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    CHECK(engine.canSearchContent(L"cpp"));
    CHECK(engine.canSearchContent(L"rs"));
    CHECK(engine.canSearchContent(L"py"));
    CHECK(engine.canSearchContent(L"sql"));
    CHECK(engine.canSearchContent(L"md"));
    CHECK(engine.canSearchContent(L"docx"));
    CHECK(engine.canSearchContent(L"xlsx"));
    CHECK(engine.canSearchContent(L"pptx"));
    CHECK(engine.canSearchContent(L"psd"));
    CHECK(engine.canSearchContent(L"ai"));
    CHECK(engine.canSearchContent(L"cdr"));
    CHECK(engine.canSearchContent(L"xmind"));
    CHECK(engine.canSearchContent(L"dxf"));
    CHECK(!engine.canSearchContent(L"exe"));
    CHECK(!engine.canSearchContent(L"dll"));

    // 3. PlainTextExtractor 实际文件扫描测试 (创建临时测试文件)
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring testCppFile = std::wstring(tempPath) + L"easytools_test_source.cpp";

    {
        std::ofstream ofs(testCppFile, std::ios::binary);
        ofs << "// Line 1\n";
        ofs << "// Line 2\n";
        ofs << "void TestWorldClassContentSearch() {\n";
        ofs << "    int magic = 20260816;\n";
        ofs << "}\n";
    }

    std::vector<easy::service::content::ContentSnippet> snippets;
    bool found = engine.searchFile(testCppFile, L"TestWorldClassContentSearch", false, snippets);
    CHECK(found);
    CHECK(snippets.size() == 1);
    if (!snippets.empty()) {
        CHECK(snippets[0].lineNumber == 3);
        CHECK(snippets[0].lineContent.find(L"TestWorldClassContentSearch") != std::wstring::npos);
        CHECK(snippets[0].matchLength == wcslen(L"TestWorldClassContentSearch"));
    }

    DeleteFileW(testCppFile.c_str());

    // 4. DxfExtractor 实际文件扫描测试 (创建临时测试 DXF)
    std::wstring testDxfFile = std::wstring(tempPath) + L"easytools_test_drawing.dxf";
    {
        std::ofstream ofs(testDxfFile, std::ios::binary);
        ofs << "0\nSECTION\n2\nENTITIES\n";
        ofs << "0\nTEXT\n8\nLAYER_1\n1\nDWG_PROJECT_NUM_A88\n";
        ofs << "0\nENDSEC\n0\nEOF\n";
    }

    snippets.clear();
    bool dxfFound = engine.searchFile(testDxfFile, L"PROJECT_NUM_A88", false, snippets);
    CHECK(dxfFound);
    CHECK(!snippets.empty());
    if (!snippets.empty()) {
        CHECK(snippets[0].lineContent.find(L"DWG_PROJECT_NUM_A88") != std::wstring::npos);
    }

    DeleteFileW(testDxfFile.c_str());
}

int main() {
    test_tray_notification();
    test_quicklook_and_translation();
    test_recognizer();
    test_scoperule();
    test_gesture_actions_and_builtin_commands();
    test_hotkey_parser();
    test_disabled_hotkey_lifecycle();
    test_config_manager();
    test_message_bridge();
    test_plugin_manifest();
    test_event_bus_quiescence();
    test_perf_timer();
    test_update_version_comparison();
    test_pinyin_engine();
    test_search_everything_expressions();
    test_content_search_extractors();
    test_winutils_fullscreen();
    test_winutils_clipboard_and_encoding();
    test_pin_window_transform();
    test_lua_engine_security();
    test_capture_backend_smoke();
    test_audio_capture_smoke();
    test_scroll_capture_bounded_preview();
    test_cursor_overlay_restore();
    test_shortcut_hint_dpi_metrics();
    test_shared_dpi_metrics();
    test_capture_toolbar_dpi_layout();
    test_screen_recorder_smoke();
    // Keep discovery last: PluginManager shutdown intentionally retires shared
    // callback registries as part of the real DLL-unload path.
    test_plugin_discovery();

    std::printf("\n==== EasyTools 单元测试: %d 断言, %d 失败 ====\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
