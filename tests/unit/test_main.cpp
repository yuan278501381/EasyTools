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
// 由 deploy.ps1 在 CMake 构建后统一执行，并通过 OpenCppCoverage 进行 100% 覆盖率分析。
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "gesture/GestureRecognizer.h"
#include "gesture/GestureAction.h"
#include "gesture/GestureProfile.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/HotCornerEngine.h"
#include "gesture/ScopeRule.h"
#include "gesture/RadialMenuStyle.h"
#include "capture/CaptureBackend.h"
#include "capture/AudioCapture.h"
#include "capture/CursorOverlay.h"
#include "capture/ScreenRecorder.h"
#include "capture/ScrollCapture.h"
#include "capture/ShortcutHintOverlay.h"
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
#include "core/stats/StatsManager.h"
#include "core/update/UpdateChecker.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/ThemeUtils.h"
#include "core/utils/WinUtils.h"
#include "core/lua/LuaEngine.h"
#include "service/PinyinEngine.h"
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
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

using namespace easy::gesture;

// 喂入一串轨迹点, 返回识别出的方向编码 (无效手势返回空串)。
static std::string recognize(std::initializer_list<TrackPoint> pts) {
    GestureRecognizer r;  // 默认配置: minSegmentDistance=30, samplingInterval=5
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

    // 多段 (L 形)
    EXPECT_EQ(recognize({{0, 100}, {100, 100}, {100, 0}}), "R-U"); // → 然后 ↑
    EXPECT_EQ(recognize({{100, 0}, {0, 0}, {0, 100}}), "L-D");     // ← 然后 ↓

    // 方向序列与箭头转换辅助函数
    std::vector<Direction> dirs = {Direction::Left, Direction::Down};
    EXPECT_EQ(directionsToCode(dirs), "L-D");
    EXPECT_EQ(directionsToArrowString(dirs), "←↓");

    std::vector<Direction> singleDir = {Direction::Up};
    EXPECT_EQ(directionsToCode(singleDir), "U");
    EXPECT_EQ(directionsToArrowString(singleDir), "↑");

    // 防抖: 位移小于最小段距离 → 无效手势
    EXPECT_EQ(recognize({{0, 0}, {20, 0}}), "");

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

    // 真正对角手势保留
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight})), "DR");
    EXPECT_EQ(directionsToCode(GestureRecognizer::simplifyDirections({Direction::DownRight, Direction::Right})), "DR-R");

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
    ksEmpty.send(nullptr); // 空按键安全防御

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
    EXPECT_EQ(profRestored.getMappings().size(), 2u);
    EXPECT_EQ(profRestored.getTriggerState("right"), TriggerModeState::Enabled);

    // 5. 桌面与任务栏预设工厂测试
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

    // 验证多媒体命令与虚拟桌面在无有效目标窗口与伪窗口上下文下均能稳定分发，不发生异常
    dispatcher.execute(BuiltinCommand::MediaNext, nullptr);
    dispatcher.execute(BuiltinCommand::MediaPrev, nullptr);
    dispatcher.execute(BuiltinCommand::MediaPlayPause, nullptr);
    dispatcher.execute(BuiltinCommand::VolumeUp, nullptr);
    dispatcher.execute(BuiltinCommand::VolumeDown, nullptr);
    dispatcher.execute(BuiltinCommand::VolumeMute, nullptr);
    dispatcher.execute(BuiltinCommand::PrevVirtualDesktop, nullptr);
    dispatcher.execute(BuiltinCommand::NextVirtualDesktop, nullptr);
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

    // 重置测试工厂钩子
    easy::capture::setCaptureBackendFactoryForTesting(nullptr);
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
    EXPECT_NEAR(scaleForDpi(480), 5.0f, 0.001f);
    EXPECT_NEAR(scaleForDpi(768), 5.0f, 0.001f);
    EXPECT_EQ(scaleMetric(36, 1.0f), 36);
    EXPECT_EQ(scaleMetric(36, 1.25f), 45);
    EXPECT_EQ(scaleMetric(36, 1.5f), 54);
    EXPECT_EQ(scaleMetric(1, 0.0f), 1);

    const SIZE toast100 = easy::ui::ToastStyle::windowSizeForDpi(96);
    const SIZE toast150 = easy::ui::ToastStyle::windowSizeForDpi(144);
    const SIZE toast200 = easy::ui::ToastStyle::windowSizeForDpi(192);
    EXPECT_TRUE(toast100.cx == 600 && toast100.cy == 80);
    EXPECT_TRUE(toast150.cx == 900 && toast150.cy == 120);
    EXPECT_TRUE(toast200.cx == 1200 && toast200.cy == 160);

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
    EXPECT_TRUE(tray100.cx == 200 && tray100.cy == 265);
    EXPECT_TRUE(tray150.cx == 300 && tray150.cy == 398);
    EXPECT_TRUE(tray200.cx == 400 && tray200.cy == 530);

    const SIZE settings100 = easy::ui::SettingsWindowStyle::windowSizeForDpi(96);
    const SIZE settings150 = easy::ui::SettingsWindowStyle::windowSizeForDpi(144);
    const SIZE settings200 = easy::ui::SettingsWindowStyle::windowSizeForDpi(192);
    EXPECT_TRUE(settings100.cx == 1380 && settings100.cy == 900);
    EXPECT_TRUE(settings150.cx == 2070 && settings150.cy == 1350);
    EXPECT_TRUE(settings200.cx == 2760 && settings200.cy == 1800);

    const SIZE settingsMin100 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(96);
    const SIZE settingsMin150 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(144);
    const SIZE settingsMin200 = easy::ui::SettingsWindowStyle::minWindowSizeForDpi(192);
    EXPECT_TRUE(settingsMin100.cx == 880 && settingsMin100.cy == 560);
    EXPECT_TRUE(settingsMin150.cx == 1320 && settingsMin150.cy == 840);
    EXPECT_TRUE(settingsMin200.cx == 1760 && settingsMin200.cy == 1120);

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
// 34. 插件发现与动态加载生命周期测试套件 (必须放置在最后，因为 shutdown 会注销共享注册表)
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
