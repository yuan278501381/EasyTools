#include "core/lua/LuaEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"

#include <windows.h>
#include <shellapi.h>

namespace easy::core {

LuaEngine& LuaEngine::instance() {
    static LuaEngine inst;
    return inst;
}

bool LuaEngine::initialize() {
    try {
        m_lua = std::make_unique<sol::state>();
        m_lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::os);
        bindApi();
        LOG_INFO("Lua 脚本引擎初始化成功");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Lua 引擎初始化失败: {}", e.what());
        return false;
    }
}

bool LuaEngine::executeScript(const std::string& script) {
    if (!m_lua) return false;

    try {
        auto result = m_lua->safe_script(script);
        if (!result.valid()) {
            sol::error err = result;
            LOG_ERROR("Lua 执行错误: {}", err.what());
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Lua 运行时异常: {}", e.what());
        return false;
    }
}

void LuaEngine::bindApi() {
    // 创建一个全局表 easyTools
    sol::table easyTools = m_lua->create_named_table("easyTools");

    // 1. 鼠标控制 API
    easyTools.set_function("mouseMove", [](int x, int y) {
        SetCursorPos(x, y);
    });

    easyTools.set_function("mouseClick", [](int button) {
        // button: 1=Left, 2=Right, 3=Middle
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        if (button == 1) {
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(INPUT));
        } else if (button == 2) {
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            SendInput(1, &input, sizeof(INPUT));
        } else if (button == 3) {
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            SendInput(1, &input, sizeof(INPUT));
        }
    });

    // 2. 键盘控制 API (简单实现)
    easyTools.set_function("keyboardPress", [](int vkCode) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = static_cast<WORD>(vkCode);

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = static_cast<WORD>(vkCode);
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
    });

    // 3. 执行系统命令/打开文件
    easyTools.set_function("runCommand", [](const std::string& cmd) {
        std::wstring wcmd = easy::core::WinUtils::utf8ToWide(cmd);
        ShellExecuteW(nullptr, L"open", wcmd.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    });

    // 4. 弹窗提示
    easyTools.set_function("alert", [](const std::string& msg) {
        std::wstring wmsg = easy::core::WinUtils::utf8ToWide(msg);
        MessageBoxW(nullptr, wmsg.c_str(), L"EasyTools - Lua Script", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
    });

    // 5. 打印日志
    easyTools.set_function("logInfo", [](const std::string& msg) {
        LOG_INFO("[Lua] {}", msg);
    });
}

} // namespace easy::core
