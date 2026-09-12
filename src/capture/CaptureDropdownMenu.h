#pragma once
#ifndef TOOLS3000_CAPTURE_CAPTUREDROPDOWNMENU_H
#define TOOLS3000_CAPTURE_CAPTUREDROPDOWNMENU_H

#include "capture/CaptureState.h"

namespace tools3000::capture {

/// 打开或轮转多态微晶下拉菜单
void openDropdownMenu(CaptureState& state, DropdownType type, const D2D1_RECT_F& anchorRect);

/// 提交下拉选项变更并写入持久化配置 (ConfigManager)
void commitDropdownSelection(CaptureState& state, DropdownType type, int id);

/// 更新下拉选项悬停状态及实时质感预览 (Hover Preview)
void updateDropdownHover(CaptureState& state, int hoverIndex);

/// 关闭下拉菜单并根据需要恢复预览前的原始值
void closeDropdownMenu(CaptureState& state, bool restorePreview = true);

}  // namespace tools3000::capture

#endif  // TOOLS3000_CAPTURE_CAPTUREDROPDOWNMENU_H
