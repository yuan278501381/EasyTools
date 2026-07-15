#include "core/hotkey/KeyboardHook.h"
#include "core/logger/Logger.h"
#include "core/stats/StatsManager.h"
#include <vector>
#include <string>
#include <format>
#include <mutex>

namespace easy::core {

KeyboardHook& KeyboardHook::instance() {
    static KeyboardHook inst;
    return inst;
}

bool KeyboardHook::install() {
    if (m_hookHandle) {
        LOG_WARN("Hook already installed");
        return true;
    }

    m_hookHandle = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        lowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0  // 全局钩子
    );

    if (!m_hookHandle) {
        LOG_ERROR("Hook installation failed, error={}", GetLastError());
        return false;
    }

    LOG_INFO("Hook installed");
    return true;
}

void KeyboardHook::uninstall() {
    if (m_hookHandle) {
        UnhookWindowsHookEx(m_hookHandle);
        m_hookHandle = nullptr;
        LOG_INFO("Hook uninstalled");
    }
}

void KeyboardHook::setKeycastCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard lock(m_callbackMutex);
    m_keycastCallback = std::move(cb);
}

LRESULT CALLBACK KeyboardHook::lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    auto& self = KeyboardHook::instance();

    if (nCode >= 0 && !self.m_paused.load(std::memory_order_relaxed)) {
        try {
            auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

            // 忽略注入的按键
            if (!(data->flags & LLKHF_INJECTED)) {
                if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                    // 记录按键统计
                    StatsManager::instance().recordKey(data->vkCode);

                    // --- 广义组合键回显逻辑 ---
                    std::function<void(const std::string&)> keycastCallback;
                    {
                        std::lock_guard lock(self.m_callbackMutex);
                        keycastCallback = self.m_keycastCallback;
                    }
                    if (keycastCallback) {
                        static std::vector<std::string> currentSequence;
                        static uint64_t lastKeyTick = 0;
                        static std::mutex sequenceMutex;
                        std::lock_guard<std::mutex> lock(sequenceMutex);

                        uint64_t now = GetTickCount64();

                        // 超时清空序列 (1500ms 内没有新按键则打断)
                        if (now - lastKeyTick > 1500) {
                            currentSequence.clear();
                        }
                        lastKeyTick = now;

                        // 转换 vkCode 为按键名
                        char keyName[64] = {0};
                        UINT scanCode = MapVirtualKeyW(data->vkCode, MAPVK_VK_TO_VSC);
                        
                        // 处理特殊键扩展位
                        switch (data->vkCode) {
                            case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
                            case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
                            case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
                            case VK_NUMLOCK:
                                scanCode |= KF_EXTENDED;
                                break;
                        }

                        if (GetKeyNameTextA(scanCode << 16, keyName, sizeof(keyName)) > 0) {
                            std::string keyStr(keyName);

                            // 修饰键过滤：如果只有修饰键，也可直接显示，但通常我们要拼装
                            // 为了更美观，可以把 LCTRL / RCTRL 等统一为 Ctrl
                            if (data->vkCode == VK_LCONTROL || data->vkCode == VK_RCONTROL) keyStr = "Ctrl";
                            else if (data->vkCode == VK_LMENU || data->vkCode == VK_RMENU) keyStr = "Alt";
                            else if (data->vkCode == VK_LSHIFT || data->vkCode == VK_RSHIFT) keyStr = "Shift";
                            else if (data->vkCode == VK_LWIN || data->vkCode == VK_RWIN) keyStr = "Win";

                            // 避免重复推入修饰键（比如按住 Ctrl 不放）
                            if (currentSequence.empty() || currentSequence.back() != keyStr) {
                                currentSequence.push_back(keyStr);
                            }
                            
                            // 拼接显示
                            std::string display;
                            for (size_t i = 0; i < currentSequence.size(); ++i) {
                                if (i > 0) display += " \xe2\x9e\x9c "; // arrow ->
                                display += currentSequence[i];
                            }
                            
                            keycastCallback(display);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("KeyboardHook 发生未捕获异常 {}", e.what());
        } catch (...) {
            LOG_ERROR("KeyboardHook 发生未知异常");
        }
    }

    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
}

} // namespace easy::core
