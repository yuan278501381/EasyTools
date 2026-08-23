#include "capture/CaptureRenderer.h"
#include "core/logger/Logger.h"
#include "core/utils/WinUtils.h"
#include "core/config/ConfigManager.h"
#include "core/utils/ThemeUtils.h"
#include "capture/CaptureHistory.h"
#include "capture/CaptureToolbarLayout.h"
#include <format>
#include <algorithm>
#include <cmath>
#include <utility>
#include <opencv2/imgproc.hpp>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace Microsoft::WRL;

namespace easy::capture {

static std::wstring tooltipForButton(ToolbarCommand cmd, bool chinese) {
    if (!chinese) {
        switch (cmd) {
            case ToolbarCommand::SelectTool: return L"Annotation tool";
            case ToolbarCommand::SelectColor: return L"Change color";
            case ToolbarCommand::Undo: return L"Undo (Ctrl+Z)";
            case ToolbarCommand::Redo: return L"Redo (Ctrl+Y)";
            case ToolbarCommand::Clear: return L"Clear annotations";
            case ToolbarCommand::ExtractText: return L"Extract text (OCR)";
            case ToolbarCommand::PinWindow: return L"Pin to screen";
            case ToolbarCommand::ScrollCapture: return L"Scrolling capture";
            case ToolbarCommand::Confirm: return L"Finish (Enter)";
            case ToolbarCommand::Cancel: return L"Cancel (Esc)";
            default: return L"";
        }
    }
    switch (cmd) {
        case ToolbarCommand::SelectTool: return L"选择标注";
        case ToolbarCommand::SelectColor: return L"更改颜色";
        case ToolbarCommand::Undo: return L"撤销 (Ctrl+Z)";
        case ToolbarCommand::Redo: return L"重做 (Ctrl+Y)";
        case ToolbarCommand::Clear: return L"清空标注";
        case ToolbarCommand::ExtractText: return L"提取文字 (OCR)";
        case ToolbarCommand::PinWindow: return L"贴图到屏幕";
        case ToolbarCommand::ScrollCapture: return L"长截图";
        case ToolbarCommand::Confirm: return L"确认 (Enter)";
        case ToolbarCommand::Cancel: return L"取消 (ESC)";
        default: return L"";
    }
}

bool CaptureRenderer::initialize(HWND hwnd, CaptureState& state) {
    releaseWindowResources();
    m_hwnd = hwnd;
    if (createRenderResources(state)) return true;
    releaseWindowResources();
    return false;
}

void CaptureRenderer::shutdown() {
    releaseWindowResources();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void CaptureRenderer::applyThemeColors() {
    if (!m_renderTarget) return;

    auto& cfg = easy::core::ConfigManager::instance();
    const std::string accent = cfg.get<std::string>("/general/accentColor", "blue");
    const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);

    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 1.0f),
        m_borderBrush.ReleaseAndGetAddressOf()
    );
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.6f),
        m_crosshairBrush.ReleaseAndGetAddressOf()
    );
    m_renderTarget->CreateSolidColorBrush(
        D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.3f),
        m_windowHighlightBrush.ReleaseAndGetAddressOf()
    );
}

bool CaptureRenderer::createRenderResources(CaptureState& state) {
    HRESULT hr;

    if (!m_d2dFactory) {
        hr = D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
        if (FAILED(hr)) return false;
    }

    if (!m_dwriteFactory) {
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
        if (FAILED(hr)) return false;
    }

    if (!updateDpiScale(state.dpiScale)) return false;

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    auto rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    auto hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top),
        D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(rtProps, hwndProps, m_renderTarget.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // 禁用 D2D 的自动 DPI 缩放
    m_renderTarget->SetDpi(96.0f, 96.0f);

    // 画笔
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.5f), m_dimBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.8f), m_infoBgBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), m_infoTextBrush.GetAddressOf());
    applyThemeColors();

    return true;
}

void CaptureRenderer::releaseWindowResources() {
    m_windowHighlightBrush.Reset();
    m_crosshairBrush.Reset();
    m_infoTextBrush.Reset();
    m_infoBgBrush.Reset();
    m_borderBrush.Reset();
    m_dimBrush.Reset();
    m_screenBitmap.Reset();
    m_markupCacheBitmap.Reset();
    m_historyBitmap.Reset();
    m_textInputFormat.Reset();
    m_infoTextFormat.Reset();
    m_textScale = 0.0f;
    m_renderTarget.Reset();
    m_hwnd = nullptr;
}

bool CaptureRenderer::updateDpiScale(float scale) {
    scale = std::clamp(scale > 0.0f ? scale : 1.0f, 1.0f, 5.0f);
    if (m_infoTextFormat && m_textInputFormat &&
        std::abs(scale - m_textScale) < 0.01f) {
        return true;
    }
    if (!m_dwriteFactory) return false;

    ComPtr<IDWriteTextFormat> infoFormat;
    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f * scale, L"zh-CN",
        infoFormat.GetAddressOf());
    if (FAILED(hr) || !infoFormat) return false;
    infoFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    infoFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    ComPtr<IDWriteTextFormat> textInputFormat;
    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 18.0f * scale, L"zh-CN",
        textInputFormat.GetAddressOf());
    if (FAILED(hr) || !textInputFormat) return false;
    textInputFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textInputFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    m_infoTextFormat = std::move(infoFormat);
    m_textInputFormat = std::move(textInputFormat);
    m_textScale = scale;
    return true;
}

bool CaptureRenderer::updateScreenBitmap(const cv::Mat& image) {
    m_screenBitmap.Reset();
    if (!m_renderTarget || image.empty()) {
        LOG_ERROR("截图底图上传失败: 渲染目标或图像为空");
        return false;
    }

    cv::Mat bgra;
    D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (image.channels() == 4) {
        bgra = image;
        // A 32-bit BI_RGB screen DIB does not define/populate its alpha byte
        // (it is commonly zero). Treat it as opaque without a full-screen pass
        // that writes 255 into every fourth byte.
        alphaMode = D2D1_ALPHA_MODE_IGNORE;
    } else if (image.channels() == 3) {
        cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
    } else if (image.channels() == 1) {
        cv::cvtColor(image, bgra, cv::COLOR_GRAY2BGRA);
    } else {
        LOG_ERROR("截图底图上传失败: 不支持的通道数={}", image.channels());
        return false;
    }

    if (!bgra.isContinuous()) bgra = bgra.clone();
    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, alphaMode));
    const HRESULT hr = m_renderTarget->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(bgra.cols), static_cast<UINT32>(bgra.rows)),
        bgra.data, static_cast<UINT32>(bgra.step[0]), props, m_screenBitmap.GetAddressOf());
    if (FAILED(hr) || !m_screenBitmap) {
        LOG_ERROR("截图底图上传 Direct2D 失败, hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    invalidate();
    return true;
}

void CaptureRenderer::drawDimOverlay(const D2D1_RECT_F& selRect, CaptureState&) {
    auto size = m_renderTarget->GetSize();

    // 上
    m_renderTarget->FillRectangle(D2D1::RectF(0, 0, size.width, selRect.top), m_dimBrush.Get());
    // 下
    m_renderTarget->FillRectangle(D2D1::RectF(0, selRect.bottom, size.width, size.height), m_dimBrush.Get());
    // 左
    m_renderTarget->FillRectangle(D2D1::RectF(0, selRect.top, selRect.left, selRect.bottom), m_dimBrush.Get());
    // 右
    m_renderTarget->FillRectangle(D2D1::RectF(selRect.right, selRect.top, size.width, selRect.bottom), m_dimBrush.Get());
}

void CaptureRenderer::drawSelection(const D2D1_RECT_F& rect, CaptureState& state) {
    const float scale = std::clamp(
        state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    // 选区边框（紫色）
    m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f * scale);

    // 八个控制点
    const float controlSize = 6.0f * scale;
    float cx = (rect.left + rect.right) / 2;
    float cy = (rect.top + rect.bottom) / 2;

    D2D1_POINT_2F controls[] = {
        {rect.left, rect.top}, {cx, rect.top}, {rect.right, rect.top},
        {rect.left, cy}, {rect.right, cy},
        {rect.left, rect.bottom}, {cx, rect.bottom}, {rect.right, rect.bottom}
    };

    for (auto& pt : controls) {
        m_renderTarget->FillRectangle(
            D2D1::RectF(pt.x - controlSize / 2, pt.y - controlSize / 2,
                        pt.x + controlSize / 2, pt.y + controlSize / 2),
            m_borderBrush.Get()
        );
    }
}

void CaptureRenderer::drawSizeInfo(const D2D1_RECT_F& rect, CaptureState& state) {
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);

    auto info = std::format(L"{}×{}", w, h);

    // 尺寸标签背景
    const float scale = std::clamp(
        state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    float labelW = 100.0f * scale;
    float labelH = 24.0f * scale;
    float labelX = rect.left;
    float labelY = rect.top - labelH - 4.0f * scale;
    if (labelY < 0) labelY = rect.bottom + 4.0f * scale;

    // 统一玻璃风：尺寸标签也用磨砂面板（小标签走廉价无图层模式）
    drawGlassPanel(D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH), 5.0f * scale, false);
    m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                             m_infoTextFormat.Get(),
                             D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH),
                             m_infoTextBrush.Get());
}

void CaptureRenderer::drawToolbar(const D2D1_RECT_F& selectionRect, CaptureState& state) {
    rebuildCaptureToolbar(state, selectionRect, m_renderTarget->GetSize());
    if (state.toolbarButtons.empty()) return;

    D2D1_RECT_F bounds = state.toolbarButtons.front().rect;
    for (const auto& button : state.toolbarButtons) {
        bounds.left = std::min(bounds.left, button.rect.left);
        bounds.top = std::min(bounds.top, button.rect.top);
        bounds.right = std::max(bounds.right, button.rect.right);
        bounds.bottom = std::max(bounds.bottom, button.rect.bottom);
    }
    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    auto bgRect = D2D1::RectF(
        bounds.left - 8.0f * scale, bounds.top - 6.0f * scale,
        bounds.right + 8.0f * scale, bounds.bottom + 6.0f * scale);
    // 磨砂玻璃 HUD 面板（替代原扁平深色条）
    drawGlassPanel(bgRect, 9.0f, false);

    // 分组分隔线（工具 | 颜色 | 命令），提升辨识度
    if (state.mode != OverlayMode::RecordRegion && state.toolbarButtons.size() >= 25) {
        ComPtr<ID2D1SolidColorBrush> sep;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.14f), sep.GetAddressOf());
        float y0 = state.toolbarButtons.front().rect.top + 3.0f;
        float y1 = state.toolbarButtons.front().rect.bottom - 3.0f;
        auto drawSep = [&](size_t l, size_t r) {
            if (sep && r < state.toolbarButtons.size() &&
                std::abs(state.toolbarButtons[l].rect.top -
                         state.toolbarButtons[r].rect.top) < 0.5f) {
                float mx = (state.toolbarButtons[l].rect.right + state.toolbarButtons[r].rect.left) * 0.5f;
                m_renderTarget->DrawLine(D2D1::Point2F(mx, y0), D2D1::Point2F(mx, y1), sep.Get(), 1.0f);
            }
        };
        drawSep(11, 12);  // 工具 | 颜色
        drawSep(16, 17);  // 颜色 | 命令
    }

    auto& cfg = easy::core::ConfigManager::instance();
    const std::string accent = cfg.get<std::string>("/general/accentColor", "blue");
    const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);

    ComPtr<ID2D1SolidColorBrush> buttonBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<ID2D1SolidColorBrush> dangerBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.10f), buttonBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.95f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.25f, 0.25f, 0.85f), dangerBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.20f), hoverBrush.GetAddressOf());

    for (const auto& button : state.toolbarButtons) {
        bool isTool = button.command == ToolbarCommand::SelectTool;
        bool isActiveTool = isTool && button.tool == state.currentTool;
        bool isColor = button.command == ToolbarCommand::SelectColor;
        bool isDanger = button.command == ToolbarCommand::Cancel || button.command == ToolbarCommand::Clear;
        bool isHovered = state.currentCursor.x >= button.rect.left && state.currentCursor.x <= button.rect.right &&
                         state.currentCursor.y >= button.rect.top  && state.currentCursor.y <= button.rect.bottom;

        auto rounded = D2D1::RoundedRect(button.rect, 5.0f * scale, 5.0f * scale);

        if (isColor) {
            // 颜色色板: 用色块填充, 当前色加白色描边, 悬停加淡描边
            ComPtr<ID2D1SolidColorBrush> swatch;
            m_renderTarget->CreateSolidColorBrush(
                D2D1::ColorF(button.color.r / 255.0f, button.color.g / 255.0f,
                             button.color.b / 255.0f, 1.0f),
                swatch.GetAddressOf());
            if (swatch) m_renderTarget->FillRoundedRectangle(rounded, swatch.Get());
            bool isActiveColor = button.color.r == state.currentColor.r &&
                                 button.color.g == state.currentColor.g &&
                                 button.color.b == state.currentColor.b;
            if (isActiveColor) {
                m_renderTarget->DrawRoundedRectangle(rounded, m_infoTextBrush.Get(), 2.0f);
            } else if (isHovered && hoverBrush) {
                m_renderTarget->DrawRoundedRectangle(rounded, hoverBrush.Get(), 1.5f);
            }
            continue;  // 色板不画文字
        }

        auto* fillBrush = isActiveTool ? activeBrush.Get()
                        : isDanger ? dangerBrush.Get()
                        : isHovered ? hoverBrush.Get()
                        : buttonBrush.Get();
        m_renderTarget->FillRoundedRectangle(rounded, fillBrush);

        if (isActiveTool) {
            m_renderTarget->DrawRoundedRectangle(rounded, m_infoTextBrush.Get(), 1.2f);
        }

        m_renderTarget->DrawText(
            button.label.c_str(), static_cast<UINT32>(button.label.size()),
            m_infoTextFormat.Get(),
            button.rect,
            m_infoTextBrush.Get()
        );
    }

    // 悬停浮层提示：把生僻字形翻译成「名称 + 快捷键」
    for (const auto& button : state.toolbarButtons) {
        if (state.currentCursor.x < button.rect.left || state.currentCursor.x > button.rect.right ||
            state.currentCursor.y < button.rect.top  || state.currentCursor.y > button.rect.bottom) {
            continue;
        }
        std::wstring tip = tooltipForButton(button.command, state.toolbarLayoutChinese);
        if (tip.empty()) break;

        float tw = (16.0f + static_cast<float>(tip.size()) * 12.0f) * scale;
        float th = 22.0f * scale;
        auto sz = m_renderTarget->GetSize();
        float bcx = (button.rect.left + button.rect.right) * 0.5f;
        float tx = std::clamp(bcx - tw * 0.5f, 4.0f * scale,
                              std::max(4.0f * scale, sz.width - tw - 4.0f * scale));
        float ty = button.rect.top - th - 6.0f * scale;
        if (ty < 4.0f * scale) ty = button.rect.bottom + 6.0f * scale;

        drawGlassPanel(D2D1::RectF(tx, ty, tx + tw, ty + th), 5.0f * scale, false);
        m_renderTarget->DrawText(tip.c_str(), static_cast<UINT32>(tip.size()),
                                 m_infoTextFormat.Get(),
                                 D2D1::RectF(tx, ty, tx + tw, ty + th),
                                 m_infoTextBrush.Get());
        break;
    }
}

void CaptureRenderer::drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough) {
    if (!m_renderTarget) return;
    auto rr = D2D1::RoundedRect(rect, radius, radius);

    ComPtr<ID2D1SolidColorBrush> tint, sheen, border;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.74f), tint.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.16f), sheen.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.12f), border.GetAddressOf());

    // 透视玻璃：圆角裁剪 → 透出未暗化的背后画面 → 深色蒙版（图层，较重）。
    bool glass = false;
    if (seeThrough && m_d2dFactory && m_screenBitmap) {
        ComPtr<ID2D1RoundedRectangleGeometry> geo;
        if (SUCCEEDED(m_d2dFactory->CreateRoundedRectangleGeometry(rr, geo.GetAddressOf())) && geo) {
            ComPtr<ID2D1Layer> layer;
            if (SUCCEEDED(m_renderTarget->CreateLayer(layer.GetAddressOf())) && layer) {
                m_renderTarget->PushLayer(
                    D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get(),
                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                        1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE),
                    layer.Get());
                m_renderTarget->DrawBitmap(m_screenBitmap.Get(), rect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &rect);
                if (tint) m_renderTarget->FillRectangle(rect, tint.Get());
                m_renderTarget->PopLayer();
                glass = true;
            }
        }
    }
    if (!glass) {
        // 廉价磨砂：半透明深色直接叠加在当前帧上（透出已暗化的背后画面），无图层开销。
        if (tint) m_renderTarget->FillRoundedRectangle(rr, tint.Get());
        else m_renderTarget->FillRoundedRectangle(rr, m_infoBgBrush.Get());
    }

    // 顶部高光（玻璃边沿反光）+ 细边框
    if (sheen) m_renderTarget->DrawLine(
        D2D1::Point2F(rect.left + radius, rect.top + 1.0f),
        D2D1::Point2F(rect.right - radius, rect.top + 1.0f), sheen.Get(), 1.2f);
    if (border) m_renderTarget->DrawRoundedRectangle(rr, border.Get(), 1.0f);
}

void CaptureRenderer::drawMarkupPreview(const D2D1_RECT_F& selectionRect, CaptureState& state) {
    if (!state.markupBaseReady || state.markup.elementCount() == 0 || !m_renderTarget) return;

    // 仅在标注内容变化时才重新合成并重建 D2D 位图，否则复用缓存。
    // 这条路径原本每帧（16ms）都 clone 整图 + 重绘全部标注 + 新建位图，是主要性能浪费点。
    if (m_markupCacheDirty || !m_markupCacheBitmap) {
        cv::Mat composite = state.markup.getCompositeImage();
        if (composite.empty()) return;

        cv::Mat bgra;
        if (composite.channels() == 3) {
            cv::cvtColor(composite, bgra, cv::COLOR_BGR2BGRA);
        } else if (composite.channels() == 4) {
            bgra = composite;
        } else {
            return;
        }

        D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        ComPtr<ID2D1Bitmap> bitmap;
        HRESULT hr = m_renderTarget->CreateBitmap(
            D2D1::SizeU(bgra.cols, bgra.rows),
            bgra.data,
            bgra.cols * 4,
            bitmapProps,
            bitmap.GetAddressOf()
        );
        if (FAILED(hr) || !bitmap) return;
        m_markupCacheBitmap = bitmap;
        m_markupCacheDirty = false;
    }

    if (m_markupCacheBitmap) {
        m_renderTarget->DrawBitmap(m_markupCacheBitmap.Get(), selectionRect);
    }
}

void CaptureRenderer::drawActiveMarkupPreview(const D2D1_RECT_F& selectionRect, CaptureState& state) {
    if (!state.isMarking || !m_renderTarget) return;

    float x1 = static_cast<float>(state.markupStart.x);
    float y1 = static_cast<float>(state.markupStart.y);
    float x2 = static_cast<float>(state.markupEnd.x);
    float y2 = static_cast<float>(state.markupEnd.y);
    auto rect = D2D1::RectF(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2));

    switch (state.currentTool) {
        case MarkupTool::Rectangle:
        case MarkupTool::Mosaic:
            m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f);
            break;

        case MarkupTool::Highlight: {
            ComPtr<ID2D1SolidColorBrush> highlightBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.78f, 0.18f, 0.28f), highlightBrush.GetAddressOf());
            m_renderTarget->FillRectangle(rect, highlightBrush.Get());
            m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 1.0f);
            break;
        }

        case MarkupTool::Arrow:
            m_renderTarget->DrawLine(
                D2D1::Point2F(x1, y1),
                D2D1::Point2F(x2, y2),
                m_borderBrush.Get(),
                2.0f
            );
            break;

        case MarkupTool::Ellipse:
            m_renderTarget->DrawEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F((x1 + x2) / 2.0f, (y1 + y2) / 2.0f),
                    std::abs(x2 - x1) / 2.0f,
                    std::abs(y2 - y1) / 2.0f
                ),
                m_borderBrush.Get(),
                2.0f
            );
            break;

        case MarkupTool::Pen:
            if (state.penPoints.size() >= 2) {
                for (size_t i = 1; i < state.penPoints.size(); ++i) {
                    m_renderTarget->DrawLine(
                        D2D1::Point2F(selectionRect.left + static_cast<float>(state.penPoints[i - 1].x),
                                      selectionRect.top + static_cast<float>(state.penPoints[i - 1].y)),
                        D2D1::Point2F(selectionRect.left + static_cast<float>(state.penPoints[i].x),
                                      selectionRect.top + static_cast<float>(state.penPoints[i].y)),
                        m_borderBrush.Get(),
                        2.0f
                    );
                }
            }
            break;

        default:
            break;
    }
}

void CaptureRenderer::drawDynamicMagnifier(CaptureState& state) {
    if (!m_renderTarget || !m_screenBitmap) return;

    float r = static_cast<float>(state.dynamicMagnifierRadius);
    float scale = state.dynamicMagnifierScale;

    // 获取光标在覆盖层中的位置 (屏幕坐标 -> 覆盖层坐标)
    float cx = static_cast<float>(state.currentCursor.x);
    float cy = static_cast<float>(state.currentCursor.y);

    float srcR = r / scale;
    D2D1_RECT_F srcRect = D2D1::RectF(cx - srcR, cy - srcR, cx + srcR, cy + srcR);
    D2D1_RECT_F dstRect = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);

    // 创建圆形裁剪
    ComPtr<ID2D1EllipseGeometry> ellipse;
    m_d2dFactory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), ellipse.GetAddressOf());

    if (ellipse) {
        ComPtr<ID2D1Layer> layer;
        m_renderTarget->CreateLayer(layer.GetAddressOf());
        m_renderTarget->PushLayer(D2D1::LayerParameters(
            D2D1::InfiniteRect(), ellipse.Get(),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(),
            1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE), layer.Get());

        m_renderTarget->DrawBitmap(m_screenBitmap.Get(), dstRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, srcRect);

        m_renderTarget->PopLayer();

        // 边框
        m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), m_borderBrush.Get(), 2.0f);
        
        // 十字准星
        m_renderTarget->DrawLine(D2D1::Point2F(cx - 5, cy), D2D1::Point2F(cx + 5, cy), m_borderBrush.Get(), 1.0f);
        m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 5), D2D1::Point2F(cx, cy + 5), m_borderBrush.Get(), 1.0f);
    }
}

void CaptureRenderer::drawSelectionLoupe(float cx, float cy, CaptureState& state) {
    if (!m_renderTarget || !m_screenBitmap) return;

    int px = static_cast<int>(cx);
    int py = static_cast<int>(cy);
    int r = 0, g = 0, b = 0;
    bool hasColor = sampleScreenColor(px, py, r, g, b, state);

    float scale = (state.dpiScale > 0) ? state.dpiScale : 1.0f;
    constexpr float zoom = 9.0f; 
    
    int gridCountX = 17;
    int gridCountY = 7;
    float loupeBoxW = gridCountX * zoom * scale;
    float loupeBoxH = gridCountY * zoom * scale;
    float panelH = 96.0f * scale; 
    const float totalH = loupeBoxH + panelH;
    const float pad = 12.0f * scale;

    auto size = m_renderTarget->GetSize();

    float lx = cx + 24.0f * scale;
    float ly = cy + 24.0f * scale;

    if (lx + loupeBoxW + pad > size.width) lx = cx - 24.0f * scale - loupeBoxW;
    if (ly + totalH + pad > size.height) ly = cy - 24.0f * scale - totalH;
    lx = std::clamp(lx, pad, std::max(pad, size.width - loupeBoxW - pad));
    ly = std::clamp(ly, pad, std::max(pad, size.height - totalH - pad));

    D2D1_RECT_F dst = D2D1::RectF(lx, ly, lx + loupeBoxW, ly + loupeBoxH);
    
    float srcHalfX = gridCountX / 2.0f; 
    float srcHalfY = gridCountY / 2.0f; 
    D2D1_RECT_F src = D2D1::RectF(cx - srcHalfX, cy - srcHalfY, cx + srcHalfX, cy + srcHalfY);
    
    m_renderTarget->DrawBitmap(m_screenBitmap.Get(), dst, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);

    ComPtr<ID2D1SolidColorBrush> gridBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.4f), gridBrush.GetAddressOf());
    if (gridBrush) {
        for (int i = 0; i <= gridCountY; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx, ly + lineOffset), D2D1::Point2F(lx + loupeBoxW, ly + lineOffset), gridBrush.Get(), 1.0f);
        }
        for (int i = 0; i <= gridCountX; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx + lineOffset, ly), D2D1::Point2F(lx + lineOffset, ly + loupeBoxH), gridBrush.Get(), 1.0f);
        }
    }

    ComPtr<ID2D1SolidColorBrush> blueCrosshair;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.7f, 1.0f, 0.6f), blueCrosshair.GetAddressOf());
    
    float pixelSz = zoom * scale;
    float mcx = lx + (gridCountX / 2) * pixelSz; 
    float mcy = ly + (gridCountY / 2) * pixelSz; 

    if (blueCrosshair) {
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, ly, mcx + pixelSz, mcy), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, mcy + pixelSz, mcx + pixelSz, ly + loupeBoxH), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(lx, mcy, mcx, mcy + pixelSz), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx + pixelSz, mcy, lx + loupeBoxW, mcy + pixelSz), blueCrosshair.Get());
    }

    ComPtr<ID2D1SolidColorBrush> blackBrush, whiteBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0,0,0, 1.0f), blackBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1,1,1, 1.0f), whiteBrush.GetAddressOf());
    if (blackBrush && whiteBrush) {
        D2D1_RECT_F centerCell = D2D1::RectF(mcx, mcy, mcx + pixelSz, mcy + pixelSz);
        m_renderTarget->DrawRectangle(centerCell, blackBrush.Get(), 1.5f);
    }
    
    m_renderTarget->DrawRectangle(dst, blackBrush.Get(), 1.0f);

    D2D1_ROUNDED_RECT panelRounded = D2D1::RoundedRect(
        D2D1::RectF(lx, ly + loupeBoxH, lx + loupeBoxW, ly + totalH),
        4.0f * scale, 4.0f * scale
    );
    ComPtr<ID2D1SolidColorBrush> panelBg;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.14f, 0.96f), panelBg.GetAddressOf());
    if (panelBg) m_renderTarget->FillRoundedRectangle(panelRounded, panelBg.Get());

    if (hasColor && m_infoTextFormat && m_dwriteFactory) {
        std::wstring text1 = std::format(L"({} , {})", px, py);
        std::wstring text2 = state.colorFormatHex ? 
                             std::format(L"#{:02X}{:02X}{:02X}", r, g, b) : 
                             std::format(L"rgb({}, {}, {})", r, g, b);
        std::wstring text3 = L"Shift: 切换格式";
        
        bool toast = (state.loupeToastUntil != 0 && GetTickCount() < state.loupeToastUntil);
        std::wstring text4 = toast ? L"✓ 颜色已复制!" : L"C: 复制颜色值";

        float tx = lx;
        float tw = loupeBoxW;
        float currY = ly + loupeBoxH + 8.0f * scale;
        
        m_renderTarget->DrawTextW(text1.c_str(), (UINT32)text1.size(), m_infoTextFormat.Get(),
            D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), m_infoTextBrush.Get());
        currY += 22.0f * scale;

        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(), 1000.0f, 24.0f * scale, layout.GetAddressOf());
        float textW = 0;
        if (layout) {
            DWRITE_TEXT_METRICS metrics;
            layout->GetMetrics(&metrics);
            textW = metrics.width;
        }

        float boxSz = 12.0f * scale;
        float gap = 6.0f * scale;
        float totalBlockW = boxSz + gap + textW;
        float startX = tx + (tw - totalBlockW) / 2.0f;

        ComPtr<ID2D1SolidColorBrush> colorBox;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f), colorBox.GetAddressOf());
        if (colorBox) {
            float boxY = currY + (20.0f * scale - boxSz) / 2.0f;
            D2D1_ROUNDED_RECT colorRect = D2D1::RoundedRect(D2D1::RectF(startX, boxY, startX + boxSz, boxY + boxSz), 2.0f * scale, 2.0f * scale);
            m_renderTarget->FillRoundedRectangle(colorRect, colorBox.Get());
            m_renderTarget->DrawRoundedRectangle(colorRect, whiteBrush.Get(), 1.0f * scale);
            
            m_renderTarget->DrawTextW(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(),
                D2D1::RectF(startX + boxSz + gap, currY, startX + totalBlockW, currY + 24.0f * scale), m_infoTextBrush.Get());
        }

        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        currY += 22.0f * scale;

        ComPtr<ID2D1SolidColorBrush> hintBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.78f, 1.0f), hintBrush.GetAddressOf());
        if (hintBrush) {
            m_renderTarget->DrawTextW(text3.c_str(), (UINT32)text3.size(), m_infoTextFormat.Get(),
                D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), hintBrush.Get());
        }
        currY += 20.0f * scale;

        ComPtr<ID2D1SolidColorBrush> actionBrush;
        if (toast) {
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.85f, 0.6f, 1.0f), actionBrush.GetAddressOf());
        } else {
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.75f, 1.0f, 1.0f), actionBrush.GetAddressOf());
        }
        if (actionBrush) {
            m_renderTarget->DrawTextW(text4.c_str(), (UINT32)text4.size(), m_infoTextFormat.Get(),
                D2D1::RectF(tx, currY, tx + tw, currY + 24.0f * scale), actionBrush.Get());
        }
    }

    if (blackBrush) {
        m_renderTarget->DrawRectangle(D2D1::RectF(lx, ly, lx + loupeBoxW, ly + totalH), blackBrush.Get(), 1.0f);
    }
}

void CaptureRenderer::drawCrosshair(float x, float y) {
    auto size = m_renderTarget->GetSize();
    m_renderTarget->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, size.height), m_crosshairBrush.Get(), 1.0f);
    m_renderTarget->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(size.width, y), m_crosshairBrush.Get(), 1.0f);
}

bool CaptureRenderer::sampleScreenColor(int x, int y, int& r, int& g, int& b, CaptureState& state) const {
    if (state.frozenScreen.empty()) return false;
    if (x < 0 || y < 0 || x >= state.frozenScreen.cols || y >= state.frozenScreen.rows) return false;
    if (state.frozenScreen.channels() == 4) {
        const cv::Vec4b& px = state.frozenScreen.at<cv::Vec4b>(y, x);
        b = px[0]; g = px[1]; r = px[2];
    } else if (state.frozenScreen.channels() == 3) {
        const cv::Vec3b& px = state.frozenScreen.at<cv::Vec3b>(y, x);
        b = px[0]; g = px[1]; r = px[2];
    } else {
        return false;
    }
    return true;
}

void CaptureRenderer::updateHistoryBitmap(CaptureState& state) {
    m_historyBitmap.Reset();
    auto entry = CaptureHistory::instance().get(state.historyIndex);
    if (entry && !entry->image.empty() && m_renderTarget) {
        cv::Mat bgra;
        if (entry->image.channels() == 3) {
            cv::cvtColor(entry->image, bgra, cv::COLOR_BGR2BGRA);
        } else if (entry->image.channels() == 4) {
            bgra = entry->image;
        }
        if (!bgra.empty()) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            m_renderTarget->CreateBitmap(
                D2D1::SizeU(bgra.cols, bgra.rows),
                bgra.data, bgra.cols * 4, props, m_historyBitmap.GetAddressOf()
            );
        }
    }
    m_needsRender = true;
}

void CaptureRenderer::render(CaptureState& state) {
    if (!m_renderTarget) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (state.historyMode) {
        auto size = m_renderTarget->GetSize();
        m_renderTarget->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), m_dimBrush.Get());
        
        if (m_historyBitmap) {
            auto bmpSize = m_historyBitmap->GetSize();
            float x = (size.width - bmpSize.width) / 2.0f;
            float y = (size.height - bmpSize.height) / 2.0f;
            D2D1_RECT_F dstRect = D2D1::RectF(x, y, x + bmpSize.width, y + bmpSize.height);
            m_renderTarget->DrawBitmap(m_historyBitmap.Get(), dstRect);
            m_renderTarget->DrawRectangle(dstRect, m_borderBrush.Get(), 2.0f);
            
            auto info = std::format(L"历史回放: {} / {}", state.historyIndex + 1, CaptureHistory::instance().count());
            drawGlassPanel(D2D1::RectF(x, y - 30.0f, x + 200.0f, y - 6.0f), 5.0f, false);
            m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                                     m_infoTextFormat.Get(),
                                     D2D1::RectF(x, y - 30.0f, x + 200.0f, y - 6.0f),
                                     m_infoTextBrush.Get());
        } else {
            auto info = L"暂无历史截图";
            m_renderTarget->DrawText(info, 6, m_infoTextFormat.Get(),
                                     D2D1::RectF(0, 0, size.width, size.height), m_infoTextBrush.Get());
        }
        
        m_renderTarget->EndDraw();
        return;
    }

    // 绘制冻结的屏幕
    if (m_screenBitmap) {
        auto size = m_renderTarget->GetSize();
        m_renderTarget->DrawBitmap(m_screenBitmap.Get(),
                                   D2D1::RectF(0, 0, size.width, size.height));
    }


    if ((int)state.state.load() == (int)OverlayState::Selecting && state.dragging) {
        // 正在拖拽选区
        float x1 = static_cast<float>(std::min(state.dragStart.x, state.dragEnd.x));
        float y1 = static_cast<float>(std::min(state.dragStart.y, state.dragEnd.y));
        float x2 = static_cast<float>(std::max(state.dragStart.x, state.dragEnd.x));
        float y2 = static_cast<float>(std::max(state.dragStart.y, state.dragEnd.y));

        D2D1_RECT_F selRect = D2D1::RectF(x1, y1, x2, y2);
        drawDimOverlay(selRect, state);
        drawSelection(selRect, state);
        drawSizeInfo(selRect, state);
        // 拖拽中也显示取色放大镜，便于像素级对齐选区边缘
        drawSelectionLoupe(static_cast<float>(state.currentCursor.x), static_cast<float>(state.currentCursor.y), state);
    } else if ((int)state.state.load() == (int)OverlayState::Selected || (int)state.state.load() == (int)OverlayState::Marking) {
        // 选区已确认
        float x1 = static_cast<float>(std::min(state.dragStart.x, state.dragEnd.x));
        float y1 = static_cast<float>(std::min(state.dragStart.y, state.dragEnd.y));
        float x2 = static_cast<float>(std::max(state.dragStart.x, state.dragEnd.x));
        float y2 = static_cast<float>(std::max(state.dragStart.y, state.dragEnd.y));

        D2D1_RECT_F selRect = D2D1::RectF(x1, y1, x2, y2);
        drawDimOverlay(selRect, state);
        drawMarkupPreview(selRect, state);
        drawActiveMarkupPreview(selRect, state);

        if (state.currentTool == MarkupTool::Magnifier && !state.isMarking && !state.isManipulating) {
            drawDynamicMagnifier(state);
        }

        drawSelection(selRect, state);
        drawSizeInfo(selRect, state);
        drawToolbar(selRect, state);
    } else {
        // 空闲/选区前 — 全屏变暗 + 十字准星 + 窗口高亮
        auto size = m_renderTarget->GetSize();
        m_renderTarget->FillRectangle(
            D2D1::RectF(0, 0, size.width, size.height), m_dimBrush.Get()
        );

        // 检测光标下的窗口并高亮其边界
        if (state.detectedWindow.right > state.detectedWindow.left && 
            state.detectedWindow.bottom > state.detectedWindow.top) {
            D2D1_RECT_F winRect = D2D1::RectF(
                static_cast<float>(state.detectedWindow.left),
                static_cast<float>(state.detectedWindow.top),
                static_cast<float>(state.detectedWindow.right),
                static_cast<float>(state.detectedWindow.bottom)
            );
            // 窗口区域显示原始截图（去掉变暗效果）
            if (m_screenBitmap) {
                m_renderTarget->DrawBitmap(m_screenBitmap.Get(), winRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &winRect);
            }
            // 紫色边框高亮
            m_renderTarget->DrawRectangle(winRect, m_borderBrush.Get(), 2.0f);
            // 显示窗口尺寸
            drawSizeInfo(winRect, state);
        }

        if (state.options.showCrosshair) {
            drawCrosshair(static_cast<float>(state.currentCursor.x),
                          static_cast<float>(state.currentCursor.y));
            // 预选悬停时显示像素级取色放大镜（坐标 + RGB/HEX，按 C 复制）
            drawSelectionLoupe(static_cast<float>(state.currentCursor.x), static_cast<float>(state.currentCursor.y), state);
        }
    }

    
    m_renderTarget->EndDraw();
}

} // namespace easy::capture
