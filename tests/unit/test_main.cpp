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

#include <cstdio>
#include <initializer_list>
#include <string>

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
}

int main() {
    test_recognizer();
    test_scoperule();

    std::printf("\n==== EasyTools 单元测试: %d 断言, %d 失败 ====\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
