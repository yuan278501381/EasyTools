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
    bool isSeparatorBefore = false;
};

bool useChineseLabels() {
    const auto language = easy::core::ConfigManager::instance().get<std::string>(
        "/general/language", "auto");
    return language == "zh-CN" ||
           (language == "auto" && easy::core::WinUtils::isSystemLanguageChinese());
}

// 1. 主工具栏规格
std::vector<ButtonSpec> primaryButtonSpecs(const CaptureState& state, bool zh) {
    (void)zh;
    if (state.mode == OverlayMode::RecordRegion) {
        return {
            {ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, L"", 34.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false},
            {ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, true},
        };
    }

    std::vector<ButtonSpec> specs;
    // 形状工具组（矩形 / 椭圆）
    specs.push_back({ToolbarCommand::SelectTool, 
                     (state.currentTool == MarkupTool::Ellipse ? MarkupTool::Ellipse : MarkupTool::Rectangle),
                     {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 画笔工具组（铅笔 / 荧光笔）
    specs.push_back({ToolbarCommand::SelectTool,
                     (state.currentTool == MarkupTool::Highlight ? MarkupTool::Highlight : MarkupTool::Pen),
                     {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 箭头工具组
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Arrow, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 文本工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Text, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 序号工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Number, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 马赛克工具
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Mosaic, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 智能消除 / 橡皮擦
    specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Inpaint, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 操作组（带前置分隔线）
    specs.push_back({ToolbarCommand::Undo, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, true});
    specs.push_back({ToolbarCommand::Redo, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 系统能力（带前置分隔线）
    specs.push_back({ToolbarCommand::ExtractText, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, true});
    specs.push_back({ToolbarCommand::PinWindow, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});
    specs.push_back({ToolbarCommand::ScrollCapture, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    // 动作组（带前置分隔线）
    specs.push_back({ToolbarCommand::Cancel, MarkupTool::Rectangle, {}, L"", 30.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, true});
    specs.push_back({ToolbarCommand::Confirm, MarkupTool::Rectangle, {}, L"", 38.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, false, false});

    return specs;
}

// 2. 二级属性栏规格（根据 currentTool 动态变幻）
std::vector<ButtonSpec> secondaryButtonSpecs(const CaptureState& state, bool zh) {
    (void)zh;
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
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Rectangle, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Ellipse, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});

            // 填充 Toggle（纯矢量图标，无生硬汉字）
            specs.push_back({ToolbarCommand::ToggleFill, state.currentTool, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, state.currentFillMode, false, true, false});

            // 线条样式下拉
            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, state.currentTool, {}, L"", 38.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true, false});

            // 线宽
            specs.push_back({ToolbarCommand::CycleStrokeWidth, state.currentTool, {}, std::format(L"{}", state.currentStrokeWidth), 36.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});

            // 圆角（仅矩形支持）
            if (state.currentTool == MarkupTool::Rectangle) {
                specs.push_back({ToolbarCommand::CycleElementCornerRadius, state.currentTool, {}, std::format(L"{}", static_cast<int>(state.currentElementCornerRadius)), 36.0f, static_cast<int>(state.currentElementCornerRadius), LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            }

            // 7 色调色板（带前置分隔线）
            bool firstColor = true;
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, firstColor});
                firstColor = false;
            }
            break;
        }

        case MarkupTool::Pen:
        case MarkupTool::Highlight: {
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Pen, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            specs.push_back({ToolbarCommand::SelectTool, MarkupTool::Highlight, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});

            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, state.currentTool, {}, L"", 38.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true, false});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, state.currentTool, {}, std::format(L"{}", state.currentStrokeWidth), 36.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});

            bool firstColor = true;
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, firstColor});
                firstColor = false;
            }
            break;
        }

        case MarkupTool::Arrow: {
            specs.push_back({ToolbarCommand::ToggleArrowStyleDropdown, MarkupTool::Arrow, {}, L"", 38.0f, 0, LineStyle::Solid, state.currentArrowStyle, false, true, true, false});
            specs.push_back({ToolbarCommand::ToggleLineStyleDropdown, MarkupTool::Arrow, {}, L"", 38.0f, 0, state.currentLineStyle, ArrowStyle::Standard, false, true, true, false});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Arrow, {}, std::format(L"{}", state.currentStrokeWidth), 36.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});

            bool firstColor = true;
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, firstColor});
                firstColor = false;
            }
            break;
        }

        case MarkupTool::Text: {
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Text, {}, std::format(L"{}", state.currentStrokeWidth > 10 ? state.currentStrokeWidth : 18), 38.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            specs.push_back({ToolbarCommand::ToggleFill, MarkupTool::Text, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, state.currentFillMode, false, true, false});

            bool firstColor = true;
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, firstColor});
                firstColor = false;
            }
            break;
        }

        case MarkupTool::Mosaic: {
            specs.push_back({ToolbarCommand::SelectMosaicType, MarkupTool::Mosaic, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            specs.push_back({ToolbarCommand::SelectMosaicType, MarkupTool::Mosaic, {}, L"", 28.0f, 1, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            specs.push_back({ToolbarCommand::CycleStrokeWidth, MarkupTool::Mosaic, {}, std::format(L"{}", state.currentStrokeWidth * 3), 36.0f, state.currentStrokeWidth, LineStyle::Solid, ArrowStyle::Standard, false, false, true, false});
            break;
        }

        case MarkupTool::Number: {
            specs.push_back({ToolbarCommand::ToggleFill, MarkupTool::Number, {}, L"", 28.0f, 0, LineStyle::Solid, ArrowStyle::Standard, state.currentFillMode, false, true, false});

            bool firstColor = true;
            for (const auto& color : colors) {
                specs.push_back({ToolbarCommand::SelectColor, state.currentTool, color, L"", 20.0f, 0, LineStyle::Solid, ArrowStyle::Standard, false, false, true, firstColor});
                firstColor = false;
            }
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
    const float sepWidth = 7.0f * scale;
    const float paddingX = 8.0f * scale;
    const float paddingY = 6.0f * scale;
    const float tierGap = 6.0f * scale;
    const float screenMargin = 8.0f * scale;
    const bool chinese = useChineseLabels();

    // 1. 主工具栏布局
    const auto priSpecs = primaryButtonSpecs(state, chinese);
    float priContentW = 0.0f;
    for (const auto& sp : priSpecs) {
        float extra = sp.isSeparatorBefore ? sepWidth : 0.0f;
        priContentW += extra + sp.baseWidth * scale + gapX;
    }
    if (!priSpecs.empty()) priContentW -= gapX;

    float priPanelW = priContentW + 2.0f * paddingX;
    float priPanelH = buttonHeight + 2.0f * paddingY;

    // 针对极限小屏幕 / 超高 DPI 的响应式宽度自适应压缩 (Responsive Scaling Gate)
    const float maxAvailPriW = std::max(40.0f * scale, surfaceSize.width - 2.0f * screenMargin - 2.0f * paddingX);
    float priShrink = (priContentW > maxAvailPriW && priContentW > 0.0f) ? (maxAvailPriW / priContentW) : 1.0f;
    if (priShrink < 1.0f) {
        priContentW *= priShrink;
        priPanelW = priContentW + 2.0f * paddingX;
    }

    // 2. 二级属性栏布局
    const auto secSpecs = secondaryButtonSpecs(state, chinese);
    float secContentW = 0.0f;
    for (const auto& sp : secSpecs) {
        float extra = sp.isSeparatorBefore ? sepWidth : 0.0f;
        secContentW += extra + sp.baseWidth * scale + gapX;
    }
    if (!secSpecs.empty()) secContentW -= gapX;

    float secPanelW = secSpecs.empty() ? 0.0f : (secContentW + 2.0f * paddingX);
    float secPanelH = secSpecs.empty() ? 0.0f : (buttonHeight + 2.0f * paddingY);

    const float maxAvailSecW = std::max(40.0f * scale, surfaceSize.width - 2.0f * screenMargin - 2.0f * paddingX);
    float secShrink = (secContentW > maxAvailSecW && secContentW > 0.0f) ? (maxAvailSecW / secContentW) : 1.0f;
    if (secShrink < 1.0f) {
        secContentW *= secShrink;
        secPanelW = secContentW + 2.0f * paddingX;
    }

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
        if (sp.isSeparatorBefore) {
            curX += sepWidth * priShrink;
        }
        float w = sp.baseWidth * scale * priShrink;
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
        btn.isSeparatorBefore = sp.isSeparatorBefore;
        btn.rect = D2D1::RectF(curX, curY, curX + w, curY + buttonHeight);
        state.toolbarButtons.push_back(btn);
        curX += w + gapX * priShrink;
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
            if (sp.isSeparatorBefore) {
                sCurX += sepWidth * secShrink;
            }
            float w = sp.baseWidth * scale * secShrink;
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
            btn.isSeparatorBefore = sp.isSeparatorBefore;
            btn.rect = D2D1::RectF(sCurX, sCurY, sCurX + w, sCurY + buttonHeight);
            state.secondaryToolbarButtons.push_back(btn);
            sCurX += w + gapX * secShrink;
        }
    }

    // 3. 选区侧边自适应浮动菜单布局 (PixPin 标杆：紧贴选区右侧垂直排列，带屏幕右边缘自适应翻转)
    state.selectionSideButtons.clear();
    float selW = selectionRect.right - selectionRect.left;
    float selH = selectionRect.bottom - selectionRect.top;
    if (state.mode == OverlayMode::Screenshot && selW >= 20.0f * scale && selH >= 20.0f * scale) {
        const float sideBtnSz = 28.0f * scale;
        const float sideGapY = 5.0f * scale;
        const float sidePad = 4.0f * scale;
        const int sideCount = 3; // 1: 圆角, 2: 反选, 3: 重置直角
        float sideH = sideCount * sideBtnSz + (sideCount - 1) * sideGapY + 2.0f * sidePad;
        float sideW = sideBtnSz + 2.0f * sidePad;

        // 默认紧贴选区右侧
        float sideX = selectionRect.right + 8.0f * scale;
        // 如果右侧空间不足以容纳侧边栏 + 滑块弹窗 (约 220px)，自动平滑翻转至选区左侧
        if (sideX + sideW + 200.0f * scale > surfaceSize.width - screenMargin) {
            sideX = selectionRect.left - sideW - 8.0f * scale;
        }
        // 如果左侧也超出屏幕，则紧贴选区内部右侧
        if (sideX < screenMargin) {
            sideX = std::max(screenMargin, selectionRect.left + 8.0f * scale);
        }

        // Y 轴垂直居中对齐选区
        float sideY = selectionRect.top + (selH - sideH) * 0.5f;
        sideY = std::clamp(sideY, screenMargin, surfaceSize.height - sideH - screenMargin);

        state.selectionSideRect = D2D1::RectF(sideX, sideY, sideX + sideW, sideY + sideH);

        // 按钮 1: 调节选区圆角
        ToolbarButton btnCorner;
        btnCorner.command = ToolbarCommand::SideToggleCornerRadius;
        btnCorner.tool = MarkupTool::Rectangle;
        btnCorner.rect = D2D1::RectF(sideX + sidePad, sideY + sidePad, sideX + sidePad + sideBtnSz, sideY + sidePad + sideBtnSz);
        state.selectionSideButtons.push_back(btnCorner);

        // 按钮 2: 反向选择 / 选区扩展
        ToolbarButton btnInvert;
        btnInvert.command = ToolbarCommand::SideInvertSelection;
        btnInvert.tool = MarkupTool::Rectangle;
        float y2 = sideY + sidePad + sideBtnSz + sideGapY;
        btnInvert.rect = D2D1::RectF(sideX + sidePad, y2, sideX + sidePad + sideBtnSz, y2 + sideBtnSz);
        state.selectionSideButtons.push_back(btnInvert);

        // 按钮 3: 重置选区直角 (0px)
        ToolbarButton btnReset;
        btnReset.command = ToolbarCommand::SideResetSelection;
        btnReset.tool = MarkupTool::Rectangle;
        float y3 = y2 + sideBtnSz + sideGapY;
        btnReset.rect = D2D1::RectF(sideX + sidePad, y3, sideX + sidePad + sideBtnSz, y3 + sideBtnSz);
        state.selectionSideButtons.push_back(btnReset);
    }

    state.toolbarLayoutSelection = selectionRect;
    state.toolbarLayoutSurface = surfaceSize;
    state.toolbarLayoutScale = scale;
    state.toolbarLayoutMode = state.mode;
    state.toolbarLayoutChinese = chinese;
    state.toolbarLayoutValid = true;
}

}  // namespace easy::capture
