// ─────────────────────────────────────────────────────────────────────────────
// LuaEngine.cpp — Lua 脚本运行时实现
//
// 暴露给脚本的 API 命名空间 (对齐 implementation_plan.md §2.4):
//   easy.log       info / warn / error
//   easy.keyboard  sendKeys / keyDown / keyUp
//   easy.mouse     moveTo / click / scroll
//   easy.clipboard getText / setText
//   easy.shell     open / run / runAsync
//   easy.window    getForeground / getTitle / getClass / getProcess /
//                  minimize / maximize / restore / close / setTopmost
//   easy.screen    getPixelColor
//   easy.fs        readFile / writeFile / exists
//   easy.http      get / post
//   easy.ui        toast / notify / confirm / inputBox
//   easy.url       encode / decode
//
// 兼容旧脚本: 同时保留 easyTools.* 别名 (指向 easy 表)。
// ─────────────────────────────────────────────────────────────────────────────

#include "core/lua/LuaEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <array>
#include <fstream>
#include <sstream>
#include <utility>

namespace easy::core {

namespace {

// ── 键盘: 解析并发送组合键 (如 "Ctrl+Shift+T") ────────────────────────────────
WORD virtualKeyFromToken(const std::string& tok) {
    static const std::unordered_map<std::string, WORD> kMap = {
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4}, {"F5", VK_F5},
        {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8}, {"F9", VK_F9}, {"F10", VK_F10},
        {"F11", VK_F11}, {"F12", VK_F12},
        {"TAB", VK_TAB}, {"ENTER", VK_RETURN}, {"RETURN", VK_RETURN}, {"SPACE", VK_SPACE},
        {"ESC", VK_ESCAPE}, {"ESCAPE", VK_ESCAPE}, {"DELETE", VK_DELETE}, {"DEL", VK_DELETE},
        {"BACKSPACE", VK_BACK}, {"HOME", VK_HOME}, {"END", VK_END},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT}, {"INSERT", VK_INSERT},
    };
    std::string up = WinUtils::toLower(tok);
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = kMap.find(up);
    if (it != kMap.end()) return it->second;
    if (up.size() == 1) return static_cast<WORD>(up[0]);
    return 0;
}

void sendKeys(const std::string& combo) {
    uint8_t mods = 0;
    WORD vk = 0;
    std::stringstream ss(combo);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        // trim
        size_t b = tok.find_first_not_of(" \t");
        size_t e = tok.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        tok = tok.substr(b, e - b + 1);
        std::string low = WinUtils::toLower(tok);
        if (low == "ctrl" || low == "control") mods |= MOD_CONTROL;
        else if (low == "alt") mods |= MOD_ALT;
        else if (low == "shift") mods |= MOD_SHIFT;
        else if (low == "win" || low == "super" || low == "meta") mods |= MOD_WIN;
        else vk = virtualKeyFromToken(tok);
    }
    if (vk == 0) {
        LOG_WARN("[Lua] keyboard.sendKeys 无法解析主键: {}", combo);
        return;
    }

    std::vector<INPUT> inputs;
    auto push = [&](WORD k, bool up) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = k;
        in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        inputs.push_back(in);
    };
    static constexpr std::array<std::pair<uint8_t, WORD>, 4> kMods = {{
        {MOD_CONTROL, VK_CONTROL}, {MOD_ALT, VK_MENU}, {MOD_SHIFT, VK_SHIFT}, {MOD_WIN, VK_LWIN},
    }};
    for (auto [m, k] : kMods) if (mods & m) push(k, false);
    push(vk, false);
    push(vk, true);
    for (auto it = kMods.rbegin(); it != kMods.rend(); ++it) if (mods & it->first) push(it->second, true);
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

// ── 剪贴板读取 (UTF-8) ────────────────────────────────────────────────────────
std::string clipboardGetText() {
    if (!OpenClipboard(nullptr)) return {};
    std::string result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* wptr = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = WinUtils::wstringToUtf8(wptr);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

// ── 目标窗口: 显式句柄或默认前台窗口 ──────────────────────────────────────────
// 接受 sol::optional (绑定回调收到的类型); 在该 sol2 配置下它与 std::optional 不同型。
HWND resolveHwnd(sol::optional<int64_t> handle) {
    if (handle && *handle != 0) return reinterpret_cast<HWND>(static_cast<uintptr_t>(*handle));
    HWND fg = GetForegroundWindow();
    return fg ? GetAncestor(fg, GA_ROOT) : nullptr;
}

// ── URL 编码/解码 ─────────────────────────────────────────────────────────────
std::string urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hexVal = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (s[i] == '+') ? ' ' : s[i];
    }
    return out;
}

// ── 极简 HTTP 客户端 (WinHTTP)，返回 {status, body} ──────────────────────────
struct HttpResult {
    int status = 0;
    std::string body;
};

HttpResult httpRequest(const std::wstring& method, const std::string& url, const std::string& body) {
    HttpResult result;

    std::wstring wurl = WinUtils::utf8ToWstring(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]   = {};
    wchar_t path[2048]  = {};
    wchar_t extra[2048] = {};
    uc.lpszHostName   = host;   uc.dwHostNameLength   = ARRAYSIZE(host);
    uc.lpszUrlPath    = path;   uc.dwUrlPathLength    = ARRAYSIZE(path);
    uc.lpszExtraInfo  = extra;  uc.dwExtraInfoLength  = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        LOG_WARN("[Lua] http: 无效的 URL: {}", url);
        return result;
    }
    const bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring object = std::wstring(path) + extra;  // 路径 + ?查询串

    HINTERNET hSession = WinHttpOpen(L"EasyTools/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    HINTERNET hRequest = nullptr;
    if (hConnect) {
        hRequest = WinHttpOpenRequest(hConnect, method.c_str(), object.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      https ? WINHTTP_FLAG_SECURE : 0);
    }

    if (hRequest) {
        BOOL ok;
        if (body.empty()) {
            ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        } else {
            static constexpr wchar_t kCtype[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
            ok = WinHttpSendRequest(hRequest, kCtype, static_cast<DWORD>(-1L),
                                    const_cast<char*>(body.data()),
                                    static_cast<DWORD>(body.size()),
                                    static_cast<DWORD>(body.size()), 0);
        }

        if (ok && WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD statusCode = 0, len = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len, WINHTTP_NO_HEADER_INDEX);
            result.status = static_cast<int>(statusCode);

            DWORD avail = 0;
            do {
                avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (WinHttpReadData(hRequest, chunk.data(), avail, &read) && read > 0) {
                    result.body.append(chunk.data(), read);
                }
            } while (avail > 0);
        } else {
            LOG_WARN("[Lua] http 请求失败: {} (err={})", url, GetLastError());
        }
        WinHttpCloseHandle(hRequest);
    }
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

LuaEngine& LuaEngine::instance() {
    static LuaEngine inst;
    return inst;
}

bool LuaEngine::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        m_lua = std::make_unique<sol::state>();
        // 沙箱: 开放安全标准库; 不开放 io / debug。
        m_lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                              sol::lib::math, sol::lib::table, sol::lib::coroutine,
                              sol::lib::utf8, sol::lib::os);
        // 收紧 os: 保留 time/date/clock 等只读函数, 移除可改动系统状态的危险函数。
        if (sol::optional<sol::table> os = (*m_lua)["os"]) {
            for (const char* fn : {"execute", "remove", "rename", "exit", "tmpname", "setlocale"}) {
                (*os)[fn] = sol::nil;
            }
        }
        bindApi();
        LOG_INFO("Lua 脚本引擎初始化成功 (easy.* API 已注入)");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Lua 引擎初始化失败: {}", e.what());
        m_lua.reset();
        return false;
    }
}

void LuaEngine::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lua.reset();
    LOG_INFO("Lua 脚本引擎已关闭");
}

bool LuaEngine::runProtected(const std::function<sol::protected_function_result()>& fn,
                             const char* context) {
    if (!m_lua) {
        LOG_ERROR("[Lua] 引擎未初始化, 无法执行 {}", context);
        return false;
    }
    try {
        sol::protected_function_result r = fn();
        if (!r.valid()) {
            sol::error err = r;
            LOG_ERROR("[Lua] {} 执行错误: {}", context, err.what());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[Lua] {} 运行时异常: {}", context, e.what());
        return false;
    }
}

bool LuaEngine::executeScript(const std::string& script) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return runProtected([&] { return m_lua->safe_script(script, sol::script_pass_on_error); },
                        "executeScript");
}

bool LuaEngine::executeFile(const std::string& utf8Path) {
    std::ifstream f(WinUtils::utf8ToWstring(utf8Path).c_str(), std::ios::binary);
    if (!f) {
        LOG_ERROR("[Lua] 无法打开脚本文件: {}", utf8Path);
        return false;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    return executeScript(buf.str());
}

// ── API 绑定 ──────────────────────────────────────────────────────────────────

void LuaEngine::bindApi() {
    sol::table easy = m_lua->create_named_table("easy");
    bindLog(easy);
    bindKeyboard(easy);
    bindMouse(easy);
    bindClipboard(easy);
    bindShell(easy);
    bindWindow(easy);
    bindScreen(easy);
    bindFs(easy);
    bindHttp(easy);
    bindUi(easy);
    bindUrl(easy);

    // 向后兼容: easyTools 作为 easy 的别名
    (*m_lua)["easyTools"] = easy;
}

void LuaEngine::bindLog(sol::table& easy) {
    sol::table t = easy.create_named("log");
    t.set_function("info",  [](const std::string& m) { LOG_INFO("[Lua] {}", m); });
    t.set_function("warn",  [](const std::string& m) { LOG_WARN("[Lua] {}", m); });
    t.set_function("error", [](const std::string& m) { LOG_ERROR("[Lua] {}", m); });
}

void LuaEngine::bindKeyboard(sol::table& easy) {
    sol::table t = easy.create_named("keyboard");
    t.set_function("sendKeys", [](const std::string& combo) { sendKeys(combo); });
    t.set_function("keyDown", [](int vk) {
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = static_cast<WORD>(vk);
        SendInput(1, &in, sizeof(INPUT));
    });
    t.set_function("keyUp", [](int vk) {
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = static_cast<WORD>(vk);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    });
}

void LuaEngine::bindMouse(sol::table& easy) {
    sol::table t = easy.create_named("mouse");
    t.set_function("moveTo", [](int x, int y) { SetCursorPos(x, y); });
    t.set_function("click", [](int button) {
        DWORD down = MOUSEEVENTF_LEFTDOWN, up = MOUSEEVENTF_LEFTUP;
        if (button == 2)      { down = MOUSEEVENTF_RIGHTDOWN;  up = MOUSEEVENTF_RIGHTUP; }
        else if (button == 3) { down = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; }
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = down;
        in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = up;
        SendInput(2, in, sizeof(INPUT));
    });
    t.set_function("scroll", [](int amount) {
        INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(amount * WHEEL_DELTA);
        SendInput(1, &in, sizeof(INPUT));
    });
}

void LuaEngine::bindClipboard(sol::table& easy) {
    sol::table t = easy.create_named("clipboard");
    t.set_function("getText", []() { return clipboardGetText(); });
    t.set_function("setText", [](const std::string& s) { return WinUtils::copyToClipboard(s); });
}

void LuaEngine::bindShell(sol::table& easy) {
    sol::table t = easy.create_named("shell");
    t.set_function("open", [](const std::string& target) {
        ShellExecuteW(nullptr, L"open", WinUtils::utf8ToWstring(target).c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
    });
    auto runImpl = [](const std::string& cmd, bool wait) {
        std::wstring wcmd = WinUtils::utf8ToWstring(cmd);
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        // CreateProcessW 需要可写的命令行缓冲
        std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
        buf.push_back(L'\0');
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            if (wait) WaitForSingleObject(pi.hProcess, 30000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
        LOG_WARN("[Lua] shell.run 启动失败: {}", cmd);
        return false;
    };
    t.set_function("run",      [runImpl](const std::string& cmd) { return runImpl(cmd, true); });
    t.set_function("runAsync", [runImpl](const std::string& cmd) { return runImpl(cmd, false); });
}

void LuaEngine::bindWindow(sol::table& easy) {
    sol::table t = easy.create_named("window");
    t.set_function("getForeground", []() -> int64_t {
        return static_cast<int64_t>(reinterpret_cast<uintptr_t>(resolveHwnd(sol::optional<int64_t>{})));
    });
    t.set_function("getTitle", [](sol::optional<int64_t> h) {
        return WinUtils::wstringToUtf8(WinUtils::getWindowTitle(resolveHwnd(h)));
    });
    t.set_function("getClass", [](sol::optional<int64_t> h) {
        return WinUtils::wstringToUtf8(WinUtils::getWindowClassName(resolveHwnd(h)));
    });
    t.set_function("getProcess", [](sol::optional<int64_t> h) {
        return WinUtils::getProcessNameFromWindow(resolveHwnd(h));
    });
    t.set_function("minimize", [](sol::optional<int64_t> h) { ShowWindow(resolveHwnd(h), SW_MINIMIZE); });
    t.set_function("maximize", [](sol::optional<int64_t> h) { ShowWindow(resolveHwnd(h), SW_MAXIMIZE); });
    t.set_function("restore",  [](sol::optional<int64_t> h) { ShowWindow(resolveHwnd(h), SW_RESTORE); });
    t.set_function("close",    [](sol::optional<int64_t> h) {
        if (HWND w = resolveHwnd(h)) PostMessageW(w, WM_CLOSE, 0, 0);
    });
    t.set_function("setTopmost", [](bool topmost, sol::optional<int64_t> h) {
        if (HWND w = resolveHwnd(h))
            SetWindowPos(w, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
}

void LuaEngine::bindScreen(sol::table& easy) {
    sol::table t = easy.create_named("screen");
    // 返回 {r, g, b}
    t.set_function("getPixelColor", [this](int x, int y) {
        HDC hdc = GetDC(nullptr);
        COLORREF c = GetPixel(hdc, x, y);
        ReleaseDC(nullptr, hdc);
        sol::table rgb = m_lua->create_table();
        rgb["r"] = GetRValue(c);
        rgb["g"] = GetGValue(c);
        rgb["b"] = GetBValue(c);
        return rgb;
    });
}

void LuaEngine::bindFs(sol::table& easy) {
    sol::table t = easy.create_named("fs");
    t.set_function("exists", [](const std::string& p) {
        return std::filesystem::exists(WinUtils::utf8ToWstring(p));
    });
    t.set_function("readFile", [](const std::string& p) -> std::string {
        std::ifstream f(WinUtils::utf8ToWstring(p).c_str(), std::ios::binary);
        if (!f) return {};
        std::stringstream buf; buf << f.rdbuf();
        return buf.str();
    });
    t.set_function("writeFile", [](const std::string& p, const std::string& content) {
        std::ofstream f(WinUtils::utf8ToWstring(p).c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        return static_cast<bool>(f);
    });
}

void LuaEngine::bindHttp(sol::table& easy) {
    sol::table t = easy.create_named("http");
    t.set_function("get", [this](const std::string& url) {
        HttpResult r = httpRequest(L"GET", url, {});
        sol::table out = m_lua->create_table();
        out["status"] = r.status;
        out["body"] = r.body;
        return out;
    });
    t.set_function("post", [this](const std::string& url, sol::optional<std::string> body) {
        HttpResult r = httpRequest(L"POST", url, body.value_or(""));
        sol::table out = m_lua->create_table();
        out["status"] = r.status;
        out["body"] = r.body;
        return out;
    });
}

void LuaEngine::bindUi(sol::table& easy) {
    sol::table t = easy.create_named("ui");
    auto box = [](const std::string& msg, const std::string& title, UINT flags) {
        return MessageBoxW(nullptr, WinUtils::utf8ToWstring(msg).c_str(),
                           WinUtils::utf8ToWstring(title).c_str(), flags | MB_TOPMOST);
    };
    t.set_function("toast", [box](const std::string& msg) {
        box(msg, "EasyTools", MB_OK | MB_ICONINFORMATION);
    });
    t.set_function("notify", [box](const std::string& title, const std::string& msg) {
        box(msg, title, MB_OK | MB_ICONINFORMATION);
    });
    t.set_function("confirm", [box](const std::string& msg) {
        return box(msg, "EasyTools", MB_YESNO | MB_ICONQUESTION) == IDYES;
    });
    // inputBox: 受限于无原生输入框，暂以剪贴板内容回填 (后续可接 WebView 弹窗)
    t.set_function("inputBox", [](const std::string& /*prompt*/) { return clipboardGetText(); });
}

void LuaEngine::bindUrl(sol::table& easy) {
    sol::table t = easy.create_named("url");
    t.set_function("encode", [](const std::string& s) { return urlEncode(s); });
    t.set_function("decode", [](const std::string& s) { return urlDecode(s); });
}

}  // namespace easy::core
