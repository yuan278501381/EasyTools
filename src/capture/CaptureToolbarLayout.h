#pragma once

#include "capture/CaptureState.h"

namespace tools3000::capture {

/// Rebuilds the capture/record toolbar from one shared layout algorithm used by
/// both painting and hit testing. Rows wrap when high-DPI controls would exceed
/// the physical work surface.
void rebuildCaptureToolbar(CaptureState& state, const D2D1_RECT_F& selectionRect,
                           D2D1_SIZE_F surfaceSize);

/// 执行截图向区域录屏模式的原子转换与状态清理
void transitionToRecordMode(CaptureState& state);

/// 获取工具栏按钮的悬停提示文本
std::wstring tooltipForButton(const ToolbarButton& button, bool chinese);

/// 计算悬停提示浮层矩形（含边界约束与智能防遮挡几何避让）
D2D1_RECT_F calculateTooltipRect(const D2D1_RECT_F& buttonRect, float tw, float th, float scale,
                                 D2D1_SIZE_F surfaceSize, const D2D1_RECT_F& selRect,
                                 const D2D1_RECT_F& secondaryRect);

}  // namespace tools3000::capture
