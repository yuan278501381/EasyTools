#pragma once
#ifndef EASYTOOLS_CAPTURE_CAPTUREVECTORICONS_H
#define EASYTOOLS_CAPTURE_CAPTUREVECTORICONS_H

#include <d2d1.h>
#include <wrl/client.h>

namespace easy::capture {

enum class CaptureIconId {
    None = 0,
    // 标注工具
    ToolRectangle,
    ToolEllipse,
    ToolPen,
    ToolHighlight,
    ToolArrow,
    ToolArrowThin,
    ToolArrowDouble,
    ToolText,
    ToolNumber,
    ToolMosaic,
    ToolBlur,
    ToolInpaint,
    
    // 动作与控制
    ActionUndo,
    ActionRedo,
    ActionClear,
    ActionExtractText,
    ActionPinWindow,
    ActionScrollCapture,
    ActionRecordStart,
    ActionRecordPause,
    ActionRecordStop,
    ActionToggleMic,
    ActionToggleSpeaker,
    ActionCopy,
    ActionSave,
    ActionCancel,
    ActionConfirm,
    
    // 二级属性与样式
    PropSolidLine,
    PropDashedLine,
    PropDottedLine,
    PropDashDotLine,
    PropStrokeWidth,
    PropCornerRadius,
    PropFillOutline,
    PropFillSolid,
    PropPipette,
    PropPalette,
    PropQrCode,
};

class CaptureVectorIcons {
public:
    /// 在指定的矩形区域内居中渲染矢量微图标
    static void renderIcon(ID2D1RenderTarget* rt, ID2D1Factory* factory,
                           CaptureIconId iconId, const D2D1_RECT_F& rect,
                           ID2D1Brush* brush, float scale);
};

} // namespace easy::capture

#endif // EASYTOOLS_CAPTURE_CAPTUREVECTORICONS_H
