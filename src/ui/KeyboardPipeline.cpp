#include "ui/KeyboardPipeline.h"
#include <windows.h>
#include <shellapi.h>
#include <wrl.h>
#include <wrl/event.h>
#include <WebView2.h>

namespace easy::ui {

void KeyboardPipeline::applyWebKeyboardPolicy(ICoreWebView2Controller* controller, bool devToolsAllowed) {
    if (!controller) return;

    EventRegistrationToken token{};
    using Microsoft::WRL::Callback;
    controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [devToolsAllowed](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (!args) return S_OK;

                COREWEBVIEW2_KEY_EVENT_KIND keyKind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                args->get_KeyEventKind(&keyKind);
                UINT vKey = 0;
                args->get_VirtualKey(&vKey);

                const bool isKeyDown = (keyKind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN ||
                                        keyKind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN);
                const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                // 1. 屏蔽不需要的 Chromium 浏览器默认快捷键行为（防止意外触发打印、下载、网页查找、历史记录等）
                if (isKeyDown) {
                    if (ctrl && !alt && !shift) {
                        switch (vKey) {
                            case 'P': // 打印
                            case 'F': // 查找
                            case 'U': // 查看源代码
                            case 'J': // 下载
                            case 'H': // 历史记录
                            case 'O': // 打开文件
                            case 'S': // 保存网页
                                args->put_Handled(TRUE);
                                return S_OK;
                            case 'R': // 刷新页面
                                if (!devToolsAllowed) {
                                    args->put_Handled(TRUE);
                                    return S_OK;
                                }
                                break;
                            default:
                                break;
                        }
                    }

                    // 屏蔽 F1 帮助、F3 查找下一个、F7 光标浏览
                    if (vKey == VK_F1 || vKey == VK_F3 || vKey == VK_F7) {
                        args->put_Handled(TRUE);
                        return S_OK;
                    }

                    // 屏蔽 F5 刷新（非调试模式）
                    if (vKey == VK_F5 && !devToolsAllowed) {
                        args->put_Handled(TRUE);
                        return S_OK;
                    }

                    // 屏蔽 Alt+Left / Alt+Right 触发的浏览器后退/前进
                    if (alt && (vKey == VK_LEFT || vKey == VK_RIGHT)) {
                        args->put_Handled(TRUE);
                        return S_OK;
                    }
                }

                // 2. 针对所有的快捷键录入组合（包括 Alt+Space, Ctrl+Shift+..., Win+... 等），
                // 显式保证其不被 WebView2 意外拦截并无损送达前端 DOM
                args->put_Handled(FALSE);
                return S_OK;
            }
        ).Get(),
        &token
    );
}

} // namespace easy::ui
