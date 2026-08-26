#include "core/hotkey/KeyboardHook.h"
#include "core/logger/Logger.h"
#include "core/stats/StatsManager.h"
#include "core/config/ConfigManager.h"
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

void KeyboardHook::setKeyInterceptor(std::function<bool(DWORD vkCode, WPARAM wParam)> interceptor) {
    std::lock_guard lock(m_callbackMutex);
    m_keyInterceptor = std::move(interceptor);
}

void KeyboardHook::setKeyboardActivityCallback(std::function<void(DWORD vkCode, WPARAM wParam)> cb) {
    std::lock_guard lock(m_callbackMutex);
    m_activityCallback = std::move(cb);
}

LRESULT CALLBACK KeyboardHook::lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    auto& self = KeyboardHook::instance();

    if (nCode >= 0 && !self.m_paused.load(std::memory_order_relaxed)) {
        try {
            auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

            // 忽略注入的按键
            if (!(data->flags & LLKHF_INJECTED)) {
                // 优先执行键盘活动监听器（例如鼠标聚光灯双击 Ctrl 与退场检测）
                std::function<void(DWORD, WPARAM)> actCallback;
                {
                    std::lock_guard lock(self.m_callbackMutex);
                    actCallback = self.m_activityCallback;
                }
                if (actCallback) {
                    actCallback(data->vkCode, wParam);
                }

                // 优先执行自定义按键拦截器（例如 QuickLook 空格预览）
                std::function<bool(DWORD, WPARAM)> interceptor;
                {
                    std::lock_guard lock(self.m_callbackMutex);
                    interceptor = self.m_keyInterceptor;
                }
                if (interceptor && interceptor(data->vkCode, wParam)) {
                    return 1; // 消费并拦截按键
                }

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
                        // 检查按键过滤模式 (Filter Mode)
                        // 1. "smart_shortcuts": 智能过滤（仅显示真正组合键与特殊功能键，严格抑制单点 Shift/Ctrl/Alt，杜绝打字打扰）
                        // 2. "with_single_modifiers": 显示单按修饰键与 Alt 菜单激活序列
                        // 3. "all_keys": 全量按键回显
                        std::string filterMode = easy::core::ConfigManager::instance().get<std::string>(
                            "/keycast/filterMode", "");
                        if (filterMode.empty()) {
                            bool onlyShortcuts = easy::core::ConfigManager::instance().get<bool>(
                                "/general/keycastOnlyShortcuts", true);
                            filterMode = onlyShortcuts ? "smart_shortcuts" : "all_keys";
                        }

                        DWORD vk = data->vkCode;
                        bool isMod = (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                                      vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                                      vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                                      vk == VK_LWIN || vk == VK_RWIN);

                        bool isFunctionalKey = (vk >= VK_F1 && vk <= VK_F24) ||
                                               (vk == VK_ESCAPE || vk == VK_TAB || vk == VK_RETURN || vk == VK_BACK ||
                                                vk == VK_DELETE || vk == VK_INSERT || vk == VK_HOME || vk == VK_END ||
                                                vk == VK_PRIOR || vk == VK_NEXT || vk == VK_CAPITAL || vk == VK_SNAPSHOT ||
                                                vk == VK_PAUSE || vk == VK_SCROLL || vk == VK_LEFT || vk == VK_UP ||
                                                vk == VK_RIGHT || vk == VK_DOWN || vk == VK_SPACE);

                        bool hasCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                        bool hasAlt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                        bool hasShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                        bool hasWin  = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                        
                        // 是否存在修饰键伴随实际非修饰主键（如 Ctrl+C, Win+D, Alt+Tab）
                        bool isComboWithModifier = (hasCtrl || hasAlt || hasWin || hasShift) && !isMod;

                        bool shouldDisplay = false;
                        if (filterMode == "all_keys") {
                            shouldDisplay = true;
                        } else if (filterMode == "with_single_modifiers") {
                            // 包含单按修饰键、组合键、功能键
                            shouldDisplay = isMod || isComboWithModifier || isFunctionalKey;
                        } else {
                            // "smart_shortcuts" 智能模式：严格抑制孤立修饰键单点，仅当组合键或功能键时显示
                            shouldDisplay = isComboWithModifier || isFunctionalKey;
                        }

                        if (shouldDisplay) {
                            // 转换当前键为主按键名
                            char keyName[64] = {0};
                            UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                            switch (vk) {
                                case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
                                case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
                                case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
                                case VK_NUMLOCK:
                                    scanCode |= KF_EXTENDED;
                                    break;
                            }

                            std::string mainKey;
                            if (GetKeyNameTextA(scanCode << 16, keyName, sizeof(keyName)) > 0) {
                                mainKey = keyName;
                            }

                            if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) mainKey = "Ctrl";
                            else if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) mainKey = "Alt";
                            else if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) mainKey = "Shift";
                            else if (vk == VK_LWIN || vk == VK_RWIN) mainKey = "Win";
                            else if (vk == VK_SPACE) mainKey = "Space";

                            // 组合键格式化 (如 Ctrl + Shift + A)
                            std::vector<std::string> combo;
                            if (hasCtrl && mainKey != "Ctrl") combo.push_back("Ctrl");
                            if (hasWin && mainKey != "Win") combo.push_back("Win");
                            if (hasAlt && mainKey != "Alt") combo.push_back("Alt");
                            if (hasShift && mainKey != "Shift") combo.push_back("Shift");
                            if (!mainKey.empty()) combo.push_back(mainKey);

                            if (!combo.empty()) {
                                std::string display;
                                for (size_t i = 0; i < combo.size(); ++i) {
                                    if (i > 0) display += " + ";
                                    display += combo[i];
                                }
                                keycastCallback(display);
                            }
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
