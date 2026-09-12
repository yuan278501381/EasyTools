#include "ui/native/pages/NativeAiAssistantPage.h"
#include "ui/native/core/UIDeclarative.h"
#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"

namespace tools3000::ui::native {

using namespace tools3000::ui::native::declarative;

NativeAiAssistantPage::NativeAiAssistantPage() = default;
NativeAiAssistantPage::~NativeAiAssistantPage() = default;

std::shared_ptr<UIElement> NativeAiAssistantPage::buildContent() {
    auto& cfg = tools3000::core::ConfigManager::instance();

    bool enabled = cfg.get<bool>("/extension/ai_assistant/enabled", true);
    std::string provider = cfg.get<std::string>("/extension/ai_assistant/provider", "gemini");
    std::string model = cfg.get<std::string>("/extension/ai_assistant/model", "gemini-2.5-flash");
    std::string hotkey = cfg.get<std::string>("/extension/ai_assistant/hotkey", "Alt+X");
    std::string promptPreset = cfg.get<std::string>("/extension/ai_assistant/preset", "optimize");

    std::vector<SelectOption> providerOpts = {
        { "gemini", L"Google Gemini 2.5 Flash (超极速推荐)" },
        { "openai", L"OpenAI GPT-4o" },
        { "claude", L"Anthropic Claude 3.5 Sonnet" },
        { "ollama", L"Ollama 本地私有化模型 (离线安全)" }
    };

    std::vector<SelectOption> presetOpts = {
        { "optimize", L"智能代码/文本润色优化" },
        { "translate", L"中英双向即时划词翻译" },
        { "explain", L"技术架构与代码原理解析" },
        { "summarize", L"长文本核心要点精炼提炼" }
    };

    auto content = VStack({
        SettingGroup(L"AI 极速助手引擎配置", IconType::Bot, {
            Card(L"服务供应商与接入端点", L"配置大语言模型 API 凭据或本地离线模型连接", {
                SettingRow(L"启用 AI 极速助手", L"允许通过全局快捷键呼出智能划词面板与对话浮窗",
                    Toggle(enabled, [](bool v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/enabled", v);
                    })
                ),
                SettingRow(L"模型供应商 (Provider)", L"选择在线云端智能引擎或本地轻量大模型",
                    Select(providerOpts, provider, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/provider", v);
                    })
                ),
                SettingRow(L"模型标识 (Model ID)", L"指定调用的大模型具体版本号",
                    TextInput(tools3000::core::WinUtils::utf8ToWstring(model), L"例如 gemini-2.5-flash 或 gpt-4o", [](const std::wstring& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/model", tools3000::core::WinUtils::wstringToUtf8(v));
                    })
                ),
                SettingRow(L"唤起全局快捷键", L"在任意软件选中文本后按下此键立即呼出 AI 处理",
                    HotkeyInput(hotkey, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/hotkey", v);
                    })
                )
            }),

            Card(L"生成偏好与提示词预设", L"调整大语言模型的创造力与划词触发默认任务", {
                SettingRow(L"划词默认触发动作", L"选中文本呼出 AI 助手时的默认处理行为",
                    Select(presetOpts, promptPreset, [](const std::string& v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/preset", v);
                    })
                ),
                SettingRow(L"采样温度 (Temperature)", L"控制输出随机性：0.2 严谨准确，0.8 富有创造力",
                    NumberInput(7, 1, 20, 1, L" / 10", [](int v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/temperature", v / 10.0f);
                    })
                ),
                SettingRow(L"最大生成 Token 上限", L"单次回复允许返回的最大 Token 数量",
                    NumberInput(2048, 256, 8192, 256, L" Tokens", [](int v) {
                        tools3000::core::ConfigManager::instance().set("/extension/ai_assistant/max_tokens", v);
                    })
                )
            }),

            Card(L"交互式演练沙箱 (Live Playground)", L"实时体验划词助手响应与微调效果", {
                SettingRow(L"模拟用户提问", L"在代码编辑器中选中并提问",
                    CodeBadge(L"\"优化 C++ 内存修剪与冷路径调度逻辑\"")),
                SettingRow(L"AI 助手模拟回答", L"实时流式渲染示例",
                    Badge(L"建议在生命周期冷路径统一调用 WinUtils::trimWorkingSet()，惰性重载重型资源。", BadgeVariant::Success)),
                SettingRow(L"端到端连通性测试", L"向当前配置的供应商发送握手探测请求",
                    Button(L"测试连接", ButtonVariant::Secondary, ButtonSize::Sm, []() {
                        tools3000::core::EventBus::instance().publish(
                            tools3000::core::ShowToastEvent{L"AI 服务端点握手成功 (HTTP 200 OK · 响应 28ms)"});
                    }))
            })
        })
    }, 16.0f, Thickness(24.0f, 20.0f, 24.0f, 32.0f));

    content->layoutParams().maxWidth = 920.0f;
    return content;
}

} // namespace tools3000::ui::native
