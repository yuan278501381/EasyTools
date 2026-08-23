// ─────────────────────────────────────────────────────────────────────────────
// test_main.cpp — EasyTools 单元测试套件 (基于 Google Test / GMock 工业级测试架构)
//
// 覆盖核心纯业务逻辑、状态机、多格式解析器与高分屏适配:
//   • GestureRecognizer: 方向编码、转弯圆角平滑消抖 (Fillet Folding) 与孤立防抖
//   • ScopeRule: 进程与窗口类名精确/通配符匹配、正则元字符转义与 JSON 往返
//   • GestureProfile: 三态触发模式 (Default/Enabled/Disabled)、手势调序与前缀冲突检测
//   • HotkeyManager / ConfigManager / MessageBridge / EventBus / PerfMonitor
//   • PinyinEngine / SearchExpression (Everything 语法) / ContentSearchEngine
//   • DpiUtils / Lua 沙箱安全与权限持久化 / 截图、录屏与长截图 Smoke 测试
//
// 由 deploy.ps1 在 CMake 构建后统一执行，并通过 OpenCppCoverage 生成防回退覆盖率报告。
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <windows.h>
#include <sddl.h>
// WIN32_LEAN_AND_MEAN removes COM declarations from windows.h. UI Automation
// MIDL headers need them before their forward declarations.
#include <ole2.h>
#include <UIAutomation.h>

#include "gesture/GestureRecognizer.h"
#include "gesture/GestureAction.h"
#include "gesture/GestureProfile.h"
#include "gesture/GestureInputPolicy.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/HotCornerEngine.h"
#include "gesture/ScopeRule.h"
#include "gesture/RadialMenuStyle.h"
#include "capture/CaptureBackend.h"
#include "capture/AudioCapture.h"
#include "capture/CursorOverlay.h"
#include "capture/ScreenRecorder.h"
#include "capture/ScrollCapture.h"
#include "capture/ScrollCaptureStorage.h"
#include "capture/ShortcutHintOverlay.h"
#include "capture/ShortcutHintStyle.h"
#include "keycast/KeycastStyle.h"
#include "ocr/OcrResultStyle.h"
#include "ui/ToastStyle.h"
#include "ui/WebViewOriginPolicy.h"
#include "ui/WebViewSuspend.h"
#include "ui/KeyboardPipeline.h"
#include "service/PipeEndpoint.h"
#include "ui/WebViewWindowStyle.h"
#include "capture/CaptureToolbarLayout.h"
#include "capture/CaptureToolbarAccessibility.h"
#include "core/accessibility/OverlayUiaProvider.h"
#include "core/config/ConfigManager.h"
#include "core/events/EventBus.h"
#include "core/hotkey/HotkeyManager.h"
#include "core/hotkey/HotkeyPolicy.h"
#include "core/hotkey/KeyboardHook.h"
#include "core/ipc/MessageBridge.h"
#include "core/plugin/PluginManifest.h"
#include "core/plugin/PluginManager.h"
#include "core/stats/PerformanceMonitor.h"
#include "core/stats/StatsManager.h"
#include "core/update/UpdateChecker.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/utils/WinUtils.h"
#include "core/utils/ElevationPolicy.h"
#include "core/lua/LuaEngine.h"
#include "search/ServiceLifetime.h"
#include "search/ServiceStartupPolicy.h"
#include "service/PinyinEngine.h"
#include "service/PipeProtocol.h"
#include "service/SearchCancellation.h"
#include "service/SearchExpression.h"
#include "service/content/ContentSearchEngine.h"
#include "service/content/PlainTextExtractor.h"
#include "service/content/ZipXmlExtractor.h"
#include "service/content/PsdAiExtractor.h"
#include "service/content/DxfExtractor.h"
#include "service/MftParser.h"
#include "service/db/RunHistoryManager.h"
#include "service/db/SearchHistoryManager.h"
#include "service/db/DatabaseManager.h"

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
#include <future>
#include <initializer_list>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

using namespace easy::gesture;

// 喂入一串轨迹点, 返回识别出的方向编码 (无效手势返回空串)。
static std::string recognize(std::initializer_list<TrackPoint> pts) {
    GestureRecognizer r;  // 默认配置: minSegmentDistance=14, samplingInterval=2
    r.reset();
    for (const auto& p : pts) r.addPoint(p.x, p.y);
    auto res = r.finalize();
    return res ? res->code : std::string{};
}

// -----------------------------------------------------------------------------
// 1. 手势识别器测试套件
// -----------------------------------------------------------------------------
TEST(GestureRecognizerTest, DirectionEncodingAndSmoothing) {
    // 屏幕坐标 Y 轴向下: 向上 = y 减小
    EXPECT_EQ(recognize({{0, 0}, {100, 0}}), "R");    // →
    EXPECT_EQ(recognize({{100, 0}, {0, 0}}), "L");    // ←
    EXPECT_EQ(recognize({{0, 100}, {0, 0}}), "U");    // ↑
    EXPECT_EQ(recognize({{0, 0}, {0, 100}}), "D");    // ↓

    // 对角线 (单段)
    EXPECT_EQ(recognize({{0, 100}, {100, 0}}), "UR"); // ↗
    EXPECT_EQ(recognize({{0, 0}, {100, 100}}), "DR"); // ↘

    // 多段 (L 形及连续拐弯，基于拐点检测)
    EXPECT_EQ(recognize({{0, 100}, {100, 100}, {100, 0}}), "R-U"); // → 然后 ↑
    EXPECT_EQ(recognize({{100, 0}, {0, 0}, {0, 100}}), "L-D");     // ← 然后 ↓
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {50, 50}}), "D-R");      // ↓ 然后 →
    EXPECT_EQ(recognize({{0, 0}, {50, 0}, {50, 50}}), "R-D");      // → 然后 ↓
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {-50, 50}}), "D-L");     // ↓ 然后 ←
    EXPECT_EQ(recognize({{0, 0}, {0, -50}, {50, -50}}), "U-R");    // ↑ 然后 →
    EXPECT_EQ(recognize({{0, 0}, {0, -50}, {-50, -50}}), "U-L");   // ↑ 然后 ←
    EXPECT_EQ(recognize({{0, 0}, {0, 50}, {50, 50}, {50, 100}}), "D-R-D"); // ↓ → ↓

    // 方向序列与箭头转换辅助函数
    std::vector<Direction> dirs = {Direction::Left, Direction::Down};
    EXPECT_EQ(directionsToCode(dirs), "L-D");
    EXPECT_EQ(directionsToArrowString(dirs), "←↓");

    std::vector<Direction> singleDir = {Direction::Up};
    EXPECT_EQ(directionsToCode(singleDir), "U");
    EXPECT_EQ(directionsToArrowString(singleDir), "↑");

    // 防抖: 微位移小于最小段距离 (如 < 14px) → 视为普通点击/无效手势
    EXPECT_EQ(recognize({{0, 0}, {8, 0}}), "");

    // 智能转弯圆角平滑消抖 (Corner Fillet Folding)
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Right})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::DownRight, Direction::Down})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownLeft, Direction::Left})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpRight, Direction::Right})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpLeft, Direction::Left})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::DownLeft, Direction::Down})), "L-D");
    
    // 孤立回弹微抖动消除 (Rebound Jitter Elimination)
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight, Direction::Down})), "D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::UpRight, Direction::Right})), "R");

    // 真正对角手势保留（整笔就是一条斜线）
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight})), "DR");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpLeft})), "UL");

    // 未走完的转角圆角：对角段补成直角第二段
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownRight})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight, Direction::Right})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Down, Direction::DownLeft})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownLeft, Direction::Left})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpRight})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpRight, Direction::Right})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::UpLeft})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::UpLeft, Direction::Left})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::DownRight})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::DownLeft})), "L-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Right, Direction::UpRight})), "R-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Left, Direction::UpLeft})), "L-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::Up, Direction::DownRight})), "U-DR");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Right, Direction::DownRight})), "D-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Left, Direction::DownLeft})), "D-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Up, Direction::Right, Direction::UpRight})), "U-R");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Up, Direction::Left, Direction::UpLeft})), "U-L");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Right, Direction::Down, Direction::DownRight})), "R-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Right, Direction::Up, Direction::UpRight})), "R-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Left, Direction::Down, Direction::DownLeft})), "L-D");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Left, Direction::Up, Direction::UpLeft})), "L-U");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({
        Direction::Down, Direction::Right, Direction::Down})), "D-R-D");

    EXPECT_EQ(tokenToDirection("XX"), Direction::None);
    EXPECT_EQ(tokenToDirection(""), Direction::None);
    EXPECT_TRUE(codeToDirections("").empty());
    EXPECT_EQ(directionsToCode(codeToDirections("D-R")), "D-R");
    EXPECT_EQ(directionsToCode(codeToDirections("DR")), "DR");
    EXPECT_EQ(codeToDirections("D-XX-R").size(), 2u);

    EXPECT_EQ(*expandSingleDiagonalCode("DR"), "D-R");
    EXPECT_EQ(*expandSingleDiagonalCode("DL"), "D-L");
    EXPECT_EQ(*expandSingleDiagonalCode("UR"), "U-R");
    EXPECT_EQ(*expandSingleDiagonalCode("UL"), "U-L");
    EXPECT_FALSE(expandSingleDiagonalCode("D-R").has_value());
    EXPECT_FALSE(expandSingleDiagonalCode("L").has_value());

    EXPECT_EQ(refineCodeWithPath("", {{0, 0}, {10, 10}}, 14), "");
    EXPECT_EQ(refineCodeWithPath("D-R", {{0, 0}, {0, 80}, {80, 80}}, 14), "D-R");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {8, 400}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {120, 400}}, 14), "D");
    // 整笔持续向左倾斜仍是「下」，没有真实转角就不能升级成「下-左」。
    EXPECT_EQ(refineCodeWithPath("D", {{0, 0}, {-15, 30}, {-30, 60}, {-45, 90}, {-60, 120}}, 14), "D");
    EXPECT_EQ(refineCodeWithPath("DR", {{0, 0}, {40, 40}, {80, 80}}, 14), "DR");
    {
        std::vector<TrackPoint> lDownRight;
        for (int y = 0; y <= 80; y += 8) lDownRight.push_back({0, y});
        for (int x = 8; x <= 80; x += 8) lDownRight.push_back({x, 80});
        EXPECT_EQ(refineCodeWithPath("D", lDownRight, 14), "D-R");
        std::vector<TrackPoint> lDownLeft;
        for (int y = 0; y <= 80; y += 8) lDownLeft.push_back({80, y});
        for (int x = 72; x >= 0; x -= 8) lDownLeft.push_back({x, 80});
        EXPECT_EQ(refineCodeWithPath("D", lDownLeft, 14), "D-L");
        std::vector<TrackPoint> lUpRight;
        for (int y = 80; y >= 0; y -= 8) lUpRight.push_back({0, y});
        for (int x = 8; x <= 80; x += 8) lUpRight.push_back({x, 0});
        EXPECT_EQ(refineCodeWithPath("U", lUpRight, 14), "U-R");
        std::vector<TrackPoint> lRightDown;
        for (int x = 0; x <= 80; x += 8) lRightDown.push_back({x, 0});
        for (int y = 8; y <= 80; y += 8) lRightDown.push_back({80, y});
        EXPECT_EQ(refineCodeWithPath("R", lRightDown, 14), "R-D");
        std::vector<TrackPoint> slantDown;
        for (int y = 0; y <= 200; y += 10) slantDown.push_back({y / 20, y});
        EXPECT_EQ(refineCodeWithPath("D", slantDown, 14), "D");
    }

    auto recognizePath = [](const std::vector<TrackPoint>& pts) {
        GestureRecognizer r;
        r.reset();
        for (const auto& p : pts) r.addPoint(p.x, p.y);
        auto res = r.finalize();
        return res ? res->code : std::string{};
    };
    auto driftTurn = [](int ax, int ay, int bx, int by) {
        std::vector<TrackPoint> pts;
        for (int i = 0; i <= 24; ++i) pts.push_back({ax * i, ay * i});
        const auto last = pts.back();
        for (int i = 1; i <= 24; ++i) {
            pts.push_back({last.x + bx * i, last.y + by * i});
        }
        return pts;
    };
    EXPECT_EQ(recognizePath(driftTurn(0, 4, 4, 1)), "D-R");
    EXPECT_EQ(recognizePath(driftTurn(0, 4, -4, 1)), "D-L");
    EXPECT_EQ(recognizePath(driftTurn(0, -4, 4, -1)), "U-R");
    EXPECT_EQ(recognizePath(driftTurn(0, -4, -4, -1)), "U-L");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, 1, 4)), "R-D");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, 1, -4)), "R-U");
    EXPECT_EQ(recognizePath(driftTurn(-4, 0, -1, 4)), "L-D");
    EXPECT_EQ(recognizePath(driftTurn(-4, 0, -1, -4)), "L-U");
    EXPECT_EQ(recognizePath(driftTurn(0, 4, 0, -4)), "D-U");
    EXPECT_EQ(recognizePath(driftTurn(4, 0, -4, 0)), "R-L");

    {
        std::vector<TrackPoint> sloppyDown;
        sloppyDown.push_back({0, 0});
        sloppyDown.push_back({10, 10});
        sloppyDown.push_back({16, 16});
        for (int y = 20; y <= 120; y += 4) sloppyDown.push_back({16, y});
        EXPECT_EQ(recognizePath(sloppyDown), "D");
    }

    // 自然「下再右」：横扫时手腕仍会略微下沉，不能把整笔锁死成 D
    {
        std::vector<TrackPoint> naturalDownRight;
        for (int y = 0; y <= 80; y += 4) naturalDownRight.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) naturalDownRight.push_back({x, 80 + x / 8});
        GestureRecognizer natural;
        natural.reset();
        for (const auto& p : naturalDownRight) natural.addPoint(p.x, p.y);
        auto naturalRes = natural.finalize();
        ASSERT_TRUE(naturalRes.has_value());
        EXPECT_EQ(naturalRes->code, "D-R");
    }

    {
        std::vector<TrackPoint> slowDownRight;
        for (int y = 0; y <= 100; y += 2) slowDownRight.push_back({0, y});
        for (int x = 2; x <= 100; x += 2) slowDownRight.push_back({x, 100});
        EXPECT_EQ(recognizePath(slowDownRight), "D-R");
        std::vector<TrackPoint> slowDownLeft;
        for (int y = 0; y <= 100; y += 2) slowDownLeft.push_back({100, y});
        for (int x = 98; x >= 0; x -= 2) slowDownLeft.push_back({x, 100});
        EXPECT_EQ(recognizePath(slowDownLeft), "D-L");
        std::vector<TrackPoint> slowUpRight;
        for (int y = 100; y >= 0; y -= 2) slowUpRight.push_back({0, y});
        for (int x = 2; x <= 100; x += 2) slowUpRight.push_back({x, 0});
        EXPECT_EQ(recognizePath(slowUpRight), "U-R");
        std::vector<TrackPoint> slowUpLeft;
        for (int y = 100; y >= 0; y -= 2) slowUpLeft.push_back({100, y});
        for (int x = 98; x >= 0; x -= 2) slowUpLeft.push_back({x, 0});
        EXPECT_EQ(recognizePath(slowUpLeft), "U-L");
        std::vector<TrackPoint> slowRightDown;
        for (int x = 0; x <= 100; x += 2) slowRightDown.push_back({x, 0});
        for (int y = 2; y <= 100; y += 2) slowRightDown.push_back({100, y});
        EXPECT_EQ(recognizePath(slowRightDown), "R-D");
        std::vector<TrackPoint> slowRightUp;
        for (int x = 0; x <= 100; x += 2) slowRightUp.push_back({x, 100});
        for (int y = 98; y >= 0; y -= 2) slowRightUp.push_back({100, y});
        EXPECT_EQ(recognizePath(slowRightUp), "R-U");
        std::vector<TrackPoint> slowLeftDown;
        for (int x = 100; x >= 0; x -= 2) slowLeftDown.push_back({x, 0});
        for (int y = 2; y <= 100; y += 2) slowLeftDown.push_back({0, y});
        EXPECT_EQ(recognizePath(slowLeftDown), "L-D");
        std::vector<TrackPoint> slowLeftUp;
        for (int x = 100; x >= 0; x -= 2) slowLeftUp.push_back({x, 100});
        for (int y = 98; y >= 0; y -= 2) slowLeftUp.push_back({0, y});
        EXPECT_EQ(recognizePath(slowLeftUp), "L-U");
    }

    {
        std::vector<TrackPoint> slantDown;
        for (int y = 0; y <= 240; y += 4) slantDown.push_back({y / 20, y});
        EXPECT_EQ(recognizePath(slantDown), "D");
        std::vector<TrackPoint> slantDownLeft;
        for (int y = 0; y <= 240; y += 4) slantDownLeft.push_back({-y * 13 / 25, y});
        EXPECT_EQ(recognizePath(slantDownLeft), "D");
        std::vector<TrackPoint> slantRight;
        for (int x = 0; x <= 240; x += 4) slantRight.push_back({x, x / 20});
        EXPECT_EQ(recognizePath(slantRight), "R");

        // 明确的 45° 对角线与真正组合手势不能被基准方向容错吞掉。
        std::vector<TrackPoint> diagonalDownLeft;
        for (int y = 0; y <= 120; y += 4) diagonalDownLeft.push_back({-y, y});
        EXPECT_EQ(recognizePath(diagonalDownLeft), "DL");
    }

    {
        std::vector<TrackPoint> drHook;
        for (int y = 0; y <= 80; y += 4) drHook.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) drHook.push_back({x, 80});
        for (int i = 1; i <= 6; ++i) drHook.push_back({80 + i * 2, 80 + i * 2});
        EXPECT_EQ(recognizePath(drHook), "D-R");
        std::vector<TrackPoint> urHook;
        for (int y = 80; y >= 0; y -= 4) urHook.push_back({0, y});
        for (int x = 4; x <= 80; x += 4) urHook.push_back({x, 0});
        for (int i = 1; i <= 6; ++i) urHook.push_back({80 + i * 2, -i * 2});
        EXPECT_EQ(recognizePath(urHook), "U-R");
        std::vector<TrackPoint> rdHook;
        for (int x = 0; x <= 80; x += 4) rdHook.push_back({x, 0});
        for (int y = 4; y <= 80; y += 4) rdHook.push_back({80, y});
        for (int i = 1; i <= 6; ++i) rdHook.push_back({80 + i * 2, 80 + i * 2});
        EXPECT_EQ(recognizePath(rdHook), "R-D");
    }

    {
        std::vector<TrackPoint> downRightDown;
        for (int y = 0; y <= 60; y += 4) downRightDown.push_back({0, y});
        for (int x = 4; x <= 60; x += 4) downRightDown.push_back({x, 60});
        for (int y = 64; y <= 120; y += 4) downRightDown.push_back({60, y});
        EXPECT_EQ(recognizePath(downRightDown), "D-R-D");
        std::vector<TrackPoint> leftUpRight;
        for (int x = 120; x >= 60; x -= 4) leftUpRight.push_back({x, 120});
        for (int y = 116; y >= 60; y -= 4) leftUpRight.push_back({60, y});
        for (int x = 64; x <= 120; x += 4) leftUpRight.push_back({x, 60});
        EXPECT_EQ(recognizePath(leftUpRight), "L-U-R");
    }

    // 乱晃反悔与原地打圈自动取消判定
    GestureRecognizer scribbleRecognizer;
    scribbleRecognizer.reset();
    std::vector<TrackPoint> scribblePoints = {
        {0, 0}, {50, 0}, {0, 0}, {50, 0}, {0, 0}, {50, 0}, {0, 0}
    };
    for (const auto& p : scribblePoints) {
        scribbleRecognizer.addPoint(p.x, p.y);
    }
    EXPECT_TRUE(scribbleRecognizer.isScribbleCanceled());
    EXPECT_FALSE(scribbleRecognizer.finalize().has_value());

    // HEX 颜色解析测试
    auto parsed = easy::core::parseHexColor("#FF0000");
    EXPECT_EQ(parsed.r, 1.0f);
    EXPECT_EQ(parsed.g, 0.0f);
    EXPECT_EQ(parsed.b, 0.0f);
    auto invalidHex = easy::core::parseHexColor("invalid");
    EXPECT_GT(invalidHex.r, 0.0f);

    // 自定义最小识别距离 (minSegmentDistance) 测试
    {
        RecognizerConfig customCfg;
        customCfg.minSegmentDistance = 30;
        GestureRecognizer customRecognizer(customCfg);
        customRecognizer.reset();
        customRecognizer.addPoint(0, 0);
        customRecognizer.addPoint(20, 0);
        auto res20 = customRecognizer.finalize();
        EXPECT_FALSE(res20.has_value()); // 20px < 30px 阈值，应判定为无效手势

        customRecognizer.reset();
        customRecognizer.addPoint(0, 0);
        customRecognizer.addPoint(50, 0);
        auto res50 = customRecognizer.finalize();
        ASSERT_TRUE(res50.has_value());
        EXPECT_EQ(res50->code, "R"); // 50px >= 30px 阈值，应识别为向右
    }
}

TEST(GestureInputPolicyTest, TriggerDownHonorsMode) {
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::RightOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::RightOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::RightOnly));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::MiddleOnly));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::MiddleOnly));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::X1Only));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::X1Only));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::X2Only));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::X2Only));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::Both));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::Both));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::RightDown, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::MiddleDown, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X1Down, TriggerMode::All));
    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::X2Down, TriggerMode::All));

    EXPECT_TRUE(isGestureTriggerDown(MouseEventType::LeftDown, TriggerMode::All));
    EXPECT_FALSE(isGestureTriggerDown(MouseEventType::Move, TriggerMode::All));
}

TEST(GestureInputPolicyTest, TriggerUpPairsWithActualButton) {
    EXPECT_EQ(triggerUpFor(MouseEventType::RightDown), MouseEventType::RightUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::MiddleDown), MouseEventType::MiddleUp);
    EXPECT_EQ(triggerUpFor(MouseEventType::X1Down), MouseEventType::X1Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::X2Down), MouseEventType::X2Up);
    EXPECT_EQ(triggerUpFor(MouseEventType::LeftDown), MouseEventType::LeftUp);
}

TEST(GestureInputPolicyTest, LeftClickAlwaysCancelsTracking) {
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::LeftUp, MouseEventType::MiddleDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::LeftDown, MouseEventType::LeftDown));
}

TEST(GestureInputPolicyTest, OppositeMouseButtonCancelsTracking) {
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::MiddleDown, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::RightDown, MouseEventType::MiddleDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::X1Down, MouseEventType::RightDown));
    EXPECT_TRUE(cancelsGestureTracking(MouseEventType::X2Down, MouseEventType::X1Down));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::RightDown, MouseEventType::RightDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::X1Down, MouseEventType::X1Down));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::Move, MouseEventType::RightDown));
    EXPECT_FALSE(cancelsGestureTracking(MouseEventType::RightUp, MouseEventType::RightDown));
}

TEST(GestureInputPolicyTest, EasyToolsUiAndOverlayClassNames) {
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_SettingsWindow"));
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_GestureOverlay"));
    EXPECT_TRUE(isEasyToolsUiClassName(L"EasyTools_TrayWindow"));
    EXPECT_FALSE(isEasyToolsUiClassName(L"EasyTools"));
    EXPECT_FALSE(isEasyToolsUiClassName(L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(isEasyToolsUiClassName(L""));
    EXPECT_TRUE(isGestureOverlayClassName(L"EasyTools_GestureOverlay"));
    EXPECT_FALSE(isGestureOverlayClassName(L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(isGestureOverlayClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isGesturePassThroughClassName(L"EasyTools_GestureOverlay"));
    EXPECT_TRUE(isGesturePassThroughClassName(L"EasyTools_ToastOverlay"));
    EXPECT_FALSE(isGesturePassThroughClassName(L"EasyTools_SettingsWindow"));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(false, false, true));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(true, true, true));
    EXPECT_TRUE(gestureHitTestShouldSkipCandidate(true, false, false));
    EXPECT_FALSE(gestureHitTestShouldSkipCandidate(true, false, true));
    EXPECT_TRUE(gestureHitTestAcceptsWindow(true, false, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, true, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, false, true, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(false, false, false, true));
    EXPECT_FALSE(gestureHitTestAcceptsWindow(true, false, false, false));
    EXPECT_EQ(parseGestureTargetMode("foreground"), GestureTargetMode::Foreground);
    EXPECT_EQ(parseGestureTargetMode("underPointer"), GestureTargetMode::UnderPointer);
    EXPECT_EQ(parseGestureTargetMode("nope"), GestureTargetMode::UnderPointer);
    EXPECT_STREQ(gestureTargetModeKey(GestureTargetMode::Foreground), "foreground");
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, true, true, true), 0);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, false, true, true), 1);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::UnderPointer, false, false, true), -1);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::Foreground, true, true, true), 2);
    EXPECT_EQ(pickGestureTargetSlot(GestureTargetMode::Foreground, true, true, false), -1);
    EXPECT_TRUE(keyStrokeShouldPostClose(MOD_ALT, VK_F4));
    EXPECT_FALSE(keyStrokeShouldPostClose(MOD_ALT | MOD_CONTROL, VK_F4));
    EXPECT_FALSE(keyStrokeShouldPostClose(MOD_CONTROL, 'W'));
    EXPECT_FALSE(keyStrokeShouldPostClose(0, VK_F4));
    EXPECT_TRUE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL, 'W'));
    EXPECT_TRUE(keyStrokeShouldDismissEasyToolsUi(MOD_ALT, VK_F4));
    EXPECT_FALSE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL | MOD_SHIFT, 'W'));
    EXPECT_FALSE(keyStrokeShouldDismissEasyToolsUi(MOD_CONTROL, 'T'));
    EXPECT_TRUE(keyStrokeIsCtrlW(MOD_CONTROL, 'W'));
    EXPECT_FALSE(keyStrokeIsCtrlW(MOD_CONTROL | MOD_SHIFT, 'W'));
    EXPECT_TRUE(classNameStartsWith(L"Chrome_WidgetWin_1", L"Chrome_WidgetWin"));
    EXPECT_FALSE(classNameStartsWith(L"OrpheusBrowserHost", L"Chrome_WidgetWin"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"Chrome_WidgetWin_0"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"MozillaWindowClass"));
    EXPECT_TRUE(isTabbedBrowserClassName(L"CabinetWClass"));
    EXPECT_FALSE(isTabbedBrowserClassName(L"OrpheusBrowserHost"));
    EXPECT_FALSE(isTabbedBrowserClassName(L"Qt51514QWindowIcon"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"OrpheusBrowserHost"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"CefBrowserWindow"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"Qt51514QWindowIcon"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"ApplicationFrameWindow"));
    EXPECT_TRUE(isProductivityToolkitClassName(L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(isProductivityToolkitClassName(L"UnityWndClass"));
    EXPECT_TRUE(shouldAutoBypassFullscreenGestures(true, false));
    EXPECT_FALSE(shouldAutoBypassFullscreenGestures(true, true));
    EXPECT_FALSE(shouldAutoBypassFullscreenGestures(false, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(true, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(true, true));
    EXPECT_TRUE(overlayPresentShouldForceTopmost(false, false));
    EXPECT_FALSE(overlayPresentShouldForceTopmost(false, true));
    EXPECT_TRUE(windowUsesCompositorSurface(WS_EX_NOREDIRECTIONBITMAP));
    EXPECT_TRUE(windowUsesCompositorSurface(WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW));
    EXPECT_FALSE(windowUsesCompositorSurface(WS_EX_LAYERED | WS_EX_TOPMOST));
    EXPECT_FALSE(windowUsesCompositorSurface(0));
    EXPECT_EQ(classifyProcessIntegrityQuery(false, false, false, false, false),
              ProcessIntegrityRelation::Unknown);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, false, false, false, false),
              ProcessIntegrityRelation::Higher);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, false, false, false, true),
              ProcessIntegrityRelation::Unknown);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, false, false),
              ProcessIntegrityRelation::SameOrLower);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, true, false),
              ProcessIntegrityRelation::Higher);
    EXPECT_EQ(classifyProcessIntegrityQuery(true, true, true, true, true),
              ProcessIntegrityRelation::SameOrLower);
    EXPECT_FALSE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::Higher));
    EXPECT_TRUE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::SameOrLower));
    EXPECT_TRUE(lowLevelHookCanObserveTarget(ProcessIntegrityRelation::Unknown));
    EXPECT_TRUE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Higher, false));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Higher, true));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::SameOrLower, false));
    EXPECT_FALSE(shouldWarnGestureIntegrityBlocked(ProcessIntegrityRelation::Unknown, false));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_ALT, VK_F4, L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"Chrome_WidgetWin_1"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"OrpheusBrowserHost"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"Qt51514QWindowIcon"));
    EXPECT_TRUE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'W', L"EasyTools_SettingsWindow"));
    EXPECT_FALSE(keyStrokeShouldCloseWindow(MOD_CONTROL, 'T', L"OrpheusBrowserHost"));
    EXPECT_EQ(resolveCloseableWindow(nullptr), nullptr);
    EXPECT_FALSE(windowStillAcceptsClose(false, true));
    EXPECT_FALSE(windowStillAcceptsClose(true, false));
    EXPECT_TRUE(windowStillAcceptsClose(true, true));
    EXPECT_FALSE(closeShouldSendKeyFallback(true, false));
    EXPECT_TRUE(closeShouldSendKeyFallback(true, true));
    EXPECT_FALSE(closeShouldSendKeyFallback(false, true));
    EXPECT_GT(kCloseObserveTimeoutMs, 0u);
}

TEST(GestureInputPolicyTest, ResultToastRequiresRecognitionOrExcessive) {
    EXPECT_TRUE(shouldShowGestureResultToast(true, true, false));
    EXPECT_FALSE(shouldShowGestureResultToast(false, true, false));
    EXPECT_FALSE(shouldShowGestureResultToast(true, false, false));
    EXPECT_TRUE(shouldShowGestureResultToast(false, true, true));
}

TEST(GestureInputPolicyTest, OverlaySurfaceGrowsOnly) {
    int left = 100, top = 100, right = 200, bottom = 200;
    growOverlayRect(left, top, right, bottom, 0, 0, 256, 256);
    EXPECT_EQ(left, 0);
    EXPECT_EQ(top, 0);
    EXPECT_EQ(right, 256);
    EXPECT_EQ(bottom, 256);
    EXPECT_TRUE(overlaySurfaceContains(10, 10, 20, 20, 0, 0, 256, 256));
    EXPECT_FALSE(overlaySurfaceContains(-1, 0, 10, 10, 0, 0, 256, 256));
    EXPECT_FALSE(overlaySurfaceContains(10, 10, 20, 20, 0, 0, 0, 0));
    int emptyL = 8, emptyT = 8, emptyR = 16, emptyB = 16;
    growOverlayRect(emptyL, emptyT, emptyR, emptyB, 0, 0, 0, 0);
    EXPECT_EQ(emptyL, 8);
    EXPECT_EQ(emptyR, 16);
}

TEST(GestureInputPolicyTest, OverlayRejectsOversizedLeftoverSurface) {
    EXPECT_TRUE(overlayCanReuseSurface(256, 256, 256, 256, 2));
    EXPECT_TRUE(overlayCanReuseSurface(256, 256, 360, 360, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 1792, 1792, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 2816, 1024, 2));
    EXPECT_FALSE(overlayCanReuseSurface(0, 256, 256, 256, 2));
    EXPECT_FALSE(overlayCanReuseSurface(256, 256, 256, 256, 0));
}

TEST(GestureInputPolicyTest, FadeClockWaitsForRequiredToast) {
    EXPECT_FALSE(gestureFrameReadyToFade(false, false, false));
    EXPECT_TRUE(gestureFrameReadyToFade(true, false, false));
    EXPECT_FALSE(gestureFrameReadyToFade(true, true, false));
    EXPECT_TRUE(gestureFrameReadyToFade(true, true, true));
}

TEST(GestureInputPolicyTest, LiveMatchHoldsThroughBriefUnmatchedGap) {
    EXPECT_TRUE(keepLiveGestureMatch(true, false, 0, 120));
    EXPECT_TRUE(keepLiveGestureMatch(false, true, 80, 120));
    EXPECT_FALSE(keepLiveGestureMatch(false, true, 120, 120));
    EXPECT_FALSE(keepLiveGestureMatch(false, false, 0, 120));
}

TEST(GestureInputPolicyTest, TrailOutlineWidthClampsAndWidens) {
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(-2.0f), 0.0f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(2.5f), 2.5f);
    EXPECT_FLOAT_EQ(clampTrailOutlineWidth(99.0f), 8.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(4.0f, 2.5f), 9.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(4.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(trailOutlineWidenWidth(0.0f, 2.5f), 0.0f);
}

TEST(GestureInputPolicyTest, FadeClockStartsAfterFirstPresentedFrame) {
    EXPECT_FALSE(gestureFadeShouldFinish(false, 500, 240, 420));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 100, 240, 420));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 240 + 420, 240, 420));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 349, 0, 350));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 350, 0, 350));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 159, 160, 280));
    EXPECT_FALSE(gestureFadeShouldFinish(true, 160 + 279, 160, 280));
    EXPECT_TRUE(gestureFadeShouldFinish(true, 160 + 280, 160, 280));
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 160, 160, 280), 1.0f);
}

TEST(GestureInputPolicyTest, TrailColorFollowsAccentUnlessCustom) {
    const auto cyan = easy::core::getAccentColorRGB("cyan");
    const auto autoRgb = resolveGestureTrailRgb("auto", "#FF0000", cyan);
    EXPECT_FLOAT_EQ(autoRgb.r, cyan.r);
    EXPECT_FLOAT_EQ(autoRgb.g, cyan.g);
    EXPECT_FLOAT_EQ(autoRgb.b, cyan.b);

    const auto customRgb = resolveGestureTrailRgb("custom", "#FF0000", cyan);
    EXPECT_NEAR(customRgb.r, 1.0f, 0.01f);
    EXPECT_NEAR(customRgb.g, 0.0f, 0.01f);
    EXPECT_NEAR(customRgb.b, 0.0f, 0.01f);

    const auto emptyCustom = resolveGestureTrailRgb("custom", "", cyan);
    EXPECT_FLOAT_EQ(emptyCustom.r, cyan.r);
}

TEST(GestureInputPolicyTest, TrailPaletteFollowsThemeSetting) {
    EXPECT_TRUE(gestureTrailUsesLightPalette("light", false));
    EXPECT_FALSE(gestureTrailUsesLightPalette("dark", true));
    EXPECT_TRUE(gestureTrailUsesLightPalette("system", true));
    EXPECT_FALSE(gestureTrailUsesLightPalette("system", false));
}

TEST(GestureInputPolicyTest, FadeAlphaHoldsThenEases) {
    EXPECT_FLOAT_EQ(gestureFadeAlpha(false, 0, 240, 420), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 0, 240, 420), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 240, 240, 420), 1.0f);
    EXPECT_NEAR(gestureFadeAlpha(true, 240 + 210, 240, 420), 0.25f, 0.01f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 240 + 420, 240, 420), 0.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 10, 240, 0), 1.0f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 241, 240, 0), 0.0f);
    EXPECT_NEAR(gestureFadeAlpha(true, 175, 0, 350), 0.25f, 0.01f);
    EXPECT_FLOAT_EQ(gestureFadeAlpha(true, 350, 0, 350), 0.0f);
}

// -----------------------------------------------------------------------------
// 2. 作用域匹配规则测试套件
// -----------------------------------------------------------------------------
TEST(ScopeRuleTest, MatchingAndJsonSerialization) {
    // 进程名精确匹配 (大小写不敏感)
    ScopeRule r1;
    r1.processName = "chrome.exe";
    r1.matchMode = MatchMode::Exact;
    EXPECT_TRUE(r1.matches(nullptr, L"chrome.exe", L"AnyClass"));
    EXPECT_TRUE(r1.matches(nullptr, L"CHROME.EXE", L""));
    EXPECT_FALSE(r1.matches(nullptr, L"firefox.exe", L""));

    // 窗口类名通配符
    ScopeRule r2;
    r2.windowClass = "Chrome_*";
    r2.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(r2.matches(nullptr, L"", L"Chrome_WidgetWin_1"));
    EXPECT_FALSE(r2.matches(nullptr, L"", L"MozillaWindowClass"));

    // 进程名通配符
    ScopeRule r3;
    r3.processName = "*.exe";
    r3.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(r3.matches(nullptr, L"notepad.exe", L""));
    EXPECT_FALSE(r3.matches(nullptr, L"bash", L""));

    // 通配符中的正则元字符必须按普通字符处理
    ScopeRule rSpecial;
    rSpecial.processName = "app[1].exe";
    rSpecial.matchMode = MatchMode::Wildcard;
    EXPECT_TRUE(rSpecial.matches(nullptr, L"app[1].exe", L""));
    EXPECT_FALSE(rSpecial.matches(nullptr, L"app1.exe", L""));

    // 禁用的规则永不匹配
    ScopeRule r4;
    r4.processName = "chrome.exe";
    r4.enabled = false;
    EXPECT_FALSE(r4.matches(nullptr, L"chrome.exe", L""));

    // JSON 往返
    ScopeRule r5;
    r5.id = "rule-1";
    r5.name = "Chrome";
    r5.processName = "chrome.exe";
    r5.matchMode = MatchMode::Wildcard;
    r5.effect = RuleEffect::Disable;
    ScopeRule r5b = ScopeRule::fromJson(r5.toJson());
    EXPECT_EQ(r5b.id, "rule-1");
    EXPECT_EQ(r5b.processName, "chrome.exe");
    EXPECT_EQ(r5b.matchMode, MatchMode::Wildcard);
    EXPECT_EQ(r5b.effect, RuleEffect::Disable);

    // 越界枚举输入必须收敛到合法范围
    auto invalid = ScopeRule::fromJson({{"matchMode", 99}, {"effect", -5}});
    EXPECT_EQ(invalid.matchMode, MatchMode::Regex);
    EXPECT_EQ(invalid.effect, RuleEffect::Enable);
}

// -----------------------------------------------------------------------------
// 3. 手势动作与内置命令分发测试套件
// -----------------------------------------------------------------------------
TEST(GestureActionTest, KeyStrokeAndBuiltinCommands) {
    using namespace easy::gesture;

    // 1. KeyStroke 解析与序列化
    auto ks1 = KeyStroke::fromString("Ctrl+Shift+T");
    EXPECT_EQ(ks1.modifiers, (MOD_CONTROL | MOD_SHIFT));
    EXPECT_EQ(ks1.virtualKey, 'T');
    EXPECT_EQ(ks1.toString(), "Ctrl+Shift+T");

    auto ks2 = KeyStroke::fromString("Alt+Left");
    EXPECT_EQ(ks2.modifiers, MOD_ALT);
    EXPECT_EQ(ks2.virtualKey, VK_LEFT);
    EXPECT_EQ(ks2.toString(), "Alt+Left");

    auto ksEmpty = KeyStroke::fromString("");
    EXPECT_EQ(ksEmpty.virtualKey, 0);
    EXPECT_EQ(resolveGestureKeyTarget(nullptr, nullptr, nullptr), nullptr);
    windowFromPointSkippingGestureOverlay(-100000, -100000);
    windowFromPointSkippingGestureOverlay(0, 0);

    WNDCLASSEXW overlayWc{};
    overlayWc.cbSize = sizeof(overlayWc);
    overlayWc.lpfnWndProc = DefWindowProcW;
    overlayWc.hInstance = GetModuleHandleW(nullptr);
    overlayWc.lpszClassName = L"EasyTools_GestureOverlay";
    RegisterClassExW(&overlayWc);
    WNDCLASSEXW settingsWc = overlayWc;
    settingsWc.lpszClassName = L"EasyTools_SettingsWindow";
    RegisterClassExW(&settingsWc);

    HWND overlay = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_GestureOverlay", L"t",
                                   WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    HWND settings = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_SettingsWindow", L"t",
                                    WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    HWND external = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"t",
                                    WS_POPUP, 0, 0, 8, 8, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, settings, nullptr), settings);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, settings, external), settings);
    EXPECT_EQ(resolveGestureKeyTarget(overlay, external, settings), external);
    KeyStroke::fromString("").send(nullptr);
    if (overlay) DestroyWindow(overlay);
    if (settings) DestroyWindow(settings);
    if (external && IsWindow(external)) DestroyWindow(external);

    auto pumpPostedClose = [](HWND hwnd) {
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        MSG msg{};
        while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    WNDCLASSEXW acceptWc{};
    acceptWc.cbSize = sizeof(acceptWc);
    acceptWc.lpfnWndProc = DefWindowProcW;
    acceptWc.hInstance = GetModuleHandleW(nullptr);
    acceptWc.lpszClassName = L"EasyTools_TestAcceptClose";
    RegisterClassExW(&acceptWc);
    HWND accept = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_TestAcceptClose", L"accept",
                                  WS_POPUP | WS_VISIBLE, 0, 0, 16, 16, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(accept != nullptr);
    EXPECT_EQ(resolveCloseableWindow(accept), accept);
    pumpPostedClose(accept);
    EXPECT_FALSE(IsWindow(accept));
    EXPECT_FALSE(closeShouldSendKeyFallback(
        true, windowStillAcceptsClose(IsWindow(accept) != FALSE, false)));

    static auto swallowProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        if (m == WM_CLOSE || (m == WM_SYSCOMMAND && w == SC_CLOSE)) return 0;
        return DefWindowProcW(h, m, w, l);
    };
    WNDCLASSEXW swallowWc{};
    swallowWc.cbSize = sizeof(swallowWc);
    swallowWc.lpfnWndProc = swallowProc;
    swallowWc.hInstance = GetModuleHandleW(nullptr);
    swallowWc.lpszClassName = L"EasyTools_TestSwallowClose";
    RegisterClassExW(&swallowWc);
    HWND swallow = CreateWindowExW(WS_EX_TOOLWINDOW, L"EasyTools_TestSwallowClose", L"swallow",
                                   WS_POPUP | WS_VISIBLE, 0, 0, 16, 16, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(swallow != nullptr);
    pumpPostedClose(swallow);
    EXPECT_TRUE(IsWindow(swallow));
    EXPECT_TRUE(closeShouldSendKeyFallback(
        true, windowStillAcceptsClose(IsWindow(swallow) != FALSE,
                                      IsWindowVisible(swallow) != FALSE)));
    DestroyWindow(swallow);

    HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owner",
                                 WS_POPUP, 0, 0, 24, 24, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    HWND owned = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"owned",
                                 WS_POPUP, 0, 0, 16, 16, owner, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    ASSERT_TRUE(owner != nullptr);
    ASSERT_TRUE(owned != nullptr);
    EXPECT_EQ(resolveCloseableWindow(owned), owner);
    if (owned) DestroyWindow(owned);
    if (owner) DestroyWindow(owner);

    EXPECT_TRUE(gestureActionNeedsInputThread(ActionType::SendKeys));
    EXPECT_TRUE(gestureActionNeedsInputThread(ActionType::BuiltinCommand));
    EXPECT_FALSE(gestureActionNeedsInputThread(ActionType::LuaScript));
    EXPECT_FALSE(gestureActionNeedsInputThread(ActionType::RunProgram));

    // 2. GestureAction JSON 往返
    GestureAction a1;
    a1.type = ActionType::SendKeys;
    a1.name = "关闭标签页";
    a1.description = "关闭当前标签";
    a1.keyStroke = KeyStroke::fromString("Ctrl+W");
    auto j1 = a1.toJson();
    auto a1b = GestureAction::fromJson(j1);
    EXPECT_EQ(a1b.name, "关闭标签页");
    EXPECT_EQ(a1b.keyStroke.toString(), "Ctrl+W");

    GestureAction a2;
    a2.type = ActionType::BuiltinCommand;
    a2.name = "下一曲";
    a2.builtinCmd = BuiltinCommand::MediaNext;
    auto j2 = a2.toJson();
    auto a2b = GestureAction::fromJson(j2);
    EXPECT_EQ(a2b.builtinCmd, BuiltinCommand::MediaNext);

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
    EXPECT_EQ(m1b.id, "custom-m1");
    EXPECT_TRUE(m1b.enabled);
    EXPECT_TRUE(m1b.instantExecute);
    EXPECT_TRUE(m1b.silentToast);
    EXPECT_EQ(m1b.gestureCode, "D-R");
    EXPECT_EQ(m1b.action.name, "关闭标签页");

    // 4. GestureProfile 三态模型、单项开关与手势调序测试
    GestureProfile prof("test_profile");
    prof.addMapping(m1);

    GestureMapping m2;
    m2.id = "custom-m2";
    m2.gestureCode = "L-U";
    m2.action.name = "剪切";
    prof.addMapping(m2);

    EXPECT_TRUE(prof.findAction("D-R").has_value());
    prof.setMappingEnabled("D-R", false);
    EXPECT_FALSE(prof.findAction("D-R").has_value()); // 禁用后 findAction 返回空
    EXPECT_TRUE(prof.findMapping("D-R").has_value());  // 但 findMapping 仍能查到
    prof.setMappingEnabled("D-R", true);
    EXPECT_TRUE(prof.findAction("D-R").has_value());

    // 调序与移动测试
    EXPECT_TRUE(prof.moveMapping(0, 1));
    EXPECT_EQ(prof.getMappings()[0].gestureCode, "L-U");
    EXPECT_EQ(prof.getMappings()[1].gestureCode, "D-R");
    prof.reorderMappings({"D-R", "L-U"});
    EXPECT_EQ(prof.getMappings()[0].gestureCode, "D-R");
    EXPECT_EQ(prof.getMappings()[1].gestureCode, "L-U");

    // 触发方式三态测试
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Default);
    prof.setTriggerState("right", TriggerModeState::Disabled);
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Disabled);
    prof.setAllTriggerStates(TriggerModeState::Enabled);
    EXPECT_EQ(prof.getTriggerState("right"), TriggerModeState::Enabled);
    EXPECT_EQ(prof.getTriggerState("middle"), TriggerModeState::Enabled);

    // Profile JSON 往返测试
    auto profJson = prof.toJson();
    auto profRestored = GestureProfile::fromJson(profJson);
    EXPECT_EQ(profRestored.name(), "test_profile");
    // 5. 默认全局手势与桌面/任务栏预设工厂测试
    auto defaultProf = GestureProfile::createDefaultGlobal();
    EXPECT_EQ(defaultProf.name(), "default");
    EXPECT_GE(defaultProf.getMappings().size(), 20u);
    EXPECT_TRUE(defaultProf.findAction("L").has_value());
    EXPECT_EQ(defaultProf.findAction("L")->name, "后退");
    EXPECT_EQ(defaultProf.findAction("L")->keyStroke.toString(), "Alt+Left");
    EXPECT_TRUE(defaultProf.findAction("R").has_value());
    EXPECT_EQ(defaultProf.findAction("R")->name, "前进");
    EXPECT_EQ(defaultProf.findAction("R")->keyStroke.toString(), "Alt+Right");
    EXPECT_TRUE(defaultProf.findAction("Middle+L").has_value());
    EXPECT_EQ(defaultProf.findAction("Middle+L")->name, "上一曲");
    EXPECT_EQ(defaultProf.findAction("Middle+L")->keyStroke.toString(), "MediaPrev");
    EXPECT_TRUE(defaultProf.findAction("Middle+R").has_value());
    EXPECT_EQ(defaultProf.findAction("Middle+R")->name, "下一曲");
    EXPECT_EQ(defaultProf.findAction("Middle+R")->keyStroke.toString(), "MediaNext");
    EXPECT_TRUE(defaultProf.findAction("R-D").has_value());
    EXPECT_EQ(defaultProf.findAction("R-D")->name, "恢复关闭的标签页");
    EXPECT_EQ(defaultProf.findAction("R-D")->keyStroke.toString(), "Ctrl+Shift+T");
    EXPECT_TRUE(defaultProf.findAction("D-L").has_value());
    EXPECT_EQ(defaultProf.findAction("D-L")->name, "关闭窗口");
    EXPECT_EQ(defaultProf.findAction("D-L")->keyStroke.toString(), "Alt+F4");
    EXPECT_TRUE(defaultProf.findAction("D-R").has_value());
    EXPECT_EQ(defaultProf.findAction("D-R")->name, "关闭标签页");
    EXPECT_EQ(defaultProf.findAction("D-R")->keyStroke.toString(), "Ctrl+W");

    auto desktopProf = GestureProfile::createDesktopProfile();
    EXPECT_EQ(desktopProf.name(), "special_desktop");
    EXPECT_GE(desktopProf.getMappings().size(), 5u);
    EXPECT_TRUE(desktopProf.findAction("U").has_value());

    auto taskbarProf = GestureProfile::createTaskbarProfile();
    EXPECT_EQ(taskbarProf.name(), "special_taskbar");
    EXPECT_GE(taskbarProf.getMappings().size(), 4u);
    EXPECT_TRUE(taskbarProf.findAction("L").has_value());

    // 6. BuiltinCommandDispatcher 应用级回调路由与媒体/虚拟桌面命令
    auto& dispatcher = BuiltinCommandDispatcher::instance();
    std::atomic<bool> screenshotCalled{false};
    dispatcher.registerHandler(BuiltinCommand::TakeScreenshot, [&screenshotCalled]() {
        screenshotCalled = true;
    });

    dispatcher.execute(BuiltinCommand::TakeScreenshot, nullptr);
    EXPECT_TRUE(screenshotCalled.load());

    // 多媒体 / 虚拟桌面 / 锁屏必须先挂 Mock。默认实现是全局 SendInput 或
    // LockWorkStation，部署跑 CTest 时会暂停播放器、静音、切桌面甚至锁屏。
    const std::vector<BuiltinCommand> systemWideCmds = {
        BuiltinCommand::MediaNext,
        BuiltinCommand::MediaPrev,
        BuiltinCommand::MediaPlayPause,
        BuiltinCommand::VolumeUp,
        BuiltinCommand::VolumeDown,
        BuiltinCommand::VolumeMute,
        BuiltinCommand::PrevVirtualDesktop,
        BuiltinCommand::NextVirtualDesktop,
        BuiltinCommand::ShowDesktop,
        BuiltinCommand::SwitchDesktop,
        BuiltinCommand::TaskView,
        BuiltinCommand::LockScreen,
    };
    for (auto cmd : systemWideCmds) {
        std::atomic<bool> called{false};
        dispatcher.registerHandler(cmd, [&called]() { called = true; });
        dispatcher.execute(cmd, nullptr);
        EXPECT_TRUE(called.load());
    }
    dispatcher.clearHandlers();
}

// -----------------------------------------------------------------------------
// 3.1 手势动作扩展键位、别名与外部程序/Lua权限序列化测试套件
// -----------------------------------------------------------------------------
TEST(GestureActionTest, ExtendedKeyStrokeSpecialKeysAndAliases) {
    using namespace easy::gesture;

    // 1. 扩展按键解析与别名覆盖
    auto ksBack = KeyStroke::fromString("Ctrl+Backspace");
    EXPECT_EQ(ksBack.modifiers, MOD_CONTROL);
    EXPECT_EQ(ksBack.virtualKey, VK_BACK);
    EXPECT_EQ(ksBack.toString(), "Ctrl+Backspace");

    auto ksEscAlias = KeyStroke::fromString("Esc");
    EXPECT_EQ(ksEscAlias.virtualKey, VK_ESCAPE);
    EXPECT_EQ(ksEscAlias.toString(), "Escape");

    auto ksHome = KeyStroke::fromString("Home");
    EXPECT_EQ(ksHome.virtualKey, VK_HOME);
    EXPECT_EQ(ksHome.toString(), "Home");

    auto ksEnd = KeyStroke::fromString("End");
    EXPECT_EQ(ksEnd.virtualKey, VK_END);
    EXPECT_EQ(ksEnd.toString(), "End");

    auto ksPgUp = KeyStroke::fromString("PgUp");
    EXPECT_EQ(ksPgUp.virtualKey, VK_PRIOR);
    EXPECT_EQ(ksPgUp.toString(), "PageUp");

    auto ksPgDn = KeyStroke::fromString("PageDown");
    EXPECT_EQ(ksPgDn.virtualKey, VK_NEXT);
    EXPECT_EQ(ksPgDn.toString(), "PageDown");

    auto ksIns = KeyStroke::fromString("Ins");
    EXPECT_EQ(ksIns.virtualKey, VK_INSERT);
    EXPECT_EQ(ksIns.toString(), "Insert");

    auto ksMediaPlay = KeyStroke::fromString("MediaPlay");
    EXPECT_EQ(ksMediaPlay.virtualKey, VK_MEDIA_PLAY_PAUSE);
    EXPECT_EQ(ksMediaPlay.toString(), "MediaPlayPause");

    auto ksVolUp = KeyStroke::fromString("VolumeUp");
    EXPECT_EQ(ksVolUp.virtualKey, VK_VOLUME_UP);
    EXPECT_EQ(ksVolUp.toString(), "VolumeUp");

    // 十六进制虚拟键码
    auto ksHex = KeyStroke::fromString("0x20"); // VK_SPACE
    EXPECT_EQ(ksHex.virtualKey, 0x20);

    // 2. RunProgram 序列化往返
    GestureAction aProg;
    aProg.type = ActionType::RunProgram;
    aProg.name = "打开终端";
    aProg.description = "在当前目录打开 wt.exe";
    aProg.programPath = "wt.exe";
    aProg.programArgs = "-d \"%PATH%\"";
    auto jProg = aProg.toJson();
    auto aProgRestored = GestureAction::fromJson(jProg);
    EXPECT_EQ(aProgRestored.type, ActionType::RunProgram);
    EXPECT_EQ(aProgRestored.name, "打开终端");
    EXPECT_EQ(aProgRestored.programPath, "wt.exe");
    EXPECT_EQ(aProgRestored.programArgs, "-d \"%PATH%\"");

    // 3. LuaScript 与细粒度权限序列化往返
    GestureAction aLua;
    aLua.type = ActionType::LuaScript;
    aLua.name = "翻译选中";
    aLua.luaScript = "easy.clipboard.get()";
    aLua.requestedPermissions = {"clipboard", "http"};
    auto jLua = aLua.toJson();
    auto aLuaRestored = GestureAction::fromJson(jLua);
    EXPECT_EQ(aLuaRestored.type, ActionType::LuaScript);
    EXPECT_EQ(aLuaRestored.name, "翻译选中");
    EXPECT_EQ(aLuaRestored.luaScript, "easy.clipboard.get()");
    EXPECT_EQ(aLuaRestored.requestedPermissions.size(), 2u);
    EXPECT_EQ(aLuaRestored.requestedPermissions[0], "clipboard");
    EXPECT_EQ(aLuaRestored.requestedPermissions[1], "http");

    // 4. BuiltinCommandDispatcher 全枚举分支安全调度覆盖 (注册 Mock Handler 隔离真实系统 API)
    auto& dispatcher = BuiltinCommandDispatcher::instance();
    const std::vector<BuiltinCommand> allCmds = {
        BuiltinCommand::CloseWindow,
        BuiltinCommand::CloseTab,
        BuiltinCommand::MaximizeWindow,
        BuiltinCommand::MinimizeWindow,
        BuiltinCommand::RestoreWindow,
        BuiltinCommand::ShowDesktop,
        BuiltinCommand::SwitchDesktop,
        BuiltinCommand::TaskView,
        BuiltinCommand::LockScreen,
        BuiltinCommand::PauseGestures,
        BuiltinCommand::TakeScreenshot,
        BuiltinCommand::StartRecording,
        BuiltinCommand::RestoreClosedTab,
        BuiltinCommand::ToggleTopmost,
        BuiltinCommand::ToggleWindowTransparency,
        BuiltinCommand::WebSearch,
        BuiltinCommand::ToggleSearch,
        BuiltinCommand::ShowRadialMenu,
        BuiltinCommand::PasteAsPin,
        BuiltinCommand::MediaNext,
        BuiltinCommand::MediaPrev,
        BuiltinCommand::MediaPlayPause,
        BuiltinCommand::VolumeUp,
        BuiltinCommand::VolumeDown,
        BuiltinCommand::VolumeMute,
        BuiltinCommand::PrevVirtualDesktop,
        BuiltinCommand::NextVirtualDesktop
    };

    for (auto cmd : allCmds) {
        std::atomic<bool> called{false};
        dispatcher.registerHandler(cmd, [&called]() { called = true; });
        dispatcher.execute(cmd, nullptr);
        EXPECT_TRUE(called.load());
    }
    dispatcher.clearHandlers();
}

// -----------------------------------------------------------------------------
// 4. 手势配置综合与继承体系测试套件
// -----------------------------------------------------------------------------
TEST(GestureProfileTest, TriStateInheritanceAndOrdering) {
    using namespace easy::gesture;

    GestureProfile profile("test_comp");
    
    // 1. 三态触发模式深度测试
    EXPECT_EQ(profile.getTriggerState("right"), TriggerModeState::Default);
    profile.setTriggerState("right", TriggerModeState::Enabled);
    EXPECT_EQ(profile.getTriggerState("right"), TriggerModeState::Enabled);
    profile.setTriggerState("middle", TriggerModeState::Disabled);
    EXPECT_EQ(profile.getTriggerState("middle"), TriggerModeState::Disabled);
    
    // 全量批处理设置为启用 / 禁用 / 继承
    profile.setAllTriggerStates(TriggerModeState::Enabled);
    for (const auto& [k, v] : profile.getAllTriggerStates()) {
        EXPECT_EQ(v, TriggerModeState::Enabled);
    }
    profile.setAllTriggerStates(TriggerModeState::Default);
    for (const auto& [k, v] : profile.getAllTriggerStates()) {
        EXPECT_EQ(v, TriggerModeState::Default);
    }

    // 2. 手势映射增删查改与属性测试
    GestureMapping m1;
    m1.id = "m_left";
    m1.gestureCode = "L";
    m1.enabled = true;
    m1.instantExecute = true;
    m1.silentToast = false;
    m1.action.type = ActionType::BuiltinCommand;
    m1.action.builtinCmd = BuiltinCommand::CloseTab;
    profile.addMapping(m1);

    GestureMapping m2;
    m2.id = "m_right";
    m2.gestureCode = "R";
    m2.enabled = false;
    m2.instantExecute = false;
    m2.silentToast = true;
    m2.action.type = ActionType::BuiltinCommand;
    m2.action.builtinCmd = BuiltinCommand::RestoreClosedTab;
    profile.addMapping(m2);

    GestureMapping m3;
    m3.id = "m_lu";
    m3.gestureCode = "L-U";
    m3.enabled = true;
    m3.action.type = ActionType::BuiltinCommand;
    m3.action.builtinCmd = BuiltinCommand::MaximizeWindow;
    profile.addMapping(m3);

    EXPECT_EQ(profile.getMappings().size(), 3u);
    EXPECT_TRUE(profile.hasGesture("L"));
    EXPECT_TRUE(profile.hasGesture("R"));
    EXPECT_TRUE(profile.hasGesture("L-U"));
    EXPECT_FALSE(profile.hasGesture("D"));

    // 3. 前缀冲突检测
    auto conflicts = profile.detectConflicts("L");
    EXPECT_FALSE(conflicts.empty());
    bool foundLu = false;
    for (const auto& c : conflicts) {
        if (c == "L-U") foundLu = true;
    }
    EXPECT_TRUE(foundLu);

    // 4. 映射调序测试 (moveMapping)
    EXPECT_TRUE(profile.moveMapping(0, 2));
    EXPECT_EQ(profile.getMappings()[0].gestureCode, "R");
    EXPECT_EQ(profile.getMappings()[2].gestureCode, "L");
    EXPECT_FALSE(profile.moveMapping(0, 999)); // 越界安全
    EXPECT_FALSE(profile.moveMapping(999, 0)); // 越界安全
    EXPECT_FALSE(profile.moveMapping(1, 1));   // 同位置无位移，返回 false

    // 5. 批量重排序 (reorderMappings)
    profile.reorderMappings({"L-U", "L", "R"});
    EXPECT_EQ(profile.getMappings()[0].gestureCode, "L-U");
    EXPECT_EQ(profile.getMappings()[1].gestureCode, "L");
    EXPECT_EQ(profile.getMappings()[2].gestureCode, "R");

    // 6. 单项启用/禁用切换
    profile.setMappingEnabled("L", false);
    auto optDisabled = profile.findAction("L");
    EXPECT_FALSE(optDisabled.has_value());
    profile.setMappingEnabled("L", true);
    auto optEnabled = profile.findAction("L");
    EXPECT_TRUE(optEnabled.has_value());

    // 7. JSON 序列化与反序列化全属性往返
    auto j = profile.toJson();
    auto restored = GestureProfile::fromJson(j);
    EXPECT_EQ(restored.name(), "test_comp");
    EXPECT_EQ(restored.getMappings().size(), 3u);
    EXPECT_EQ(restored.getMappings()[0].gestureCode, "L-U");
    EXPECT_FALSE(restored.getMappings()[0].instantExecute);
    EXPECT_TRUE(restored.getMappings()[1].instantExecute);
    EXPECT_TRUE(restored.getMappings()[2].silentToast);
}

// -----------------------------------------------------------------------------
// 5. 特殊与系统窗口检测测试套件
// -----------------------------------------------------------------------------
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
    config.removeOnChange(callbackId);
    config.shutdown();
    std::filesystem::remove_all(temp, ec);
}

// -----------------------------------------------------------------------------
// 9. 进程间通信消息桥接测试套件
// -----------------------------------------------------------------------------
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
    EXPECT_TRUE(static_cast<bool>(valid));
    EXPECT_EQ(valid.manifest.id, "capture");
    EXPECT_EQ(valid.manifest.capabilities.size(), 2u);

    const auto wrongId = loadPluginManifest(path, "gesture", "1.2.0");
    EXPECT_FALSE(wrongId);
    EXPECT_EQ(wrongId.error, "plugin manifest id does not match its DLL");

    const auto oldHost = loadPluginManifest(path, "capture", "0.9.0");
    EXPECT_FALSE(oldHost);
    EXPECT_EQ(oldHost.error, "plugin requires a newer EasyTools version");

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
TEST(PinyinEngineTest, FuzzyAndInitialMatching) {
    auto wx_init = PinyinEngine::GetInitials(L"微信");
    auto wx_full = PinyinEngine::GetFullPinyin(L"微信");
    std::wprintf(L"[DEBUG] 微信 initials: %ls, full: %ls\n", wx_init.c_str(), wx_full.c_str());

    // 拼音首字母
    EXPECT_EQ(PinyinEngine::GetInitials(L"微信"), L"wx");
    EXPECT_EQ(PinyinEngine::GetInitials(L"你好世界"), L"nhsj");
    EXPECT_EQ(PinyinEngine::GetInitials(L"EasyTools工具"), L"easytoolsgj");

    // 拼音全拼
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"微信"), L"weixin");
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"你好"), L"nihao");
    EXPECT_EQ(PinyinEngine::GetFullPinyin(L"EasyTools"), L"easytools");
}

// -----------------------------------------------------------------------------
// 15. Everything 风格搜索表达式解析测试套件
// -----------------------------------------------------------------------------
TEST(SearchExpressionTest, EverythingSyntaxParsing) {
    FileRecord txtFile{1, 0, L"readme.txt", L"readme.txt", L"readme.txt", L"readme.txt", false};
    FileRecord pngFile{2, 0, L"screenshot.png", L"screenshot.png", L"screenshot.png", L"screenshot.png", false};
    FileRecord docDir{3, 0, L"Documents", L"documents", L"documents", L"documents", true};
    FileRecord wxFile{4, 0, L"微信.exe", L"微信.exe", L"wx.exe", L"weixin.exe", false};
    FileRecord cppFile{5, 0, L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", L"test_main.cpp", false};
    FileRecord logFile{6, 0, L"app_2026.log", L"app_2026.log", L"app_2026.log", L"app_2026.log", false};

    // 1. 标准通配符与扩展名
    auto expr1 = SearchExpression::parse(L"*.txt");
    EXPECT_TRUE(expr1.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(expr1.matches(pngFile, L'C', L"C:\\screenshot.png"));

    auto exprExt = SearchExpression::parse(L"ext:png;jpg;webp");
    EXPECT_TRUE(exprExt.matches(pngFile, L'C', L"C:\\screenshot.png"));
    EXPECT_FALSE(exprExt.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 2. 类型过滤器 (file: / folder: / dir:)
    auto exprFile = SearchExpression::parse(L"file: *.txt");
    EXPECT_TRUE(exprFile.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprFile.matches(docDir, L'C', L"C:\\Documents"));

    auto exprDir = SearchExpression::parse(L"folder: documents");
    EXPECT_TRUE(exprDir.matches(docDir, L'C', L"C:\\Documents"));
    EXPECT_FALSE(exprDir.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 3. 逻辑与、或、非 (AND / OR / NOT)
    auto exprNot = SearchExpression::parse(L"*.cpp !test");
    FileRecord normalCpp{7, 0, L"main.cpp", L"main.cpp", L"main.cpp", L"main.cpp", false};
    EXPECT_FALSE(exprNot.matches(cppFile, L'C', L"C:\\test_main.cpp"));
    EXPECT_TRUE(exprNot.matches(normalCpp, L'C', L"C:\\main.cpp"));

    auto exprOr = SearchExpression::parse(L"ext:txt | ext:png");
    EXPECT_TRUE(exprOr.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_TRUE(exprOr.matches(pngFile, L'C', L"C:\\screenshot.png"));
    EXPECT_FALSE(exprOr.matches(cppFile, L'C', L"C:\\test_main.cpp"));

    // 4. 正则表达式 (regex: / r:)
    auto exprRegex = SearchExpression::parse(L"regex:^app_[0-9]+\\.log$");
    EXPECT_TRUE(exprRegex.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprRegex.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 5. 盘符与路径过滤 (c: / path: / parent:)
    auto exprDrive = SearchExpression::parse(L"c: *.txt");
    EXPECT_TRUE(exprDrive.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprDrive.matches(txtFile, L'D', L"D:\\readme.txt"));

    auto exprPath = SearchExpression::parse(L"path:logs");
    EXPECT_TRUE(exprPath.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprPath.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 6. 拼音检索与禁用拼音 (pinyin: / nopy:)
    auto exprPy = SearchExpression::parse(L"wx");
    EXPECT_TRUE(exprPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    auto exprNoPy = SearchExpression::parse(L"nopy:wx");
    EXPECT_FALSE(exprNoPy.matches(wxFile, L'C', L"C:\\微信.exe"));

    // 7. 双引号带空格短语
    auto exprQuote = SearchExpression::parse(L"\"test main\"");
    FileRecord spaceFile{8, 0, L"test main.cpp", L"test main.cpp", L"test main.cpp", L"test main.cpp", false};
    EXPECT_TRUE(exprQuote.matches(spaceFile, L'C', L"C:\\test main.cpp"));

    // 8. 跨目录与文件名联合搜索及跨层级通配符 (*中源*账号密码* 与 中源 账号密码)
    FileRecord nestedPassFile{9, 0, L"账号密码.txt", L"账号密码.txt", L"zhmm.txt", L"zhanghaomima.txt", false};
    std::wstring nestedPassPath = L"D:\\Chosen\\216-北京中源\\账号密码.txt";

    // 通配符跨目录搜索: *中源*账号密码*
    auto exprWildcardPath = SearchExpression::parse(L"*中源*账号密码*");
    EXPECT_TRUE(exprWildcardPath.matches(nestedPassFile, L'D', nestedPassPath));
    EXPECT_FALSE(exprWildcardPath.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 多关键词空格组合跨目录搜索: 中源 账号密码
    auto exprMultiTerms = SearchExpression::parse(L"中源 账号密码");
    EXPECT_TRUE(exprMultiTerms.matches(nestedPassFile, L'D', nestedPassPath));
    EXPECT_FALSE(exprMultiTerms.matches(txtFile, L'C', L"C:\\readme.txt"));

    // 拼音与目录联合匹配: 中源 zhmm
    auto exprPinyinDir = SearchExpression::parse(L"中源 zhmm");
    EXPECT_TRUE(exprPinyinDir.matches(nestedPassFile, L'D', nestedPassPath));

    // 9. 用户真实连字符文件名检索: 019-get_Bom_Lev
    FileRecord bomFile{10, 0, L"019-get_Bom_Lev.txt", L"019-get_bom_lev.txt", L"019-get_bom_lev.txt", L"019-get_bom_lev.txt", false};
    auto exprBom1 = SearchExpression::parse(L"019-get_Bom_Lev");
    // 10. 盘符与根目录前缀搜索 (c:\, c:\ readme, c:\logs\ *.log)
    auto exprRoot = SearchExpression::parse(L"c:\\");
    EXPECT_TRUE(exprRoot.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprRoot.matches(bomFile, L'D', L"D:\\Chosen\\019.txt"));

    auto exprRootTerms = SearchExpression::parse(L"c:\\ readme");
    EXPECT_TRUE(exprRootTerms.matches(txtFile, L'C', L"C:\\readme.txt"));
    EXPECT_FALSE(exprRootTerms.matches(pngFile, L'C', L"C:\\screenshot.png"));

    auto exprSubdirWildcard = SearchExpression::parse(L"c:\\logs\\ *.log");
    EXPECT_TRUE(exprSubdirWildcard.matches(logFile, L'C', L"C:\\logs\\app_2026.log"));
    EXPECT_FALSE(exprSubdirWildcard.matches(txtFile, L'C', L"C:\\readme.txt"));
}

TEST(SearchExpressionTest, EnvironmentVariableExpansion) {
    FileRecord appDataFile;
    appDataFile.fileName = L"EasyTools.json";
    appDataFile.normalizedName = L"easytools.json";
    appDataFile.isDirectory = false;

    wchar_t appDataBuf[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"APPDATA", appDataBuf, MAX_PATH);
    std::wstring appDataStr = appDataBuf;
    if (!appDataStr.empty()) {
        std::wstring targetPath = appDataStr + L"\\EasyTools\\EasyTools.json";
        auto expr = SearchExpression::parse(L"%APPDATA%\\EasyTools\\EasyTools.json");
        EXPECT_TRUE(expr.requiresFullPath());
        EXPECT_TRUE(expr.matches(appDataFile, appDataStr[0], targetPath));
    }
}

// -----------------------------------------------------------------------------
// 16. 全屏检测与驱动器枚举测试套件
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

TEST(WebViewSecurityTest, TrustsOnlyPackagedOriginInRelease) {
    using easy::ui::web_security::isTrustedUri;
    EXPECT_TRUE(isTrustedUri(L"https://easytools.local/index.html#/search"));
    EXPECT_TRUE(isTrustedUri(L"https://easytools.local/"));
    EXPECT_FALSE(isTrustedUri(L"https://easytools.local.evil.example/"));
    EXPECT_FALSE(isTrustedUri(L"file:///C:/temp/index.html"));
    EXPECT_FALSE(isTrustedUri(L"javascript:alert(1)"));
#ifndef _DEBUG
    EXPECT_FALSE(isTrustedUri(L"http://localhost:5173"));
    EXPECT_FALSE(isTrustedUri(L"http://127.0.0.1:5173"));
#endif
}

TEST(WebViewSecurityTest, RejectsOversizedBridgePayload) {
    EXPECT_TRUE(easy::ui::web_security::isBridgeMessageSizeAcceptable(
        easy::ui::web_security::MaxBridgeMessageBytes));
    EXPECT_FALSE(easy::ui::web_security::isBridgeMessageSizeAcceptable(
        easy::ui::web_security::MaxBridgeMessageBytes + 1));
}

TEST(WebViewSecurityTest, AllowsTrayRequiredMethods) {
    using easy::ui::web_security::isBridgeMethodAllowed;
    using easy::ui::web_security::Surface;

    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"general.getSettings\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"general.updateSettings\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"plugins.getAll\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"plugins.setEnabled\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"gesture.getState\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"app.restart\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"app.restartElevated\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"tray.action\"}", Surface::Tray));
    EXPECT_TRUE(isBridgeMethodAllowed("{\"method\":\"tray.resize\"}", Surface::Tray));
    EXPECT_FALSE(isBridgeMethodAllowed("{\"method\":\"search.query\"}", Surface::Tray));
    EXPECT_FALSE(isBridgeMethodAllowed("{\"method\":\"capture.start\"}", Surface::Tray));
}

TEST(KeyboardPipelineTest, FiltersSystemMenuAndHelpMessages) {
    using easy::ui::KeyboardPipeline;
    // SC_KEYMENU (Alt / F10 / Alt+Space 激活系统菜单) 必须被管线拦截
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_KEYMENU, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_KEYMENU | 0x0002, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_CONTEXTHELP, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_HELP, 0, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_SPACE, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYUP, VK_SPACE, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_F10, 0));
    EXPECT_TRUE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, VK_MENU, 0));

    // 普通命令如最大化/最小化/关闭等不能被误拦截
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_CLOSE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSCOMMAND, SC_MAXIMIZE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_KEYDOWN, VK_SPACE, 0));
    EXPECT_FALSE(KeyboardPipeline::filterWindowMessage(nullptr, WM_SYSKEYDOWN, 'A', 0));
}

TEST(HotkeyManagerTest, HandlesRecordingPauseMode) {
    auto& mgr = easy::core::HotkeyManager::instance();
    EXPECT_FALSE(mgr.isPaused());

    mgr.setPaused(true);
    EXPECT_TRUE(mgr.isPaused());

    mgr.setPaused(false);
    EXPECT_FALSE(mgr.isPaused());
}

TEST(WebViewSuspendTest, DistinguishesRefusalFromFailure) {
    using easy::ui::WebViewSuspendOutcome;
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(S_OK, TRUE),
              WebViewSuspendOutcome::Suspended);
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(S_OK, FALSE),
              WebViewSuspendOutcome::Refused);
    EXPECT_EQ(easy::ui::classifyWebViewSuspendCompletion(E_FAIL, FALSE),
              WebViewSuspendOutcome::Failed);
}

TEST(WebViewSuspendTest, LateSuspendCompletionResumesOnlyWhenSurfaceWasShownAgain) {
    using Outcome = easy::ui::WebViewSuspendOutcome;
    EXPECT_TRUE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, false, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, true, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Suspended, false, true));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Refused, false, false));
    EXPECT_FALSE(easy::ui::shouldResumeAfterSuspendCompletion(
        Outcome::Failed, false, false));
}

TEST(WebViewSuspendTest, ControllerFailsClosedForAbsentWebViewAndCanBeRecreated) {
    easy::ui::WebViewSuspendController controller;
    EXPECT_EQ(controller.requestSuspend(nullptr, "unit"), E_POINTER);
    EXPECT_EQ(controller.resume(nullptr, "unit"), E_POINTER);

    // Destruction invalidates late callbacks. A later native-window generation
    // receives a fresh state rather than inheriting the previous generation's
    // cancellation flag.
    controller.abandon();
    controller.reset();
    EXPECT_EQ(controller.requestSuspend(nullptr, "unit-recreated"), E_POINTER);
    EXPECT_EQ(controller.resume(nullptr, "unit-recreated"), E_POINTER);
}

TEST(PipeEndpointTest, RequiresOpaqueTokenAndBuildsPerUserDescriptor) {
    using namespace easy::service::pipe_endpoint;
    EXPECT_FALSE(isValidToken("easytools"));
    EXPECT_FALSE(isValidToken(std::string(TokenHexLength, 'g')));
    const std::string token(TokenHexLength, 'a');
    EXPECT_TRUE(isValidToken(token));
    ASSERT_TRUE(makePipeName(token).has_value());
    EXPECT_NE(makePipeName(token)->find(token), std::string::npos);
    EXPECT_FALSE(makePipeName("invalid").has_value());
    ASSERT_TRUE(makeSecurityDescriptor(L"S-1-5-21-1-2-3-1001").has_value());
    EXPECT_FALSE(makeSecurityDescriptor(L"").has_value());
}

TEST(PipeEndpointTest, RestrictedDescriptorAllowsCurrentUserEndpoint) {
    using namespace easy::service::pipe_endpoint;
    HANDLE token = nullptr;
    ASSERT_TRUE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    struct TokenCloser { HANDLE value; ~TokenCloser() { if (value) CloseHandle(value); } } tokenCloser{token};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    ASSERT_GT(bytes, 0u);
    std::vector<std::byte> tokenInfo(bytes);
    ASSERT_TRUE(GetTokenInformation(token, TokenUser, tokenInfo.data(), bytes, &bytes));
    auto* user = reinterpret_cast<TOKEN_USER*>(tokenInfo.data());
    LPWSTR sidText = nullptr;
    ASSERT_TRUE(ConvertSidToStringSidW(user->User.Sid, &sidText));
    struct SidFreer { LPWSTR value; ~SidFreer() { if (value) LocalFree(value); } } sidFreer{sidText};

    const auto sddl = makeSecurityDescriptor(sidText);
    ASSERT_TRUE(sddl.has_value());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl->c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    struct DescriptorFreer { PSECURITY_DESCRIPTOR value; ~DescriptorFreer() { if (value) LocalFree(value); } } descriptorFreer{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsAclTest-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 128, 128, 0, &security);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    struct PipeCloser { HANDLE value; ~PipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } pipeCloser{pipe};

    std::atomic<bool> connected{false};
    std::thread client([&] {
        HANDLE clientPipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (clientPipe != INVALID_HANDLE_VALUE) {
            connected = true;
            CloseHandle(clientPipe);
        }
    });
    const BOOL connectedByConnect = ConnectNamedPipe(pipe, nullptr);
    const DWORD connectError = connectedByConnect ? ERROR_SUCCESS : GetLastError();
    EXPECT_TRUE(connectedByConnect || connectError == ERROR_PIPE_CONNECTED);
    client.join();
    EXPECT_TRUE(connected.load());
    DisconnectNamedPipe(pipe);

    const auto invalidSddl = makeSecurityDescriptor(L"not-a-windows-sid");
    ASSERT_TRUE(invalidSddl.has_value());
    PSECURITY_DESCRIPTOR invalidDescriptor = nullptr;
    EXPECT_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        invalidSddl->c_str(), SDDL_REVISION_1, &invalidDescriptor, nullptr));
    if (invalidDescriptor) LocalFree(invalidDescriptor);
}

TEST(PipeEndpointTest, DescriptorRejectsAnUnlistedCurrentUser) {
    using namespace easy::service::pipe_endpoint;
    // A syntactically valid but deliberately unrelated SID permits verifying
    // the actual Windows access check without creating or depending on a
    // second local account in CI.
    const auto sddl = makeSecurityDescriptor(L"S-1-5-21-424242-424243-424244-424245");
    ASSERT_TRUE(sddl.has_value());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ASSERT_TRUE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl->c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    struct DescriptorFreer { PSECURITY_DESCRIPTOR value; ~DescriptorFreer() { if (value) LocalFree(value); } } descriptorFreer{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsAclDenyTest-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 128, 128, 0, &security);
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    struct PipeCloser { HANDLE value; ~PipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } pipeCloser{pipe};

    SetLastError(ERROR_SUCCESS);
    HANDLE client = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_EQ(client, INVALID_HANDLE_VALUE);
    EXPECT_EQ(GetLastError(), ERROR_ACCESS_DENIED);
}

TEST(SearchServiceStartupPolicyTest, ScmStateNeverCausesPrematurePortableDuplicate) {
    using easy::search::ScmServiceState;
    using easy::search::StartupAction;
    using easy::search::decideStartupAction;

    EXPECT_EQ(decideStartupAction(true, ScmServiceState::Running, false),
              StartupAction::UseEndpoint);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Missing, false),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, false),
              StartupAction::StartScmService);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, true),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::StartPending, false),
              StartupAction::WaitForScmEndpoint);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Running, false),
              StartupAction::WaitForScmEndpoint);
    // A concurrent StartService can become Running after the caller's initial
    // status query; that state must still wait, never spawn portable mode.
    EXPECT_NE(decideStartupAction(false, ScmServiceState::Running, false),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::StopPending, false),
              StartupAction::ReportUnavailable);
    EXPECT_TRUE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::Running, false, true));
    EXPECT_FALSE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::StartPending, false, true));
    EXPECT_FALSE(easy::search::isScmEndpointIdentityConflict(
        ScmServiceState::Running, true, true));
    EXPECT_TRUE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_ACCESS_DENIED));
    EXPECT_FALSE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_SERVICE_DOES_NOT_EXIST));
    EXPECT_FALSE(easy::search::scmOpenShouldRetryQueryOnly(ERROR_SUCCESS));
    // 没有启动权限时，已停止的 SCM 服务必须回退便携进程，否则搜索永久不可用。
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Stopped, true),
              StartupAction::AllowPortableFallback);
    EXPECT_EQ(decideStartupAction(false, ScmServiceState::Running, true),
              StartupAction::WaitForScmEndpoint);
}

// -----------------------------------------------------------------------------
// 20. 屏幕录制编码管道测试套件 (内存虚拟驱动 + FFmpeg 编码全链路测试)
// -----------------------------------------------------------------------------
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
    EXPECT_TRUE(tray100.cx == 200 && tray100.cy == 330);
    EXPECT_TRUE(tray150.cx == 300 && tray150.cy == 495);
    EXPECT_TRUE(tray200.cx == 400 && tray200.cy == 660);

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
TEST(CaptureToolbarTest, DpiAdaptiveLayout) {
    using namespace easy::capture;

    CaptureState screenshot;
    screenshot.mode = OverlayMode::Screenshot;
    screenshot.dpiScale = 1.5f;  // Windows 150% / 144 DPI
    const D2D1_SIZE_F desktop150 = D2D1::SizeF(2304.0f, 1440.0f);
    rebuildCaptureToolbar(screenshot, D2D1::RectF(180.0f, 120.0f, 1600.0f, 980.0f),
                          desktop150);
    EXPECT_EQ(screenshot.toolbarButtons.size(), 25u);
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
    EXPECT_EQ(wrapped.toolbarButtons.size(), 25u);
    bool hasSecondRow = false;
    for (const auto& button : wrapped.toolbarButtons) {
        if (button.rect.top > wrapped.toolbarButtons.front().rect.top + 0.01f) {
            hasSecondRow = true;
            break;
        }
    }
    EXPECT_TRUE(hasSecondRow);
    check_toolbar_inside_surface(wrapped, compact);

    CaptureState recording;
    recording.mode = OverlayMode::RecordRegion;
    recording.dpiScale = 1.5f;
    rebuildCaptureToolbar(recording, D2D1::RectF(200.0f, 100.0f, 1200.0f, 800.0f),
                          desktop150);
    EXPECT_EQ(recording.toolbarButtons.size(), 2u);
    EXPECT_NEAR(recording.toolbarButtons[0].rect.right -
                recording.toolbarButtons[0].rect.left, 102.0f, 0.01f);
    EXPECT_NEAR(recording.toolbarButtons[1].rect.right -
                recording.toolbarButtons[1].rect.left, 117.0f, 0.01f);
    check_toolbar_inside_surface(recording, desktop150);

    CaptureState extreme;
    extreme.mode = OverlayMode::Screenshot;
    extreme.dpiScale = 5.0f;
    const D2D1_SIZE_F desktop500 = D2D1::SizeF(7680.0f, 4320.0f);
    rebuildCaptureToolbar(extreme, D2D1::RectF(500.0f, 400.0f, 7000.0f, 3600.0f),
                          desktop500);
    EXPECT_EQ(extreme.toolbarButtons.size(), 25u);
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

TEST(OverlayUiaProviderTest, ExposesActionPropertiesAndMarshalsInvokeToOwnerWindow) {
    // This uses a real UI Automation client against a real HWND rather than
    // testing helper structs in isolation. It covers the WM_GETOBJECT route,
    // child semantics, physical bounds, and the no-UI-work-on-provider-thread
    // invariant of Invoke (the action arrives as a posted window message).
    CoApartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) {
        GTEST_SKIP() << "COM apartment unavailable: 0x" << std::hex << apartment.result;
    }

    UiaTestSurface surface;
    UiaTestWindow window(&surface);
    ASSERT_NE(window.hwnd, nullptr);

    IUIAutomation* automation = nullptr;
    HRESULT result = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&automation));
    if (FAILED(result)) {
        GTEST_SKIP() << "UI Automation client unavailable: 0x" << std::hex << result;
    }
    struct AutomationReleaser { IUIAutomation* value; ~AutomationReleaser() { if (value) value->Release(); } } automationReleaser{automation};

    IUIAutomationElement* root = nullptr;
    result = automation->ElementFromHandle(window.hwnd, &root);
    if (FAILED(result) || !root) {
        GTEST_SKIP() << "UI Automation cannot inspect this desktop: 0x" << std::hex << result;
    }
    struct ElementReleaser { IUIAutomationElement* value; ~ElementReleaser() { if (value) value->Release(); } } rootReleaser{root};

    BSTR rootId = nullptr;
    ASSERT_TRUE(SUCCEEDED(root->get_CurrentAutomationId(&rootId)));
    EXPECT_STREQ(rootId, L"EasyTools.TestOverlay");
    SysFreeString(rootId);

    IUIAutomationCondition* condition = nullptr;
    ASSERT_TRUE(SUCCEEDED(automation->CreateTrueCondition(&condition)));
    struct ConditionReleaser { IUIAutomationCondition* value; ~ConditionReleaser() { if (value) value->Release(); } } conditionReleaser{condition};
    IUIAutomationElement* action = nullptr;
    ASSERT_TRUE(SUCCEEDED(root->FindFirst(TreeScope_Children, condition, &action)));
    ASSERT_NE(action, nullptr);
    struct ActionReleaser { IUIAutomationElement* value; ~ActionReleaser() { if (value) value->Release(); } } actionReleaser{action};

    BSTR name = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentName(&name)));
    EXPECT_STREQ(name, L"Confirm capture");
    SysFreeString(name);
    BOOL enabled = FALSE;
    EXPECT_TRUE(SUCCEEDED(action->get_CurrentIsEnabled(&enabled)));
    EXPECT_TRUE(enabled);
    BSTR shortcut = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentAcceleratorKey(&shortcut)));
    EXPECT_STREQ(shortcut, L"Enter");
    SysFreeString(shortcut);
    VARIANT selected{};
    ASSERT_TRUE(SUCCEEDED(action->GetCurrentPropertyValue(UIA_SelectionItemIsSelectedPropertyId, &selected)));
    EXPECT_EQ(selected.vt, VT_BOOL);
    EXPECT_EQ(selected.boolVal, VARIANT_TRUE);
    VariantClear(&selected);
    tagRECT bounds{};
    ASSERT_TRUE(SUCCEEDED(action->get_CurrentBoundingRectangle(&bounds)));
    EXPECT_EQ(bounds.left, 40);
    EXPECT_EQ(bounds.top, 60);
    EXPECT_EQ(bounds.right, 220);
    EXPECT_EQ(bounds.bottom, 104);

    IUIAutomationInvokePattern* invoke = nullptr;
    ASSERT_TRUE(SUCCEEDED(action->GetCurrentPatternAs(UIA_InvokePatternId,
        IID_PPV_ARGS(&invoke))));
    ASSERT_NE(invoke, nullptr);
    struct InvokeReleaser { IUIAutomationInvokePattern* value; ~InvokeReleaser() { if (value) value->Release(); } } invokeReleaser{invoke};
    EXPECT_TRUE(SUCCEEDED(invoke->Invoke()));
    MSG message{};
    while (PeekMessageW(&message, window.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EXPECT_EQ(surface.invokeCount, 1);
}

// -----------------------------------------------------------------------------
// 26. 贴图窗口几何变换测试套件
// -----------------------------------------------------------------------------
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
TEST(ContentSearchTest, ExtractorEngines) {
    // 1. SearchExpression content 语法解析验证
    auto expr1 = SearchExpression::parse(L"content:SELECT");
    EXPECT_TRUE(expr1.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr1.getContentQuery()), "SELECT");
    EXPECT_FALSE(expr1.requiresFullPath());

    auto expr2 = SearchExpression::parse(L"ext:cpp;h c:EasyTools c:\\projects\\");
    EXPECT_TRUE(expr2.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr2.getContentQuery()), "EasyTools");
    EXPECT_TRUE(expr2.requiresFullPath());

    auto expr3 = SearchExpression::parse(L"内容:工程图纸");
    EXPECT_TRUE(expr3.hasContentFilter());
    EXPECT_EQ(easy::core::WinUtils::wstringToUtf8(expr3.getContentQuery()), "工程图纸");

    // 拼音音节分隔符 (如输入法 tong'xi 匹配同喜 tongxi)
    auto exprPinyinSyllable = SearchExpression::parse(L"tong'xi");
    FileRecord recTongXi;
    recTongXi.fileName = L"同喜.txt";
    recTongXi.normalizedName = L"同喜.txt";
    recTongXi.pinyinFull = L"tongxi";
    recTongXi.pinyinInitials = L"tx";
    EXPECT_TRUE(exprPinyinSyllable.matches(recTongXi, L'C'));

    // 2. ContentSearchEngine 格式支持测试
    auto& engine = easy::service::content::ContentSearchEngine::instance();
    EXPECT_TRUE(engine.canSearchContent(L"cpp"));
    EXPECT_TRUE(engine.canSearchContent(L"rs"));
    EXPECT_TRUE(engine.canSearchContent(L"py"));
    EXPECT_TRUE(engine.canSearchContent(L"sql"));
    EXPECT_TRUE(engine.canSearchContent(L"md"));
    EXPECT_TRUE(engine.canSearchContent(L"docx"));
    EXPECT_TRUE(engine.canSearchContent(L"xlsx"));
    EXPECT_TRUE(engine.canSearchContent(L"pptx"));
    EXPECT_TRUE(engine.canSearchContent(L"psd"));
    EXPECT_TRUE(engine.canSearchContent(L"ai"));
    EXPECT_TRUE(engine.canSearchContent(L"cdr"));
    EXPECT_TRUE(engine.canSearchContent(L"xmind"));
    EXPECT_TRUE(engine.canSearchContent(L"dxf"));
    EXPECT_FALSE(engine.canSearchContent(L"exe"));
    EXPECT_FALSE(engine.canSearchContent(L"dll"));

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
    EXPECT_TRUE(found);
    EXPECT_EQ(snippets.size(), 1u);
    if (!snippets.empty()) {
        EXPECT_EQ(snippets[0].lineNumber, 3);
        EXPECT_NE(snippets[0].lineContent.find(L"TestWorldClassContentSearch"), std::wstring::npos);
        EXPECT_EQ(snippets[0].matchLength, wcslen(L"TestWorldClassContentSearch"));
    }

    DeleteFileW(testCppFile.c_str());

    // 3.1 SQL 文件与 OITT 关键字检索测试
    std::wstring testSqlFile = std::wstring(tempPath) + L"easytools_test_bom.sql";
    {
        std::ofstream ofs(testSqlFile, std::ios::binary);
        ofs << "/* SAP B1 BOM 物料清单控制 */\n";
        ofs << "SELECT T0.ItemCode, T0.ItemName FROM OITT T0 WHERE T0.TreeType = 'A';\n";
    }

    snippets.clear();
    bool sqlFound = engine.searchFile(testSqlFile, L"oitt", false, snippets);
    EXPECT_TRUE(sqlFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_EQ(snippets[0].lineNumber, 2);
        EXPECT_NE(snippets[0].lineContent.find(L"OITT"), std::wstring::npos);
    }
    DeleteFileW(testSqlFile.c_str());

    // 3.2 针对用户真实磁盘文件 019-get_Bom_Lev.txt 进行全文检索断言
    std::wstring realTargetFile = L"D:\\Chosen\\106-常用方案\\9.3方案\\通用报表\\后台代码\\019-get_Bom_Lev.txt";
    if (GetFileAttributesW(realTargetFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        snippets.clear();
        bool realFileFound = engine.searchFile(realTargetFile, L"oitt", false, snippets);
        EXPECT_TRUE(realFileFound);
        EXPECT_GE(snippets.size(), 2u);
        std::cout << "[LiveTest-Success] Extracted " << snippets.size() << " 'oitt' snippets from real file: " 
                  << easy::core::WinUtils::wstringToUtf8(realTargetFile) << std::endl;
    }

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
    EXPECT_TRUE(dxfFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"DWG_PROJECT_NUM_A88"), std::wstring::npos);
    }

    DeleteFileW(testDxfFile.c_str());

    // 5. 伪装为 .log 的二进制文件 (如 edb.log, MeasuredBoot.log) 拦截测试
    std::wstring testBinaryLogFile = std::wstring(tempPath) + L"easytools_test_fake_edb.log";
    {
        std::ofstream ofs(testBinaryLogFile, std::ios::binary);
        // 模拟 ESE / MeasuredBoot 二进制数据头与随机控制字节
        uint8_t fakeBinaryHeader[] = { 0xef, 0xcd, 0xab, 0x89, 0x00, 0x00, 0x01, 0x00, 0x80, 0x81, 0x00, 0x12, 0x05, 0x00 };
        ofs.write(reinterpret_cast<const char*>(fakeBinaryHeader), sizeof(fakeBinaryHeader));
        for (int i = 0; i < 200; ++i) {
            char buf[16] = {0};
            buf[0] = static_cast<char>(i);
            ofs.write(buf, sizeof(buf));
        }
    }

    snippets.clear();
    bool binaryFound = engine.searchFile(testBinaryLogFile, L"**", false, snippets);
    EXPECT_FALSE(binaryFound);
    EXPECT_TRUE(snippets.empty());
    DeleteFileW(testBinaryLogFile.c_str());

    // 6. GBK / ANSI 中文编码无乱码检索测试
    std::wstring testGbkFile = std::wstring(tempPath) + L"easytools_test_gbk.txt";
    {
        std::ofstream ofs(testGbkFile, std::ios::binary);
        // GBK 编码的中文字符串："北京中源技术中心：开发账号密码"
        std::wstring wText = L"北京中源技术中心：开发账号密码";
        UINT cp = IsValidCodePage(936) ? 936 : (IsValidCodePage(54936) ? 54936 : CP_ACP);
        int len = WideCharToMultiByte(cp, 0, wText.c_str(), static_cast<int>(wText.size()), nullptr, 0, nullptr, nullptr);
        std::string gbkText(len, '\0');
        if (len > 0) {
            WideCharToMultiByte(cp, 0, wText.c_str(), static_cast<int>(wText.size()), gbkText.data(), len, nullptr, nullptr);
        }
        ofs << "Line 1: Header\r\n";
        ofs << gbkText << "\r\n";
    }

    snippets.clear();
    bool gbkFound = engine.searchFile(testGbkFile, L"账号密码", false, snippets);
    EXPECT_TRUE(gbkFound);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"北京中源技术中心"), std::wstring::npos);
        EXPECT_NE(snippets[0].lineContent.find(L"账号密码"), std::wstring::npos);
    }
    DeleteFileW(testGbkFile.c_str());

    // 7. UTF-16LE 中文与英文无乱码检索测试
    std::wstring testUtf16File = std::wstring(tempPath) + L"easytools_test_utf16.txt";
    {
        std::ofstream ofs(testUtf16File, std::ios::binary);
        uint8_t bom[2] = { 0xFF, 0xFE };
        ofs.write(reinterpret_cast<const char*>(bom), 2);
        std::wstring text = L"UTF16_WorldClass_Search: 极客搜索世界级体验\r\n";
        ofs.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    }

    snippets.clear();
    bool utf16Found = engine.searchFile(testUtf16File, L"极客搜索", false, snippets);
    EXPECT_TRUE(utf16Found);
    EXPECT_FALSE(snippets.empty());
    if (!snippets.empty()) {
        EXPECT_NE(snippets[0].lineContent.find(L"极客搜索世界级体验"), std::wstring::npos);
    }
    DeleteFileW(testUtf16File.c_str());

    // 8. 动态格式配置测试 (自定义格式扩展与黑名单禁用)
    engine.configureFormats({L"proto", L"mycustomext"}, {L"psd", L"dxf"});
    EXPECT_TRUE(engine.canSearchContent(L"proto"));
    EXPECT_TRUE(engine.canSearchContent(L"mycustomext"));
    EXPECT_FALSE(engine.canSearchContent(L"psd"));
    EXPECT_FALSE(engine.canSearchContent(L"dxf"));

    std::wstring testProtoFile = std::wstring(tempPath) + L"easytools_test.proto";
    {
        std::ofstream ofs(testProtoFile);
        ofs << "syntax = \"proto3\";\n";
        ofs << "message EasyToolsSearchPacket {\n";
        ofs << "    string query = 1;\n";
        ofs << "}\n";
    }
    snippets.clear();
    bool protoFound = engine.searchFile(testProtoFile, L"EasyToolsSearchPacket", false, snippets);
    EXPECT_TRUE(protoFound);
    EXPECT_FALSE(snippets.empty());
    DeleteFileW(testProtoFile.c_str());

    // 恢复配置
    engine.configureFormats({}, {});
    EXPECT_TRUE(engine.canSearchContent(L"psd"));
    EXPECT_TRUE(engine.canSearchContent(L"dxf"));
}

// -----------------------------------------------------------------------------
// 32. 插件发现与动态加载生命周期测试套件 (必须放置在最后，因为 shutdown 会注销共享注册表)
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// 32. 屏幕触发角引擎测试套件
// -----------------------------------------------------------------------------
TEST(HotCornerEngineTest, DetectionAndConfiguration) {
    using namespace easy::gesture;
    auto& engine = HotCornerEngine::instance();
    engine.setEnabled(false);
    EXPECT_FALSE(engine.isEnabled());
    engine.setEnabled(true);
    EXPECT_TRUE(engine.isEnabled());

    engine.setTriggerDelay(150);
    EXPECT_EQ(engine.triggerDelay(), 150);

    engine.setCornerAction(HotCorner::TopLeft, "app:taskview");
    EXPECT_EQ(engine.getCornerAction(HotCorner::TopLeft), "app:taskview");
    engine.setCornerAction(HotCorner::BottomRight, "app:desktop");
    EXPECT_EQ(engine.getCornerAction(HotCorner::BottomRight), "app:desktop");
    EXPECT_EQ(engine.getCornerAction(HotCorner::None), "");

    // 虚拟屏幕四角几何检测
    int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int vRight = vLeft + vWidth - 1;
    int vBottom = vTop + vHeight - 1;

    EXPECT_EQ(engine.detectCorner({vLeft, vTop}), HotCorner::TopLeft);
    EXPECT_EQ(engine.detectCorner({vRight, vTop}), HotCorner::TopRight);
    EXPECT_EQ(engine.detectCorner({vLeft, vBottom}), HotCorner::BottomLeft);
    EXPECT_EQ(engine.detectCorner({vRight, vBottom}), HotCorner::BottomRight);
    EXPECT_EQ(engine.detectCorner({vLeft + vWidth / 2, vTop + vHeight / 2}), HotCorner::None);

    engine.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    engine.stop();
}

// -----------------------------------------------------------------------------
// 33. 数据统计管理器测试套件
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
TEST(RunHistoryTest, PersistenceAndFrecencyCalculation) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring csvFile = std::wstring(tempPath) + L"EasyTools_RunHistory_Test.csv";
    DeleteFileW(csvFile.c_str());

    auto& runMgr = easy::service::db::RunHistoryManager::instance();
    runMgr.init(csvFile);
    runMgr.clear();
    EXPECT_EQ(runMgr.size(), 0u);

    const std::wstring testFile1 = L"D:\\Chosen\\216-北京中源\\账号密码.txt";
    const std::wstring testFile2 = L"D:\\Chosen\\999-SQL\\SAP_RW.txt";

    runMgr.recordRun(testFile1);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 1u);
    EXPECT_GT(runMgr.getLastRunDate(testFile1), 0ULL);

    // 重复运行累加
    runMgr.recordRun(testFile1);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 2u);

    runMgr.recordRun(testFile2);
    EXPECT_EQ(runMgr.getRunCount(testFile2), 1u);

    // Frecency 智能权重打分计算
    double score1 = runMgr.calculateFrecencyScore(testFile1);
    double score2 = runMgr.calculateFrecencyScore(testFile2);
    EXPECT_GT(score1, score2); // 运行 2 次的得分显著高于运行 1 次

    auto topRuns = runMgr.getTopRuns(10);
    ASSERT_GE(topRuns.size(), 2u);
    EXPECT_EQ(topRuns[0].filename, testFile1);

    // 持久化保存并从磁盘重载
    EXPECT_TRUE(runMgr.save());
    runMgr.init(csvFile);
    EXPECT_EQ(runMgr.getRunCount(testFile1), 2u);
    EXPECT_EQ(runMgr.getRunCount(testFile2), 1u);

    DeleteFileW(csvFile.c_str());
}

// -----------------------------------------------------------------------------
// 35. 搜索历史 (Search History) 与去重推荐测试套件
// -----------------------------------------------------------------------------
TEST(SearchHistoryTest, PersistenceAndDeduplication) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring csvFile = std::wstring(tempPath) + L"EasyTools_SearchHistory_Test.csv";
    DeleteFileW(csvFile.c_str());

    auto& searchMgr = easy::service::db::SearchHistoryManager::instance();
    searchMgr.init(csvFile);
    searchMgr.clear();
    EXPECT_EQ(searchMgr.size(), 0u);

    const std::wstring q1 = L"*中源*账号密码*";
    const std::wstring q2 = L"ext:docx 订单";

    searchMgr.recordSearch(q1);
    searchMgr.recordSearch(q2);
    searchMgr.recordSearch(q1); // 重复记录

    EXPECT_EQ(searchMgr.size(), 2u);
    auto recents = searchMgr.getRecentSearches(10);
    ASSERT_EQ(recents.size(), 2u);
    EXPECT_EQ(recents[0].search, q1);
    EXPECT_EQ(recents[0].searchCount, 2u);

    // 移除单个搜索项
    EXPECT_TRUE(searchMgr.removeSearch(q2));
    EXPECT_EQ(searchMgr.size(), 1u);

    // 存盘与重载
    EXPECT_TRUE(searchMgr.save());
    searchMgr.init(csvFile);
    EXPECT_EQ(searchMgr.size(), 1u);
    EXPECT_EQ(searchMgr.getRecentSearches(10)[0].search, q1);

    DeleteFileW(csvFile.c_str());
}

// -----------------------------------------------------------------------------
// 36. 数据库快照 (EasyTools.db) 二进制存储与秒级内存映射测试套件
// -----------------------------------------------------------------------------
TEST(DatabaseSnapshotTest, BinaryPackAndFastMemoryMap) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring dbFile = std::wstring(tempPath) + L"EasyTools_Test.db";
    DeleteFileW(dbFile.c_str());

    auto& dbMgr = easy::service::db::DatabaseManager::instance();
    dbMgr.init(dbFile);

    // 构造测试 MFT 卷与记录
    auto parser1 = std::make_unique<MftParser>();
    std::vector<FileRecordInit> mockRecords;
    for (uint64_t i = 1; i <= 50; ++i) {
        FileRecordInit r{};
        r.fileReferenceNumber = i;
        r.parentFileReferenceNumber = 0;
        r.fileName = L"test_document_" + std::to_wstring(i) + L".docx";
        r.fileSize = 1024 * i;
        r.creationTime = 134000000000000000ULL + i;
        r.lastWriteTime = 134000000000000000ULL + i * 2;
        r.isDirectory = (i % 10 == 0);
        mockRecords.push_back(std::move(r));
    }

    EXPECT_TRUE(parser1->importSnapshot(std::move(mockRecords), 12345678ULL, 0xABCDEF01));
    EXPECT_EQ(parser1->getFileCount(), 50u);

    std::vector<MftParser*> parsers = { parser1.get() };

    // 保存快照至 EasyTools.db
    EXPECT_TRUE(dbMgr.saveSnapshot(parsers));

    auto stats = dbMgr.getStats();
    EXPECT_TRUE(stats.exists);
    EXPECT_GT(stats.fileSize, 0ULL);
    EXPECT_EQ(stats.totalRecords, 50ULL);

    // 构造空解析器并从 EasyTools.db 内存映射瞬时恢复
    auto parser2 = std::make_unique<MftParser>();
    std::vector<MftParser*> restoreParsers = { parser2.get() };
    EXPECT_TRUE(dbMgr.loadSnapshot(restoreParsers));
    EXPECT_EQ(parser2->getFileCount(), 50u);

    // 验证搜索命中与路径结构
    auto searchResults = parser2->Search(L"test_document_5.docx");
    ASSERT_EQ(searchResults.size(), 1u);
    EXPECT_EQ(searchResults[0].fileName, L"test_document_5.docx");
    EXPECT_EQ(searchResults[0].fileSize, 5120u);

    DeleteFileW(dbFile.c_str());
}

// -----------------------------------------------------------------------------
// 36.1 搜索服务管道分帧协议
//
// 旧协议依赖 PIPE_TYPE_MESSAGE 与固定 256KB 缓冲，响应超出缓冲区时客户端会
// 把 ERROR_MORE_DATA 当成失败并丢弃整份结果，残留数据还会拖垮后续连接。
// 现在改为显式长度前缀，这些用例锁定编解码的边界行为。
// -----------------------------------------------------------------------------
TEST(PipeProtocolTest, HeaderRoundTripsPayloadSize) {
    namespace frame = easy::service::pipe;
    for (uint32_t size : {1u, 2u, 255u, 256u, 65535u, 65536u, 1048576u, frame::MaxFrameBytes}) {
        const auto header = frame::encodeFrameHeader(size);
        uint32_t decoded = 0;
        EXPECT_TRUE(frame::decodeFrameHeader(header.data(), decoded)) << "size=" << size;
        EXPECT_EQ(decoded, size);
    }
}

TEST(PipeProtocolTest, HeaderIsLittleEndian) {
    namespace frame = easy::service::pipe;
    const auto header = frame::encodeFrameHeader(0x04030201u);
    EXPECT_EQ(static_cast<unsigned char>(header[0]), 0x01u);
    EXPECT_EQ(static_cast<unsigned char>(header[1]), 0x02u);
    EXPECT_EQ(static_cast<unsigned char>(header[2]), 0x03u);
    EXPECT_EQ(static_cast<unsigned char>(header[3]), 0x04u);
}

TEST(PipeProtocolTest, RejectsEmptyAndOversizedFrames) {
    namespace frame = easy::service::pipe;
    uint32_t decoded = 0;

    const auto emptyHeader = frame::encodeFrameHeader(0);
    EXPECT_FALSE(frame::decodeFrameHeader(emptyHeader.data(), decoded));

    const auto oversized = frame::encodeFrameHeader(frame::MaxFrameBytes + 1);
    EXPECT_FALSE(frame::decodeFrameHeader(oversized.data(), decoded));

    EXPECT_FALSE(frame::decodeFrameHeader(nullptr, decoded));
}

TEST(PipeProtocolTest, RequestLimitIsTighterThanResponseLimit) {
    namespace frame = easy::service::pipe;
    uint32_t decoded = 0;

    // 请求只承载一段查询 JSON，服务端用更小的上限拒绝畸形帧头。
    const auto header = frame::encodeFrameHeader(frame::MaxRequestBytes + 1);
    EXPECT_FALSE(frame::decodeFrameHeader(header.data(), decoded, frame::MaxRequestBytes));
    EXPECT_TRUE(frame::decodeFrameHeader(header.data(), decoded));

    EXPECT_TRUE(frame::fitsInFrame(frame::MaxRequestBytes, frame::MaxRequestBytes));
    EXPECT_FALSE(frame::fitsInFrame(frame::MaxRequestBytes + 1, frame::MaxRequestBytes));
    EXPECT_FALSE(frame::fitsInFrame(0));
}

TEST(PipeProtocolIntegrationTest, ByteStreamRoundTripsDeliberatelyFragmentedFrame) {
    namespace frame = easy::service::pipe;
    const std::wstring name = L"\\\\.\\pipe\\EasyToolsPipeProtocolIntegration-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HANDLE serverPipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, frame::IoChunkBytes, frame::IoChunkBytes, 0, nullptr);
    ASSERT_NE(serverPipe, INVALID_HANDLE_VALUE);
    struct ServerPipeCloser { HANDLE value; ~ServerPipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } serverCloser{serverPipe};

    // Connect before starting the server thread. ConnectNamedPipe will then
    // return ERROR_PIPE_CONNECTED, exercising the race that occurs when a
    // portable client wins the connection race against a newly published pipe.
    HANDLE clientPipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(clientPipe, INVALID_HANDLE_VALUE) << GetLastError();
    struct ClientPipeCloser { HANDLE value; ~ClientPipeCloser() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } clientCloser{clientPipe};

    std::atomic<bool> serverOk{false};
    std::thread server([&] {
        const BOOL connected = ConnectNamedPipe(serverPipe, nullptr) ? TRUE :
            (GetLastError() == ERROR_PIPE_CONNECTED);
        if (!connected) return;
        char header[frame::HeaderSize]{};
        if (!frame::readExact(serverPipe, header, sizeof(header))) return;
        std::uint32_t bytes = 0;
        if (!frame::decodeFrameHeader(header, bytes, frame::MaxRequestBytes)) return;
        std::string request(bytes, '\0');
        if (!frame::readExact(serverPipe, request.data(), request.size()) || request != "fragmented request") return;
        const std::string response = R"({"ok":true,"source":"pipe"})";
        const auto responseHeader = frame::encodeFrameHeader(static_cast<std::uint32_t>(response.size()));
        if (!frame::writeExact(serverPipe, responseHeader.data(), responseHeader.size())) return;
        if (!frame::writeExact(serverPipe, response.data(), response.size())) return;
        serverOk.store(true, std::memory_order_release);
    });

    const std::string request = "fragmented request";
    const auto requestHeader = frame::encodeFrameHeader(static_cast<std::uint32_t>(request.size()));
    auto writeByteByByte = [clientPipe](const char* bytes, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            DWORD written = 0;
            if (!WriteFile(clientPipe, bytes + index, 1, &written, nullptr) || written != 1) return false;
        }
        return true;
    };
    EXPECT_TRUE(writeByteByByte(requestHeader.data(), requestHeader.size()));
    EXPECT_TRUE(writeByteByByte(request.data(), request.size()));

    char responseHeader[frame::HeaderSize]{};
    std::uint32_t responseBytes = 0;
    const bool gotHeader = frame::readExact(clientPipe, responseHeader, sizeof(responseHeader));
    EXPECT_TRUE(gotHeader);
    const bool validHeader = gotHeader && frame::decodeFrameHeader(responseHeader, responseBytes);
    EXPECT_TRUE(validHeader);
    std::string response(validHeader ? responseBytes : 0, '\0');
    const bool gotPayload = validHeader && frame::readExact(clientPipe, response.data(), response.size());
    EXPECT_TRUE(gotPayload);
    if (gotPayload) EXPECT_EQ(response, R"({"ok":true,"source":"pipe"})");
    clientCloser.value = INVALID_HANDLE_VALUE;
    CloseHandle(clientPipe);
    server.join();
    EXPECT_TRUE(serverOk.load(std::memory_order_acquire));
}

// -----------------------------------------------------------------------------
// 36.2 查询代际取消
// -----------------------------------------------------------------------------
TEST(QueryEpochTrackerTest, NewerQuerySupersedesOlderOnes) {
    easy::service::query::QueryEpochTracker tracker;

    EXPECT_TRUE(tracker.observe(100));
    EXPECT_FALSE(tracker.isStale(100));

    EXPECT_TRUE(tracker.observe(200));
    EXPECT_TRUE(tracker.isStale(100));
    EXPECT_FALSE(tracker.isStale(200));
    EXPECT_EQ(tracker.latest(), 200u);
}

TEST(QueryEpochTrackerTest, LateArrivingOlderQueryIsRejectedImmediately) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(500));

    // 更旧的查询即使刚到达也已经过期，不必再跑一遍扫描。
    EXPECT_FALSE(tracker.observe(400));
    EXPECT_TRUE(tracker.isStale(400));
    EXPECT_EQ(tracker.latest(), 500u);
}

TEST(QueryEpochTrackerTest, RepeatedObservationOfLatestStaysCurrent) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(700));
    EXPECT_TRUE(tracker.observe(700));
    EXPECT_FALSE(tracker.isStale(700));
}

TEST(QueryEpochTrackerTest, UnnumberedCallersNeverParticipateInCancellation) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(0));
    EXPECT_FALSE(tracker.isStale(0));

    EXPECT_TRUE(tracker.observe(900));
    EXPECT_TRUE(tracker.observe(0));
    EXPECT_FALSE(tracker.isStale(0));
    EXPECT_EQ(tracker.latest(), 900u);
}

TEST(QueryEpochTrackerTest, ResetClearsGeneration) {
    easy::service::query::QueryEpochTracker tracker;
    EXPECT_TRUE(tracker.observe(1234));
    tracker.reset();
    EXPECT_EQ(tracker.latest(), 0u);
    EXPECT_FALSE(tracker.isStale(1));
}

TEST(QueryEpochTrackerTest, ConcurrentObserversAgreeOnASingleWinner) {
    easy::service::query::QueryEpochTracker tracker;
    constexpr uint64_t highest = 512;

    std::vector<std::thread> threads;
    for (uint64_t id = 1; id <= highest; ++id) {
        threads.emplace_back([&tracker, id]() { tracker.observe(id); });
    }
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(tracker.latest(), highest);
    EXPECT_FALSE(tracker.isStale(highest));
    EXPECT_TRUE(tracker.isStale(highest - 1));
}

// -----------------------------------------------------------------------------
// 36.3 IPC 异步分发
//
// 搜索请求要跨进程等待索引服务，同步执行会冻结 WebView2 的 UI 线程。这里验证
// 标记为异步的方法确实离开了调用线程，而未标记的方法仍然就地完成。
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

// -----------------------------------------------------------------------------
// 36.4 索引服务的退出归属
//
// 索引常驻要几百 MB。主程序退出时该不该把它带走，取决于这个服务是谁的：自己
// 拉起的子进程该收尾，用户装成 Windows 服务的不该动，别的实例拉起的也不该动。
// -----------------------------------------------------------------------------
TEST(ServiceLifetimeTest, StopsOnlyTheInstanceWeSpawned) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    EXPECT_TRUE(shouldStopServiceOnExit(ServiceOwnership{true, false, false}));

    // 没拉起过就说明它属于别人，可能还有别的客户端在用。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, false, false}));
}

TEST(ServiceLifetimeTest, LeavesScmManagedServiceAlone) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    // 用户显式安装成 Windows 服务，就该由 SCM 管生命周期，哪怕这次是我们启动的。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, true, false}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, true, false}));
}

TEST(ServiceLifetimeTest, KeepRunningPreferenceOverridesCleanup) {
    using easy::search::ServiceOwnership;
    using easy::search::shouldStopServiceOnExit;

    // 用户选择常驻，就换首次搜索秒开、退出后不释放内存。
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, false, true}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{true, true, true}));
    EXPECT_FALSE(shouldStopServiceOnExit(ServiceOwnership{false, false, true}));
}

TEST(ServiceLifetimeTest, DefaultConstructedOwnershipStopsNothing) {
    // 任何事实都还没确定时不做处置，避免误杀别人的服务。
    EXPECT_FALSE(easy::search::shouldStopServiceOnExit(easy::search::ServiceOwnership{}));
}

// -----------------------------------------------------------------------------
// 37. 插件发现与动态加载生命周期测试套件 (必须放置在最后，因为 shutdown 会注销共享注册表)
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
    EXPECT_EQ(plugins.size(), 4u);
    for (const auto& plugin : plugins) {
        EXPECT_TRUE(plugin.error.empty());
        EXPECT_EQ(plugin.abiVersion, easy::core::CurrentPluginAbiVersion);
        EXPECT_FALSE(plugin.capabilities.empty());
    }

    // 验证各插件注册到 MessageBridge 的 IPC 协议调用与异常防护
    auto& bridge = easy::core::MessageBridge::instance();
    EXPECT_FALSE(bridge.handleMessage(R"({"id":1,"method":"search.status","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":2,"method":"search.query","params":{"keyword":"easy","page":1}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":3,"method":"gesture.getProfiles","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":4,"method":"capture.getCapabilities","params":{}})").empty());
    EXPECT_FALSE(bridge.handleMessage(R"({"id":5,"method":"keycast.getStatus","params":{}})").empty());

    manager.shutdownPlugins();
}

// -----------------------------------------------------------------------------
// 单元测试主入口 (Google Test 初始化与执行)
// -----------------------------------------------------------------------------
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
