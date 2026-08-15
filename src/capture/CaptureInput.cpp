#define RENDER_TIMER_ID 1
#include <algorithm>
#include <windowsx.h>
#include "capture/CaptureInput.h"
#include "capture/CaptureHistory.h"
#include "capture/PinWindow.h"
#include "capture/ScrollCapture.h"
#include "capture/ScrollCaptureOverlay.h"
#include "capture/ShortcutHintOverlay.h"
#include "capture/CaptureToolbarLayout.h"
#include "core/logger/Logger.h"
#include "core/utils/DpiUtils.h"
#include "core/utils/WinUtils.h"
#include "core/events/EventBus.h"
#include <windows.h>
#include <commdlg.h>
#include <imm.h>
#include <array>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <filesystem>

namespace easy::capture {

static HCURSOR cursorForArea(HitArea area) {
    switch (area) {
        case HitArea::LT:
        case HitArea::RB:
            return LoadCursor(nullptr, IDC_SIZENWSE);
        case HitArea::RT:
        case HitArea::LB:
            return LoadCursor(nullptr, IDC_SIZENESW);
        case HitArea::T:
        case HitArea::B:
            return LoadCursor(nullptr, IDC_SIZENS);
        case HitArea::L:
        case HitArea::R:
            return LoadCursor(nullptr, IDC_SIZEWE);
        case HitArea::Body:
            return LoadCursor(nullptr, IDC_SIZEALL);
        default:
            return LoadCursor(nullptr, IDC_ARROW);
    }
}

static DWORD filterIndexForFormat(ImageFormat format) {
    switch (format) {
        case ImageFormat::JPEG: return 2;
        case ImageFormat::BMP: return 3;
        case ImageFormat::WebP: return 4;
        default: return 1;
    }
}

static const wchar_t* extensionForFormat(ImageFormat format) {
    switch (format) {
        case ImageFormat::JPEG: return L"jpg";
        case ImageFormat::BMP: return L"bmp";
        case ImageFormat::WebP: return L"webp";
        default: return L"png";
    }
}

static ImageFormat formatForSavePath(const std::filesystem::path& path, DWORD filterIndex) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension == L".jpg" || extension == L".jpeg") return ImageFormat::JPEG;
    if (extension == L".bmp") return ImageFormat::BMP;
    if (extension == L".webp") return ImageFormat::WebP;
    if (extension == L".png") return ImageFormat::PNG;
    switch (filterIndex) {
        case 2: return ImageFormat::JPEG;
        case 3: return ImageFormat::BMP;
        case 4: return ImageFormat::WebP;
        default: return ImageFormat::PNG;
    }
}


void CaptureInput::initialize(HWND hwnd, CaptureState& state, CaptureRenderer& renderer,
                              std::function<void()> cancelCb,
                              std::function<void(CaptureCompletion)> confirmCb) {
    m_hwnd = hwnd;
    m_state = &state;
    m_renderer = &renderer;
    m_cancelCb = std::move(cancelCb);
    m_confirmCb = std::move(confirmCb);
}

void CaptureInput::updateHoverCursor(POINT point) {
    HCURSOR cur = nullptr;
    auto st = m_state->state.load();
    if ((int)st == (int)OverlayState::Selected || (int)st == (int)OverlayState::Marking) {
        if (hitTestToolbar(point)) {
            cur = LoadCursor(nullptr, IDC_ARROW);
        } else {
            // 选区控制点/边框优先（与点击命中顺序一致），其次才是选中元素的手柄
            HitArea sel = hitTestSelectionBox(point);
            if (sel != HitArea::None) {
                cur = cursorForArea(sel);
            } else {
                if (m_state->activeElement) {
                    cur = cursorForArea(m_state->activeElement->hitTestEx(toMarkupPoint(point)));
                }
                if (!cur) {
                    HitResult hit = m_state->markup.getElementAtEx(toMarkupPoint(point));
                    if (hit.element) {
                        cur = cursorForArea(hit.area);
                    }
                }
            }
        }
    }
    if (!cur) cur = LoadCursor(nullptr, IDC_CROSS);
    SetCursor(cur);
}

HitArea CaptureInput::hitTestSelectionBox(POINT point) const {
    auto r = currentSelectionRect();
    if (r.right - r.left < 1.0f || r.bottom - r.top < 1.0f) return HitArea::None;

    const float dpiScale = std::clamp(
        m_state->dpiScale > 0.0f ? m_state->dpiScale : 1.0f, 1.0f, 5.0f);
    const float hw = 9.0f * dpiScale;   // 控制点命中半径
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;

    struct H { float x, y; HitArea area; };
    const H handles[8] = {
        {r.left,  r.top,    HitArea::LT}, {cx,      r.top,    HitArea::T},
        {r.right, r.top,    HitArea::RT}, {r.right, cy,       HitArea::R},
        {r.right, r.bottom, HitArea::RB}, {cx,      r.bottom, HitArea::B},
        {r.left,  r.bottom, HitArea::LB}, {r.left,  cy,       HitArea::L},
    };
    float px = static_cast<float>(point.x);
    float py = static_cast<float>(point.y);
    for (const auto& h : handles) {
        if (std::abs(px - h.x) <= hw && std::abs(py - h.y) <= hw) return h.area;
    }

    // 边框带（用于整体移动）：靠近任意一条边即可拖动整块选区
    const float band = 6.0f * dpiScale;
    bool insideX = px >= r.left - band && px <= r.right + band;
    bool insideY = py >= r.top - band && py <= r.bottom + band;
    bool nearLeft   = std::abs(px - r.left)   <= band && insideY;
    bool nearRight  = std::abs(px - r.right)  <= band && insideY;
    bool nearTop    = std::abs(py - r.top)    <= band && insideX;
    bool nearBottom = std::abs(py - r.bottom) <= band && insideX;
    if (nearLeft || nearRight || nearTop || nearBottom) return HitArea::Body;
    return HitArea::None;
}

void CaptureInput::adjustSelection(HitArea handle, int dx, int dy) {
    // 记录调整前的选区左上角，用于已有标注的坐标重映射
    int oldLeft = std::min(m_state->dragStart.x, m_state->dragEnd.x);
    int oldTop  = std::min(m_state->dragStart.y, m_state->dragEnd.y);

    float l = static_cast<float>(std::min(m_state->dragStart.x, m_state->dragEnd.x));
    float t = static_cast<float>(std::min(m_state->dragStart.y, m_state->dragEnd.y));
    float r = static_cast<float>(std::max(m_state->dragStart.x, m_state->dragEnd.x));
    float b = static_cast<float>(std::max(m_state->dragStart.y, m_state->dragEnd.y));

    if (handle == HitArea::Body) {
        l += dx; r += dx; t += dy; b += dy;
    } else {
        switch (handle) {
            case HitArea::LT: l += dx; t += dy; break;
            case HitArea::T:  t += dy; break;
            case HitArea::RT: r += dx; t += dy; break;
            case HitArea::R:  r += dx; break;
            case HitArea::RB: r += dx; b += dy; break;
            case HitArea::B:  b += dy; break;
            case HitArea::LB: l += dx; b += dy; break;
            case HitArea::L:  l += dx; break;
            default: break;
        }
    }

    float maxW = 1.0e6f, maxH = 1.0e6f;
    if (m_renderer->getRenderTarget()) {
        auto s = m_renderer->getRenderTarget()->GetSize();
        maxW = s.width; maxH = s.height;
    }

    if (handle == HitArea::Body) {
        // 移动：整体夹取到屏幕内，保持尺寸不变
        float w = r - l, h = b - t;
        if (l < 0)     { l = 0;        r = w; }
        if (t < 0)     { t = 0;        b = h; }
        if (r > maxW)  { r = maxW;     l = maxW - w; }
        if (b > maxH)  { b = maxH;     t = maxH - h; }
    } else {
        // 缩放：夹取边界并保证最小尺寸
        l = std::clamp(l, 0.0f, maxW); r = std::clamp(r, 0.0f, maxW);
        t = std::clamp(t, 0.0f, maxH); b = std::clamp(b, 0.0f, maxH);
        const float minSize = 8.0f;
        if (r - l < minSize) {
            if (handle == HitArea::L || handle == HitArea::LT || handle == HitArea::LB) l = r - minSize;
            else r = l + minSize;
        }
        if (b - t < minSize) {
            if (handle == HitArea::T || handle == HitArea::LT || handle == HitArea::RT) t = b - minSize;
            else b = t + minSize;
        }
    }

    m_state->dragStart = { static_cast<LONG>(l), static_cast<LONG>(t) };
    m_state->dragEnd   = { static_cast<LONG>(r), static_cast<LONG>(b) };

    int newLeft = std::min(m_state->dragStart.x, m_state->dragEnd.x);
    int newTop  = std::min(m_state->dragStart.y, m_state->dragEnd.y);

    if (m_state->markup.hasAnyMarkup()) {
        // 已有标注（含撤销栈）：按左上角位移反向平移，使其"钉"在原屏幕内容上；并按新选区重裁底图
        m_state->markup.translateAll(oldLeft - newLeft, oldTop - newTop);
        rebuildMarkupBase();
        m_renderer->markMarkupDirty();
    } else {
        // 无任何标注：底图在下次标注时按新选区再裁（避免每帧重裁）
        m_state->markupBaseReady = false;
    }
    m_renderer->invalidate();
}

ToolbarButton* CaptureInput::hitTestToolbar(POINT point) {
    rebuildToolbarButtons(currentSelectionRect());
    for (auto& button : m_state->toolbarButtons) {
        if (point.x >= button.rect.left && point.x <= button.rect.right &&
            point.y >= button.rect.top && point.y <= button.rect.bottom) {
            return &button;
        }
    }
    return nullptr;
}

void CaptureInput::rebuildToolbarButtons(const D2D1_RECT_F& selectionRect) {
    if (!m_renderer->getRenderTarget()) return;
    rebuildCaptureToolbar(*m_state, selectionRect,
                          m_renderer->getRenderTarget()->GetSize());
}

void CaptureInput::executeToolbarCommand(const ToolbarButton& button) {
    if (m_state->activeElement) {
        m_state->activeElement->isActive = false;
        m_state->activeElement->isEditing = false;
        m_state->activeElement = nullptr;
    }

    switch (button.command) {
        case ToolbarCommand::SelectTool:
            m_state->currentTool = button.tool;
            m_state->state = OverlayState::Selected;
            m_state->isMarking = false;
            break;

        case ToolbarCommand::SelectColor:
            m_state->currentColor = button.color;
            break;

        case ToolbarCommand::Undo:
            m_state->markup.undo();
            break;

        case ToolbarCommand::Redo:
            m_state->markup.redo();
            break;

        case ToolbarCommand::Clear:
            m_state->markup.clearAll();
            prepareMarkupBase();
            break;

        case ToolbarCommand::ExtractText:
            if (m_state->ocrCallback) {
                int x1 = std::min(m_state->dragStart.x, m_state->dragEnd.x);
                int y1 = std::min(m_state->dragStart.y, m_state->dragEnd.y);
                int w = std::abs(m_state->dragEnd.x - m_state->dragStart.x);
                int h = std::abs(m_state->dragEnd.y - m_state->dragStart.y);
                
                cv::Mat cropped;
                if (m_state->markup.elementCount() > 0) cropped = m_state->markup.getCompositeImage();
                else {
                    cv::Rect roi(x1, y1, w, h);
                    roi &= cv::Rect(0, 0, m_state->frozenScreen.cols, m_state->frozenScreen.rows);
                    if (roi.area() > 0) m_state->frozenScreen(roi).copyTo(cropped);
                }
                if (cropped.channels() == 4) {
                    cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
                }

                int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                CaptureRegion region{x1 + offsetX, y1 + offsetY, w, h};
                auto ocrCb = m_state->ocrCallback;
                if(m_cancelCb) m_cancelCb(); 
                ocrCb(region, cropped);
            }
            break;

        case ToolbarCommand::PinWindow: {
            int x1 = std::min(m_state->dragStart.x, m_state->dragEnd.x);
            int y1 = std::min(m_state->dragStart.y, m_state->dragEnd.y);
            int w = std::abs(m_state->dragEnd.x - m_state->dragStart.x);
            int h = std::abs(m_state->dragEnd.y - m_state->dragStart.y);
            
            cv::Mat cropped;
            if (m_state->markup.elementCount() > 0) cropped = m_state->markup.getCompositeImage();
            else {
                cv::Rect roi(x1, y1, w, h);
                roi &= cv::Rect(0, 0, m_state->frozenScreen.cols, m_state->frozenScreen.rows);
                if (roi.area() > 0) m_state->frozenScreen(roi).copyTo(cropped);
            }
            if (cropped.channels() == 4) {
                cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
            }

            int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            
            // 先关闭截图覆盖层
            if(m_cancelCb) m_cancelCb();

            // 贴图在原位置
            PinWindow::create(cropped, x1 + offsetX, y1 + offsetY);
            break;
        }

        case ToolbarCommand::ScrollCapture: {
            int x1 = std::min(m_state->dragStart.x, m_state->dragEnd.x);
            int y1 = std::min(m_state->dragStart.y, m_state->dragEnd.y);
            int w = std::abs(m_state->dragEnd.x - m_state->dragStart.x);
            int h = std::abs(m_state->dragEnd.y - m_state->dragStart.y);

            int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            
            RECT capRect = {x1 + offsetX, y1 + offsetY, x1 + offsetX + w, y1 + offsetY + h};
            
            // 关闭覆盖层
            if(m_cancelCb) m_cancelCb();

            // 启动长截图；完成结果由 ScreenCapture 统一保存、复制并写入历史。
            ScrollCaptureOptions opts;
            opts.captureRect = capRect;
            opts.mode = ScrollMode::Auto;
            ScrollCaptureOverlay::instance().show(capRect);
            ScrollCapture::instance().start(opts);
            if (ScrollCapture::instance().isRunning()) {
                ShortcutHintOverlay::instance().show(
                    ShortcutHintContext::ScrollCapture,
                    {(capRect.left + capRect.right) / 2, (capRect.top + capRect.bottom) / 2});
                easy::core::EventBus::instance().publish(
                    easy::core::ShowToastEvent{L"正在长截图，按 Esc 停止"});
            }
            break;
        }

        case ToolbarCommand::Confirm:
            if(m_confirmCb) m_confirmCb({});
            break;

        case ToolbarCommand::Cancel:
            if(m_cancelCb) m_cancelCb();
            break;
    }
}

void CaptureInput::setCurrentTool(MarkupTool tool) {
    if (m_state->activeElement) {
        m_state->activeElement->isActive = false;
        m_state->activeElement->isEditing = false;
        m_state->activeElement = nullptr;
    }
    m_state->currentTool = tool;
    m_state->isMarking = false;
    if ((int)m_state->state.load() == (int)OverlayState::Marking) m_state->state = OverlayState::Selected;
    m_renderer->markMarkupDirty();
}

bool CaptureInput::isPointInSelection(POINT point) const {
    auto rect = currentSelectionRect();
    return point.x >= rect.left && point.x <= rect.right &&
           point.y >= rect.top && point.y <= rect.bottom;
}

cv::Point CaptureInput::toMarkupPoint(POINT point) const {
    auto rect = currentSelectionRect();
    int width = std::max(1, static_cast<int>(rect.right - rect.left));
    int height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    int x = std::clamp(static_cast<int>(point.x - rect.left), 0, width - 1);
    int y = std::clamp(static_cast<int>(point.y - rect.top), 0, height - 1);
    return {x, y};
}

void CaptureInput::beginMarkup(POINT point) {
    if (!isPointInSelection(point)) return;
    prepareMarkupBase();
    if (!m_state->markupBaseReady) return;

    cv::Point local = toMarkupPoint(point);
    if (m_state->currentTool == MarkupTool::Number) {
        m_state->markup.addNumberMark(local, m_state->currentColor);
        return;
    }
    if (m_state->currentTool == MarkupTool::Magnifier) {
        m_state->markup.addMagnifier(local);
        return;
    }
    if (m_state->currentTool == MarkupTool::Text) {
        if (m_state->activeElement) {
            m_state->activeElement->isActive = false;
            m_state->activeElement->isEditing = false;
        }
        m_state->markup.addText(local, "", m_state->currentColor, 18.0f * (m_state->dpiScale > 0 ? m_state->dpiScale : 1.0f));
        m_state->activeElement = m_state->markup.getElementAtEx(local).element;
        if (m_state->activeElement) {
            m_state->activeElement->isActive = true;
            m_state->activeElement->isEditing = true;
        }
        m_state->state = OverlayState::Selected;
        return;
    }

    m_state->markupStart = point;
    m_state->markupEnd = point;
    m_state->penPoints.clear();
    if (m_state->currentTool == MarkupTool::Pen) {
        m_state->penPoints.push_back(local);
    }
    m_state->isMarking = true;
    m_state->state = OverlayState::Marking;
}

void CaptureInput::updateMarkup(POINT point) {
    if (!m_state->isMarking) return;

    m_state->markupEnd = point;
    if (m_state->currentTool == MarkupTool::Pen) {
        cv::Point local = toMarkupPoint(point);
        if (m_state->penPoints.empty() || m_state->penPoints.back() != local) {
            m_state->penPoints.push_back(local);
        }
    }
}

void CaptureInput::finishMarkup(POINT point) {
    if (!m_state->isMarking) return;

    m_state->markupEnd = point;
    cv::Point start = toMarkupPoint(m_state->markupStart);
    cv::Point end = toMarkupPoint(m_state->markupEnd);
    int dx = std::abs(end.x - start.x);
    int dy = std::abs(end.y - start.y);

    if (m_state->currentTool != MarkupTool::Pen && dx < 3 && dy < 3) {
        m_state->isMarking = false;
        m_state->state = OverlayState::Selected;
        return;
    }

    switch (m_state->currentTool) {
        case MarkupTool::Rectangle:
            m_state->markup.drawRectangle(start, end, m_state->currentColor);
            break;

        case MarkupTool::Arrow:
            m_state->markup.drawArrow(start, end, m_state->currentColor);
            break;

        case MarkupTool::Ellipse:
            m_state->markup.drawEllipse(start, end, m_state->currentColor);
            break;

        case MarkupTool::Pen:
            m_state->markup.drawPenStroke(m_state->penPoints, m_state->currentColor);
            break;

        case MarkupTool::Highlight:
            m_state->markup.drawHighlight(start, end, m_state->currentColor);
            break;

        case MarkupTool::Mosaic:
            m_state->markup.applyMosaic(start, end);
            break;

        default:
            break;
    }

    m_state->isMarking = false;
    m_state->state = OverlayState::Selected;
}

void CaptureInput::prepareMarkupBase() {
    if (m_state->markupBaseReady || m_state->frozenScreen.empty()) return;

    auto rect = currentSelectionRect();
    int x = static_cast<int>(rect.left);
    int y = static_cast<int>(rect.top);
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);
    if (w <= 0 || h <= 0) return;

    cv::Rect roiRect(x, y, w, h);
    roiRect &= cv::Rect(0, 0, m_state->frozenScreen.cols, m_state->frozenScreen.rows);
    if (roiRect.area() <= 0) return;

    cv::Mat cropped;
    m_state->frozenScreen(roiRect).copyTo(cropped);
    if (cropped.channels() == 4) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
    }

    m_state->markup.setBaseImage(cropped);
    m_state->markupBaseReady = true;
}

void CaptureInput::rebuildMarkupBase() {
    if (m_state->frozenScreen.empty()) return;
    auto rect = currentSelectionRect();
    int x = static_cast<int>(rect.left);
    int y = static_cast<int>(rect.top);
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);
    if (w <= 0 || h <= 0) return;

    cv::Rect roi(x, y, w, h);
    roi &= cv::Rect(0, 0, m_state->frozenScreen.cols, m_state->frozenScreen.rows);
    if (roi.area() <= 0) return;

    cv::Mat cropped;
    m_state->frozenScreen(roi).copyTo(cropped);
    if (cropped.channels() == 4) {
        cv::cvtColor(cropped, cropped, cv::COLOR_BGRA2BGR);
    }
    m_state->markup.updateBaseImage(cropped);  // 保留标注，仅替换底图
    m_state->markupBaseReady = true;
}

RECT CaptureInput::detectWindowUnderCursor(POINT cursorPos) {
    HWND hwnd = WindowFromPoint(cursorPos);
    RECT rc{};
    if (hwnd) {
        GetWindowRect(hwnd, &rc);
        // 转为虚拟屏幕坐标
        int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rc.left -= offsetX;
        rc.top -= offsetY;
        rc.right -= offsetX;
        rc.bottom -= offsetY;
    }
    return rc;
}

D2D1_RECT_F CaptureInput::currentSelectionRect() const {
    float x1 = static_cast<float>(std::min(m_state->dragStart.x, m_state->dragEnd.x));
    float y1 = static_cast<float>(std::min(m_state->dragStart.y, m_state->dragEnd.y));
    float x2 = static_cast<float>(std::max(m_state->dragStart.x, m_state->dragEnd.x));
    float y2 = static_cast<float>(std::max(m_state->dragStart.y, m_state->dragEnd.y));
    return D2D1::RectF(x1, y1, x2, y2);
}

LRESULT CaptureInput::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    

    // 窗口过程兜底：渲染/标注路径含 OpenCV 分配、std::format 等可抛操作，异常绝不能逃逸到
    // Win32 派发层（否则 std::terminate 崩溃）。
    try {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            if (!this) break;
            m_renderer->markMarkupDirty();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // 文字编辑失焦提交
            if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                m_state->activeElement->isEditing = false;
                m_state->activeElement->isActive = false;
                m_state->activeElement = nullptr;
                m_renderer->invalidate();
                return 0; // 拦截点击，转为渲染态
            }

            if ((int)m_state->state.load() == (int)OverlayState::Selected || (int)m_state->state.load() == (int)OverlayState::Marking) {
                HitArea selHit = HitArea::None;
                if (auto* button = hitTestToolbar(point)) {
                    executeToolbarCommand(*button);
                } else if ((selHit = hitTestSelectionBox(point)) != HitArea::None) {
                    // 拖拽控制点缩放选区 / 拖拽边框移动选区（已有标注时会自动重映射坐标）
                    m_state->isAdjustingSelection = true;
                    m_state->selAdjustHandle = selHit;
                    m_state->selAdjustLast = point;
                } else if (isPointInSelection(point)) {
                    cv::Point local = toMarkupPoint(point);
                    HitResult hit = m_state->markup.getElementAtEx(local);

                    if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                        if (!hit.element || hit.element != m_state->activeElement) {
                            m_state->activeElement->isEditing = false;
                        }
                    }

                    if (hit.element) {
                        if (m_state->activeElement && m_state->activeElement != hit.element) {
                            m_state->activeElement->isActive = false;
                            m_state->activeElement->isEditing = false;
                        }
                        m_state->activeElement = hit.element;
                        m_state->activeElement->isActive = true;

                        if (hit.area == HitArea::Body) {
                            m_state->dragHandle = HitArea::None;
                            m_state->isManipulating = true;
                            m_state->lastMousePos = point;
                        } else {
                            m_state->dragHandle = hit.area;
                            m_state->isManipulating = true;
                            m_state->lastMousePos = point;
                        }
                    } else {
                        if (m_state->activeElement) {
                            m_state->activeElement->isActive = false;
                            m_state->activeElement->isEditing = false;
                            m_state->activeElement = nullptr;
                        }
                        beginMarkup(point);
                    }
                }
                return 0;
            }

            m_state->dragStart = point;
            m_state->dragEnd = m_state->dragStart;
            m_state->dragging = true;
            m_state->state = OverlayState::Selecting;
            if (m_state->options.showShortcutHints) {
                ShortcutHintOverlay::instance().show(
                    m_state->mode == OverlayMode::RecordRegion
                        ? ShortcutHintContext::RecordSelecting
                        : ShortcutHintContext::CaptureSelecting);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!this) break;
            m_state->currentCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            // Mouse coordinates are client-relative while monitor APIs require
            // virtual-desktop screen coordinates (which may also be negative).
            POINT screenPoint = m_state->currentCursor;
            ClientToScreen(hwnd, &screenPoint);
            HMONITOR hMon = MonitorFromPoint(screenPoint, MONITOR_DEFAULTTONEAREST);
            const UINT dpiX = easy::core::dpi::effectiveDpiForMonitor(hMon);
            const float newScale = easy::core::dpi::scaleForDpi(dpiX);
            if (std::abs(newScale - m_state->dpiScale) >= 0.01f) {
                m_state->dpiScale = newScale;
                if (!m_renderer->updateDpiScale(newScale)) {
                    LOG_WARN("截图覆盖层 DPI 文本资源更新失败: dpi={}", dpiX);
                }
                m_renderer->invalidate();
            }

            if (m_state->isAdjustingSelection) {
                int dx = m_state->currentCursor.x - m_state->selAdjustLast.x;
                int dy = m_state->currentCursor.y - m_state->selAdjustLast.y;
                adjustSelection(m_state->selAdjustHandle, dx, dy);
                m_state->selAdjustLast = m_state->currentCursor;
                m_renderer->invalidate();
            } else if (m_state->isManipulating && m_state->activeElement) {
                int dx = m_state->currentCursor.x - m_state->lastMousePos.x;
                int dy = m_state->currentCursor.y - m_state->lastMousePos.y;
                if (m_state->dragHandle == HitArea::None) {
                    m_state->activeElement->moveBy(dx, dy);
                } else {
                    m_state->activeElement->resize(dx, dy, m_state->dragHandle);
                }
                m_state->lastMousePos = m_state->currentCursor;
                m_renderer->markMarkupDirty();   // 元素几何变了，合成缓存失效
            } else if (m_state->isMarking) {
                updateMarkup(m_state->currentCursor);
                m_renderer->invalidate();        // 拖拽预览走 D2D，合成图不变
            } else if (m_state->dragging) {
                m_state->dragEnd = m_state->currentCursor;
                m_renderer->invalidate();
            } else {
                if ((int)m_state->state.load() == (int)OverlayState::Selecting && !m_state->dragging) {
                    if (m_state->options.autoDetectWindow) {
                        // 未拖拽时检测光标下的窗口
                        POINT screenPt = m_state->currentCursor;
                        int offX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                        int offY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                        screenPt.x += offX;
                        screenPt.y += offY;
                        m_state->detectedWindow = detectWindowUnderCursor(screenPt);
                    } else {
                        m_state->detectedWindow = {};
                    }
                }
                m_renderer->invalidate();        // 十字准星/动态放大镜跟随光标
            }

            updateHoverCursor(m_state->currentCursor);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!this) break;
            m_renderer->markMarkupDirty();

            if (m_state->isAdjustingSelection) {
                m_state->isAdjustingSelection = false;
                m_state->selAdjustHandle = HitArea::None;
                return 0;
            }

            if (m_state->isManipulating) {
                m_state->isManipulating = false;
                return 0;
            }

            if (m_state->isMarking) {
                finishMarkup({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }

            if (!m_state->dragging) break;
            m_state->dragEnd = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            m_state->dragging = false;

            int w = std::abs(m_state->dragEnd.x - m_state->dragStart.x);
            int h = std::abs(m_state->dragEnd.y - m_state->dragStart.y);

            if (w > 3 && h > 3) {
                m_state->state = OverlayState::Selected;
                prepareMarkupBase();
                if (m_state->options.showShortcutHints) {
                    ShortcutHintOverlay::instance().show(
                        m_state->mode == OverlayMode::RecordRegion
                            ? ShortcutHintContext::RecordSelecting
                            : ShortcutHintContext::CaptureSelected);
                }
            } else {
                // 拖拽太小，视为点击——吸附到检测的窗口
                if (m_state->detectedWindow.right > m_state->detectedWindow.left &&
                    m_state->detectedWindow.bottom > m_state->detectedWindow.top) {
                    m_state->dragStart = {static_cast<LONG>(m_state->detectedWindow.left),
                                         static_cast<LONG>(m_state->detectedWindow.top)};
                    m_state->dragEnd = {static_cast<LONG>(m_state->detectedWindow.right),
                                       static_cast<LONG>(m_state->detectedWindow.bottom)};
                    m_state->state = OverlayState::Selected;
                    prepareMarkupBase();
                    if (m_state->options.showShortcutHints) {
                        ShortcutHintOverlay::instance().show(
                            m_state->mode == OverlayMode::RecordRegion
                                ? ShortcutHintContext::RecordSelecting
                                : ShortcutHintContext::CaptureSelected);
                    }
                } else {
                    if (m_cancelCb) m_cancelCb();
                }
            }
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            if (this && (int)m_state->state.load() == (int)OverlayState::Selected) {
                m_renderer->markMarkupDirty();
                if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text) {
                    m_state->activeElement->isEditing = true;
                    return 0;
                }
                if (m_confirmCb) m_confirmCb({});
            }
            return 0;
        }

        case WM_CHAR: {
            if (this && m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                m_renderer->markMarkupDirty();
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch == 0x08) { // Backspace
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(m_state->activeElement->text);
                    if (!wstr.empty()) {
                        wstr.pop_back();
                        m_state->activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                        m_state->activeElement->textRenderSize = cv::Size(0,0);
                    }
                } else if (ch == 0x0D || ch == 0x0A) { // Enter
                    m_state->activeElement->isEditing = false;
                } else if (ch >= 0x20) {
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(m_state->activeElement->text);
                    wstr.push_back(ch);
                    m_state->activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                    m_state->activeElement->textRenderSize = cv::Size(0,0);
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (!this) break;
            m_renderer->markMarkupDirty();
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Magnifier) {
                m_state->activeElement->magnifierScale += (zDelta > 0) ? 0.2f : -0.2f;
                m_state->activeElement->magnifierScale = std::clamp(m_state->activeElement->magnifierScale, 1.0f, 8.0f);
            } else if (m_state->currentTool == MarkupTool::Magnifier && !m_state->isMarking && !m_state->isManipulating) {
                m_state->dynamicMagnifierScale += (zDelta > 0) ? 0.2f : -0.2f;
                m_state->dynamicMagnifierScale = std::clamp(m_state->dynamicMagnifierScale, 1.0f, 8.0f);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (!this) break;
            m_renderer->markMarkupDirty();

            // 历史回放快捷键
            if (wParam == VK_OEM_2) { // '/'
                m_state->historyMode = !m_state->historyMode;
                if (m_state->historyMode) {
                    m_state->historyIndex = 0;
                    m_renderer->updateHistoryBitmap(*m_state);
                }
                m_renderer->invalidate();
                return 0;
            }
            if (m_state->historyMode) {
                if (wParam == VK_OEM_COMMA) { // ',' 上一张
                    if (m_state->historyIndex < CaptureHistory::instance().count() - 1) {
                        m_state->historyIndex++;
                        m_renderer->updateHistoryBitmap(*m_state);
                    }
                    return 0;
                }
                if (wParam == VK_OEM_PERIOD) { // '.' 下一张
                    if (m_state->historyIndex > 0) {
                        m_state->historyIndex--;
                        m_renderer->updateHistoryBitmap(*m_state);
                    }
                    return 0;
                }
                // 在历史模式下，按ESC退出
                if (wParam == VK_ESCAPE) {
                    m_state->historyMode = false;
                    m_renderer->invalidate();
                    return 0;
                }
                return 0;
            }

            bool editingText = m_state->activeElement &&
                               m_state->activeElement->tool == MarkupTool::Text &&
                               m_state->activeElement->isEditing;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool hasSelection = ((int)m_state->state.load() == (int)OverlayState::Selected ||
                                 (int)m_state->state.load() == (int)OverlayState::Marking);

            // 方向键微调与光标像素级移动
            if (!editingText) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                
                // 存在选区时：方向键移动选区，Shift+方向键调整选区尺寸
                if (hasSelection && (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT)) {
                    int step = ctrl ? 10 : 1;
                    int dx = (wParam == VK_LEFT ? -step : wParam == VK_RIGHT ? step : 0);
                    int dy = (wParam == VK_UP   ? -step : wParam == VK_DOWN  ? step : 0);
                    
                    if (m_state->activeElement) {
                        m_state->activeElement->moveBy(dx, dy);
                    } else if (shift) {
                        adjustSelection(HitArea::RB, dx, dy);
                    } else {
                        adjustSelection(HitArea::Body, dx, dy);
                    }
                    prepareMarkupBase();
                    m_renderer->invalidate();
                    return 0;
                }
                
                // 无选区时：方向键或 WASD 控制光标进行 1px 微调（精准取色）
                if (!hasSelection) {
                    POINT pt;
                    GetCursorPos(&pt);
                    bool moved = false;
                    if (wParam == 'W' || wParam == VK_UP) { pt.y -= 1; moved = true; }
                    if (wParam == 'S' || wParam == VK_DOWN) { pt.y += 1; moved = true; }
                    if (wParam == 'A' || wParam == VK_LEFT) { pt.x -= 1; moved = true; }
                    if (wParam == 'D' || wParam == VK_RIGHT) { pt.x += 1; moved = true; }
                    if (moved) {
                        SetCursorPos(pt.x, pt.y);
                        m_renderer->invalidate();
                        return 0;
                    }
                }
            }

            // ESC 分级退出：编辑文字→退出编辑；选中元素→取消选中；否则→关闭截图
            if (wParam == VK_ESCAPE) {
                if (editingText) {
                    m_state->activeElement->isEditing = false;
                } else if (m_state->activeElement) {
                    m_state->activeElement->isActive = false;
                    m_state->activeElement = nullptr;
                } else {
                    if (m_cancelCb) m_cancelCb();
                }
                return 0;
            }

            // 正在输入文字：其余按键交给 WM_CHAR
            if (editingText) return 0;

            // 取色：选区前/拖拽中按 C 复制 HEX 格式，按 Shift+C 复制 RGB 格式
            if (wParam == 'C' && !ctrl &&
                ((int)m_state->state.load() == (int)OverlayState::Idle ||
                 (int)m_state->state.load() == (int)OverlayState::Selecting)) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                int cr, cg, cb;
                if (m_renderer->sampleScreenColor(m_state->currentCursor.x, m_state->currentCursor.y, cr, cg, cb, *m_state)) {
                    std::string colorText = shift ? std::format("rgb({}, {}, {})", cr, cg, cb)
                                                  : std::format("#{:02X}{:02X}{:02X}", cr, cg, cb);
                    easy::core::WinUtils::copyToClipboard(colorText);
                    m_state->loupeToastUntil = GetTickCount() + 1200;
                    m_renderer->invalidate();
                }
                return 0;
            }

            // Ctrl+A: 一键全选当前屏幕
            if (ctrl && wParam == 'A') {
                m_state->dragStart = {0, 0};
                m_state->dragEnd = {m_state->frozenScreen.cols, m_state->frozenScreen.rows};
                m_state->state = OverlayState::Selected;
                prepareMarkupBase();
                m_renderer->invalidate();
                if (m_state->options.showShortcutHints) {
                    ShortcutHintOverlay::instance().show(
                        m_state->mode == OverlayMode::RecordRegion
                            ? ShortcutHintContext::RecordSelecting
                            : ShortcutHintContext::CaptureSelected);
                }
                return 0;
            }

            // 全局命令
            switch (wParam) {
                case VK_RETURN:
                    if (hasSelection) if (m_confirmCb) m_confirmCb({});
                    return 0;
                case 'C':
                    if (ctrl && hasSelection) {
                        if (m_confirmCb) m_confirmCb({CaptureCompletionAction::Copy});
                        return 0;
                    }
                    break;
                case 'T':
                    if (ctrl && hasSelection) {
                        ToolbarButton pinBtn;
                        pinBtn.command = ToolbarCommand::PinWindow;
                        executeToolbarCommand(pinBtn);
                        return 0;
                    } else if (!ctrl) {
                        setCurrentTool(MarkupTool::Text);
                        return 0;
                    }
                    break;
                case 'S':
                    if (ctrl && hasSelection) {
                        std::array<wchar_t, 32768> fileBuffer{};
                        const auto defaultExtension = extensionForFormat(m_state->options.format);
                        const auto defaultName = std::wstring(L"EasyTools_Screenshot.") + defaultExtension;
                        wcsncpy_s(fileBuffer.data(), fileBuffer.size(), defaultName.c_str(), _TRUNCATE);
                        OPENFILENAMEW ofn{};
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFile = fileBuffer.data();
                        ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
                        ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0JPEG Image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0Bitmap (*.bmp)\0*.bmp\0WebP Image (*.webp)\0*.webp\0";
                        ofn.nFilterIndex = filterIndexForFormat(m_state->options.format);
                        ofn.lpstrDefExt = defaultExtension;
                        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST |
                                    OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
                        if (GetSaveFileNameW(&ofn)) {
                            std::filesystem::path selected(fileBuffer.data());
                            if (!selected.has_extension()) {
                                selected += L".";
                                selected += extensionForFormat(formatForSavePath(selected, ofn.nFilterIndex));
                            }
                            if (m_confirmCb) {
                                m_confirmCb({
                                    CaptureCompletionAction::SaveAs,
                                    easy::core::WinUtils::wstringToUtf8(selected.wstring()),
                                    formatForSavePath(selected, ofn.nFilterIndex)});
                            }
                        }
                        return 0;
                    } else if (!ctrl) {
                        setCurrentTool(MarkupTool::Spotlight);
                        return 0;
                    }
                    break;
                case 'Z':
                    if (ctrl) { m_state->activeElement = nullptr; m_state->markup.undo(); }
                    return 0;
                case 'Y':
                    if (ctrl) { m_state->activeElement = nullptr; m_state->markup.redo(); }
                    return 0;
                case VK_DELETE:
                    if (m_state->activeElement) {
                        m_state->markup.removeElement(m_state->activeElement->id);
                        m_state->activeElement = nullptr;
                    }
                    return 0;
                case '1': if (!ctrl) { m_state->currentColor = MarkupColor::Red();    return 0; } break;
                case '2': if (!ctrl) { m_state->currentColor = MarkupColor::Yellow(); return 0; } break;
                case '3': if (!ctrl) { m_state->currentColor = MarkupColor::Green();  return 0; } break;
                case '4': if (!ctrl) { m_state->currentColor = MarkupColor::Blue();   return 0; } break;
                case '5': if (!ctrl) { m_state->currentColor = MarkupColor::White();  return 0; } break;
                case 'R': if (!ctrl) { setCurrentTool(MarkupTool::Rectangle); return 0; } break;
                case 'A': if (!ctrl) { setCurrentTool(MarkupTool::Arrow);     return 0; } break;
                case 'O': case 'E': if (!ctrl) { setCurrentTool(MarkupTool::Ellipse); return 0; } break;
                case 'P': if (!ctrl) { setCurrentTool(MarkupTool::Pen);       return 0; } break;
                case 'H': if (!ctrl) { setCurrentTool(MarkupTool::Highlight); return 0; } break;
                case 'M': if (!ctrl) { setCurrentTool(MarkupTool::Mosaic);    return 0; } break;
                case 'N': if (!ctrl) { setCurrentTool(MarkupTool::Number);    return 0; } break;
                case 'G': if (!ctrl) { setCurrentTool(MarkupTool::Magnifier); return 0; } break;
                case 'W': if (!ctrl) { setCurrentTool(MarkupTool::Watermark); return 0; } break;
                case 'I': if (!ctrl) { setCurrentTool(MarkupTool::Inpaint);   return 0; } break;
                default: break;
            }
            return 0;
        }

        case WM_TIMER: {
            if (this && wParam == RENDER_TIMER_ID) {
                if (m_state->isFadingOut) {
                    DWORD elapsed = GetTickCount() - m_state->fadeOutStart;
                    if (elapsed >= 150) {
                        if (m_cancelCb) m_cancelCb();
                    } else {
                        float alpha = 1.0f - (elapsed / 150.0f);
                        SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha * 255), LWA_ALPHA);
                    }
                    return 0;
                }
                
                // 取色放大镜：复制成功提示的存续期间持续重绘，到期后再渲染一帧将其清除
                if (m_state->loupeToastUntil != 0) {
                    m_renderer->invalidate();
                    if (GetTickCount() >= m_state->loupeToastUntil) m_state->loupeToastUntil = 0;
                }
                // 文字编辑期间需持续重算以保留闪烁光标；其余情况按脏标记重绘。
                bool editingText = m_state->activeElement &&
                                   m_state->activeElement->tool == MarkupTool::Text &&
                                   m_state->activeElement->isEditing;
                if (editingText) {
                    m_renderer->markMarkupDirty();
                    m_renderer->invalidate();
                }
                if (m_renderer->needsRender()) {
                    m_renderer->clearNeedsRender();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    } catch (const std::exception& e) {
        LOG_ERROR("CaptureOverlay 窗口过程异常: {}", e.what());
    } catch (...) {
        LOG_ERROR("CaptureOverlay 窗口过程未知异常");
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace easy::capture
