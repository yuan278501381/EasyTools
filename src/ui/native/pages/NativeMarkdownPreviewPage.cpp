#include "ui/native/pages/NativeMarkdownPreviewPage.h"
#include "ui/native/core/UIDeclarative.h"
#include "core/config/ConfigManager.h"
#include "core/events/EventBus.h"

namespace tools3000::ui::native {

using namespace tools3000::ui::native::declarative;

NativeMarkdownPreviewPage::NativeMarkdownPreviewPage() = default;
NativeMarkdownPreviewPage::~NativeMarkdownPreviewPage() = default;

std::shared_ptr<UIElement> NativeMarkdownPreviewPage::buildContent() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/extension/markdown_preview/enabled", true);
    std::string theme = cfg.get<std::string>("/extension/markdown_preview/theme", "github");
    std::string codeTheme = cfg.get<std::string>("/extension/markdown_preview/code_theme", "onedark");
    bool showLineNumbers = cfg.get<bool>("/extension/markdown_preview/line_numbers", true);
    bool enableKatex = cfg.get<bool>("/extension/markdown_preview/katex", true);
    bool enableMermaid = cfg.get<bool>("/extension/markdown_preview/mermaid", true);

    std::vector<SelectOption> themeOpts = {
        { "github", L"GitHub 现代排版风格 (标准推荐)" },
        { "typography", L"Typography 优雅排版风格 (长文阅读)" },
        { "dark", L"Obsidian 暗夜深色极客风" }
    };

    std::vector<SelectOption> codeOpts = {
        { "onedark", L"Atom One Dark (经典高对比)" },
        { "vscode", L"VS Code Modern Dark+" },
        { "monokai", L"Monokai Pro 经典原色" }
    };

    auto content = VStack({
        SettingGroup(L"Markdown 极速预览引擎", IconType::FileText, {
            Card(L"排版风格与代码高亮", L"配置文件选中时空格快速预览的视觉呈现", {
                SettingRow(L"启用空格快速预览", L"在文件资源管理器选中 .md 文件按下空格呼出预览",
                    Toggle(enabled, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/enabled", v);
                    })
                ),
                SettingRow(L"Markdown 文档渲染风格", L"页面主体无衬线排版与标题边距预设",
                    Select(themeOpts, theme, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/theme", v);
                    })
                ),
                SettingRow(L"代码块语法着色主题", L"内联与围栏代码块着色配色方案",
                    Select(codeOpts, codeTheme, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/code_theme", v);
                    })
                ),
                SettingRow(L"代码块显示左侧行号", L"在代码预览左侧对齐显示行数序号",
                    Toggle(showLineNumbers, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/line_numbers", v);
                    })
                )
            }),

            Card(L"科学公式与扩展语法", L"支持现代 Markdown 生态图表与数学标记", {
                SettingRow(L"支持 KaTeX 数学公式渲染", L"解析 $inline$ 与 $$display$$ 复杂数学公式",
                    Toggle(enableKatex, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/katex", v);
                    })
                ),
                SettingRow(L"支持 Mermaid 矢量架构图", L"解析流程图、序列图与状态机代码块",
                    Toggle(enableMermaid, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/markdown_preview/mermaid", v);
                    })
                )
            }),

            Card(L"交互式渲染演练沙箱 (Live Playground)", L"README.md - 空格预览模拟视窗", {
                SettingRow(L"示例标题与公式", L"# Tools3000 Productivity Suite",
                    CodeBadge(L"$$ E = mc^2 $$")),
                SettingRow(L"性能与渲染速度", L"基于纯原生 DirectWrite / Direct2D 零损耗解析",
                    Badge(L"首屏耗时 < 0.3ms / 0 内存开销", BadgeVariant::Success)),
                SettingRow(L"快捷呼出提示", L"在 Windows 资源管理器中选中任意 .md 文件并按下 Space 即可触发",
                    Button(L"体验预览说明", ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        tools3000::core::EventBus::instance().publish(
                            tools3000::core::ShowToastEvent{L"在资源管理器中选中 Markdown/代码文件，按 Space 键即可秒开预览"});
                    }))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

} // namespace tools3000::ui::native
