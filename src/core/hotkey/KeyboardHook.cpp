#include "core/hotkey/KeyboardHook.h"
#include "core/logger/Logger.h"
#include "core/stats/StatsManager.h"
#include "ui/KeycastOverlay.h"
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
        LOG_WARN("键盘钩子已安装, 跳过重复安装");
        return true;
    }

    m_hookHandle = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        lowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0  // 全局钩子
    );

    if (!m_hookHandle) {
        LOG_ERROR("安装键盘钩子失败, error={}", GetLastError());
        return false;
    }

    LOG_INFO("低级键盘钩子已安装，开始统计按键数据");
    return true;
}

void KeyboardHook::uninstall() {
    if (m_hookHandle) {
        UnhookWindowsHookEx(m_hookHandle);
        m_hookHandle = nullptr;
        LOG_INFO("低级键盘钩子已卸载");
    }
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
                if (easy::ui::KeycastOverlay::instance().isEnabled()) {
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

                    // 获取当前修饰键状态
                    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

                    bool isModifierKey = (data->vkCode >= VK_SHIFT && data->vkCode <= VK_MENU) || 
                                         (data->vkCode == VK_LWIN || data->vkCode == VK_RWIN) ||
                                         (data->vkCode == VK_LCONTROL || data->vkCode == VK_RCONTROL) ||
                                         (data->vkCode == VK_LMENU || data->vkCode == VK_RMENU) ||
                                         (data->vkCode == VK_LSHIFT || data->vkCode == VK_RSHIFT);

                    std::string keyStr;
                    
                    if (!isModifierKey) {
                        // 构建组合部分
                        if (ctrl) keyStr += "Ctrl+";
                        if (alt) keyStr += "Alt+";
                        if (shift) keyStr += "Shift+";
                        if (win) keyStr += "Win+";

                        // 解析键名
                        UINT scanCode = MapVirtualKeyW(data->vkCode, MAPVK_VK_TO_VSC);
                        // 处理一些不依赖 MapVirtualKey 的特殊键
                        switch (data->vkCode) {
                            case VK_LEFT: keyStr += "Left"; break;
                            case VK_RIGHT: keyStr += "Right"; break;
                            case VK_UP: keyStr += "Up"; break;
                            case VK_DOWN: keyStr += "Down"; break;
                            case VK_PRIOR: keyStr += "PageUp"; break;
                            case VK_NEXT: keyStr += "PageDown"; break;
                            case VK_END: keyStr += "End"; break;
                            case VK_HOME: keyStr += "Home"; break;
                            case VK_INSERT: keyStr += "Insert"; break;
                            case VK_DELETE: keyStr += "Delete"; break;
                            default: {
                                wchar_t keyName[64] = {};
                                if (GetKeyNameTextW(static_cast<LONG>(scanCode) << 16, keyName, 64) > 0) {
                                    char utf8[128] = {};
                                    WideCharToMultiByte(CP_UTF8, 0, keyName, -1, utf8, 128, nullptr, nullptr);
                                    keyStr += utf8;
                                } else {
                                    keyStr += std::format("0x{:02X}", data->vkCode);
                                }
                                break;
                            }
                        }
                    } else {
                        // 单独按下修饰键
                        if (data->vkCode == VK_LCONTROL || data->vkCode == VK_RCONTROL || data->vkCode == VK_CONTROL) keyStr = "Ctrl";
                        else if (data->vkCode == VK_LMENU || data->vkCode == VK_RMENU || data->vkCode == VK_MENU) keyStr = "Alt";
                        else if (data->vkCode == VK_LSHIFT || data->vkCode == VK_RSHIFT || data->vkCode == VK_SHIFT) keyStr = "Shift";
                        else if (data->vkCode == VK_LWIN || data->vkCode == VK_RWIN) keyStr = "Win";
                    }

                    // 1. 如果带有修饰键（组合键），或者本身是修饰键，或者序列不为空（广义组合键中间态）
                    if (ctrl || alt || shift || win || isModifierKey || !currentSequence.empty()) {
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
                        
                        easy::ui::KeycastOverlay::instance().showKeys(display);
                    }
                }
            }
        }
        } catch (const std::exception& e) {
            LOG_ERROR("KeyboardHook 发生未捕获异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("KeyboardHook 发生未知异常");
        }
    }

    // 只做统计，绝不拦截，必须调用 CallNextHookEx
    return CallNextHookEx(self.m_hookHandle, nCode, wParam, lParam);
}

} // namespace easy::core
