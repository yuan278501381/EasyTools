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
#include "capture/CaptureToolbarAccessibility.h"
#include "core/accessibility/OverlayAnnouncement.h"
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

namespace {

std::string formatColorText(ColorFormatType type, int r, int g, int b) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxVal = std::max({rf, gf, bf}), minVal = std::min({rf, gf, bf});
    float delta = maxVal - minVal;

    switch (type) {
        case ColorFormatType::HEX:
            return std::format("#{:02X}{:02X}{:02X}", r, g, b);
        case ColorFormatType::RGB:
            return std::format("rgb({}, {}, {})", r, g, b);
        case ColorFormatType::RGBA:
            return std::format("rgba({}, {}, {}, 1.0)", r, g, b);
        case ColorFormatType::HEX_0x:
            return std::format("0x{:02X}{:02X}{:02X}", r, g, b);
        case ColorFormatType::HSL: {
            float lf = (maxVal + minVal) * 0.5f;
            float sf = 0.0f, hf = 0.0f;
            if (delta > 1e-5f) {
                sf = lf > 0.5f ? delta / (2.0f - maxVal - minVal) : delta / (maxVal + minVal);
                if (maxVal == rf) hf = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
                else if (maxVal == gf) hf = (bf - rf) / delta + 2.0f;
                else hf = (rf - gf) / delta + 4.0f;
                hf /= 6.0f;
            }
            int h = static_cast<int>(std::round(hf * 360.0f)) % 360;
            int s = static_cast<int>(std::round(sf * 100.0f));
            int l = static_cast<int>(std::round(lf * 100.0f));
            return std::format("hsl({}, {}%, {}%)", h, s, l);
        }
        case ColorFormatType::HSV: {
            float vf = maxVal;
            float sf = maxVal > 1e-5f ? delta / maxVal : 0.0f;
            float hf = 0.0f;
            if (delta > 1e-5f) {
                if (maxVal == rf) hf = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
                else if (maxVal == gf) hf = (bf - rf) / delta + 2.0f;
                else hf = (rf - gf) / delta + 4.0f;
                hf /= 6.0f;
            }
            int h = static_cast<int>(std::round(hf * 360.0f)) % 360;
            int s = static_cast<int>(std::round(sf * 100.0f));
            int v = static_cast<int>(std::round(vf * 100.0f));
            return std::format("hsv({}, {}%, {}%)", h, s, v);
        }
        case ColorFormatType::CMYK: {
            float kf = 1.0f - std::max({rf, gf, bf});
            int c = 0, m = 0, y = 0, k = static_cast<int>(std::round(kf * 100.0f));
            if (kf < 1.0f - 1e-5f) {
                c = static_cast<int>(std::round((1.0f - rf - kf) / (1.0f - kf) * 100.0f));
                m = static_cast<int>(std::round((1.0f - gf - kf) / (1.0f - kf) * 100.0f));
                y = static_cast<int>(std::round((1.0f - bf - kf) / (1.0f - kf) * 100.0f));
            }
            return std::format("cmyk({}%, {}%, {}%, {}%)", c, m, y, k);
        }
        case ColorFormatType::DEC: {
            uint32_t decVal = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            return std::format("{}", decVal);
        }
        default:
            return std::format("#{:02X}{:02X}{:02X}", r, g, b);
    }
}

} // namespace

static HCURSOR getBeautifulCrosshairCursor() {
    static HCURSOR s_cursor = nullptr;
    if (s_cursor) return s_cursor;

    const int sz = 64;
    const int mid = 31;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sz;
    bmi.bmiHeader.biHeight = -sz; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    uint32_t* pixels = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbmColor = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
    ReleaseDC(nullptr, hdc);

    if (!hbmColor || !pixels) {
        s_cursor = LoadCursor(nullptr, IDC_CROSS);
        return s_cursor;
    }

    // 初始化为完全透明 (0x00000000)
    std::fill_n(pixels, sz * sz, 0x00000000);

    auto setPixelARGB = [&](int x, int y, uint32_t argb) {
        if (x >= 0 && x < sz && y >= 0 && y < sz) {
            pixels[y * sz + x] = argb;
        }
    };

    const uint32_t COLOR_WHITE = 0xFFFFFFFF; // 100% 不透明纯白
    const uint32_t COLOR_BLACK = 0xFF000000; // 100% 不透明纯黑

    // 精致修长准星 (纤细美学)：
    // 臂长 16px (从 3px 到 18px 跨越)，中心留空 2px
    // 白心 1px 细线 (0 偏移)，两侧各 1px 细黑边 (-1 与 +1 偏移) -> 总宽 3px，极致纤细精致！
    const int startDist = 3;
    const int endDist = 18;

    // 水平臂 (左、右)
    for (int dist = startDist; dist <= endDist; ++dist) {
        int xs[2] = { mid - dist, mid + dist };
        for (int x : xs) {
            // 1px 纯白内芯
            setPixelARGB(x, mid, COLOR_WHITE);
            // 上下 1px 细黑描边
            setPixelARGB(x, mid - 1, COLOR_BLACK);
            setPixelARGB(x, mid + 1, COLOR_BLACK);
        }
    }

    // 垂直臂 (上、下)
    for (int dist = startDist; dist <= endDist; ++dist) {
        int ys[2] = { mid - dist, mid + dist };
        for (int y : ys) {
            // 1px 纯白内芯
            setPixelARGB(mid, y, COLOR_WHITE);
            // 左右 1px 细黑描边
            setPixelARGB(mid - 1, y, COLOR_BLACK);
            setPixelARGB(mid + 1, y, COLOR_BLACK);
        }
    }

    // 四个端点外侧封口黑边
    setPixelARGB(mid - (endDist + 1), mid, COLOR_BLACK);
    setPixelARGB(mid + (endDist + 1), mid, COLOR_BLACK);
    setPixelARGB(mid, mid - (endDist + 1), COLOR_BLACK);
    setPixelARGB(mid, mid + (endDist + 1), COLOR_BLACK);

    // 中心微孔 4 个内端封口黑边
    setPixelARGB(mid - (startDist - 1), mid, COLOR_BLACK);
    setPixelARGB(mid + (startDist - 1), mid, COLOR_BLACK);
    setPixelARGB(mid, mid - (startDist - 1), COLOR_BLACK);
    setPixelARGB(mid, mid + (startDist - 1), COLOR_BLACK);

    // 针对 32-bit ARGB 光标创建 1-bit 单色掩码
    HBITMAP hbmMask = CreateBitmap(sz, sz, 1, 1, nullptr);

    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = mid;
    ii.yHotspot = mid;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    s_cursor = CreateIconIndirect(&ii);

    if (hbmMask) DeleteObject(hbmMask);
    if (hbmColor) DeleteObject(hbmColor);

    if (!s_cursor) s_cursor = LoadCursor(nullptr, IDC_CROSS);
    return s_cursor;
}

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
        case HitArea::CornerRadius:
            return getBeautifulCrosshairCursor();
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
            // 1. 如果当前选中的元素处于激活态，优先显示其手柄/主体光标（移动/缩放）
            if (m_state->activeElement && m_state->activeElement->isActive) {
                HitArea elemHit = m_state->activeElement->hitTestEx(toMarkupPoint(point));
                if (elemHit != HitArea::None) {
                    cur = cursorForArea(elemHit);
                }
            }

            // 2. 选区控制点/边框
            if (!cur) {
                HitArea sel = hitTestSelectionBox(point);
                if (sel != HitArea::None) {
                    cur = cursorForArea(sel);
                }
            }

            // 3. 画布上其他未激活元素的悬停命中
            if (!cur) {
                HitResult hit = m_state->markup.getElementAtEx(toMarkupPoint(point));
                if (hit.element) {
                    cur = cursorForArea(hit.area);
                }
            }

            // 4. 处于选区内部进行标注时，按当前工具分发光标 (仅 Text 标注时为 IBEAM)
            if (!cur && isPointInSelection(point)) {
                if (m_state->currentTool == MarkupTool::Text) {
                    cur = LoadCursor(nullptr, IDC_IBEAM);
                }
            }
        }
    }

    // 默认光标：永远使用高精度自然美感十字准星
    if (!cur) cur = getBeautifulCrosshairCursor();
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

    // 1. 优先检测圆角手柄（Figma 风格 4 个角内侧控制点）
    float selW = r.right - r.left;
    float selH = r.bottom - r.top;
    if (selW >= 36.0f * dpiScale && selH >= 36.0f * dpiScale) {
        float offset = std::clamp(std::max(12.0f, m_state->cornerRadius + 6.0f), 10.0f, std::min(selW, selH) * 0.45f) * dpiScale;
        const H cornerHandles[4] = {
            {r.left + offset,  r.top + offset,    HitArea::CornerRadius},
            {r.right - offset, r.top + offset,    HitArea::CornerRadius},
            {r.right - offset, r.bottom - offset, HitArea::CornerRadius},
            {r.left + offset,  r.bottom - offset, HitArea::CornerRadius}
        };
        for (const auto& h : cornerHandles) {
            if (std::abs(px - h.x) <= hw && std::abs(py - h.y) <= hw) return h.area;
        }
    }

    // 2. 检测 8 个外框拉伸调整控制点
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

bool CaptureInput::invokeToolbarButton(std::size_t index) {
    if (!m_state || !m_renderer ||
        (m_state->state != OverlayState::Selected && m_state->state != OverlayState::Marking)) {
        return false;
    }
    rebuildToolbarButtons(currentSelectionRect());
    if (index >= m_state->toolbarButtons.size()) return false;

    // Copy before execution: confirm/cancel callbacks may synchronously change
    // the overlay state and invalidate the backing vector.
    const ToolbarButton button = m_state->toolbarButtons[index];
    executeToolbarCommand(button);
    if (m_renderer) m_renderer->invalidate();
    return true;
}

void CaptureInput::executeToolbarCommand(const ToolbarButton& button) {
    // CaptureInput only receives window messages on the overlay UI thread. The
    // announcement therefore never calls UIA/Win32 from a hook or worker
    // thread, and remains invisible to sighted users.
    easy::core::accessibility::announceOverlay(
        m_hwnd, toolbarButtonAccessibleName(button));

    // 仅在切换其他工具或执行重置/确认时退出选中态；改颜色时保留当前激活元素并实时更新颜色
    if (button.command != ToolbarCommand::SelectColor) {
        if (m_state->activeElement) {
            m_state->activeElement->isActive = false;
            m_state->activeElement->isEditing = false;
            if (m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->text.empty()) {
                m_state->markup.removeElement(m_state->activeElement->id);
            }
            m_state->activeElement = nullptr;
        }
    }

    switch (button.command) {
        case ToolbarCommand::SelectTool:
            m_state->currentTool = button.tool;
            m_state->state = OverlayState::Selected;
            m_state->isMarking = false;
            break;

        case ToolbarCommand::SelectColor:
            m_state->currentColor = button.color;
            if (m_state->activeElement) {
                m_state->activeElement->color = button.color;
                m_renderer->markMarkupDirty();
                m_renderer->invalidate();
            }
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

        case ToolbarCommand::ToggleCornerRadius: {
            static const std::array<float, 5> radiuses = {0.0f, 8.0f, 12.0f, 16.0f, 24.0f};
            auto it = std::find_if(radiuses.begin(), radiuses.end(), [&](float r) {
                return std::abs(r - m_state->cornerRadius) < 1.0f;
            });
            if (it == radiuses.end()) {
                m_state->cornerRadius = 8.0f;
            } else {
                size_t nextIdx = (std::distance(radiuses.begin(), it) + 1) % radiuses.size();
                m_state->cornerRadius = radiuses[nextIdx];
            }
            prepareMarkupBase();
            m_renderer->invalidate();
            break;
        }

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
    const float currentDpiScale = m_state->dpiScale > 0.0f ? m_state->dpiScale : 1.0f;
    if (m_state->currentTool == MarkupTool::Number) {
        m_state->markup.addNumberMark(local, m_state->currentColor, currentDpiScale);
        return;
    }
    if (m_state->currentTool == MarkupTool::Magnifier) {
        m_state->markup.addMagnifier(local, 2.0f, static_cast<int>(std::round(60.0f * currentDpiScale)));
        return;
    }
    if (m_state->currentTool == MarkupTool::Text) {
        if (m_state->activeElement) {
            m_state->activeElement->isActive = false;
            m_state->activeElement->isEditing = false;
        }
        auto* elem = m_state->markup.addText(local, "", m_state->currentColor, 18.0f * currentDpiScale);
        m_state->activeElement = elem;
        if (m_state->activeElement) {
            m_state->activeElement->isActive = true;
            m_state->activeElement->isEditing = true;
        }
        m_state->state = OverlayState::Selected;
        m_renderer->markMarkupDirty();
        m_renderer->invalidate();
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

std::vector<RECT> CaptureInput::detectWindowHierarchy(POINT cursorPos) {
    std::vector<RECT> hierarchy;
    int offsetX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int offsetY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    // 1. 穿透覆盖层查找真实窗口
    HWND hTop = nullptr;
    struct SearchParam {
        POINT pt;
        HWND exclude;
        HWND found;
    } param{cursorPos, m_hwnd, nullptr};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* p = reinterpret_cast<SearchParam*>(lParam);
        if (hwnd == p->exclude || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
        RECT r;
        GetWindowRect(hwnd, &r);
        if (PtInRect(&r, p->pt)) {
            p->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&param));
    hTop = param.found;

    if (hTop && hTop != m_hwnd) {
        // 2. 深入获取真实子控件
        POINT clientPt = cursorPos;
        ScreenToClient(hTop, &clientPt);
        HWND hChild = RealChildWindowFromPoint(hTop, clientPt);
        if (!hChild) hChild = hTop;

        // 3. 从内到外依次收集子控件与父窗口
        HWND curr = hChild;
        while (curr && curr != GetDesktopWindow()) {
            RECT rc{};
            if (GetWindowRect(curr, &rc) && (rc.right - rc.left > 8) && (rc.bottom - rc.top > 8)) {
                rc.left -= offsetX;
                rc.top -= offsetY;
                rc.right -= offsetX;
                rc.bottom -= offsetY;

                bool exists = false;
                for (const auto& existing : hierarchy) {
                    if (std::abs(existing.left - rc.left) <= 2 &&
                        std::abs(existing.top - rc.top) <= 2 &&
                        std::abs(existing.right - rc.right) <= 2 &&
                        std::abs(existing.bottom - rc.bottom) <= 2) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    hierarchy.push_back(rc);
                }
            }
            curr = GetParent(curr);
        }
    }

    // 4. 追加当前光标所在的单屏全屏区域
    HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(hMon, &mi)) {
            RECT monRc = mi.rcMonitor;
            monRc.left -= offsetX;
            monRc.top -= offsetY;
            monRc.right -= offsetX;
            monRc.bottom -= offsetY;

            bool exists = false;
            for (const auto& existing : hierarchy) {
                if (existing.left == monRc.left && existing.top == monRc.top &&
                    existing.right == monRc.right && existing.bottom == monRc.bottom) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                hierarchy.push_back(monRc);
            }
        }
    }

    // 5. 兜底全屏
    if (hierarchy.empty()) {
        RECT fullRc{0, 0, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        hierarchy.push_back(fullRc);
    }

    return hierarchy;
}

RECT CaptureInput::detectWindowUnderCursor(POINT cursorPos) {
    auto list = detectWindowHierarchy(cursorPos);
    if (!list.empty()) return list.front();
    return {};
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
        case WM_SETCURSOR: {
            if (!this || !m_state) break;
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                updateHoverCursor(pt);
                return TRUE;
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!this) break;
            m_renderer->markMarkupDirty();
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            if ((int)m_state->state.load() == (int)OverlayState::Selected || (int)m_state->state.load() == (int)OverlayState::Marking) {
                HitArea selHit = HitArea::None;
                if (auto* button = hitTestToolbar(point)) {
                    // 如果正在编辑文字且点击的不是选颜色按钮，退出编辑
                    if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                        if (button->command != ToolbarCommand::SelectColor) {
                            m_state->activeElement->isEditing = false;
                            if (m_state->activeElement->text.empty()) {
                                m_state->markup.removeElement(m_state->activeElement->id);
                                m_state->activeElement = nullptr;
                            }
                        }
                    }
                    executeToolbarCommand(*button);
                    return 0;
                }
                
                // 选区控制点/边框/圆角手柄调整
                if ((selHit = hitTestSelectionBox(point)) != HitArea::None) {
                    if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                        m_state->activeElement->isEditing = false;
                        if (m_state->activeElement->text.empty()) {
                            m_state->markup.removeElement(m_state->activeElement->id);
                            m_state->activeElement = nullptr;
                        }
                    }
                    if (selHit == HitArea::CornerRadius) {
                        m_state->isAdjustingCornerRadius = true;
                        m_state->cornerDragStartRadius = m_state->cornerRadius;
                        m_state->cornerDragStartPos = point;
                        return 0;
                    }
                    m_state->isAdjustingSelection = true;
                    m_state->selAdjustHandle = selHit;
                    m_state->selAdjustLast = point;
                    return 0;
                }
                
                if (isPointInSelection(point)) {
                    cv::Point local = toMarkupPoint(point);
                    
                    // 1. 如果当前有处于激活态的元素，优先测试该元素的把手或主体
                    HitArea activeElemHit = HitArea::None;
                    if (m_state->activeElement && m_state->activeElement->isActive) {
                        activeElemHit = m_state->activeElement->hitTestEx(local);
                    }
                    
                    if (activeElemHit != HitArea::None) {
                        // 点击在当前已选中的元素上（把手或主体）
                        if (m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                            // 正在编辑时点击主体/把手，提交编辑态转为移动/缩放态
                            m_state->activeElement->isEditing = false;
                            if (m_state->activeElement->text.empty()) {
                                m_state->markup.removeElement(m_state->activeElement->id);
                                m_state->activeElement = nullptr;
                                m_renderer->invalidate();
                                return 0;
                            }
                        }
                        
                        m_state->dragHandle = (activeElemHit == HitArea::Body) ? HitArea::None : activeElemHit;
                        m_state->isManipulating = true;
                        m_state->lastMousePos = point;
                        m_renderer->invalidate();
                        return 0;
                    }
                    
                    // 2. 测试是否命中了画布上的其他标注元素
                    HitResult hit = m_state->markup.getElementAtEx(local);
                    if (hit.element) {
                        // 提交之前编辑的文字
                        if (m_state->activeElement && m_state->activeElement != hit.element) {
                            m_state->activeElement->isActive = false;
                            m_state->activeElement->isEditing = false;
                            if (m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->text.empty()) {
                                m_state->markup.removeElement(m_state->activeElement->id);
                            }
                        }
                        
                        m_state->activeElement = hit.element;
                        m_state->activeElement->isActive = true;
                        m_state->activeElement->isEditing = false;
                        
                        m_state->dragHandle = (hit.area == HitArea::Body) ? HitArea::None : hit.area;
                        m_state->isManipulating = true;
                        m_state->lastMousePos = point;
                        m_renderer->invalidate();
                        return 0;
                    }
                    
                    // 3. 点击在空白区域
                    if (m_state->activeElement) {
                        m_state->activeElement->isActive = false;
                        m_state->activeElement->isEditing = false;
                        if (m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->text.empty()) {
                            m_state->markup.removeElement(m_state->activeElement->id);
                        }
                        m_state->activeElement = nullptr;
                    }
                    
                    // 在空白区域开始新的标注操作
                    beginMarkup(point);
                    return 0;
                }
                return 0;
            }

            m_state->dragStart = point;
            m_state->dragEnd = m_state->dragStart;
            m_state->lastMousePos = point;
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

            if (m_state->isAdjustingCornerRadius) {
                // 以选区中心为基准：往内拉增加圆角，往外推减小圆角
                auto r = currentSelectionRect();
                float cx = (r.left + r.right) * 0.5f;
                float cy = (r.top + r.bottom) * 0.5f;
                float selW = r.right - r.left;
                float selH = r.bottom - r.top;
                float maxRadius = std::min(selW, selH) * 0.5f;

                float signX = (m_state->cornerDragStartPos.x < cx) ? 1.0f : -1.0f;
                float signY = (m_state->cornerDragStartPos.y < cy) ? 1.0f : -1.0f;
                float dx = (m_state->currentCursor.x - m_state->cornerDragStartPos.x) * signX;
                float dy = (m_state->currentCursor.y - m_state->cornerDragStartPos.y) * signY;
                float delta = (dx + dy) * 0.5f;

                float newR = std::clamp(m_state->cornerDragStartRadius + delta, 0.0f, maxRadius);
                m_state->cornerRadius = std::round(newR);
                m_renderer->invalidate();
            } else if (m_state->isAdjustingSelection) {
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
                m_renderer->invalidate();        // 立即触发 Direct2D 实时重绘
            } else if (m_state->isMarking) {
                updateMarkup(m_state->currentCursor);
                m_renderer->invalidate();        // 拖拽预览走 D2D，合成图不变
            } else if (m_state->dragging) {
                const bool spaceDown = (GetKeyState(VK_SPACE) & 0x8000) != 0;
                if (spaceDown) {
                    // Space 键按住时：平移当前选区（Snipaste 核心交互机制）
                    int dx = m_state->currentCursor.x - m_state->lastMousePos.x;
                    int dy = m_state->currentCursor.y - m_state->lastMousePos.y;
                    m_state->dragStart.x += dx;
                    m_state->dragStart.y += dy;
                    m_state->dragEnd.x += dx;
                    m_state->dragEnd.y += dy;
                } else {
                    m_state->dragEnd = m_state->currentCursor;
                }
                m_state->lastMousePos = m_state->currentCursor;
                m_renderer->invalidate();
            } else {
                if (((int)m_state->state.load() == (int)OverlayState::Idle ||
                     (int)m_state->state.load() == (int)OverlayState::Selecting) && !m_state->dragging) {
                    if (m_state->options.autoDetectWindow) {
                        POINT screenPt = m_state->currentCursor;
                        int offX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                        int offY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                        screenPt.x += offX;
                        screenPt.y += offY;
                        m_state->detectedWindowHierarchy = detectWindowHierarchy(screenPt);
                        m_state->detectedWindowHierarchyIndex = 0;
                        if (!m_state->detectedWindowHierarchy.empty()) {
                            m_state->detectedWindow = m_state->detectedWindowHierarchy[0];
                        } else {
                            m_state->detectedWindow = {};
                        }
                    } else {
                        m_state->detectedWindow = {};
                        m_state->detectedWindowHierarchy.clear();
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

            if (m_state->isAdjustingCornerRadius) {
                m_state->isAdjustingCornerRadius = false;
                prepareMarkupBase();
                m_renderer->invalidate();
                return 0;
            }

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
                    m_state->cornerRadius = m_state->detectedWindowCornerRadius; // 自动继承现代 Win11 窗口圆角
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

        case WM_RBUTTONUP: {
            if (!this) break;
            
            // 1. 正在拖拽选区中：右键取消本次拖拽
            if (m_state->dragging) {
                m_state->dragging = false;
                m_state->state = OverlayState::Selecting;
                m_renderer->invalidate();
                return 0;
            }

            // 2. 正在编辑文字：右键退出文字编辑
            if (m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                m_state->activeElement->isEditing = false;
                if (m_state->activeElement->text.empty()) {
                    m_state->markup.removeElement(m_state->activeElement->id);
                    m_state->activeElement = nullptr;
                }
                m_renderer->invalidate();
                return 0;
            }

            // 3. 有激活选中的标注元素：取消选中
            if (m_state->activeElement) {
                m_state->activeElement->isActive = false;
                m_state->activeElement = nullptr;
                m_renderer->invalidate();
                return 0;
            }

            // 4. 处于选区态（Selected/Marking）：右键重置选区回到初始未选区态
            if ((int)m_state->state.load() == (int)OverlayState::Selected || (int)m_state->state.load() == (int)OverlayState::Marking) {
                m_state->state = OverlayState::Selecting;
                m_state->dragStart = {0, 0};
                m_state->dragEnd = {0, 0};
                m_state->markup.clearAll();
                m_renderer->invalidate();
                return 0;
            }

            // 5. 无选区状态下：鼠标右键直接退出截图
            if (m_cancelCb) {
                m_cancelCb();
            }
            return 0;
        }

        case WM_IME_STARTCOMPOSITION: {
            if (this && m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                HIMC hImc = ImmGetContext(hwnd);
                if (hImc) {
                    COMPOSITIONFORM cf{};
                    cf.dwStyle = CFS_POINT;
                    auto selRect = currentSelectionRect();
                    cf.ptCurrentPos.x = static_cast<LONG>(selRect.left + m_state->activeElement->startPt.x);
                    cf.ptCurrentPos.y = static_cast<LONG>(selRect.top + m_state->activeElement->startPt.y + m_state->activeElement->fontSize + 6);
                    ImmSetCompositionWindow(hImc, &cf);
                    ImmReleaseContext(hwnd, hImc);
                }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_IME_COMPOSITION: {
            if (this && m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                if (lParam & GCS_RESULTSTR) {
                    HIMC hImc = ImmGetContext(hwnd);
                    if (hImc) {
                        LONG bytes = ImmGetCompositionStringW(hImc, GCS_RESULTSTR, nullptr, 0);
                        if (bytes > 0) {
                            std::wstring resultStr(bytes / sizeof(wchar_t), L'\0');
                            ImmGetCompositionStringW(hImc, GCS_RESULTSTR, &resultStr[0], bytes);

                            std::wstring wstr = easy::core::WinUtils::utf8ToWstring(m_state->activeElement->text);
                            wstr.append(resultStr);
                            m_state->activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                            m_state->activeElement->textRenderSize = cv::Size(0, 0);
                            m_renderer->markMarkupDirty();
                            m_renderer->invalidate();
                        }
                        ImmReleaseContext(hwnd, hImc);
                        return 0; // 消费输入法上屏文字，防止 DefWindowProcW 再次派发导致重复
                    }
                }
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_IME_CHAR: {
            return 0;
        }

        case WM_CHAR: {
            if (this && m_state->activeElement && m_state->activeElement->tool == MarkupTool::Text && m_state->activeElement->isEditing) {
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch == 0x08) { // Backspace
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(m_state->activeElement->text);
                    if (!wstr.empty()) {
                        wstr.pop_back();
                        m_state->activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                        m_state->activeElement->textRenderSize = cv::Size(0, 0);
                        m_renderer->markMarkupDirty();
                        m_renderer->invalidate();
                    }
                } else if (ch == 0x0D || ch == 0x0A) { // Enter 提交
                    m_state->activeElement->isEditing = false;
                    m_renderer->markMarkupDirty();
                    m_renderer->invalidate();
                } else if (ch >= 0x20 || ch == 0x09) {
                    std::wstring wstr = easy::core::WinUtils::utf8ToWstring(m_state->activeElement->text);
                    wstr.push_back(ch);
                    m_state->activeElement->text = easy::core::WinUtils::wstringToUtf8(wstr);
                    m_state->activeElement->textRenderSize = cv::Size(0, 0);
                    m_renderer->markMarkupDirty();
                    m_renderer->invalidate();
                }
                return 0;
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (!this) break;
            m_renderer->markMarkupDirty();
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);

            // 1. 选区前/未拖拽状态：滚轮切换探测窗口层级 (子控件 <-> 父容器 <-> 顶级窗口 <-> 全屏)
            if (((int)m_state->state.load() == (int)OverlayState::Idle ||
                 (int)m_state->state.load() == (int)OverlayState::Selecting) && !m_state->dragging) {
                if (!m_state->detectedWindowHierarchy.empty()) {
                    int count = static_cast<int>(m_state->detectedWindowHierarchy.size());
                    if (zDelta > 0) {
                        m_state->detectedWindowHierarchyIndex = std::min(m_state->detectedWindowHierarchyIndex + 1, count - 1);
                    } else if (zDelta < 0) {
                        m_state->detectedWindowHierarchyIndex = std::max(m_state->detectedWindowHierarchyIndex - 1, 0);
                    }
                    m_state->detectedWindow = m_state->detectedWindowHierarchy[m_state->detectedWindowHierarchyIndex];
                    m_renderer->invalidate();
                    return 0;
                }
            }

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

            // 方向键 / WASD 像素级微调与光标移动
            if (!editingText) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool isDirectionKey = (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT ||
                                       (!ctrl && (wParam == 'W' || wParam == 'S' || wParam == 'A' || wParam == 'D')));
                
                // 存在选区时：方向键 / WASD 移动选区，Shift+方向键 / Shift+WASD 调整选区尺寸
                if (hasSelection && isDirectionKey) {
                    int step = ctrl ? 10 : 1;
                    int dx = (wParam == VK_LEFT || wParam == 'A' ? -step : (wParam == VK_RIGHT || wParam == 'D' ? step : 0));
                    int dy = (wParam == VK_UP   || wParam == 'W' ? -step : (wParam == VK_DOWN  || wParam == 'S' ? step : 0));
                    
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

            // Shift: 选区前/拖拽中依次循环切换 8 种常用颜色格式
            if (wParam == VK_SHIFT && !ctrl &&
                ((int)m_state->state.load() == (int)OverlayState::Idle ||
                 (int)m_state->state.load() == (int)OverlayState::Selecting)) {
                int nextIdx = (static_cast<int>(m_state->colorFormat) + 1) % static_cast<int>(ColorFormatType::COUNT);
                m_state->colorFormat = static_cast<ColorFormatType>(nextIdx);
                m_state->colorFormatHex = (m_state->colorFormat == ColorFormatType::HEX);
                m_renderer->invalidate();
                return 0;
            }

            // 取色：选区前/拖拽中按 C 复制当前格式到剪贴板
            if (wParam == 'C' && !ctrl &&
                ((int)m_state->state.load() == (int)OverlayState::Idle ||
                 (int)m_state->state.load() == (int)OverlayState::Selecting)) {
                int cr = 0, cg = 0, cb = 0;
                if (m_renderer->sampleScreenColor(m_state->currentCursor.x, m_state->currentCursor.y, cr, cg, cb, *m_state)) {
                    std::string colorText = formatColorText(m_state->colorFormat, cr, cg, cb);
                    easy::core::WinUtils::copyToClipboard(colorText);
                    m_state->loupeToastUntil = GetTickCount() + 1200;
                    m_renderer->invalidate();
                }
                return 0;
            }

            // ESC 分级退出：编辑文字→退出编辑；选中元素→取消选中；有选区→取消选区；否则→关闭截图
            if (wParam == VK_ESCAPE) {
                if (editingText) {
                    m_state->activeElement->isEditing = false;
                    if (m_state->activeElement->text.empty()) {
                        m_state->markup.removeElement(m_state->activeElement->id);
                        m_state->activeElement = nullptr;
                    }
                    m_renderer->invalidate();
                } else if (m_state->activeElement) {
                    m_state->activeElement->isActive = false;
                    m_state->activeElement = nullptr;
                    m_renderer->invalidate();
                } else if ((int)m_state->state.load() == (int)OverlayState::Selected || (int)m_state->state.load() == (int)OverlayState::Marking) {
                    m_state->state = OverlayState::Selecting;
                    m_state->dragStart = {0, 0};
                    m_state->dragEnd = {0, 0};
                    m_state->markup.clearAll();
                    m_renderer->invalidate();
                } else {
                    if (m_cancelCb) m_cancelCb();
                }
                return 0;
            }

            // Delete / Backspace: 在非文本编辑状态下删除当前选中的标注元素
            if ((wParam == VK_DELETE || wParam == VK_BACK) && !editingText && m_state->activeElement) {
                m_state->markup.removeElement(m_state->activeElement->id);
                m_state->activeElement = nullptr;
                m_renderer->markMarkupDirty();
                m_renderer->invalidate();
                return 0;
            }

            // 正在输入文字：其余按键交给 WM_CHAR
            if (editingText) return 0;

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
                case VK_OEM_4: // '[' 键：减小选区圆角
                    if (hasSelection) {
                        static const std::array<float, 5> radiuses = {0.0f, 8.0f, 12.0f, 16.0f, 24.0f};
                        for (int i = (int)radiuses.size() - 1; i >= 0; --i) {
                            if (radiuses[i] < m_state->cornerRadius - 0.5f) {
                                m_state->cornerRadius = radiuses[i];
                                break;
                            }
                            if (i == 0) m_state->cornerRadius = 0.0f;
                        }
                        prepareMarkupBase();
                        m_renderer->invalidate();
                        return 0;
                    }
                    break;
                case VK_OEM_6: // ']' 键：增加选区圆角
                    if (hasSelection) {
                        static const std::array<float, 5> radiuses = {0.0f, 8.0f, 12.0f, 16.0f, 24.0f};
                        for (size_t i = 0; i < radiuses.size(); ++i) {
                            if (radiuses[i] > m_state->cornerRadius + 0.5f) {
                                m_state->cornerRadius = radiuses[i];
                                break;
                            }
                            if (i == radiuses.size() - 1) m_state->cornerRadius = 24.0f;
                        }
                        prepareMarkupBase();
                        m_renderer->invalidate();
                        return 0;
                    }
                    break;
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
                case VK_UP:
                case VK_DOWN:
                case VK_LEFT:
                case VK_RIGHT: {
                    const bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const int step = isShift ? 10 : 1;
                    int dx = 0, dy = 0;
                    if (wParam == VK_LEFT)  dx = -step;
                    if (wParam == VK_RIGHT) dx = step;
                    if (wParam == VK_UP)    dy = -step;
                    if (wParam == VK_DOWN)  dy = step;

                    POINT pt;
                    GetCursorPos(&pt);
                    pt.x += dx;
                    pt.y += dy;
                    SetCursorPos(pt.x, pt.y);

                    m_state->currentCursor = {pt.x, pt.y};
                    if (m_state->dragging) {
                        m_state->dragEnd = m_state->currentCursor;
                    }
                    m_renderer->invalidate();
                    return 0;
                }
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
