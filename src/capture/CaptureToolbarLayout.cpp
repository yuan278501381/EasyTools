#include "capture/CaptureToolbarLayout.h"

#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace easy::capture {
namespace {

struct ButtonSpec {
    ToolbarCommand command = ToolbarCommand::SelectTool;
    MarkupTool tool = MarkupTool::Rectangle;
    MarkupColor color = MarkupColor::Red();
    std::wstring label;
    float baseWidth = 30.0f;
};

bool useChineseLabels() {
    const auto language = easy::core::ConfigManager::instance().get<std::string>(
        "/general/language", "auto");
    return language == "zh-CN" ||
           (language == "auto" && easy::core::WinUtils::isSystemLanguageChinese());
}

std::vector<ButtonSpec> buttonSpecs(const CaptureState& state, bool zh) {
    if (state.mode == OverlayMode::RecordRegion) {
        return {
            {ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, zh ? L"● 录制" : L"● Rec", 68.0f},
            {ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, zh ? L"✕ 取消" : L"✕ Cancel", 78.0f},
        };
    }

    static constexpr std::array tools{
        std::pair{MarkupTool::Rectangle, L"□"},
        std::pair{MarkupTool::Arrow, L"↗"},
        std::pair{MarkupTool::Ellipse, L"○"},
        std::pair{MarkupTool::Pen, L"✎"},
        std::pair{MarkupTool::Highlight, L"▰"},
        std::pair{MarkupTool::Mosaic, L"▦"},
        std::pair{MarkupTool::Text, L"T"},
        std::pair{MarkupTool::Number, L"①"},
        std::pair{MarkupTool::Magnifier, L"⌕"},
        std::pair{MarkupTool::Spotlight, L"☀"},
        std::pair{MarkupTool::Watermark, L"©"},
        std::pair{MarkupTool::Inpaint, L"✦"},
    };
    static const std::array colors{
        MarkupColor::Red(), MarkupColor::Yellow(), MarkupColor::Green(),
        MarkupColor::Blue(), MarkupColor::White(),
    };

    std::vector<ButtonSpec> specs;
    specs.reserve(tools.size() + colors.size() + 8);
    for (const auto& [tool, label] : tools) {
        specs.push_back({ToolbarCommand::SelectTool, tool, {}, label});
    }
    for (const auto& color : colors) {
        specs.push_back({ToolbarCommand::SelectColor, MarkupTool::Rectangle, color, L""});
    }
    specs.push_back({ToolbarCommand::Undo, MarkupTool::Rectangle, {}, L"↩"});
    specs.push_back({ToolbarCommand::Redo, MarkupTool::Rectangle, {}, L"↪"});
    specs.push_back({ToolbarCommand::Clear, MarkupTool::Rectangle, {}, L"⌫"});
    specs.push_back({ToolbarCommand::ToggleCornerRadius, MarkupTool::Rectangle, {}, L"╭╮"});
    specs.push_back({ToolbarCommand::ExtractText, MarkupTool::Rectangle, {}, zh ? L"文" : L"T"});
    specs.push_back({ToolbarCommand::PinWindow, MarkupTool::Rectangle, {}, L"⌖"});
    specs.push_back({ToolbarCommand::ScrollCapture, MarkupTool::Rectangle, {}, zh ? L"长" : L"⇊"});
    specs.push_back({ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, L"✕"});
    specs.push_back({ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, L"✓"});
    return specs;
}

}  // namespace

void rebuildCaptureToolbar(CaptureState& state, const D2D1_RECT_F& selectionRect,
                           D2D1_SIZE_F surfaceSize) {
    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f,
                                   1.0f, 5.0f);
    const auto close = [](float lhs, float rhs) {
        return std::abs(lhs - rhs) < 0.01f;
    };
    if (state.toolbarLayoutValid && !state.toolbarButtons.empty() &&
        state.toolbarLayoutMode == state.mode && close(state.toolbarLayoutScale, scale) &&
        close(state.toolbarLayoutSurface.width, surfaceSize.width) &&
        close(state.toolbarLayoutSurface.height, surfaceSize.height) &&
        close(state.toolbarLayoutSelection.left, selectionRect.left) &&
        close(state.toolbarLayoutSelection.top, selectionRect.top) &&
        close(state.toolbarLayoutSelection.right, selectionRect.right) &&
        close(state.toolbarLayoutSelection.bottom, selectionRect.bottom)) {
        return;
    }

    state.toolbarButtons.clear();
    state.toolbarLayoutValid = false;
    if (surfaceSize.width <= 0.0f || surfaceSize.height <= 0.0f) return;

    const float buttonSize = 30.0f * scale;
    const float gap = 4.0f * scale;
    const float rowGap = 6.0f * scale;
    const float paddingX = 8.0f * scale;
    const float paddingY = 6.0f * scale;
    const float screenMargin = 8.0f * scale;
    const bool chinese = useChineseLabels();
    const auto specs = buttonSpecs(state, chinese);
    if (specs.empty()) return;

    const float availablePanelWidth = std::max(
        buttonSize + 2.0f * paddingX,
        surfaceSize.width - 2.0f * screenMargin);
    const float availableContentWidth = std::max(
        buttonSize, availablePanelWidth - 2.0f * paddingX);

    std::vector<std::vector<std::size_t>> rows(1);
    std::vector<float> rowWidths(1, 0.0f);
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const float width = specs[index].baseWidth * scale;
        const float addition = rows.back().empty() ? width : gap + width;
        if (!rows.back().empty() && rowWidths.back() + addition > availableContentWidth) {
            rows.emplace_back();
            rowWidths.push_back(0.0f);
        }
        if (!rows.back().empty()) rowWidths.back() += gap;
        rows.back().push_back(index);
        rowWidths.back() += width;
    }

    const float contentWidth = *std::max_element(rowWidths.begin(), rowWidths.end());
    const float panelWidth = std::min(availablePanelWidth, contentWidth + 2.0f * paddingX);
    const float panelHeight = 2.0f * paddingY +
        rows.size() * buttonSize + (rows.size() - 1) * rowGap;
    const float maxX = std::max(screenMargin,
        surfaceSize.width - panelWidth - screenMargin);
    const float panelX = std::clamp(selectionRect.left, screenMargin, maxX);
    float panelY = selectionRect.bottom + 8.0f * scale;
    if (panelY + panelHeight > surfaceSize.height - screenMargin) {
        panelY = selectionRect.top - panelHeight - 8.0f * scale;
    }
    panelY = std::clamp(panelY, screenMargin,
                        std::max(screenMargin, surfaceSize.height - panelHeight - screenMargin));

    state.toolbarButtons.reserve(specs.size());
    for (std::size_t row = 0; row < rows.size(); ++row) {
        float x = panelX + paddingX;
        const float y = panelY + paddingY + row * (buttonSize + rowGap);
        for (const auto index : rows[row]) {
            const auto& spec = specs[index];
            const float width = spec.baseWidth * scale;
            ToolbarButton button;
            button.command = spec.command;
            button.tool = spec.tool;
            button.color = spec.color;
            button.label = spec.label;
            button.rect = D2D1::RectF(x, y, x + width, y + buttonSize);
            state.toolbarButtons.push_back(std::move(button));
            x += width + gap;
        }
    }

    state.toolbarLayoutSelection = selectionRect;
    state.toolbarLayoutSurface = surfaceSize;
    state.toolbarLayoutScale = scale;
    state.toolbarLayoutMode = state.mode;
    state.toolbarLayoutChinese = chinese;
    state.toolbarLayoutValid = true;
}

}  // namespace easy::capture
