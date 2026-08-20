// ─────────────────────────────────────────────────────────────────────────────
// GestureAction.cpp — 手势动作执行与序列化
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureAction.h"
#include "gesture/BuiltinCommands.h"
#include "gesture/GestureInputPolicy.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"
#include "core/utils/WinUtils.h"
#include "core/lua/LuaEngine.h"

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <array>
#include <chrono>
#include <format>
#include <string>
#include <thread>

namespace easy::gesture {

// ── KeyStroke ────────────────────────────────────────────────────────────────

KeyStroke KeyStroke::fromString(const std::string& str) {
    KeyStroke ks;
    std::string remaining = str;

    // 解析修饰键
    auto consume = [&](const std::string& prefix, uint8_t mod) {
        size_t pos = remaining.find(prefix);
        if (pos != std::string::npos) {
            remaining.erase(pos, prefix.size());
            ks.modifiers |= mod;
        }
    };

    consume("Ctrl+",  MOD_CONTROL);
    consume("Alt+",   MOD_ALT);
    consume("Shift+", MOD_SHIFT);
    consume("Win+",   MOD_WIN);

    // 特殊键与多媒体键映射表
    static const std::unordered_map<std::string, uint16_t> keyMap = {
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5},
        {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10},
        {"F11", VK_F11}, {"F12", VK_F12},
        {"Tab", VK_TAB}, {"Enter", VK_RETURN}, {"Return", VK_RETURN}, {"Space", VK_SPACE},
        {"Escape", VK_ESCAPE}, {"Esc", VK_ESCAPE},
        {"Backspace", VK_BACK}, {"Back", VK_BACK},
        {"Delete", VK_DELETE}, {"Del", VK_DELETE},
        {"Insert", VK_INSERT}, {"Ins", VK_INSERT},
        {"Home", VK_HOME}, {"End", VK_END},
        {"PageUp", VK_PRIOR}, {"PgUp", VK_PRIOR},
        {"PageDown", VK_NEXT}, {"PgDn", VK_NEXT},
        {"Left", VK_LEFT}, {"Right", VK_RIGHT}, {"Up", VK_UP}, {"Down", VK_DOWN},
        {"MediaNext", VK_MEDIA_NEXT_TRACK}, {"MediaPrev", VK_MEDIA_PREV_TRACK},
        {"MediaPlay", VK_MEDIA_PLAY_PAUSE}, {"MediaPlayPause", VK_MEDIA_PLAY_PAUSE},
        {"VolumeMute", VK_VOLUME_MUTE}, {"VolumeUp", VK_VOLUME_UP}, {"VolumeDown", VK_VOLUME_DOWN},
    };

    auto it = keyMap.find(remaining);
    if (it != keyMap.end()) {
        ks.virtualKey = it->second;
    } else if (remaining.size() >= 3 && remaining.starts_with("0x")) {
        try {
            ks.virtualKey = static_cast<uint16_t>(std::stoul(remaining.substr(2), nullptr, 16));
        } catch (...) {
            ks.virtualKey = 0;
        }
    } else if (remaining.size() == 1) {
        ks.virtualKey = static_cast<uint16_t>(std::toupper(static_cast<unsigned char>(remaining[0])));
    }

    return ks;
}

std::string KeyStroke::toString() const {
    std::string result;
    if (modifiers & MOD_CONTROL) result += "Ctrl+";
    if (modifiers & MOD_ALT)     result += "Alt+";
    if (modifiers & MOD_SHIFT)   result += "Shift+";
    if (modifiers & MOD_WIN)     result += "Win+";

    // 虚拟键码 → 名称
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= '0' && virtualKey <= '9') {
        result += static_cast<char>(virtualKey);
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        result += "F" + std::to_string(virtualKey - VK_F1 + 1);
    } else {
        static const std::unordered_map<uint16_t, std::string> revMap = {
            {VK_TAB, "Tab"}, {VK_RETURN, "Enter"}, {VK_SPACE, "Space"},
            {VK_ESCAPE, "Escape"}, {VK_BACK, "Backspace"}, {VK_DELETE, "Delete"},
            {VK_INSERT, "Insert"}, {VK_HOME, "Home"}, {VK_END, "End"},
            {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
            {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_UP, "Up"}, {VK_DOWN, "Down"},
            {VK_MEDIA_NEXT_TRACK, "MediaNext"}, {VK_MEDIA_PREV_TRACK, "MediaPrev"},
            {VK_MEDIA_PLAY_PAUSE, "MediaPlayPause"}, {VK_VOLUME_MUTE, "VolumeMute"},
            {VK_VOLUME_UP, "VolumeUp"}, {VK_VOLUME_DOWN, "VolumeDown"},
        };
        if (auto it = revMap.find(virtualKey); it != revMap.end()) {
            result += it->second;
        } else {
            result += "0x" + std::format("{:02X}", virtualKey);
        }
    }
    return result;
}

namespace {

bool isGlobalKey(WORD vk) {
    return (vk >= VK_VOLUME_MUTE && vk <= VK_MEDIA_PLAY_PAUSE) ||
           (vk >= 0xB4 && vk <= 0xB7) ||
           (vk == VK_SNAPSHOT);
}

std::wstring windowClassName(HWND hwnd) noexcept {
    wchar_t cls[256] = {};
    if (hwnd) GetClassNameW(hwnd, cls, 256);
    return cls;
}

std::string describeWindow(HWND hwnd) {
    if (!hwnd) return "null";
    if (!IsWindow(hwnd)) {
        return std::format("0x{:X}(stale)", reinterpret_cast<uintptr_t>(hwnd));
    }
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    return std::format("0x{:X} class={} pid={}",
                       reinterpret_cast<uintptr_t>(root),
                       easy::core::WinUtils::wstringToUtf8(windowClassName(root)),
                       pid);
}

bool isEasyToolsUiWindow(HWND hwnd) noexcept {
    if (!hwnd || !IsWindow(hwnd)) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;
    return isEasyToolsUiClassName(windowClassName(root));
}

bool isGesturePassThroughWindow(HWND hwnd) noexcept {
    if (!hwnd || !IsWindow(hwnd)) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root) root = hwnd;
    if (isGestureOverlayClassName(windowClassName(root))) return true;
    const LONG_PTR ex = GetWindowLongPtrW(root, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT) && (ex & WS_EX_TOPMOST) &&
        (ex & WS_EX_NOACTIVATE) && isEasyToolsUiWindow(root)) {
        return true;
    }
    return false;
}

HWND asLiveWindow(HWND hwnd) noexcept {
    if (!hwnd || !IsWindow(hwnd)) return nullptr;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}

bool isWindowCloaked(HWND hwnd) noexcept {
    BOOL cloaked = FALSE;
    if (!hwnd) return false;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return false;
    }
    return cloaked != FALSE;
}

bool windowContainsPoint(HWND hwnd, POINT pt) noexcept {
    RECT rc{};
    return hwnd && GetWindowRect(hwnd, &rc) && PtInRect(&rc, pt) != FALSE;
}

bool isAcceptedGestureHitWindow(HWND hwnd, POINT pt) noexcept {
    HWND root = asLiveWindow(hwnd);
    if (!root) return false;
    return gestureHitTestAcceptsWindow(
        IsWindowVisible(root) != FALSE,
        isGesturePassThroughWindow(root),
        isEasyToolsUiWindow(root),
        isWindowCloaked(root),
        windowContainsPoint(root, pt));
}

struct EnumGestureHitCtx {
    POINT pt{};
    HWND result = nullptr;
};

BOOL CALLBACK enumGestureHitProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumGestureHitCtx*>(lp);
    if (!ctx) return FALSE;
    if (!isAcceptedGestureHitWindow(hwnd, ctx->pt)) return TRUE;
    ctx->result = asLiveWindow(hwnd);
    return FALSE;
}

HWND firstExternalWindow(HWND a, HWND b, HWND c) noexcept {
    for (HWND h : {asLiveWindow(a), asLiveWindow(b), asLiveWindow(c)}) {
        if (h && !isEasyToolsUiWindow(h)) return h;
    }
    return nullptr;
}

struct ThreadInputAttach {
    DWORD self = 0;
    DWORD other = 0;
    bool attached = false;

    ThreadInputAttach(DWORD selfTid, DWORD otherTid) : self(selfTid), other(otherTid) {
        if (other && other != self) {
            attached = AttachThreadInput(self, other, TRUE) != FALSE;
        }
    }
    ~ThreadInputAttach() {
        if (attached) AttachThreadInput(self, other, FALSE);
    }
    ThreadInputAttach(const ThreadInputAttach&) = delete;
    ThreadInputAttach& operator=(const ThreadInputAttach&) = delete;
};

void pulseForegroundUnlock() noexcept {
    INPUT inp{};
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = VK_MENU;
    inp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(INPUT));
}

bool activateTargetWindow(HWND targetHwnd, bool allowWait) noexcept {
    targetHwnd = asLiveWindow(targetHwnd);
    if (!targetHwnd || isEasyToolsUiWindow(targetHwnd)) {
        LOG_WARN("手势目标窗口不可用或属于 EasyTools UI: {}", describeWindow(targetHwnd));
        return false;
    }

    AllowSetForegroundWindow(ASFW_ANY);
    LockSetForegroundWindow(LSFW_UNLOCK);

    HWND curFg = GetForegroundWindow();
    if (curFg == targetHwnd) return true;

    const DWORD selfTid = GetCurrentThreadId();
    const DWORD fgTid = curFg ? GetWindowThreadProcessId(curFg, nullptr) : 0;
    const DWORD targetTid = GetWindowThreadProcessId(targetHwnd, nullptr);
    ThreadInputAttach attachFg(selfTid, fgTid);
    ThreadInputAttach attachTarget(selfTid, targetTid);

    if (IsIconic(targetHwnd)) ShowWindow(targetHwnd, SW_RESTORE);
    BringWindowToTop(targetHwnd);
    SetForegroundWindow(targetHwnd);

    if (GetForegroundWindow() != targetHwnd) {
        pulseForegroundUnlock();
        SetForegroundWindow(targetHwnd);
        BringWindowToTop(targetHwnd);
    }

    if (allowWait && GetForegroundWindow() != targetHwnd) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
        while (std::chrono::steady_clock::now() < deadline) {
            SetForegroundWindow(targetHwnd);
            if (GetForegroundWindow() == targetHwnd) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    const HWND nowFg = GetForegroundWindow();
    const bool ok = (nowFg == targetHwnd);
    if (!ok) {
        wchar_t cls[256] = {};
        GetClassNameW(targetHwnd, cls, 256);
        LOG_WARN("手势目标窗口未能取得前台: hwnd=0x{:X}, class={}, fg=0x{:X}",
                 reinterpret_cast<uintptr_t>(targetHwnd),
                 easy::core::WinUtils::wstringToUtf8(cls),
                 reinterpret_cast<uintptr_t>(nowFg));
    }
    return ok;
}

} // namespace

void* resolveGestureKeyTarget(void* candidate, void* gestureStart, void* previousForeground) noexcept {
    HWND resolved = firstExternalWindow(
        static_cast<HWND>(candidate),
        static_cast<HWND>(gestureStart),
        static_cast<HWND>(previousForeground));
    return resolved;
}

void* windowFromPointSkippingGestureOverlay(int x, int y) noexcept {
    POINT pt = {x, y};
    HWND quick = asLiveWindow(WindowFromPoint(pt));
    if (isAcceptedGestureHitWindow(quick, pt)) {
        return quick;
    }

    EnumGestureHitCtx ctx{};
    ctx.pt = pt;
    EnumWindows(enumGestureHitProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

bool gestureActionNeedsInputThread(ActionType type) noexcept {
    return type == ActionType::SendKeys || type == ActionType::BuiltinCommand;
}

// ── KeyStroke::send ──────────────────────────────────────────────────────────

void KeyStroke::send(void* targetWindowPtr) const {
    if (virtualKey == 0) {
        LOG_WARN("KeyStroke::send 被调用但 virtualKey 为空, 跳过");
        return;
    }

    HWND targetHwnd = asLiveWindow(static_cast<HWND>(targetWindowPtr));
    if (isEasyToolsUiWindow(targetHwnd) || isGesturePassThroughWindow(targetHwnd)) {
        LOG_WARN("拒绝向 EasyTools UI 注入按键: keys={}, target={}",
                 toString(), describeWindow(targetHwnd));
        targetHwnd = nullptr;
    }

    HWND fgBefore = GetForegroundWindow();
    if (!isGlobalKey(virtualKey) && !targetHwnd) {
        POINT pt{};
        GetCursorPos(&pt);
        targetHwnd = static_cast<HWND>(resolveGestureKeyTarget(
            fgBefore, windowFromPointSkippingGestureOverlay(pt.x, pt.y), nullptr));
    }

    LOG_INFO("手势按键注入: keys={}, target={}, fg={}",
             toString(), describeWindow(targetHwnd), describeWindow(fgBefore));

    if (!isGlobalKey(virtualKey) && !targetHwnd) {
        LOG_WARN("无外部目标窗口，放弃按键注入: keys={}, fg={}",
                 toString(), describeWindow(fgBefore));
        return;
    }

    if (!isGlobalKey(virtualKey) && targetHwnd && keyStrokeShouldPostClose(modifiers, virtualKey)) {
        PostMessageW(targetHwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        PostMessageW(targetHwnd, WM_CLOSE, 0, 0);
        LOG_INFO("关闭窗口已投递: {}", describeWindow(targetHwnd));
        return;
    }

    // 非全局键先把输入焦点切到目标窗口，再原子发送。切不过则放弃，避免 Ctrl+W 打进前台的设置页。
    if (!isGlobalKey(virtualKey) && targetHwnd) {
        if (!activateTargetWindow(targetHwnd, /*allowWait=*/true)) {
            LOG_WARN("未能激活目标窗口，放弃注入: keys={}, target={}, fg={}",
                     toString(), describeWindow(targetHwnd), describeWindow(GetForegroundWindow()));
            return;
        }
    }

    // 2. 构造原子性 INPUT 数组: 按下修饰键 → 主键按下 → 主键释放 → 逆序释放修饰键
    // 必须在单次 SendInput 调用中原子性提交全部 Down 与 Up，绝不在中间插入 Sleep，彻底杜绝修饰键粘滞与幽灵按键问题
    std::vector<INPUT> inputs;
    inputs.reserve(10);

    auto addKey = [&](WORD vk, bool up) {
        INPUT inp{};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = vk;
        inp.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        inp.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0);
        if (vk == VK_LWIN || vk == VK_RWIN || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_UP || vk == VK_DOWN || vk == VK_DELETE || vk == VK_INSERT ||
            vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
            vk == VK_APPS) {
            inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        inputs.push_back(inp);
    };

    // 修饰键按下顺序
    static constexpr std::array<std::pair<uint8_t, WORD>, 4> kMods = {{
        {MOD_CONTROL, VK_CONTROL}, {MOD_ALT, VK_MENU},
        {MOD_SHIFT, VK_SHIFT},     {MOD_WIN, VK_LWIN},
    }};

    // 按下修饰键
    for (auto [mod, vk] : kMods) {
        if (modifiers & mod) addKey(vk, /*up=*/false);
    }
    // 按下主键
    addKey(virtualKey, /*up=*/false);

    // 释放主键
    addKey(virtualKey, /*up=*/true);
    // 逆序释放修饰键
    for (auto it = kMods.rbegin(); it != kMods.rend(); ++it) {
        if (modifiers & it->first) addKey(it->second, /*up=*/true);
    }

    // 原子性提交整组按键
    UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        LOG_WARN("SendInput 未完全发送: expected={}, sent={}, lastError={}",
                 inputs.size(), sent, GetLastError());
    }
}

// ── GestureAction::execute ───────────────────────────────────────────────────

void GestureAction::execute(void* targetWindowPtr) const {
    easy::core::TraceId::Scope scope;

    switch (type) {
        case ActionType::SendKeys: {
            LOG_DEBUG("执行手势动作: SendKeys, keys={}", keyStroke.toString());
            keyStroke.send(targetWindowPtr);
            break;
        }

        case ActionType::LuaScript: {
            LOG_DEBUG("执行手势动作: LuaScript, name={}, script={}", name, luaScript.substr(0, std::min(luaScript.size(), (size_t)100)));
            easy::core::ScriptContext ctx;
            ctx.scriptId = "gesture:" + (name.empty() ? std::to_string(std::hash<std::string>{}(luaScript)) : name);
            ctx.scriptName = name.empty() ? "自定义手势脚本" : name;
            ctx.requestedPerms = requestedPermissions.empty()
                ? easy::core::LuaPermission::Safe
                : easy::core::parseLuaPermissions(requestedPermissions);
            ctx.interactive = true;
            easy::core::LuaEngine::instance().authorizeAndExecute(luaScript, ctx);
            break;
        }

        case ActionType::BuiltinCommand: {
            LOG_DEBUG("执行手势动作: BuiltinCommand, cmd={}", static_cast<int>(builtinCmd));
            BuiltinCommandDispatcher::instance().execute(builtinCmd, targetWindowPtr);
            break;
        }

        case ActionType::RunProgram: {
            LOG_DEBUG("执行手势动作: RunProgram, path={}", programPath);
            ShellExecuteA(nullptr, "open", programPath.c_str(),
                          programArgs.empty() ? nullptr : programArgs.c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        }
    }
}

// ── JSON 序列化 ──────────────────────────────────────────────────────────────

nlohmann::json GestureAction::toJson() const {
    nlohmann::json j;
    j["type"] = static_cast<int>(type);
    j["name"] = name;
    j["description"] = description;

    switch (type) {
        case ActionType::SendKeys:
            j["keyStroke"] = keyStroke.toString();
            break;
        case ActionType::LuaScript:
            j["luaScript"] = luaScript;
            if (!requestedPermissions.empty()) {
                j["permissions"] = requestedPermissions;
            }
            break;
        case ActionType::BuiltinCommand:
            j["builtinCmd"] = static_cast<int>(builtinCmd);
            break;
        case ActionType::RunProgram:
            j["programPath"] = programPath;
            j["programArgs"] = programArgs;
            break;
    }
    return j;
}

GestureAction GestureAction::fromJson(const nlohmann::json& j) {
    GestureAction action;
    action.type = static_cast<ActionType>(j.value("type", 0));
    action.name = j.value("name", "");
    action.description = j.value("description", "");

    switch (action.type) {
        case ActionType::SendKeys:
            action.keyStroke = KeyStroke::fromString(j.value("keyStroke", ""));
            break;
        case ActionType::LuaScript:
            action.luaScript = j.value("luaScript", "");
            if (j.contains("permissions") && j["permissions"].is_array()) {
                action.requestedPermissions = j["permissions"].get<std::vector<std::string>>();
            }
            break;
        case ActionType::BuiltinCommand:
            action.builtinCmd = static_cast<BuiltinCommand>(j.value("builtinCmd", 0));
            break;
        case ActionType::RunProgram:
            action.programPath = j.value("programPath", "");
            action.programArgs = j.value("programArgs", "");
            break;
    }
    return action;
}

nlohmann::json GestureMapping::toJson() const {
    nlohmann::json j;
    if (!id.empty()) j["id"] = id;
    j["enabled"] = enabled;
    j["instantExecute"] = instantExecute;
    j["silentToast"] = silentToast;
    j["gestureCode"] = gestureCode;
    j["action"] = action.toJson();
    return j;
}

GestureMapping GestureMapping::fromJson(const nlohmann::json& j) {
    GestureMapping mapping;
    mapping.id = j.value("id", "");
    mapping.enabled = j.value("enabled", true);
    mapping.instantExecute = j.value("instantExecute", false);
    mapping.silentToast = j.value("silentToast", false);
    mapping.gestureCode = j.value("gestureCode", "");
    if (j.contains("action")) {
        mapping.action = GestureAction::fromJson(j["action"]);
    }
    return mapping;
}

}  // namespace easy::gesture
