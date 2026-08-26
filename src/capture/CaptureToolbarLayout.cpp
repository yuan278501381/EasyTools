#include "capture/CaptureToolbarLayout.h"

#include "core/config/ConfigManager.h"
#include "core/utils/WinUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>
#include <format>

namespace easy::capture {
namespace {

struct ButtonSpec {
    ToolbarCommand command = ToolbarCommand::SelectTool;
    MarkupTool tool = MarkupTool::Rectangle;
    MarkupColor color = MarkupColor::Red();
    std::wstring label;
    float baseWidth = 30.0f;
    int intParam = 0;
    LineStyle lineStyleParam = LineStyle::Solid;
    ArrowStyle arrowStyleParam = ArrowStyle::Standard;
    bool boolParam = false;
    bool hasDropdown = false;
    bool isSecondary = false;
};

bool useChineseLabels() {
    const auto language = easy::core::ConfigManager::instance().get<std::string>(
        "/general/language", "auto");
    return language == "zh-CN" ||
           (language == "auto" && easy::core::WinUtils::isSystemLanguageChinese());
}

// 1. 主工具栏规格
std::vector<ButtonSpec> primaryButtonSpecs(const CaptureState& state, bool zh) {
    if (state.mode == OverlayMode::RecordRegion) {
        return {
            {ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, zh ? L"● 录制" : L"● Rec", 68.0f},
            {ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, zh ? L"✕ 取消" : L"✕ Cancel", 78.0f},
        };
    }

    std::vector<ButtonSpec> specs;
    // 形状工具组（当前激活矩形或椭圆时显示）
    specs.push_back({ToolbarCommand::SelectTool, 
                     (state.currentTool == MarkupTool::Ellipse ? MarkupTool::Ellipse : MarkupTool::Rectangle),
                     {}, L"□", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, true, false});

    // 画笔工具组（铅笔 / 荧光笔）
    specs.push_back({ToolbarCommand::SelectTool,
                     (state.currentTool == MarkupTool::Highlight ? MarkupTool::Highlight : MarkupTool::Pen),
                     {}, L"✎", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, true, false});

    // 箭头工具组
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Arrow, {}, L"↗", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, true, false});

    // 文本工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Text, {}, L"T", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});

    // 序号工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Number, {}, L"①", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});

    // 马赛克工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Mosaic, {}, L"▦", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, true, false});

    // 智能消除 / 橡皮擦
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Inpaint, {}, L"✦", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});

    // 操作
    specs.push_back({ToolbarCommand::Undo, MarkupTool::Rectangle, {}, L"↩", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});
    specs.push_back({ToolbarCommand::Redo, MarkupTool::Rectangle, {}, L"↪", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});

    // 系统能力
    specs.push_back({ToolbarCommand::ExtractText, MarkupTool::Rectangle, {}, zh ? L"文" : L"OCR", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});
    specs.push_back({ToolbarCommand::PinWindow, MarkupTool::Rectangle, {}, L"📌", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});
    specs.push_back({ToolbarCommand::ScrollCapture, MarkupTool::Rectangle, {}, zh ? L"长" : L"⇊", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});
    specs.push_back({ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, L"✕", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});
    specs.push_back({ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, L"✓", 34.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false});

    return specs;
}

// 2. 二级属性栏规格（根据 currentTool 动态变幻）
std::vector<ButtonSpec> secondaryButtonSpecs(const CaptureState& state, bool zh) {
    if (state.mode == OverlayMode::RecordRegion) return {};

    static const std::array colors{
        MarkupColor::Red(), MarkupColor::Orange(), MarkupColor::Yellow(),
        MarkupColor::Green(), MarkupColor::Blue(), MarkupColor::Black(), MarkupColor::White(),
    };

    std::vector<ButtonSpec> specs;

    switch (state.currentTool) {
        case MarkupTool::Rectangle:
        case MarkupTool::Ellipse: {
            // 子形状切换
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Rectangle, {}, L"□", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Ellipse, {}, L"○", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});

            // 填充 Toggle
            specs.push_back({ToolbarCommand::ToggleFill, state.currentTool, {}, zh ? L"填充" : L"Fill", 48.0f, 0, LineStyle::Solid, ArrowStyle::Standard, state.currentFillMode, false, true});

            // 线条样式下拉
            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, state.currentTool, {}, L"───", 46.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true});

            // 线宽
            specs.push_back({ToolbarCommand::CycleStrokeWidth, state.currentTool, {}, std::format(L"{}", state.currentStrokeWidth), 38.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true});

            // 圆角（仅矩形支持）
            if (state.currentTool == MarkupTool::Rectangle) {
                specs.push_back({ToolbarCommand::CycleElementCornerRadius, state.currentTool, {}, std::format(L"{}", static_cast<int>(state.currentElementCornerRadius)), 38.0f, static_cast<int>(state.currentElementCornerRadius), LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            }

            // 7 色调色板
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            }
            break;
        }

        case MarkupTool::Pen:
        case MarkupTool::Highlight: {
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Pen, {}, L"✎", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Highlight, {}, L"🖍", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});

            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, state.currentTool, {}, L"───", 46.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, state.currentTool, {}, std::format(L"{}", state.currentStrokeWidth), 38.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true});

            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            }
            break;
        }

        case MarkupTool::Arrow: {
            specs.push_back({ToolbarCommand::ToggleArrowStyleDropdown, MarkupTool::Arrow, {}, L"──>", 46.0f, 0, LineStyle::Solid, state.currentArrowStyle, false, true, true});
            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, MarkupTool::Arrow, {}, L"───", 46.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Arrow, {}, std::format(L"{}", state.currentStrokeWidth), 38.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true});

            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            }
            break;
        }

        case MarkupTool::Text: {
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Text, {}, std::format(L"{}", state.currentStrokeWidth > 10 ? state.currentStrokeWidth : 18), 44.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            specs.push_back({ToolbarCommand::ToggleFill, MarkupTool::Text, {}, zh ? L"背景" : L"Bg", 48.0f, 0, LineStyle::Solid, ArrowStyle::Standard, state.currentFillMode, false, true});

            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            }
            break;
        }

        case MarkupTool::Mosaic: {
            specs.push_back({ToolbarCommand::SelectMosaicType, MarkupTool::Mosaic, {}, zh ? L"像素" : L"Pixel", 44.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            specs.push_back({ToolbarCommand::SelectMosaicType, MarkupTool::Mosaic, {}, zh ? L"模糊" : L"Blur", 44.0f, 1, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Mosaic, {}, std::format(L"{}", state.currentStrokeWidth * 3), 38.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true});
            break;
        }

        default:
            break;
    }

    return specs;
}

}  // namespace

void rebuildCaptureToolbar(CaptureState& state, const D2D1_RECT_F& selectionRect,
                           D2D1_SIZE_F surfaceSize) {
    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
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
    state.secondaryToolbarButtons.clear();
    state.toolbarLayoutValid = false;
    if (surfaceSize.width <= 0.0f || surfaceSize.height <= 0.0f) return;

    const float buttonHeight = 30.0f * scale;
    const float gapX = 3.5f * scale;
    const float paddingX = 8.0f * scale;
    const float paddingY = 6.0f * scale;
    const float tierGap = 6.0f * scale;
    const float screenMargin = 8.0f * scale;
    const bool chinese = useChineseLabels();

    // 1. 主工具栏布局
    const auto priSpecs = primaryButtonSpecs(state, chinese);
    float priContentW = 0.0f;
    for (const auto& sp : priSpecs) {
        priContentW += sp.baseWidth * scale + gapX;
    }
    if (!priSpecs.empty()) priContentW -= gapX;

    float priPanelW = priContentW + 2.0f * paddingX;
    float priPanelH = buttonHeight + 2.0f * paddingY;

    // 2. 二级属性栏布局
    const auto secSpecs = secondaryButtonSpecs(state, chinese);
    float secContentW = 0.0f;
    for (const auto& sp : secSpecs) {
        secContentW += sp.baseWidth * scale + gapX;
    }
    if (!secSpecs.empty()) secContentW -= gapX;

    float secPanelW = secSpecs.empty() ? 0.0f : (secContentW + 2.0f * paddingX);
    float secPanelH = secSpecs.empty() ? 0.0f : (buttonHeight + 2.0f * paddingY);

    float totalHeight = priPanelH + (secSpecs.empty() ? 0.0f : (tierGap + secPanelH));

    // 计算放置在选区下方还是上方
    float priPanelX = std::clamp(selectionRect.left, screenMargin,
                                 std::max(screenMargin, surfaceSize.width - priPanelW - screenMargin));
    float priPanelY = selectionRect.bottom + 8.0f * scale;

    if (priPanelY + totalHeight > surfaceSize.height - screenMargin) {
        priPanelY = selectionRect.top - totalHeight - 8.0f * scale;
    }
    priPanelY = std::clamp(priPanelY, screenMargin,
                           std::max(screenMargin, surfaceSize.height - totalHeight - screenMargin));

    state.primaryToolbarRect = D2D1::RectF(priPanelX, priPanelY, priPanelX + priPanelW, priPanelY + priPanelH);

    // 填充主工具栏按钮
    float curX = priPanelX + paddingX;
    float curY = priPanelY + paddingY;
    state.toolbarButtons.reserve(priSpecs.size());
    for (const auto& sp : priSpecs) {
        float w = sp.baseWidth * scale;
        ToolbarButton btn;
        btn.command = sp.command;
        btn.tool = sp.tool;
        btn.color = sp.color;
        btn.label = sp.label;
        btn.intParam = sp.intParam;
        btn.lineStyleParam = sp.lineStyleParam;
        btn.arrowStyleParam = sp.arrowStyleParam;
        btn.boolParam = sp.boolParam;
        btn.hasDropdown = sp.hasDropdown;
        btn.isSecondary = false;
        btn.rect = D2D1::RectF(curX, curY, curX + w, curY + buttonHeight);
        state.toolbarButtons.push_back(btn);
        curX += w + gapX;
    }

    // 填充二级属性栏按钮
    if (!secSpecs.empty()) {
        float secPanelX = priPanelX;
        if (secPanelX + secPanelW > surfaceSize.width - screenMargin) {
            secPanelX = surfaceSize.width - secPanelW - screenMargin;
        }
        float secPanelY = priPanelY + priPanelH + tierGap;
        state.secondaryToolbarRect = D2D1::RectF(secPanelX, secPanelY, secPanelX + secPanelW, secPanelY + secPanelH);

        float sCurX = secPanelX + paddingX;
        float sCurY = secPanelY + paddingY;
        state.secondaryToolbarButtons.reserve(secSpecs.size());
        for (const auto& sp : secSpecs) {
            float w = sp.baseWidth * scale;
            ToolbarButton btn;
            btn.command = sp.command;
            btn.tool = sp.tool;
            btn.color = sp.color;
            btn.label = sp.label;
            btn.intParam = sp.intParam;
            btn.lineStyleParam = sp.lineStyleParam;
            btn.arrowStyleParam = sp.arrowStyleParam;
            btn.boolParam = sp.boolParam;
            btn.hasDropdown = sp.hasDropdown;
            btn.isSecondary = true;
            btn.rect = D2D1::RectF(sCurX, sCurY, sCurX + w, sCurY + buttonHeight);
            state.secondaryToolbarButtons.push_back(btn);
            sCurX += w + gapX;
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
