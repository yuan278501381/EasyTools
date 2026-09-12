#include "ui/native/pages/NativeColorPickerPage.h"
#include "ui/native/core/UIDeclarative.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"

namespace tools3000::ui::native {

using namespace tools3000::ui::native::declarative;

NativeColorPickerPage::NativeColorPickerPage() = default;
NativeColorPickerPage::~NativeColorPickerPage() = default;

std::shared_ptr<UIElement> NativeColorPickerPage::buildContent() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/extension/color_picker/enabled", true);
    std::string format = cfg.get<std::string>("/extension/color_picker/format", "hex");
    std::string hotkey = cfg.get<std::string>("/extension/color_picker/hotkey", "Alt+C");
    bool autoCopy = cfg.get<bool>("/extension/color_picker/auto_copy", true);
    std::string zoomRatio = cfg.get<std::string>("/extension/color_picker/zoom", "8x");
    bool showGrid = cfg.get<bool>("/extension/color_picker/show_grid", true);

    std::vector<SelectOption> formatOpts = {
        { "hex", L"HEX 格式 (#7C3AED / 标准十六进制)" },
        { "rgb", L"RGB 格式 (rgb(124, 58, 237))" },
        { "hsl", L"HSL 格式 (hsl(262, 83%, 58%))" },
        { "rgba", L"RGBA 格式 (rgba(124, 58, 237, 1.0))" }
    };

    std::vector<SelectOption> zoomOpts = {
        { "4x", L"4 倍率放大镜 (广角预览)" },
        { "8x", L"8 倍率放大镜 (标准像素级别)" },
        { "12x", L"12 倍率放大镜 (高精度微距)" },
        { "16x", L"16 倍率超微距 (次像素级对齐)" }
    };

    auto content = VStack({
        SettingGroup(L"屏幕拾色器参数偏好", IconType::Palette, {
            Card(L"基础拾色与快捷键", L"配置全局取色触发与自动复制规则", {
                SettingRow(L"启用屏幕拾色器", L"允许通过热键呼出全屏幕像素放大镜并精准拾取颜色",
                    Toggle(enabled, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/enabled", v);
                    })
                ),
                SettingRow(L"默认输出颜色格式", L"点击拾取时默认写入剪贴板的格式代码",
                    Select(formatOpts, format, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/format", v);
                    })
                ),
                SettingRow(L"唤起拾色全局快捷键", L"在屏幕任意位置按下快捷键呼出高精度取色器",
                    HotkeyInput(hotkey, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/hotkey", v);
                    })
                ),
                SettingRow(L"拾取后自动写入剪贴板", L"单击左键选定像素后自动将格式化色值复制至剪贴板",
                    Toggle(autoCopy, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/auto_copy", v);
                    })
                )
            }),

            Card(L"放大镜与次像素显示", L"调整光标周围像素网格与缩放倍率", {
                SettingRow(L"像素放大镜倍率", L"准星中心视野局部放大比例",
                    Select(zoomOpts, zoomRatio, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/zoom", v);
                    })
                ),
                SettingRow(L"显示十字准星与网格线", L"在放大镜中绘制 1px 精准微晶几何栅格",
                    Toggle(showGrid, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/color_picker/show_grid", v);
                    })
                )
            }),

            Card(L"色彩仿真与调色盘历史 (Live Simulation)", L"当前选定颜色在各格式下的实时呈现", {
                SettingRow(L"当前选定样本色块", L"深紫色系极客强调色 (#7C3AED)",
                    Badge(L"■■■ 色块样本", BadgeVariant::Primary)),
                SettingRow(L"HEX 十六进制色值", L"前端开发与 CSS 常用格式",
                    Button(L"复制色值 #7C3AED", ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        if (OpenClipboard(nullptr)) {
                            EmptyClipboard();
                            const wchar_t text[] = L"#7C3AED";
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(text));
                            if (hMem) {
                                void* p = GlobalLock(hMem);
                                if (p) {
                                    memcpy(p, text, sizeof(text));
                                    GlobalUnlock(hMem);
                                    SetClipboardData(CF_UNICODETEXT, hMem);
                                }
                            }
                            CloseClipboard();
                        }
                        tools3000::core::EventBus::instance().publish(
                            tools3000::core::ShowToastEvent{L"已复制色值 #7C3AED 至剪贴板"});
                    })),
                SettingRow(L"RGB 红绿蓝分量值", L"三原色整型分量",
                    CodeBadge(L"rgb(124, 58, 237)")),
                SettingRow(L"HSL 色相饱和度值", L"色彩空间感知与微调格式",
                    CodeBadge(L"hsl(262, 83%, 58%)"))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

} // namespace tools3000::ui::native
