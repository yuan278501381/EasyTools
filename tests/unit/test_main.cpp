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
#include "gesture/ScopeRule.h"
#include "core/config/ConfigManager.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/ipc/MessageBridge.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/update/UpdateChecker.h"

#include <chrono>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <thread>
#include <windows.h>

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

    // 防抖: 位移小于最小段距离 → 无效手势
    CHECK_EQ(recognize({{0, 0}, {20, 0}}), "");
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

static void test_hotkey_parser() {
    using easy::core::HotkeyDef;

    auto normal = HotkeyDef::fromString("Ctrl+Shift+A");
    CHECK(normal.has_value());
    if (normal) CHECK_EQ(normal->toString(), "Ctrl+Shift+A");

    auto special = HotkeyDef::fromString("Alt+PageDown");
    CHECK(special.has_value());
    if (special) CHECK_EQ(special->toString(), "Alt+PageDown");

    CHECK(!HotkeyDef::fromString("").has_value());
    CHECK(!HotkeyDef::fromString("Ctrl+").has_value());
    CHECK(!HotkeyDef::fromString("Ctrl+UnknownKey").has_value());
    CHECK(!HotkeyDef::fromString("Bad+Ctrl+A").has_value());
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
}

static void test_perf_timer() {
    easy::core::PerfTimer timer("gesture");
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.stop();
    const double elapsed = timer.elapsedMs();
    CHECK(elapsed >= 1.0);
    CHECK(easy::core::PerformanceMonitor::instance().getMetrics().gestureLatencyMs >= 1.0);
}

static void test_update_version_comparison() {
    using easy::core::UpdateChecker;
    CHECK(UpdateChecker::isNewerVersion("v1.2.3", "1.2.2"));
    CHECK(UpdateChecker::isNewerVersion("1.10.0", "1.9.9"));
    CHECK(UpdateChecker::isNewerVersion("2.0", "1.99.99"));
    CHECK(!UpdateChecker::isNewerVersion("1.2.3", "1.2.3"));
    CHECK(!UpdateChecker::isNewerVersion("1.2", "1.2.1"));
    CHECK(!UpdateChecker::isNewerVersion("1.2.3-beta.1", "1.2.3"));
    CHECK(!UpdateChecker::isNewerVersion("not-a-version", "1.0.0"));
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

static void test_winutils_fullscreen() {
    // 空句柄或无效句柄返回 false
    CHECK(!easy::core::WinUtils::isWindowFullscreen(nullptr));
    CHECK(!easy::core::WinUtils::isWindowFullscreen((HWND)(uintptr_t)0x12345678));
}

int main() {
    test_recognizer();
    test_scoperule();
    test_hotkey_parser();
    test_config_manager();
    test_message_bridge();
    test_perf_timer();
    test_update_version_comparison();
    test_pinyin_engine();
    test_winutils_fullscreen();

    std::printf("\n==== EasyTools 单元测试: %d 断言, %d 失败 ====\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
