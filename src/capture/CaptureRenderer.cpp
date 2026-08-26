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

static std::wstring tooltipForButton(const ToolbarButton& button, bool chinese) {
    if (button.command == ToolbarCommand::SelectTool) {
        switch (button.tool) {
            case MarkupTool::Rectangle: return chinese ? L"矩形 (R)" : L"Rectangle (R)";
            case MarkupTool::Ellipse: return chinese ? L"椭圆 (O)" : L"Ellipse (O)";
            case MarkupTool::Arrow: return chinese ? L"箭头 (A)" : L"Arrow (A)";
            case MarkupTool::Pen: return chinese ? L"涂鸦画笔 (P)" : L"Pen (P)";
            case MarkupTool::Highlight: return chinese ? L"荧光笔 (H)" : L"Highlighter (H)";
            case MarkupTool::Mosaic: return chinese ? L"马赛克 (M)" : L"Mosaic (M)";
            case MarkupTool::Text: return chinese ? L"添加文本 (T)" : L"Text (T)";
            case MarkupTool::Number: return chinese ? L"序号标记 (N)" : L"Numbered Step (N)";
            case MarkupTool::Magnifier: return chinese ? L"局部放大 (Z)" : L"Magnifier (Z)";
            case MarkupTool::Spotlight: return chinese ? L"聚光灯 (L)" : L"Spotlight (L)";
            case MarkupTool::Watermark: return chinese ? L"水印图章" : L"Watermark";
            case MarkupTool::Inpaint: return chinese ? L"智能消除 / 橡皮擦 (E)" : L"Inpaint / Eraser (E)";
            default: return chinese ? L"标注工具" : L"Tool";
        }
    }
    if (button.command == ToolbarCommand::SelectColor) {
        if (button.color.r == 244 && button.color.g == 63 && button.color.b == 94) return chinese ? L"珊瑚红" : L"Coral Red";
        if (button.color.r == 245 && button.color.g == 158 && button.color.b == 11) return chinese ? L"曜石橙" : L"Amber Orange";
        if (button.color.r == 234 && button.color.g == 179 && button.color.b == 8) return chinese ? L"明快黄" : L"Yellow";
        if (button.color.r == 16 && button.color.g == 185 && button.color.b == 129) return chinese ? L"薄荷绿" : L"Mint Green";
        if (button.color.r == 59 && button.color.g == 130 && button.color.b == 246) return chinese ? L"科技蓝" : L"Tech Blue";
        if (button.color.r == 30 && button.color.g == 41 && button.color.b == 59) return chinese ? L"极客黑" : L"Black";
        if (button.color.r == 255 && button.color.g == 255 && button.color.b == 255) return chinese ? L"纯洁白" : L"White";
        return chinese ? L"更改颜色" : L"Change color";
    }
    switch (button.command) {
        case ToolbarCommand::Undo: return chinese ? L"撤销 (Ctrl+Z)" : L"Undo (Ctrl+Z)";
        case ToolbarCommand::Redo: return chinese ? L"重做 (Ctrl+Y)" : L"Redo (Ctrl+Y)";
        case ToolbarCommand::Clear: return chinese ? L"清空所有标注" : L"Clear all annotations";
        case ToolbarCommand::ToggleCornerRadius: return chinese ? L"调节选区圆角 ([ / ])" : L"Corner radius ([ / ])";
        case ToolbarCommand::ExtractText: return chinese ? L"提取文字 (OCR)" : L"Extract text (OCR)";
        case ToolbarCommand::PinWindow: return chinese ? L"贴图置顶 (Ctrl+T)" : L"Pin to screen (Ctrl+T)";
        case ToolbarCommand::ScrollCapture: return chinese ? L"长截图" : L"Scrolling capture";
        case ToolbarCommand::Confirm: return chinese ? L"复制并完成 (Enter / Ctrl+C)" : L"Copy & Done (Enter / Ctrl+C)";
        case ToolbarCommand::Cancel: return chinese ? L"取消 (Esc)" : L"Cancel (Esc)";
        case ToolbarCommand::ToggleFill: return chinese ? L"切换填充模式" : L"Toggle Fill";
        case ToolbarCommand::CycleStrokeWidth: return chinese ? L"调节线宽 / 字号" : L"Adjust Stroke Width";
        case ToolbarCommand::CycleElementCornerRadius: return chinese ? L"调节标注圆角" : L"Corner Radius";
        case ToolbarCommand::ToggleLineStyleDropdown: return chinese ? L"线条样式 (实线/虚线)" : L"Line Style (Solid/Dashed)";
        case ToolbarCommand::ToggleArrowStyleDropdown: return chinese ? L"箭头样式" : L"Arrow Style";
        case ToolbarCommand::SelectMosaicType: return chinese ? (button.intParam == 0 ? L"像素马赛克" : L"高斯模糊") : (button.intParam == 0 ? L"Pixel Mosaic" : L"Gaussian Blur");
        default: return L"";
    }
}

namespace {

void rgbToHsl(int r, int g, int b, int& h, int& s, int& l) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxVal = std::max({rf, gf, bf}), minVal = std::min({rf, gf, bf});
    float delta = maxVal - minVal;
    float lf = (maxVal + minVal) * 0.5f;
    float sf = 0.0f, hf = 0.0f;
    if (delta > 1e-5f) {
        sf = lf > 0.5f ? delta / (2.0f - maxVal - minVal) : delta / (maxVal + minVal);
        if (maxVal == rf) {
            hf = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
        } else if (maxVal == gf) {
            hf = (bf - rf) / delta + 2.0f;
        } else {
            hf = (rf - gf) / delta + 4.0f;
        }
        hf /= 6.0f;
    }
    h = static_cast<int>(std::round(hf * 360.0f)) % 360;
    s = static_cast<int>(std::round(sf * 100.0f));
    l = static_cast<int>(std::round(lf * 100.0f));
}

void rgbToHsv(int r, int g, int b, int& h, int& s, int& v) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxVal = std::max({rf, gf, bf}), minVal = std::min({rf, gf, bf});
    float delta = maxVal - minVal;
    float vf = maxVal;
    float sf = maxVal > 1e-5f ? delta / maxVal : 0.0f;
    float hf = 0.0f;
    if (delta > 1e-5f) {
        if (maxVal == rf) {
            hf = (gf - bf) / delta + (gf < bf ? 6.0f : 0.0f);
        } else if (maxVal == gf) {
            hf = (bf - rf) / delta + 2.0f;
        } else {
            hf = (rf - gf) / delta + 4.0f;
        }
        hf /= 6.0f;
    }
    h = static_cast<int>(std::round(hf * 360.0f)) % 360;
    s = static_cast<int>(std::round(sf * 100.0f));
    v = static_cast<int>(std::round(vf * 100.0f));
}

void rgbToCmyk(int r, int g, int b, int& c, int& m, int& y, int& k) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float kf = 1.0f - std::max({rf, gf, bf});
    if (kf < 1.0f - 1e-5f) {
        c = static_cast<int>(std::round((1.0f - rf - kf) / (1.0f - kf) * 100.0f));
        m = static_cast<int>(std::round((1.0f - gf - kf) / (1.0f - kf) * 100.0f));
        y = static_cast<int>(std::round((1.0f - bf - kf) / (1.0f - kf) * 100.0f));
    } else {
        c = m = y = 0;
    }
    k = static_cast<int>(std::round(kf * 100.0f));
}

struct ColorFormatEntry {
    ColorFormatType type;
    const wchar_t* tag;
    std::wstring value;
    std::string clipText;
};

std::vector<ColorFormatEntry> getAllColorFormats(int r, int g, int b) {
    int hslH, hslS, hslL;
    rgbToHsl(r, g, b, hslH, hslS, hslL);

    int hsvH, hsvS, hsvV;
    rgbToHsv(r, g, b, hsvH, hsvS, hsvV);

    int cmykC, cmykM, cmykY, cmykK;
    rgbToCmyk(r, g, b, cmykC, cmykM, cmykY, cmykK);

    uint32_t decVal = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);

    std::vector<ColorFormatEntry> list;
    list.push_back({
        ColorFormatType::HEX, L"HEX",
        std::format(L"#{:02X}{:02X}{:02X}", r, g, b),
        std::format("#{:02X}{:02X}{:02X}", r, g, b)
    });
    list.push_back({
        ColorFormatType::RGB, L"RGB",
        std::format(L"rgb({}, {}, {})", r, g, b),
        std::format("rgb({}, {}, {})", r, g, b)
    });
    list.push_back({
        ColorFormatType::RGBA, L"RGBA",
        std::format(L"rgba({}, {}, {}, 1.0)", r, g, b),
        std::format("rgba({}, {}, {}, 1.0)", r, g, b)
    });
    list.push_back({
        ColorFormatType::HEX_0x, L"0xHEX",
        std::format(L"0x{:02X}{:02X}{:02X}", r, g, b),
        std::format("0x{:02X}{:02X}{:02X}", r, g, b)
    });
    list.push_back({
        ColorFormatType::HSL, L"HSL",
        std::format(L"hsl({}, {}%, {}%)", hslH, hslS, hslL),
        std::format("hsl({}, {}%, {}%)", hslH, hslS, hslL)
    });
    list.push_back({
        ColorFormatType::HSV, L"HSV",
        std::format(L"hsv({}, {}%, {}%)", hsvH, hsvS, hsvV),
        std::format("hsv({}, {}%, {}%)", hsvH, hsvS, hsvV)
    });
    list.push_back({
        ColorFormatType::CMYK, L"CMYK",
        std::format(L"cmyk({}%, {}%, {}%, {}%)", cmykC, cmykM, cmykY, cmykK),
        std::format("cmyk({}%, {}%, {}%, {}%)", cmykC, cmykM, cmykY, cmykK)
    });
    list.push_back({
        ColorFormatType::DEC, L"DEC",
        std::format(L"{}", decVal),
        std::format("{}", decVal)
    });

    return list;
}

} // namespace

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
        DWRITE_FONT_STRETCH_NORMAL, 14.0f * scale, L"zh-CN",
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

void CaptureRenderer::drawDimOverlay(const D2D1_RECT_F& selRect, CaptureState& state) {
    auto size = m_renderTarget->GetSize();
    float radius = state.cornerRadius * (state.dpiScale > 0.0f ? state.dpiScale : 1.0f);

    if (radius > 0.5f && m_d2dFactory) {
        // 圆角遮罩打洞：排除选区圆角矩形
        auto rounded = D2D1::RoundedRect(selRect, radius, radius);
        ComPtr<ID2D1RoundedRectangleGeometry> geo;
        if (SUCCEEDED(m_d2dFactory->CreateRoundedRectangleGeometry(rounded, geo.GetAddressOf())) && geo) {
            ComPtr<ID2D1RectangleGeometry> fullGeo;
            if (SUCCEEDED(m_d2dFactory->CreateRectangleGeometry(D2D1::RectF(0, 0, size.width, size.height), fullGeo.GetAddressOf())) && fullGeo) {
                ComPtr<ID2D1PathGeometry> path;
                if (SUCCEEDED(m_d2dFactory->CreatePathGeometry(path.GetAddressOf())) && path) {
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(path->Open(sink.GetAddressOf())) && sink) {
                        fullGeo->CombineWithGeometry(geo.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
                        sink->Close();
                        m_renderTarget->FillGeometry(path.Get(), m_dimBrush.Get());
                        return;
                    }
                }
            }
        }
    }

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
    float radius = state.cornerRadius * scale;

    // 1. 选区边框（高质感实线与圆角自适应）
    if (radius > 0.5f) {
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_borderBrush.Get(), 2.0f * scale);
    } else {
        m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f * scale);
    }

    // 2. 八个控制点：采用 PixPin 级纯白实心微圆点 + 主题色精致外描边
    float cx = (rect.left + rect.right) / 2;
    float cy = (rect.top + rect.bottom) / 2;

    D2D1_POINT_2F controls[] = {
        {rect.left, rect.top}, {cx, rect.top}, {rect.right, rect.top},
        {rect.left, cy}, {rect.right, cy},
        {rect.left, rect.bottom}, {cx, rect.bottom}, {rect.right, rect.bottom}
    };

    ComPtr<ID2D1SolidColorBrush> handleWhiteBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), handleWhiteBrush.GetAddressOf());

    const float dotRadius = 4.6f * scale;
    for (auto& pt : controls) {
        auto ellipse = D2D1::Ellipse(pt, dotRadius, dotRadius);
        if (handleWhiteBrush) {
            m_renderTarget->FillEllipse(ellipse, handleWhiteBrush.Get());
        }
        m_renderTarget->DrawEllipse(ellipse, m_borderBrush.Get(), 1.5f * scale);
    }

    // 3. Figma / PixPin 级内侧圆角调节手柄（默认隐藏，鼠标靠近拐角或拖拽时仅显示靠近的那 1 个）
    float selW = rect.right - rect.left;
    float selH = rect.bottom - rect.top;
    if (selW >= 40.0f * scale && selH >= 40.0f * scale) {
        float offset = std::clamp(std::max(14.0f, state.cornerRadius + 8.0f), 12.0f, std::min(selW, selH) * 0.40f) * scale;
        D2D1_POINT_2F cornerPts[4] = {
            {rect.left + offset,  rect.top + offset},
            {rect.right - offset, rect.top + offset},
            {rect.right - offset, rect.bottom - offset},
            {rect.left + offset,  rect.bottom - offset}
        };

        // 判定鼠标是否靠近 4 个角部的某一个（检测阈值 36px）
        const float triggerDist = 36.0f * scale;
        bool shouldShowCornerHandle = state.isAdjustingCornerRadius;
        int activeCornerIdx = -1;

        if (state.isAdjustingCornerRadius) {
            float minD = 999999.0f;
            for (int i = 0; i < 4; ++i) {
                float d = std::hypot(state.cornerDragStartPos.x - cornerPts[i].x, state.cornerDragStartPos.y - cornerPts[i].y);
                if (d < minD) { minD = d; activeCornerIdx = i; }
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                float d = std::hypot(state.currentCursor.x - cornerPts[i].x, state.currentCursor.y - cornerPts[i].y);
                if (d <= triggerDist) {
                    shouldShowCornerHandle = true;
                    activeCornerIdx = i;
                    break;
                }
            }
        }

        // 仅在鼠标靠近或正在调整时，且仅对这唯一的活跃角进行绘制（其余 3 个角绝对隐藏）
        if (shouldShowCornerHandle && activeCornerIdx >= 0 && activeCornerIdx < 4) {
            ComPtr<ID2D1SolidColorBrush> cornerRingBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.90f), cornerRingBrush.GetAddressOf());

            float outerRingRadius = (state.isAdjustingCornerRadius ? 5.2f : 4.2f) * scale;
            float innerDotRadius = (state.isAdjustingCornerRadius ? 1.8f : 1.4f) * scale;

            const auto& cpt = cornerPts[activeCornerIdx];
            // 外圈细光环
            auto outerEllipse = D2D1::Ellipse(cpt, outerRingRadius, outerRingRadius);
            if (cornerRingBrush) {
                m_renderTarget->DrawEllipse(outerEllipse, cornerRingBrush.Get(), 1.0f * scale);
            }
            m_renderTarget->DrawEllipse(outerEllipse, m_borderBrush.Get(), 0.8f * scale);

            // 中心纯白实心极细微圆点（小巧精致，不打扰心流）
            auto innerEllipse = D2D1::Ellipse(cpt, innerDotRadius, innerDotRadius);
            if (handleWhiteBrush) {
                m_renderTarget->FillEllipse(innerEllipse, handleWhiteBrush.Get());
            }

            // 4. PixPin 同款实时圆角半径微胶囊 [ ╭ 42 ]
            auto activePt = cornerPts[activeCornerIdx];
            int radVal = static_cast<int>(std::round(state.cornerRadius));
            std::wstring rText = std::format(L"{}", radVal);
            float badgeW = (28.0f + rText.size() * 8.0f) * scale;
            float badgeH = 20.0f * scale;
            
            // 胶囊位置根据所在角落智能避让
            float bx = (activeCornerIdx == 0 || activeCornerIdx == 3) ? (activePt.x + 10.0f * scale) : (activePt.x - badgeW - 10.0f * scale);
            float by = (activeCornerIdx == 0 || activeCornerIdx == 1) ? (activePt.y + 10.0f * scale) : (activePt.y - badgeH - 10.0f * scale);

            auto badgeRect = D2D1::RectF(bx, by, bx + badgeW, by + badgeH);
            auto badgeRounded = D2D1::RoundedRect(badgeRect, 4.0f * scale, 4.0f * scale);

            ComPtr<ID2D1SolidColorBrush> badgeBg, badgeBorder, badgeWhite;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.88f), badgeBg.GetAddressOf());
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), badgeBorder.GetAddressOf());
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), badgeWhite.GetAddressOf());

            if (badgeBg) m_renderTarget->FillRoundedRectangle(badgeRounded, badgeBg.Get());
            if (badgeBorder) m_renderTarget->DrawRoundedRectangle(badgeRounded, badgeBorder.Get(), 1.0f * scale);

            // 绘制与截图 100% 标杆一致的封闭圆角扇形切角矢量微图标
            float iconX = bx + 5.0f * scale;
            float iconY = by + (badgeH - 11.0f * scale) * 0.5f;
            if (badgeWhite && m_d2dFactory) {
                ComPtr<ID2D1PathGeometry> cornerIconGeo;
                m_d2dFactory->CreatePathGeometry(cornerIconGeo.GetAddressOf());
                if (cornerIconGeo) {
                    ComPtr<ID2D1GeometrySink> sink;
                    cornerIconGeo->Open(sink.GetAddressOf());
                    if (sink) {
                        float x0 = iconX + 1.2f * scale;
                        float y0 = iconY + 1.2f * scale;
                        float x1 = iconX + 9.8f * scale;
                        float y1 = iconY + 9.8f * scale;

                        sink->BeginFigure(D2D1::Point2F(x0, y1), D2D1_FIGURE_BEGIN_HOLLOW);
                        sink->AddLine(D2D1::Point2F(x0, y0 + 1.2f * scale));
                        sink->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(x0 + 1.2f * scale, y0),
                            D2D1::SizeF(1.2f * scale, 1.2f * scale),
                            0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                        sink->AddLine(D2D1::Point2F(x0 + 2.5f * scale, y0));
                        sink->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(x1, y1 - 1.2f * scale),
                            D2D1::SizeF(7.0f * scale, 7.0f * scale),
                            0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                        sink->AddLine(D2D1::Point2F(x1, y1));
                        sink->AddLine(D2D1::Point2F(x0, y1));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        sink->Close();
                    }
                    m_renderTarget->DrawGeometry(cornerIconGeo.Get(), badgeWhite.Get(), 1.2f * scale);
                }
            }

            // 绘制当前圆角数值
            if (m_infoTextFormat && badgeWhite) {
                m_renderTarget->DrawText(rText.c_str(), static_cast<UINT32>(rText.size()),
                                         m_infoTextFormat.Get(),
                                         D2D1::RectF(iconX + 13.0f * scale, by + 2.0f * scale, bx + badgeW, by + badgeH),
                                         badgeWhite.Get());
            }
        }
    }
}

void CaptureRenderer::drawSizeInfo(const D2D1_RECT_F& rect, CaptureState& state) {
    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    int rawW = static_cast<int>(rect.right - rect.left);
    int rawH = static_cast<int>(rect.bottom - rect.top);
    int curX = static_cast<int>(state.currentCursor.x > 0 ? state.currentCursor.x : rect.left);
    int curY = static_cast<int>(state.currentCursor.y > 0 ? state.currentCursor.y : rect.top);

    // 根据 px / dp 单位换算
    int displayW = (state.sizeUnit == CaptureState::SizeUnit::DeviceIndependentPixel) 
        ? static_cast<int>(std::round(rawW / (state.dpiScale > 0.0f ? state.dpiScale : 1.0f))) : rawW;
    int displayH = (state.sizeUnit == CaptureState::SizeUnit::DeviceIndependentPixel) 
        ? static_cast<int>(std::round(rawH / (state.dpiScale > 0.0f ? state.dpiScale : 1.0f))) : rawH;
    std::wstring unitStr = state.showUnitInHud 
        ? (state.sizeUnit == CaptureState::SizeUnit::DeviceIndependentPixel ? L" dp" : L" px") : L"";

    std::wstring info;
    if (state.showPositionInHud) {
        info = std::format(L"{},{}  {} × {}{}", curX, curY, displayW, displayH, unitStr);
    } else {
        info = std::format(L"{} × {}{}", displayW, displayH, unitStr);
    }
    if (state.cornerRadius > 0.5f) {
        info += std::format(L" (R: {}px)", static_cast<int>(state.cornerRadius));
    }

    // 1. DirectWrite 动态精准测量文本宽度（100% 杜绝文字溢出！）
    float textWidth = static_cast<float>(info.size()) * 8.5f * scale;
    if (m_dwriteFactory && m_infoTextFormat) {
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
            info.c_str(), static_cast<UINT32>(info.size()),
            m_infoTextFormat.Get(), 2000.0f * scale, 50.0f * scale, layout.GetAddressOf())) && layout) {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            if (tm.width > 0.0f) textWidth = tm.width;
        }
    }

    float labelW = textWidth + 18.0f * scale;
    float labelH = 26.0f * scale;
    float labelX = rect.left;
    float labelY = rect.top - labelH - 6.0f * scale;
    if (labelY < 4.0f * scale) labelY = rect.bottom + 6.0f * scale;

    auto pillRect = D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH);
    state.sizeHudRect = pillRect;

    // 判定鼠标悬停状态
    bool isHovered = (state.currentCursor.x >= pillRect.left && state.currentCursor.x <= pillRect.right &&
                      state.currentCursor.y >= pillRect.top && state.currentCursor.y <= pillRect.bottom);
    state.isSizeHudHovered = isHovered;

    auto pillRounded = D2D1::RoundedRect(pillRect, 6.0f * scale, 6.0f * scale);

    // 图 2 悬停/展开时高亮主题蓝色，默认深色磨砂亚克力
    ComPtr<ID2D1SolidColorBrush> pillBg, pillBorder, pillText;
    if (isHovered || state.isSizeMenuOpen) {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.95f), pillBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.40f), pillBorder.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), pillText.GetAddressOf());
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.88f), pillBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), pillBorder.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), pillText.GetAddressOf());
    }

    if (pillBg) m_renderTarget->FillRoundedRectangle(pillRounded, pillBg.Get());
    if (pillBorder) m_renderTarget->DrawRoundedRectangle(pillRounded, pillBorder.Get(), 1.0f * scale);

    if (m_infoTextFormat && pillText) {
        m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                                 m_infoTextFormat.Get(),
                                 D2D1::RectF(labelX + 9.0f * scale, labelY + 2.0f * scale,
                                            labelX + labelW - 4.0f * scale, labelY + labelH),
                                 pillText.Get());
    }
}

void CaptureRenderer::drawSizeMenu(const D2D1_RECT_F& hudRect, CaptureState& state) {
    if (!state.isSizeMenuOpen || !m_renderTarget) return;

    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    float menuW = 168.0f * scale;
    float menuH = 142.0f * scale;

    float menuX = hudRect.left;
    float menuY = hudRect.top - menuH - 8.0f * scale;
    if (menuY < 4.0f * scale) {
        menuY = hudRect.bottom + 8.0f * scale;
    }
    auto sz = m_renderTarget->GetSize();
    if (menuX + menuW > sz.width - 4.0f * scale) {
        menuX = sz.width - menuW - 4.0f * scale;
    }

    auto menuRect = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
    state.sizeMenuRect = menuRect;

    // 绘制纯白磨砂卡片 + 双层柔和阴影
    drawGlassPanel(menuRect, 8.0f * scale, false);

    ComPtr<ID2D1SolidColorBrush> primaryBrush, textBrush, subTextBrush, trackOffBrush, borderBrush, whiteBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), primaryBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f), textBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.48f, 0.52f, 0.60f, 0.95f), subTextBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.88f, 0.92f, 1.0f), trackOffBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f), borderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteBrush.GetAddressOf());

    // 1. 单选按钮：◉ px    ○ dp
    float row1Y = menuY + 16.0f * scale;
    float radioRadius = 6.0f * scale;

    // px 单选
    D2D1_POINT_2F rPxPt{menuX + 22.0f * scale, row1Y + 7.0f * scale};
    bool isPx = (state.sizeUnit == CaptureState::SizeUnit::Pixel);
    if (isPx) {
        m_renderTarget->DrawEllipse(D2D1::Ellipse(rPxPt, radioRadius, radioRadius), primaryBrush.Get(), 1.8f * scale);
        m_renderTarget->FillEllipse(D2D1::Ellipse(rPxPt, 3.2f * scale, 3.2f * scale), primaryBrush.Get());
    } else {
        m_renderTarget->DrawEllipse(D2D1::Ellipse(rPxPt, radioRadius, radioRadius), trackOffBrush.Get(), 1.5f * scale);
    }
    if (m_infoTextFormat && textBrush) {
        m_renderTarget->DrawText(L"px", 2, m_infoTextFormat.Get(),
            D2D1::RectF(rPxPt.x + 10.0f * scale, row1Y, rPxPt.x + 40.0f * scale, row1Y + 18.0f * scale),
            textBrush.Get());
    }

    // dp 单选
    D2D1_POINT_2F rDpPt{menuX + 92.0f * scale, row1Y + 7.0f * scale};
    bool isDp = (state.sizeUnit == CaptureState::SizeUnit::DeviceIndependentPixel);
    if (isDp) {
        m_renderTarget->DrawEllipse(D2D1::Ellipse(rDpPt, radioRadius, radioRadius), primaryBrush.Get(), 1.8f * scale);
        m_renderTarget->FillEllipse(D2D1::Ellipse(rDpPt, 3.2f * scale, 3.2f * scale), primaryBrush.Get());
    } else {
        m_renderTarget->DrawEllipse(D2D1::Ellipse(rDpPt, radioRadius, radioRadius), trackOffBrush.Get(), 1.5f * scale);
    }
    if (m_infoTextFormat && textBrush) {
        m_renderTarget->DrawText(L"dp", 2, m_infoTextFormat.Get(),
            D2D1::RectF(rDpPt.x + 10.0f * scale, row1Y, rDpPt.x + 40.0f * scale, row1Y + 18.0f * scale),
            textBrush.Get());
    }

    // 2. 辅助提示：* dp = px ÷ 屏幕缩放比
    float row2Y = row1Y + 22.0f * scale;
    std::wstring hint = state.toolbarLayoutChinese ? L"* dp = px ÷ 屏幕缩放比" : L"* dp = px ÷ Scale Factor";
    if (m_infoTextFormat && subTextBrush) {
        m_renderTarget->DrawText(hint.c_str(), static_cast<UINT32>(hint.size()), m_infoTextFormat.Get(),
            D2D1::RectF(menuX + 16.0f * scale, row2Y, menuX + menuW - 10.0f * scale, row2Y + 16.0f * scale),
            subTextBrush.Get());
    }

    // 3. 开关 1：位置 (显示/隐藏坐标)
    float row3Y = row2Y + 26.0f * scale;
    std::wstring posLabel = state.toolbarLayoutChinese ? L"位置" : L"Position";
    if (m_infoTextFormat && textBrush) {
        m_renderTarget->DrawText(posLabel.c_str(), static_cast<UINT32>(posLabel.size()), m_infoTextFormat.Get(),
            D2D1::RectF(menuX + 16.0f * scale, row3Y + 2.0f * scale, menuX + 80.0f * scale, row3Y + 22.0f * scale),
            textBrush.Get());
    }
    float swW = 32.0f * scale;
    float swH = 18.0f * scale;
    float swX = menuX + menuW - swW - 16.0f * scale;
    float swY = row3Y + 1.0f * scale;
    auto sw1Rect = D2D1::RectF(swX, swY, swX + swW, swY + swH);
    auto sw1Round = D2D1::RoundedRect(sw1Rect, swH * 0.5f, swH * 0.5f);
    if (state.showPositionInHud) {
        m_renderTarget->FillRoundedRectangle(sw1Round, primaryBrush.Get());
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(swX + swW - swH * 0.5f, swY + swH * 0.5f), (swH - 4.0f * scale) * 0.5f, (swH - 4.0f * scale) * 0.5f), whiteBrush.Get());
    } else {
        m_renderTarget->FillRoundedRectangle(sw1Round, trackOffBrush.Get());
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(swX + swH * 0.5f, swY + swH * 0.5f), (swH - 4.0f * scale) * 0.5f, (swH - 4.0f * scale) * 0.5f), whiteBrush.Get());
    }

    // 4. 开关 2：单位 (显示/隐藏 px/dp 后缀)
    float row4Y = row3Y + 28.0f * scale;
    std::wstring unitLabel = state.toolbarLayoutChinese ? L"单位" : L"Unit";
    if (m_infoTextFormat && textBrush) {
        m_renderTarget->DrawText(unitLabel.c_str(), static_cast<UINT32>(unitLabel.size()), m_infoTextFormat.Get(),
            D2D1::RectF(menuX + 16.0f * scale, row4Y + 2.0f * scale, menuX + 80.0f * scale, row4Y + 22.0f * scale),
            textBrush.Get());
    }
    float sw2Y = row4Y + 1.0f * scale;
    auto sw2Rect = D2D1::RectF(swX, sw2Y, swX + swW, sw2Y + swH);
    auto sw2Round = D2D1::RoundedRect(sw2Rect, swH * 0.5f, swH * 0.5f);
    if (state.showUnitInHud) {
        m_renderTarget->FillRoundedRectangle(sw2Round, primaryBrush.Get());
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(swX + swW - swH * 0.5f, sw2Y + swH * 0.5f), (swH - 4.0f * scale) * 0.5f, (swH - 4.0f * scale) * 0.5f), whiteBrush.Get());
    } else {
        m_renderTarget->FillRoundedRectangle(sw2Round, trackOffBrush.Get());
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(swX + swH * 0.5f, sw2Y + swH * 0.5f), (swH - 4.0f * scale) * 0.5f, (swH - 4.0f * scale) * 0.5f), whiteBrush.Get());
    }
}

void CaptureRenderer::drawToolbar(const D2D1_RECT_F& selectionRect, CaptureState& state) {
    rebuildCaptureToolbar(state, selectionRect, m_renderTarget->GetSize());
    if (state.toolbarButtons.empty()) return;

    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    
    // 1. 绘制主工具栏浮岛卡片
    drawGlassPanel(state.primaryToolbarRect, 10.0f * scale, false);

    // 2. 如果存在二级属性栏，绘制二级属性栏浮岛卡片
    if (!state.secondaryToolbarButtons.empty()) {
        drawGlassPanel(state.secondaryToolbarRect, 9.0f * scale, false);
    }

    auto& cfg = easy::core::ConfigManager::instance();
    const std::string accent = cfg.get<std::string>("/general/accentColor", "blue");
    const easy::core::AccentColorRGB themeRgb = easy::core::getAccentColorRGB(accent);

    ComPtr<ID2D1SolidColorBrush> buttonBrush;
    ComPtr<ID2D1SolidColorBrush> activeBrush;
    ComPtr<ID2D1SolidColorBrush> activeBorderBrush;
    ComPtr<ID2D1SolidColorBrush> dangerHoverBrush;
    ComPtr<ID2D1SolidColorBrush> confirmHoverBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    ComPtr<ID2D1SolidColorBrush> iconBrush;
    ComPtr<ID2D1SolidColorBrush> activeIconBrush;
    ComPtr<ID2D1SolidColorBrush> confirmIconBrush;
    ComPtr<ID2D1SolidColorBrush> dangerIconBrush;
    ComPtr<ID2D1SolidColorBrush> tipTextBrush;
    ComPtr<ID2D1SolidColorBrush> secondaryActiveBrush;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), buttonBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f), hoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.12f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.22f), secondaryActiveBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.85f), activeBorderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 1.0f), activeIconBrush.GetAddressOf());
    
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.70f, 0.38f, 0.16f), confirmHoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.65f, 0.32f, 1.0f), confirmIconBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.12f), dangerHoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.20f, 0.20f, 1.0f), dangerIconBrush.GetAddressOf());

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.17f, 0.22f, 0.92f), iconBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.15f, 0.95f), tipTextBrush.GetAddressOf());

    auto drawButtonGroup = [&](const std::vector<ToolbarButton>& btnList) {
        for (const auto& button : btnList) {
            bool isTool = button.command == ToolbarCommand::SelectTool;
            bool isActiveTool = false;
            if (isTool) {
                if (button.tool == MarkupTool::Rectangle || button.tool == MarkupTool::Ellipse) {
                    isActiveTool = (state.currentTool == MarkupTool::Rectangle || state.currentTool == MarkupTool::Ellipse);
                } else if (button.tool == MarkupTool::Pen || button.tool == MarkupTool::Highlight) {
                    isActiveTool = (state.currentTool == MarkupTool::Pen || state.currentTool == MarkupTool::Highlight);
                } else {
                    isActiveTool = (button.tool == state.currentTool);
                }
                if (button.isSecondary) {
                    isActiveTool = (button.tool == state.currentTool);
                }
            }
            if (button.command == ToolbarCommand::ToggleFill) {
                isActiveTool = state.currentFillMode;
            }

            bool isColor = button.command == ToolbarCommand::SelectColor;
            bool isDanger = button.command == ToolbarCommand::Cancel || button.command == ToolbarCommand::Clear;
            bool isConfirm = button.command == ToolbarCommand::Confirm;
            bool isHovered = state.currentCursor.x >= button.rect.left && state.currentCursor.x <= button.rect.right &&
                             state.currentCursor.y >= button.rect.top  && state.currentCursor.y <= button.rect.bottom;

            auto rounded = D2D1::RoundedRect(button.rect, 6.0f * scale, 6.0f * scale);

            if (isColor) {
                // 颜色色板: 圆形胶囊 + 双层高亮光环
                float cx = (button.rect.left + button.rect.right) * 0.5f;
                float cy = (button.rect.top + button.rect.bottom) * 0.5f;
                float r = std::min(button.rect.right - button.rect.left, button.rect.bottom - button.rect.top) * 0.40f;
                auto colorCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);

                ComPtr<ID2D1SolidColorBrush> swatch;
                m_renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(button.color.r / 255.0f, button.color.g / 255.0f,
                                 button.color.b / 255.0f, 1.0f),
                    swatch.GetAddressOf());
                if (swatch) m_renderTarget->FillEllipse(colorCircle, swatch.Get());

                bool isActiveColor = (button.color.r == state.currentColor.r &&
                                     button.color.g == state.currentColor.g &&
                                     button.color.b == state.currentColor.b);
                if (isActiveColor) {
                    auto outerRing = D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 2.5f * scale, r + 2.5f * scale);
                    m_renderTarget->DrawEllipse(outerRing, activeBorderBrush.Get(), 2.0f * scale);
                } else if (isHovered && hoverBrush) {
                    auto outerRing = D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 2.0f * scale, r + 2.0f * scale);
                    m_renderTarget->DrawEllipse(outerRing, hoverBrush.Get(), 1.5f * scale);
                }
                continue;
            }

            auto* fillBrush = isActiveTool ? (button.isSecondary ? secondaryActiveBrush.Get() : activeBrush.Get())
                            : (isConfirm && isHovered) ? confirmHoverBrush.Get()
                            : (isDanger && isHovered) ? dangerHoverBrush.Get()
                            : isHovered ? hoverBrush.Get()
                            : buttonBrush.Get();
            m_renderTarget->FillRoundedRectangle(rounded, fillBrush);

            if (isActiveTool && activeBorderBrush) {
                m_renderTarget->DrawRoundedRectangle(rounded, activeBorderBrush.Get(), 1.2f * scale);
            }

            auto* currentIconBrush = isActiveTool ? activeIconBrush.Get()
                                   : isConfirm ? confirmIconBrush.Get()
                                   : (isDanger && isHovered) ? dangerIconBrush.Get()
                                   : iconBrush.Get();
            drawVectorButtonIcon(button, button.rect, currentIconBrush, scale);

            // 如果带有下拉三角指示器，绘制小三角 v
            if (button.hasDropdown) {
                float tx = button.rect.right - 6.0f * scale;
                float ty = button.rect.bottom - 6.0f * scale;
                m_renderTarget->DrawLine(D2D1::Point2F(tx - 2.5f * scale, ty - 1.5f * scale),
                                         D2D1::Point2F(tx, ty + 1.5f * scale), currentIconBrush, 1.2f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(tx, ty + 1.5f * scale),
                                         D2D1::Point2F(tx + 2.5f * scale, ty - 1.5f * scale), currentIconBrush, 1.2f * scale);
            }
        }
    };

    // 绘制主工具栏与二级属性栏
    drawButtonGroup(state.toolbarButtons);
    if (!state.secondaryToolbarButtons.empty()) {
        drawButtonGroup(state.secondaryToolbarButtons);
    }

    // 绘制展开的二级下拉悬浮菜单
    drawSubmenu(state);

    // 悬停浮层提示：把生僻字形翻译成「名称 + 快捷键」
    auto showTooltip = [&](const std::vector<ToolbarButton>& btnList) -> bool {
        for (const auto& button : btnList) {
            if (state.currentCursor.x < button.rect.left || state.currentCursor.x > button.rect.right ||
                state.currentCursor.y < button.rect.top  || state.currentCursor.y > button.rect.bottom) {
                continue;
            }
            std::wstring tip = tooltipForButton(button, state.toolbarLayoutChinese);
            if (tip.empty()) return false;

            float tw = (20.0f + static_cast<float>(tip.size()) * 11.5f) * scale;
            float th = 22.0f * scale;
            auto sz = m_renderTarget->GetSize();
            float bcx = (button.rect.left + button.rect.right) * 0.5f;
            float tx = std::clamp(bcx - tw * 0.5f, 4.0f * scale,
                                  std::max(4.0f * scale, sz.width - tw - 4.0f * scale));
            float ty = button.rect.top - th - 6.0f * scale;
            if (ty < 4.0f * scale) ty = button.rect.bottom + 6.0f * scale;

            auto tr = D2D1::RectF(tx, ty, tx + tw, ty + th);
            drawGlassPanel(tr, 4.0f * scale, false);
            m_renderTarget->DrawText(tip.c_str(), static_cast<UINT32>(tip.size()),
                                     m_infoTextFormat.Get(), tr, tipTextBrush.Get());
            return true;
        }
        return false;
    };

    if (state.openSubmenu == SubmenuType::None) {
        if (!showTooltip(state.toolbarButtons)) {
            showTooltip(state.secondaryToolbarButtons);
        }
    }
}

void CaptureRenderer::drawSubmenu(CaptureState& state) {
    if (state.openSubmenu == SubmenuType::None || !m_renderTarget) return;

    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    
    // 线条样式下拉菜单 (实线、长虚线、点虚线、点划线)
    if (state.openSubmenu == SubmenuType::LineStyle) {
        float menuW = 120.0f * scale;
        float itemH = 26.0f * scale;
        float menuH = 4 * itemH + 12.0f * scale;

        float menuX = state.secondaryToolbarRect.left + 50.0f * scale;
        float menuY = state.secondaryToolbarRect.bottom + 6.0f * scale;
        auto sz = m_renderTarget->GetSize();
        if (menuY + menuH > sz.height - 4.0f * scale) {
            menuY = state.secondaryToolbarRect.top - menuH - 6.0f * scale;
        }

        auto menuRect = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
        state.openSubmenuRect = menuRect;
        state.submenuButtons.clear();

        drawGlassPanel(menuRect, 8.0f * scale, false);

        ComPtr<ID2D1SolidColorBrush> itemHoverBg, itemActiveBg, textBrush, lineBrush, activeLineBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f), itemHoverBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.15f), itemActiveBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f), textBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.22f, 0.26f, 0.95f), lineBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), activeLineBrush.GetAddressOf());

        struct StyleItem { LineStyle style; std::wstring label; };
        std::vector<StyleItem> items = {
            { LineStyle::Solid, state.toolbarLayoutChinese ? L"实线" : L"Solid" },
            { LineStyle::Dashed, state.toolbarLayoutChinese ? L"长虚线" : L"Dashed" },
            { LineStyle::Dotted, state.toolbarLayoutChinese ? L"点虚线" : L"Dotted" },
            { LineStyle::DashDot, state.toolbarLayoutChinese ? L"点划线" : L"DashDot" },
        };

        for (size_t i = 0; i < items.size(); ++i) {
            float iy = menuY + 6.0f * scale + i * itemH;
            auto itemRect = D2D1::RectF(menuX + 6.0f * scale, iy, menuX + menuW - 6.0f * scale, iy + itemH);

            ToolbarButton sbtn;
            sbtn.command = ToolbarCommand::SelectLineStyle;
            sbtn.lineStyleParam = items[i].style;
            sbtn.rect = itemRect;
            state.submenuButtons.push_back(sbtn);

            bool isSelected = (state.currentLineStyle == items[i].style);
            bool isHovered = (state.currentCursor.x >= itemRect.left && state.currentCursor.x <= itemRect.right &&
                              state.currentCursor.y >= itemRect.top  && state.currentCursor.y <= itemRect.bottom);

            if (isSelected) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.0f * scale, 4.0f * scale), itemActiveBg.Get());
            } else if (isHovered) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.0f * scale, 4.0f * scale), itemHoverBg.Get());
            }

            // 绘制线条样式线段
            float lx1 = itemRect.left + 8.0f * scale;
            float lx2 = itemRect.left + 50.0f * scale;
            float ly = iy + itemH * 0.5f;
            auto* curLineBrush = isSelected ? activeLineBrush.Get() : lineBrush.Get();

            if (items[i].style == LineStyle::Solid) {
                m_renderTarget->DrawLine(D2D1::Point2F(lx1, ly), D2D1::Point2F(lx2, ly), curLineBrush, 2.0f * scale);
            } else if (items[i].style == LineStyle::Dashed) {
                float seg = 6.0f * scale, sp = 4.0f * scale;
                for (float cx = lx1; cx < lx2; cx += seg + sp) {
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, ly), D2D1::Point2F(std::min(lx2, cx + seg), ly), curLineBrush, 2.0f * scale);
                }
            } else if (items[i].style == LineStyle::Dotted) {
                float seg = 2.0f * scale, sp = 3.0f * scale;
                for (float cx = lx1; cx < lx2; cx += seg + sp) {
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, ly), D2D1::Point2F(std::min(lx2, cx + seg), ly), curLineBrush, 2.0f * scale);
                }
            } else if (items[i].style == LineStyle::DashDot) {
                float cx = lx1;
                while (cx < lx2) {
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, ly), D2D1::Point2F(std::min(lx2, cx + 6.0f * scale), ly), curLineBrush, 2.0f * scale);
                    cx += 9.0f * scale;
                    if (cx < lx2) {
                        m_renderTarget->DrawLine(D2D1::Point2F(cx, ly), D2D1::Point2F(std::min(lx2, cx + 2.0f * scale), ly), curLineBrush, 2.0f * scale);
                        cx += 5.0f * scale;
                    }
                }
            }

            // 绘制文字说明
            if (m_infoTextFormat && textBrush) {
                m_renderTarget->DrawText(items[i].label.c_str(), static_cast<UINT32>(items[i].label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(lx2 + 8.0f * scale, iy + 4.0f * scale, itemRect.right, itemRect.bottom),
                    textBrush.Get());
            }
        }
    }
    // 箭头样式下拉菜单 (标准单向、细线折角、双向箭头)
    else if (state.openSubmenu == SubmenuType::ArrowStyle) {
        float menuW = 120.0f * scale;
        float itemH = 26.0f * scale;
        float menuH = 3 * itemH + 12.0f * scale;

        float menuX = state.secondaryToolbarRect.left + 10.0f * scale;
        float menuY = state.secondaryToolbarRect.bottom + 6.0f * scale;
        auto sz = m_renderTarget->GetSize();
        if (menuY + menuH > sz.height - 4.0f * scale) {
            menuY = state.secondaryToolbarRect.top - menuH - 6.0f * scale;
        }

        auto menuRect = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
        state.openSubmenuRect = menuRect;
        state.submenuButtons.clear();

        drawGlassPanel(menuRect, 8.0f * scale, false);

        ComPtr<ID2D1SolidColorBrush> itemHoverBg, itemActiveBg, textBrush, lineBrush, activeLineBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f), itemHoverBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.15f), itemActiveBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f), textBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.22f, 0.26f, 0.95f), lineBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), activeLineBrush.GetAddressOf());

        struct ArrowItem { ArrowStyle style; std::wstring label; };
        std::vector<ArrowItem> items = {
            { ArrowStyle::Standard, state.toolbarLayoutChinese ? L"标准单向" : L"Standard" },
            { ArrowStyle::Thin, state.toolbarLayoutChinese ? L"细线箭头" : L"Thin" },
            { ArrowStyle::DoubleEnded, state.toolbarLayoutChinese ? L"双向箭头" : L"Double" },
        };

        for (size_t i = 0; i < items.size(); ++i) {
            float iy = menuY + 6.0f * scale + i * itemH;
            auto itemRect = D2D1::RectF(menuX + 6.0f * scale, iy, menuX + menuW - 6.0f * scale, iy + itemH);

            ToolbarButton sbtn;
            sbtn.command = ToolbarCommand::SelectArrowStyle;
            sbtn.arrowStyleParam = items[i].style;
            sbtn.rect = itemRect;
            state.submenuButtons.push_back(sbtn);

            bool isSelected = (state.currentArrowStyle == items[i].style);
            bool isHovered = (state.currentCursor.x >= itemRect.left && state.currentCursor.x <= itemRect.right &&
                              state.currentCursor.y >= itemRect.top  && state.currentCursor.y <= itemRect.bottom);

            if (isSelected) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.0f * scale, 4.0f * scale), itemActiveBg.Get());
            } else if (isHovered) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.0f * scale, 4.0f * scale), itemHoverBg.Get());
            }

            if (m_infoTextFormat && textBrush) {
                m_renderTarget->DrawText(items[i].label.c_str(), static_cast<UINT32>(items[i].label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(itemRect.left + 8.0f * scale, iy + 4.0f * scale, itemRect.right, itemRect.bottom),
                    isSelected ? activeLineBrush.Get() : textBrush.Get());
            }
        }
    }
}

void CaptureRenderer::drawVectorButtonIcon(const ToolbarButton& button, const D2D1_RECT_F& rect, ID2D1Brush* brush, float scale) {
    if (!m_renderTarget || !brush) return;

    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;

    switch (button.command) {
        case ToolbarCommand::Confirm: {
            // 黄金比例平滑对勾 ✓（放大饱满）
            D2D1_POINT_2F p1{cx - 5.5f * scale, cy + 0.2f * scale};
            D2D1_POINT_2F p2{cx - 1.5f * scale, cy + 4.8f * scale};
            D2D1_POINT_2F p3{cx + 6.2f * scale, cy - 4.8f * scale};
            m_renderTarget->DrawLine(p1, p2, brush, 2.0f * scale);
            m_renderTarget->DrawLine(p2, p3, brush, 2.0f * scale);
            return;
        }
        case ToolbarCommand::Cancel: {
            // 精致对称叉号 ✕
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.8f * scale, cy - 4.8f * scale),
                                     D2D1::Point2F(cx + 4.8f * scale, cy + 4.8f * scale), brush, 2.0f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.8f * scale, cy - 4.8f * scale),
                                     D2D1::Point2F(cx - 4.8f * scale, cy + 4.8f * scale), brush, 2.0f * scale);
            return;
        }
        case ToolbarCommand::Undo: {
            // 撤销平滑回折圆弧 ↩
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.8f * scale, cy + 4.8f * scale),
                                     D2D1::Point2F(cx + 4.8f * scale, cy - 1.2f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.8f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx - 4.2f * scale, cy - 1.2f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.2f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx - 0.8f * scale, cy - 5.0f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.2f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx - 0.8f * scale, cy + 2.6f * scale), brush, 1.8f * scale);
            return;
        }
        case ToolbarCommand::Redo: {
            // 重做平滑回折圆弧 ↪
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.8f * scale, cy + 4.8f * scale),
                                     D2D1::Point2F(cx - 4.8f * scale, cy - 1.2f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.8f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx + 4.2f * scale, cy - 1.2f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.2f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx + 0.8f * scale, cy - 5.0f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.2f * scale, cy - 1.2f * scale),
                                     D2D1::Point2F(cx + 0.8f * scale, cy + 2.6f * scale), brush, 1.8f * scale);
            return;
        }
        case ToolbarCommand::Clear: {
            // 精致垃圾桶 🗑️
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 6.0f * scale, cy - 4.2f * scale),
                                     D2D1::Point2F(cx + 6.0f * scale, cy - 4.2f * scale), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.0f * scale, cy - 6.0f * scale),
                                     D2D1::Point2F(cx + 2.0f * scale, cy - 6.0f * scale), brush, 1.5f * scale);
            auto bin = D2D1::RectF(cx - 4.8f * scale, cy - 2.8f * scale, cx + 4.8f * scale, cy + 6.2f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bin, 1.6f * scale, 1.6f * scale), brush, 1.5f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.8f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx - 1.8f * scale, cy + 4.2f * scale), brush, 1.3f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 1.8f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx + 1.8f * scale, cy + 4.2f * scale), brush, 1.3f * scale);
            return;
        }
        case ToolbarCommand::ToggleCornerRadius: {
            // 选区圆角调节图标 ╭╮
            auto box = D2D1::RectF(cx - 6.5f * scale, cy - 5.2f * scale, cx + 6.5f * scale, cy + 5.2f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 4.0f * scale, 4.0f * scale), brush, 1.8f * scale);
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.6f * scale, 1.6f * scale), brush);
            return;
        }
        case ToolbarCommand::ExtractText: {
            // 现代 OCR 扫描框 [ A ]
            float r = 6.4f * scale;
            float arm = 2.6f * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy - r + arm), D2D1::Point2F(cx - r, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx - r + arm, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r - arm, cy - r), D2D1::Point2F(cx + r, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx + r, cy - r + arm), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy + r - arm), D2D1::Point2F(cx - r, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx - r + arm, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r - arm, cy + r), D2D1::Point2F(cx + r, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r, cy + r), D2D1::Point2F(cx + r, cy + r - arm), brush, 1.6f * scale);

            m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.8f * scale, cy + 3.4f * scale), D2D1::Point2F(cx, cy - 3.4f * scale), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 3.4f * scale), D2D1::Point2F(cx + 2.8f * scale, cy + 3.4f * scale), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.5f * scale, cy + 0.8f * scale), D2D1::Point2F(cx + 1.5f * scale, cy + 0.8f * scale), brush, 1.4f * scale);
            return;
        }
        case ToolbarCommand::PinWindow: {
            // 现代 45° 办公金属图钉 📌
            D2D1_POINT_2F pTip{cx - 5.8f * scale, cy + 5.8f * scale};
            D2D1_POINT_2F pBase{cx - 1.6f * scale, cy + 1.6f * scale};
            m_renderTarget->DrawLine(pTip, pBase, brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(pBase.x - 3.4f * scale, pBase.y - 1.0f * scale),
                                     D2D1::Point2F(pBase.x + 1.0f * scale, pBase.y + 3.4f * scale), brush, 2.2f * scale);
            m_renderTarget->DrawLine(pBase, D2D1::Point2F(cx + 4.0f * scale, cy - 4.0f * scale), brush, 2.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 1.6f * scale, cy - 6.4f * scale),
                                     D2D1::Point2F(cx + 6.4f * scale, cy - 1.6f * scale), brush, 2.2f * scale);
            return;
        }
        case ToolbarCommand::ScrollCapture: {
            // 长截图 ⇊ (展开双层页面 + 向下贯穿箭头)
            auto page = D2D1::RectF(cx - 5.5f * scale, cy - 6.5f * scale, cx + 5.5f * scale, cy + 1.5f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(page, 1.6f * scale, 1.6f * scale), brush, 1.5f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 2.0f * scale),
                                     D2D1::Point2F(cx, cy + 6.2f * scale), brush, 2.0f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy + 6.2f * scale),
                                     D2D1::Point2F(cx - 3.2f * scale, cy + 3.0f * scale), brush, 2.0f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy + 6.2f * scale),
                                     D2D1::Point2F(cx + 3.2f * scale, cy + 3.0f * scale), brush, 2.0f * scale);
            return;
        }
        case ToolbarCommand::SelectTool: {
            switch (button.tool) {
                case MarkupTool::Rectangle: {
                    auto box = D2D1::RectF(cx - 6.8f * scale, cy - 5.2f * scale, cx + 6.8f * scale, cy + 5.2f * scale);
                    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush, 1.8f * scale);
                    return;
                }
                case MarkupTool::Ellipse: {
                    auto ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), 7.0f * scale, 5.5f * scale);
                    m_renderTarget->DrawEllipse(ellipse, brush, 1.8f * scale);
                    return;
                }
                case MarkupTool::Arrow: {
                    D2D1_POINT_2F pStart{cx - 5.8f * scale, cy + 5.8f * scale};
                    D2D1_POINT_2F pEnd{cx + 5.5f * scale, cy - 5.5f * scale};
                    m_renderTarget->DrawLine(pStart, pEnd, brush, 2.0f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 1.2f * scale, cy - 5.5f * scale), pEnd, brush, 2.0f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 5.5f * scale, cy - 1.2f * scale), pEnd, brush, 2.0f * scale);
                    return;
                }
                case MarkupTool::Pen: {
                    D2D1_POINT_2F tip{cx - 6.2f * scale, cy + 6.2f * scale};
                    D2D1_POINT_2F b1{cx - 3.8f * scale, cy + 6.2f * scale};
                    D2D1_POINT_2F b2{cx - 6.2f * scale, cy + 3.8f * scale};
                    m_renderTarget->DrawLine(tip, b1, brush, 1.6f * scale);
                    m_renderTarget->DrawLine(tip, b2, brush, 1.6f * scale);
                    m_renderTarget->DrawLine(b1, D2D1::Point2F(cx + 5.5f * scale, cy - 3.0f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(b2, D2D1::Point2F(cx + 3.0f * scale, cy - 5.5f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 5.5f * scale, cy - 3.0f * scale),
                                             D2D1::Point2F(cx + 3.0f * scale, cy - 5.5f * scale), brush, 1.6f * scale);
                    return;
                }
                case MarkupTool::Highlight: {
                    auto tipBox = D2D1::RectF(cx - 6.5f * scale, cy - 1.5f * scale, cx + 6.5f * scale, cy + 4.2f * scale);
                    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(tipBox, 1.2f * scale, 1.2f * scale), brush);
                    D2D1_POINT_2F p1{cx - 3.5f * scale, cy - 5.8f * scale};
                    D2D1_POINT_2F p2{cx + 5.5f * scale, cy - 2.2f * scale};
                    m_renderTarget->DrawLine(p1, p2, brush, 2.4f * scale);
                    return;
                }
                case MarkupTool::Mosaic: {
                    float s = 3.2f * scale;
                    float g = 0.8f * scale;
                    for (int row = -1; row <= 1; ++row) {
                        for (int col = -1; col <= 1; ++col) {
                            float x = cx + col * (s + g) - s * 0.5f;
                            float y = cy + row * (s + g) - s * 0.5f;
                            auto cell = D2D1::RectF(x, y, x + s, y + s);
                            if ((row + col) % 2 == 0) {
                                m_renderTarget->FillRectangle(cell, brush);
                            } else {
                                m_renderTarget->DrawRectangle(cell, brush, 1.0f * scale);
                            }
                        }
                    }
                    return;
                }
                case MarkupTool::Text: {
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 6.0f * scale, cy - 5.8f * scale),
                                             D2D1::Point2F(cx + 6.0f * scale, cy - 5.8f * scale), brush, 2.0f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 5.8f * scale),
                                             D2D1::Point2F(cx, cy + 6.0f * scale), brush, 2.0f * scale);
                    return;
                }
                case MarkupTool::Number: {
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.8f * scale, 6.8f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.8f * scale, cy - 1.8f * scale),
                                             D2D1::Point2F(cx, cy - 3.6f * scale), brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 3.6f * scale),
                                             D2D1::Point2F(cx, cy + 3.6f * scale), brush, 2.0f * scale);
                    return;
                }
                case MarkupTool::Magnifier: {
                    D2D1_POINT_2F mCenter{cx - 1.8f * scale, cy - 1.8f * scale};
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(mCenter, 5.0f * scale, 5.0f * scale), brush, 1.8f * scale);
                    D2D1_POINT_2F hStart{cx + 2.0f * scale, cy + 2.0f * scale};
                    D2D1_POINT_2F hEnd{cx + 6.6f * scale, cy + 6.6f * scale};
                    m_renderTarget->DrawLine(hStart, hEnd, brush, 2.5f * scale);
                    return;
                }
                case MarkupTool::Spotlight: {
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 3.2f * scale, 3.2f * scale), brush, 1.6f * scale);
                    for (int a = 0; a < 8; ++a) {
                        float rad = a * 3.14159265f / 4.0f;
                        float x1 = cx + std::cos(rad) * 4.8f * scale;
                        float y1 = cy + std::sin(rad) * 4.8f * scale;
                        float x2 = cx + std::cos(rad) * 7.0f * scale;
                        float y2 = cy + std::sin(rad) * 7.0f * scale;
                        m_renderTarget->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, 1.4f * scale);
                    }
                    return;
                }
                case MarkupTool::Watermark: {
                    auto box = D2D1::RectF(cx - 6.5f * scale, cy - 5.2f * scale, cx + 6.5f * scale, cy + 5.2f * scale);
                    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 3.8f * scale, cy - 1.2f * scale),
                                             D2D1::Point2F(cx + 3.8f * scale, cy - 1.2f * scale), brush, 1.3f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.5f * scale, cy + 2.0f * scale),
                                             D2D1::Point2F(cx + 2.5f * scale, cy + 2.0f * scale), brush, 1.3f * scale);
                    return;
                }
                case MarkupTool::Inpaint: {
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 5.5f * scale, cy + 6.0f * scale),
                                             D2D1::Point2F(cx + 1.5f * scale, cy - 1.0f * scale), brush, 2.2f * scale);
                    D2D1_POINT_2F sCenter{cx + 4.2f * scale, cy - 4.2f * scale};
                    m_renderTarget->DrawLine(D2D1::Point2F(sCenter.x, sCenter.y - 3.6f * scale),
                                             D2D1::Point2F(sCenter.x, sCenter.y + 3.6f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(sCenter.x - 3.6f * scale, sCenter.y),
                                             D2D1::Point2F(sCenter.x + 3.6f * scale, sCenter.y), brush, 1.6f * scale);
                    return;
                }
                default: break;
            }
            break;
        }
        case ToolbarCommand::ToggleFill: {
            // 填充 Toggle 图标与文字
            float bW = 12.0f * scale;
            float bH = 10.0f * scale;
            float bx = cx - (button.label.empty() ? 0.0f : 12.0f * scale);
            auto box = D2D1::RectF(bx - bW * 0.5f, cy - bH * 0.5f, bx + bW * 0.5f, cy + bH * 0.5f);
            if (button.boolParam) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush);
            } else {
                m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush, 1.5f * scale);
            }
            if (!button.label.empty() && m_infoTextFormat) {
                m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(bx + bW * 0.5f + 3.0f * scale, cy - 8.0f * scale, rect.right, cy + 8.0f * scale),
                    brush);
            }
            return;
        }
        case ToolbarCommand::ToggleLineStyleDropdown: {
            // 线条样式小样
            float lx1 = rect.left + 5.0f * scale;
            float lx2 = rect.right - 12.0f * scale;
            float ly = cy;
            if (button.lineStyleParam == LineStyle::Solid) {
                m_renderTarget->DrawLine(D2D1::Point2F(lx1, ly), D2D1::Point2F(lx2, ly), brush, 2.0f * scale);
            } else if (button.lineStyleParam == LineStyle::Dashed) {
                float seg = 5.0f * scale, sp = 3.0f * scale;
                for (float sx = lx1; sx < lx2; sx += seg + sp) {
                    m_renderTarget->DrawLine(D2D1::Point2F(sx, ly), D2D1::Point2F(std::min(lx2, sx + seg), ly), brush, 2.0f * scale);
                }
            } else if (button.lineStyleParam == LineStyle::Dotted) {
                float seg = 2.0f * scale, sp = 3.0f * scale;
                for (float sx = lx1; sx < lx2; sx += seg + sp) {
                    m_renderTarget->DrawLine(D2D1::Point2F(sx, ly), D2D1::Point2F(std::min(lx2, sx + seg), ly), brush, 2.0f * scale);
                }
            } else if (button.lineStyleParam == LineStyle::DashDot) {
                float sx = lx1;
                while (sx < lx2) {
                    m_renderTarget->DrawLine(D2D1::Point2F(sx, ly), D2D1::Point2F(std::min(lx2, sx + 5.0f * scale), ly), brush, 2.0f * scale);
                    sx += 7.0f * scale;
                    if (sx < lx2) {
                        m_renderTarget->DrawLine(D2D1::Point2F(sx, ly), D2D1::Point2F(std::min(lx2, sx + 2.0f * scale), ly), brush, 2.0f * scale);
                        sx += 4.0f * scale;
                    }
                }
            }
            return;
        }
        case ToolbarCommand::ToggleArrowStyleDropdown: {
            // 箭头样式小样
            if (button.arrowStyleParam == ArrowStyle::DoubleEnded) {
                m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 8.0f * scale, cy), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 11.0f * scale, cy - 2.5f * scale), D2D1::Point2F(rect.left + 8.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 11.0f * scale, cy + 2.5f * scale), D2D1::Point2F(rect.left + 8.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.right - 17.0f * scale, cy - 2.5f * scale), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.right - 17.0f * scale, cy + 2.5f * scale), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
            } else {
                m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 6.0f * scale, cy), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.right - 17.0f * scale, cy - 3.0f * scale), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(rect.right - 17.0f * scale, cy + 3.0f * scale), D2D1::Point2F(rect.right - 14.0f * scale, cy), brush, 1.8f * scale);
            }
            return;
        }
        case ToolbarCommand::CycleStrokeWidth: {
            // 线宽图标 ☰ 与数值
            m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 6.0f * scale, cy - 3.5f * scale),
                                     D2D1::Point2F(rect.left + 13.0f * scale, cy - 3.5f * scale), brush, 1.2f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 6.0f * scale, cy),
                                     D2D1::Point2F(rect.left + 13.0f * scale, cy), brush, 2.0f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(rect.left + 6.0f * scale, cy + 3.8f * scale),
                                     D2D1::Point2F(rect.left + 13.0f * scale, cy + 3.8f * scale), brush, 3.0f * scale);
            if (!button.label.empty() && m_infoTextFormat) {
                m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(rect.left + 16.0f * scale, cy - 8.0f * scale, rect.right - 2.0f * scale, cy + 8.0f * scale),
                    brush);
            }
            return;
        }
        case ToolbarCommand::CycleElementCornerRadius: {
            // 标注圆角矢量图标 ╭ 与数值
            float ax = rect.left + 9.0f * scale;
            float ay = cy;
            m_renderTarget->DrawLine(D2D1::Point2F(ax, ay + 4.0f * scale), D2D1::Point2F(ax, ay), brush, 1.4f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax + 4.0f * scale, ay), brush, 1.4f * scale);
            if (!button.label.empty() && m_infoTextFormat) {
                m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(rect.left + 16.0f * scale, cy - 8.0f * scale, rect.right - 2.0f * scale, cy + 8.0f * scale),
                    brush);
            }
            return;
        }
        case ToolbarCommand::SelectMosaicType: {
            if (!button.label.empty() && m_infoTextFormat) {
                m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(rect.left, cy - 8.0f * scale, rect.right, cy + 8.0f * scale),
                    brush);
            }
            return;
        }
        default: break;
    }
}

void CaptureRenderer::drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough) {
    if (!m_renderTarget) return;
    (void)seeThrough;

    // 1. 多层环境光柔和微阴影（Ambient Soft Drop Shadow）
    ComPtr<ID2D1SolidColorBrush> shadow1, shadow2;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.035f), shadow1.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.065f), shadow2.GetAddressOf());

    if (shadow1) {
        auto shadowRect1 = D2D1::RectF(rect.left - 2.5f, rect.top - 1.0f, rect.right + 2.5f, rect.bottom + 4.5f);
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(shadowRect1, radius + 2.0f, radius + 2.0f), shadow1.Get());
    }
    if (shadow2) {
        auto shadowRect2 = D2D1::RectF(rect.left - 1.0f, rect.top + 0.5f, rect.right + 1.0f, rect.bottom + 2.5f);
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(shadowRect2, radius + 1.0f, radius + 1.0f), shadow2.Get());
    }

    auto rr = D2D1::RoundedRect(rect, radius, radius);

    // 2. 纯白高级磨砂亚克力主体（Pure White Matte Glass Body）
    ComPtr<ID2D1SolidColorBrush> tint, sheen, border;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.96f), tint.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), sheen.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f), border.GetAddressOf());

    if (tint) m_renderTarget->FillRoundedRectangle(rr, tint.Get());

    // 3. 顶部 1px 极细微晶反光线条（Top Edge Specular Sheen）
    if (sheen) {
        m_renderTarget->DrawLine(
            D2D1::Point2F(rect.left + radius, rect.top + 1.0f),
            D2D1::Point2F(rect.right - radius, rect.top + 1.0f), sheen.Get(), 1.0f);
    }
    // 4. 1px 极细微灰色外描边
    if (border) {
        m_renderTarget->DrawRoundedRectangle(rr, border.Get(), 1.0f);
    }
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

        if (m_markupCacheBitmap) {
            auto sz = m_markupCacheBitmap->GetPixelSize();
            if (sz.width == static_cast<UINT32>(bgra.cols) && sz.height == static_cast<UINT32>(bgra.rows)) {
                m_markupCacheBitmap->CopyFromMemory(nullptr, bgra.data, static_cast<UINT32>(bgra.cols * 4));
                m_markupCacheDirty = false;
            } else {
                m_markupCacheBitmap.Reset();
            }
        }

        if (!m_markupCacheBitmap) {
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
    constexpr float zoom = 8.0f; 
    
    // 宽敞舒适的黄金比例：27x13 点阵(宽216px)，全面适配大号清晰字体，彻底杜绝 CMYK 等长文本溢出
    int gridCountX = 27;
    int gridCountY = 13;
    float loupeBoxW = gridCountX * zoom * scale; // 216px * scale (极致宽敞，长色彩格式两端留白舒展)
    float loupeBoxH = gridCountY * zoom * scale; // 104px * scale (上半区开阔像素观察区)
    float panelH = 82.0f * scale;                // 82px * scale (下半区大号字体舒展排版，上下 104:82 黄金比例)
    const float totalH = loupeBoxH + panelH;
    const float pad = 12.0f * scale;

    auto size = m_renderTarget->GetSize();

    float lx = cx + 22.0f * scale;
    float ly = cy + 22.0f * scale;

    if (lx + loupeBoxW + pad > size.width) lx = cx - 22.0f * scale - loupeBoxW;
    if (ly + totalH + pad > size.height) ly = cy - 22.0f * scale - totalH;
    lx = std::clamp(lx, pad, std::max(pad, size.width - loupeBoxW - pad));
    ly = std::clamp(ly, pad, std::max(pad, size.height - totalH - pad));

    D2D1_RECT_F dst = D2D1::RectF(lx, ly, lx + loupeBoxW, ly + loupeBoxH);
    
    float srcHalfX = gridCountX / 2.0f; 
    float srcHalfY = gridCountY / 2.0f; 
    D2D1_RECT_F src = D2D1::RectF(cx - srcHalfX, cy - srcHalfY, cx + srcHalfX, cy + srcHalfY);
    
    m_renderTarget->DrawBitmap(m_screenBitmap.Get(), dst, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);

    // 1. 网格细线
    ComPtr<ID2D1SolidColorBrush> gridBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.7f, 0.28f), gridBrush.GetAddressOf());
    if (gridBrush) {
        for (int i = 0; i <= gridCountY; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx, ly + lineOffset), D2D1::Point2F(lx + loupeBoxW, ly + lineOffset), gridBrush.Get(), 0.8f);
        }
        for (int i = 0; i <= gridCountX; ++i) {
            float lineOffset = i * zoom * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(lx + lineOffset, ly), D2D1::Point2F(lx + lineOffset, ly + loupeBoxH), gridBrush.Get(), 0.8f);
        }
    }

    // 2. 中心十字交叉蓝带 (PixPin 经典科技蓝)
    ComPtr<ID2D1SolidColorBrush> blueCrosshair;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.80f, 0.65f), blueCrosshair.GetAddressOf());
    
    float pixelSz = zoom * scale;
    float mcx = lx + (gridCountX / 2) * pixelSz; 
    float mcy = ly + (gridCountY / 2) * pixelSz; 

    if (blueCrosshair) {
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, ly, mcx + pixelSz, mcy), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx, mcy + pixelSz, mcx + pixelSz, ly + loupeBoxH), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(lx, mcy, mcx, mcy + pixelSz), blueCrosshair.Get());
        m_renderTarget->FillRectangle(D2D1::RectF(mcx + pixelSz, mcy, lx + loupeBoxW, mcy + pixelSz), blueCrosshair.Get());
    }

    // 3. 基础画刷定义 (纯白、纯黑、提示灰)
    ComPtr<ID2D1SolidColorBrush> whiteBrush, blackBrush, whiteTextBrush, hintTextBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.08f, 1.0f), blackBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteTextBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f), hintTextBrush.GetAddressOf());

    // 4. 中心单像素目标外框 (双层黑白高反差框，确保在任何背景下绝对聚焦)
    if (whiteBrush && blackBrush) {
        D2D1_RECT_F centerCell = D2D1::RectF(mcx, mcy, mcx + pixelSz, mcy + pixelSz);
        m_renderTarget->DrawRectangle(centerCell, blackBrush.Get(), 2.0f);
        m_renderTarget->DrawRectangle(centerCell, whiteBrush.Get(), 1.2f);
    }
    
    // 5. 下半部分信息面板背景 (先填充纯黑深邃背景)
    D2D1_RECT_F panelRect = D2D1::RectF(lx, ly + loupeBoxH, lx + loupeBoxW, ly + totalH);
    ComPtr<ID2D1SolidColorBrush> panelBg;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), panelBg.GetAddressOf());
    if (panelBg) {
        m_renderTarget->FillRectangle(panelRect, panelBg.Get());
    }
    if (blackBrush) {
        m_renderTarget->DrawRectangle(panelRect, blackBrush.Get(), 1.0f * scale);
    }

    // 6. 上半区网格专属恒定双层微边框（外层 1px 细黑 + 内层 1px 纯白，完整闭合，包含上下半框交界底线）
    if (whiteBrush && blackBrush) {
        D2D1_RECT_F outerBlackRect = D2D1::RectF(dst.left - 1.0f * scale, dst.top - 1.0f * scale, dst.right + 1.0f * scale, dst.bottom + 1.0f * scale);
        m_renderTarget->DrawRectangle(outerBlackRect, blackBrush.Get(), 1.0f * scale);
        m_renderTarget->DrawRectangle(dst, whiteBrush.Get(), 1.0f * scale);
        // 显式加固上下半区交界处的底边白线，确保 100% 完整清晰
        m_renderTarget->DrawLine(D2D1::Point2F(dst.left, dst.bottom), D2D1::Point2F(dst.right, dst.bottom), whiteBrush.Get(), 1.0f * scale);
    }

    // 7. 纯白大号文字与色块排版 (严格单行不换行，极简世界级排版)
    if (hasColor && m_infoTextFormat && m_dwriteFactory) {
        m_infoTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        auto formats = getAllColorFormats(r, g, b);
        size_t curIdx = static_cast<size_t>(state.colorFormat) % formats.size();
        const auto& curEntry = formats[curIdx];

        std::wstring text1 = std::format(L"({} , {})", px, py);
        // 去除大写前缀，直接显示纯净颜色格式值（例如 rgb(58, 134, 255) 或 cmyk(100%, 100%, 100%, 100%)）
        std::wstring text2 = curEntry.value;
        std::wstring text3 = L"Shift: 切换颜色格式";
        
        bool toast = (state.loupeToastUntil != 0 && GetTickCount() < state.loupeToastUntil);
        std::wstring text4 = toast ? L"✓ 已复制到剪贴板!" : L"C: 复制颜色值";

        float tx = lx + 6.0f * scale;
        float tw = loupeBoxW - 12.0f * scale;
        float currY = ly + loupeBoxH + 4.0f * scale;
        
        // 第 1 行：坐标 (大号纯白居中)
        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_renderTarget->DrawTextW(text1.c_str(), (UINT32)text1.size(), m_infoTextFormat.Get(),
            D2D1::RectF(tx, currY, tx + tw, currY + 17.0f * scale), whiteTextBrush.Get());
        currY += 19.0f * scale;

        // 第 2 行：当前颜色值 (大号采样色块 + 纯净颜色值，无溢出舒展排版)
        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        ComPtr<IDWriteTextLayout> layout;
        m_dwriteFactory->CreateTextLayout(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(), 1000.0f, 20.0f * scale, layout.GetAddressOf());
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
            float boxY = currY + (18.0f * scale - boxSz) / 2.0f;
            D2D1_RECT_F colorRect = D2D1::RectF(startX, boxY, startX + boxSz, boxY + boxSz);
            m_renderTarget->FillRectangle(colorRect, colorBox.Get());
            if (whiteBrush) {
                m_renderTarget->DrawRectangle(colorRect, whiteBrush.Get(), 1.0f * scale);
            }
            
            m_renderTarget->DrawTextW(text2.c_str(), (UINT32)text2.size(), m_infoTextFormat.Get(),
                D2D1::RectF(startX + boxSz + gap, currY, startX + totalBlockW + 20.0f * scale, currY + 18.0f * scale), whiteTextBrush.Get());
        }
        currY += 20.0f * scale;

        // 第 3 行：Shift 切换操作指引 (Shift: 切换颜色格式)
        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (hintTextBrush) {
            m_renderTarget->DrawTextW(text3.c_str(), (UINT32)text3.size(), m_infoTextFormat.Get(),
                D2D1::RectF(tx, currY, tx + tw, currY + 17.0f * scale), hintTextBrush.Get());
        }
        currY += 18.0f * scale;

        // 第 4 行：C 复制状态 (大号居中 / 复制成功亮绿)
        ComPtr<ID2D1SolidColorBrush> actionBrush;
        if (toast) {
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.90f, 0.55f, 1.0f), actionBrush.GetAddressOf());
        } else {
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f), actionBrush.GetAddressOf());
        }
        if (actionBrush) {
            m_renderTarget->DrawTextW(text4.c_str(), (UINT32)text4.size(), m_infoTextFormat.Get(),
                D2D1::RectF(tx, currY, tx + tw, currY + 17.0f * scale), actionBrush.Get());
        }
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
        drawSizeMenu(state.sizeHudRect, state);
    } else {
        // 空闲/选区前 — 全屏变暗 + 十字准星 + 窗口高亮
        auto size = m_renderTarget->GetSize();
        m_renderTarget->FillRectangle(
            D2D1::RectF(0, 0, size.width, size.height), m_dimBrush.Get()
        );

        // 检测光标下的窗口并高亮其边界（采用 Windows 11 现代圆角贴合）
        if (state.detectedWindow.right > state.detectedWindow.left && 
            state.detectedWindow.bottom > state.detectedWindow.top) {
            D2D1_RECT_F winRect = D2D1::RectF(
                static_cast<float>(state.detectedWindow.left),
                static_cast<float>(state.detectedWindow.top),
                static_cast<float>(state.detectedWindow.right),
                static_cast<float>(state.detectedWindow.bottom)
            );
            
            float scale = (state.dpiScale > 0.0f) ? state.dpiScale : 1.0f;
            float r = state.detectedWindowCornerRadius * scale;
            auto roundedWin = D2D1::RoundedRect(winRect, r, r);

            // 窗口区域采用圆角几何裁剪透出原始画面（去掉变暗效果）
            if (m_screenBitmap && m_d2dFactory) {
                ComPtr<ID2D1RoundedRectangleGeometry> geo;
                if (SUCCEEDED(m_d2dFactory->CreateRoundedRectangleGeometry(roundedWin, geo.GetAddressOf())) && geo) {
                    ComPtr<ID2D1Layer> layer;
                    if (SUCCEEDED(m_renderTarget->CreateLayer(layer.GetAddressOf())) && layer) {
                        m_renderTarget->PushLayer(
                            D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get(),
                                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                1.0f, nullptr, D2D1_LAYER_OPTIONS_NONE),
                            layer.Get());
                        m_renderTarget->DrawBitmap(m_screenBitmap.Get(), winRect, 1.0f,
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &winRect);
                        m_renderTarget->PopLayer();
                    }
                }
            }
            // 紫色优雅圆角高亮外框
            m_renderTarget->DrawRoundedRectangle(roundedWin, m_borderBrush.Get(), 2.0f * scale);
            // 显示窗口尺寸与圆角提示
            drawSizeInfo(winRect, state);
        }

        if (state.options.showCrosshair) {
            drawCrosshair(static_cast<float>(state.currentCursor.x),
                          static_cast<float>(state.currentCursor.y));
        }
        // 预选悬停时显示像素级取色放大镜（坐标 + RGB/HEX，按 C 复制）
        drawSelectionLoupe(static_cast<float>(state.currentCursor.x), static_cast<float>(state.currentCursor.y), state);
    }

    
    m_renderTarget->EndDraw();
}

} // namespace easy::capture
