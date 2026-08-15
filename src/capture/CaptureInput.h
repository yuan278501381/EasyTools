#pragma once
#ifndef EASYTOOLS_CAPTURE_CAPTUREINPUT_H
#define EASYTOOLS_CAPTURE_CAPTUREINPUT_H

#include "capture/CaptureState.h"
#include "capture/CaptureRenderer.h"
#include <windows.h>
#include <functional>

namespace easy::capture {

class CaptureInput {
public:
    void initialize(HWND hwnd, CaptureState& state, CaptureRenderer& renderer,
                    std::function<void()> cancelCb,
                    std::function<void(CaptureCompletion)> confirmCb);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void updateHoverCursor(POINT point);
    HitArea hitTestSelectionBox(POINT point) const;
    void adjustSelection(HitArea handle, int dx, int dy);
    
    ToolbarButton* hitTestToolbar(POINT point);
    void rebuildToolbarButtons(const D2D1_RECT_F& selectionRect);
    void executeToolbarCommand(const ToolbarButton& button);
    
    void setCurrentTool(MarkupTool tool);
    bool isPointInSelection(POINT point) const;
    cv::Point toMarkupPoint(POINT point) const;
    
    void beginMarkup(POINT point);
    void updateMarkup(POINT point);
    void finishMarkup(POINT point);
    
    void prepareMarkupBase();
    void rebuildMarkupBase();
    
    RECT detectWindowUnderCursor(POINT cursorPos);
    D2D1_RECT_F currentSelectionRect() const;

    HWND m_hwnd = nullptr;
    CaptureState* m_state = nullptr;
    CaptureRenderer* m_renderer = nullptr;
    std::function<void()> m_cancelCb;
    std::function<void(CaptureCompletion)> m_confirmCb;
    
    void enableIME(bool enable);
    HIMC m_defaultImc = nullptr;
};

} // namespace easy::capture
#endif // EASYTOOLS_CAPTURE_CAPTUREINPUT_H
