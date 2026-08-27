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
        {
            std::lock_guard lock(m_callbackMutex);
            m_pendingModifierVk = 0;
            m_comboTriggered = false;
        }
        LOG_INFO("Hook uninstalled");
    }
}

void KeyboardHook::setPaused(bool paused) {
    m_paused.store(paused, std::memory_order_relaxed);
    if (paused) {
        std::lock_guard lock(m_callbackMutex);
        m_pendingModifierVk = 0;
        m_comboTriggered = false;
    }
}

bool KeyboardHook::isPaused() const {
    return m_paused.load(std::memory_order_relaxed);
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

                // --- 广义组合键回显与修饰键状态机 ---
                std::function<void(const std::string&)> keycastCallback;
                {
                    std::lock_guard lock(self.m_callbackMutex);
                    keycastCallback = self.m_keycastCallback;
                }

                if (keycastCallback) {
                    std::string filterMode = easy::core::ConfigManager::instance().get<std::string>(
                        "/keycast/filterMode", "");
                    if (filterMode.empty()) {
                        bool onlyShortcuts = easy::core::ConfigManager::instance().get<bool>(
                            "/general/keycastOnlyShortcuts", true);
                        filterMode = onlyShortcuts ? "smart_shortcuts" : "all_keys";
                    }
                    const bool includeFunctionKeys = easy::core::ConfigManager::instance().get<bool>(
                        "/keycast/includeFunctionKeys", true);

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

                    bool hasCtrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool hasAlt   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    bool hasShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool hasWin   = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

                    // 1. 处理 KeyDown 事件
                    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                        StatsManager::instance().recordKey(data->vkCode);

                        if (isMod) {
                            // 修饰键单按时进入暂存态，不立即弹出回显（等待 KeyUp 或后续组合键触发）
                            std::lock_guard lock(self.m_callbackMutex);
                            self.m_pendingModifierVk = vk;
                            self.m_comboTriggered = false;
                        } else {
                            // 主键按下，如果此前有暂存的修饰键，标记组合键已触发（吞掉单修饰键）
                            {
                                std::lock_guard lock(self.m_callbackMutex);
                                if (self.m_pendingModifierVk != 0) {
                                    self.m_comboTriggered = true;
                                }
                            }

                            bool isComboWithModifier = (hasCtrl || hasAlt || hasWin || hasShift);
                            bool isLetter = (vk >= 0x41 && vk <= 0x5A);
                            bool isOnlyShift = hasShift && !hasCtrl && !hasAlt && !hasWin;

                            bool shouldDisplay = false;
                            if (filterMode == "all_keys") {
                                shouldDisplay = true;
                            } else {
                                // 智能大写打字过滤：仅按住 Shift 连续打字母（如打 HOME）视为常规大写输入，不作为组合快捷键回显
                                if (isOnlyShift && isLetter) {
                                    shouldDisplay = false;
                                } else if (isComboWithModifier) {
                                    shouldDisplay = true;
                                } else if (isFunctionalKey && includeFunctionKeys) {
                                    shouldDisplay = true;
                                }
                            }

                            if (shouldDisplay) {
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
                                if (vk == VK_SPACE) mainKey = "Space";

                                // 组合键格式化 (标准规范顺序: Ctrl + Win + Alt + Shift + 主键)
                                std::vector<std::string> combo;
                                if (hasCtrl && mainKey != "Ctrl") combo.push_back("Ctrl");
                                if (hasWin && mainKey != "Win") combo.push_back("Win");
                                if (hasAlt && mainKey != "Alt") combo.push_back("Alt");
                                if (hasShift && mainKey != "Shift" && !(filterMode == "all_keys" && isOnlyShift && isLetter)) {
                                    combo.push_back("Shift");
                                }
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
                    // 2. 处理 KeyUp 事件（用于单按修饰键判定）
                    else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                        if (isMod) {
                            bool shouldEmitSingleMod = false;
                            DWORD targetVk = 0;
                            {
                                std::lock_guard lock(self.m_callbackMutex);
                                if (self.m_pendingModifierVk == vk && !self.m_comboTriggered) {
                                    if (filterMode == "with_single_modifiers" || filterMode == "all_keys") {
                                        shouldEmitSingleMod = true;
                                        targetVk = vk;
                                    }
                                }
                                self.m_pendingModifierVk = 0;
                                self.m_comboTriggered = false;
                            }

                            if (shouldEmitSingleMod) {
                                std::string modName = "Ctrl";
                                if (targetVk == VK_LMENU || targetVk == VK_RMENU || targetVk == VK_MENU) modName = "Alt";
                                else if (targetVk == VK_LSHIFT || targetVk == VK_RSHIFT || targetVk == VK_SHIFT) modName = "Shift";
                                else if (targetVk == VK_LWIN || targetVk == VK_RWIN) modName = "Win";
                                keycastCallback(modName);
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
