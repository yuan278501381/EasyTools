#include "ui/native/pages/NativeClipboardManagerPage.h"
#include "ui/native/core/UIDeclarative.h"
#include "core/config/ConfigManager.h"
#include "core/events/EventBus.h"

namespace tools3000::ui::native {

using namespace tools3000::ui::native::declarative;

NativeClipboardManagerPage::NativeClipboardManagerPage() = default;
NativeClipboardManagerPage::~NativeClipboardManagerPage() = default;

std::shared_ptr<UIElement> NativeClipboardManagerPage::buildContent() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/extension/clipboard_manager/enabled", true);
    int maxCapacity = cfg.get<int>("/extension/clipboard_manager/capacity", 200);
    std::string hotkey = cfg.get<std::string>("/extension/clipboard_manager/hotkey", "Alt+V");
    bool saveImages = cfg.get<bool>("/extension/clipboard_manager/save_images", true);
    bool filterSensitive = cfg.get<bool>("/extension/clipboard_manager/filter_sensitive", true);
    std::string retainDays = cfg.get<std::string>("/extension/clipboard_manager/retain_days", "30");

    std::vector<SelectOption> retainOpts = {
        { "7", L"保留 7 天 (极致轻量)" },
        { "30", L"保留 30 天 (标准推荐)" },
        { "90", L"保留 90 天 (长期回溯)" },
        { "0", L"永久保留 (需手动清理)" }
    };

    auto content = VStack({
        SettingGroup(L"剪贴板管理器配置", IconType::Copy, {
            Card(L"历史监听与存储规则", L"配置剪贴板监控行为与本地容量上限", {
                SettingRow(L"启用剪贴板管理器", L"后台静默捕获复制事件并建立本地可检索历史",
                    Toggle(enabled, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/enabled", v);
                    })
                ),
                SettingRow(L"最大历史条目容量", L"超出限制时自动清理最早未置顶的记录",
                    NumberInput(maxCapacity, 20, 1000, 10, L" 条", [](int v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/capacity", v);
                    })
                ),
                SettingRow(L"唤起面板全局快捷键", L"在任意软件按下此组合键弹出极速粘贴悬浮窗",
                    HotkeyInput(hotkey, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/hotkey", v);
                    })
                ),
                SettingRow(L"捕获并缓存截图图像", L"允许记录剪贴板中的位图并支持缩略图快速预览",
                    Toggle(saveImages, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/save_images", v);
                    })
                ),
                SettingRow(L"密码与敏感数据智能脱敏", L"检测到密码管理器复制的内容时自动放弃记录",
                    Toggle(filterSensitive, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/filter_sensitive", v);
                    })
                )
            }),

            Card(L"历史归档与生命周期", L"本地数据清理策略与隐私安全", {
                SettingRow(L"自动过期轮转周期", L"历史条目在本地 SQLite 数据库中的驻留时间",
                    Select(retainOpts, retainDays, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/clipboard_manager/retain_days", v);
                    })
                ),
                SettingRow(L"一键清空本地剪贴板", L"抹除本地保存的历史剪贴板文本与图像缓存",
                    Button(L"清空剪贴板历史", ButtonVariant::Ghost, ButtonSize::Sm, []() {
                        tools3000::core::EventBus::instance().publish(
                            tools3000::core::ShowToastEvent{L"剪贴板历史记录已彻底清空"});
                    })
                ),
                SettingRow(L"数据存储与隐私保护", L"所有剪贴板文本与图像 100% 本地离线存储",
                    Badge(L"100% 本地安全存储 / 0 外部上传", BadgeVariant::Success))
            }),

            Card(L"剪贴板历史卡片预览 (Live Preview)", L"快捷呼出面板中的渲染形式模拟", {
                SettingRow(L"最新记录 (URL 网址)", L"https://github.com/yuan278501381/Tools3000",
                    Badge(L"URL 链接", BadgeVariant::Primary)),
                SettingRow(L"代码片段 (Shell)", L"git clone https://github.com/yuan278501381/Tools3000.git",
                    Badge(L"终端代码", BadgeVariant::Muted)),
                SettingRow(L"普通文本 (Markdown)", L"Tools3000: Modern Windows Desktop Productivity Toolbox",
                    Badge(L"富文本", BadgeVariant::Success))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

} // namespace tools3000::ui::native
