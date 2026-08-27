#include "capture/CaptureRenderer.h"
#include "capture/CaptureVectorIcons.h"
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
        case ToolbarCommand::SideToggleCornerRadius: return chinese ? L"调节选区圆角半径" : L"Selection Corner Radius";
        case ToolbarCommand::SideInvertSelection: return chinese ? L"反向选择 / 选区扩展" : L"Invert Selection";
        case ToolbarCommand::SideResetSelection: return chinese ? L"重置选区直角 (0px)" : L"Reset Corner Radius";
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

    // 3. Figma / PixPin 级内侧圆角调节手柄（小选区内部彻底隐藏手柄，避免遮挡微型内容，改由外部工具栏调节）
    float selW = rect.right - rect.left;
    float selH = rect.bottom - rect.top;
    if (selW >= 110.0f * scale && selH >= 110.0f * scale) {
        float offset = std::clamp(std::max(22.0f, state.cornerRadius + 8.0f), 16.0f, std::min(selW, selH) * 0.40f) * scale;
        D2D1_POINT_2F cornerPts[4] = {
            {rect.left + offset,  rect.top + offset},
            {rect.right - offset, rect.top + offset},
            {rect.right - offset, rect.bottom - offset},
            {rect.left + offset,  rect.bottom - offset}
        };

        // 判定鼠标是否靠近角部（小选区自适应收敛为单一左上角主手柄，彻底杜绝临界跳动）
        const float triggerDist = 36.0f * scale;
        bool shouldShowCornerHandle = state.isAdjustingCornerRadius;
        int activeCornerIdx = -1;
        bool isSmallBox = (selW < 130.0f * scale || selH < 130.0f * scale);
        int checkCount = isSmallBox ? 1 : 4;

        if (state.isAdjustingCornerRadius) {
            float minD = 999999.0f;
            for (int i = 0; i < checkCount; ++i) {
                float d = std::hypot(state.cornerDragStartPos.x - cornerPts[i].x, state.cornerDragStartPos.y - cornerPts[i].y);
                if (d < minD) { minD = d; activeCornerIdx = i; }
            }
        } else {
            float minD = 999999.0f;
            for (int i = 0; i < checkCount; ++i) {
                float d = std::hypot(state.currentCursor.x - cornerPts[i].x, state.currentCursor.y - cornerPts[i].y);
                if (d <= triggerDist && d < minD) {
                    minD = d;
                    shouldShowCornerHandle = true;
                    activeCornerIdx = i;
                }
            }
        }

        // 仅在鼠标靠近或正在调整时，且仅对这唯一的活跃角进行绘制（其余 3 个角绝对隐藏）
        if (shouldShowCornerHandle && activeCornerIdx >= 0 && activeCornerIdx < 4) {
            ComPtr<ID2D1SolidColorBrush> cornerRingBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.90f), cornerRingBrush.GetAddressOf());

            float outerRingRadius = (state.isAdjustingCornerRadius ? 5.0f : 4.0f) * scale;
            float innerDotRadius = (state.isAdjustingCornerRadius ? 1.7f : 1.3f) * scale;

            const auto& cpt = cornerPts[activeCornerIdx];

            // 绘制手柄对角双向推拉微导引轨与双向指示箭头 (↖ 减小直角 / ↘ 增大圆角)
            // 根据所在角计算内向对角单位向量 (inDirX, inDirY)
            float inDirX = (activeCornerIdx == 0 || activeCornerIdx == 3) ? 0.7071f : -0.7071f;
            float inDirY = (activeCornerIdx == 0 || activeCornerIdx == 1) ? 0.7071f : -0.7071f;

            ComPtr<ID2D1SolidColorBrush> guideLineBrush, guideArrowBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.74f, 0.97f, 0.75f), guideLineBrush.GetAddressOf()); // #38BDF8 发光青天蓝
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.01f, 0.52f, 0.78f, 0.95f), guideArrowBrush.GetAddressOf()); // #0284C7 科技蓝

            float armDist = (state.isAdjustingCornerRadius ? 14.0f : 12.0f) * scale;

            if (guideLineBrush && guideArrowBrush) {
                // A. 严格物理对称的对角发光微导轨 (两端到中心黑点等长 armDist)
                D2D1_POINT_2F pOut = { cpt.x - inDirX * armDist, cpt.y - inDirY * armDist };
                D2D1_POINT_2F pIn  = { cpt.x + inDirX * armDist, cpt.y + inDirY * armDist };

                // 导轨细线 (纯白半透明底衬 + 亮青色)
                if (handleWhiteBrush) {
                    m_renderTarget->DrawLine(pOut, pIn, handleWhiteBrush.Get(), 2.0f * scale);
                }
                m_renderTarget->DrawLine(pOut, pIn, guideLineBrush.Get(), 1.2f * scale);

                // B. 外向指示箭头 (指向直角方向)
                float arrLen = 4.0f * scale;
                float perpX = -inDirY * arrLen * 0.7f;
                float perpY =  inDirX * arrLen * 0.7f;
                D2D1_POINT_2F aOut1 = { pOut.x + inDirX * arrLen + perpX, pOut.y + inDirY * arrLen + perpY };
                D2D1_POINT_2F aOut2 = { pOut.x + inDirX * arrLen - perpX, pOut.y + inDirY * arrLen - perpY };
                m_renderTarget->DrawLine(pOut, aOut1, guideArrowBrush.Get(), 1.4f * scale);
                m_renderTarget->DrawLine(pOut, aOut2, guideArrowBrush.Get(), 1.4f * scale);

                // C. 内向指示箭头 (指向圆角方向，与外向严格对称)
                D2D1_POINT_2F aIn1 = { pIn.x - inDirX * arrLen + perpX, pIn.y - inDirY * arrLen + perpY };
                D2D1_POINT_2F aIn2 = { pIn.x - inDirX * arrLen - perpX, pIn.y - inDirY * arrLen - perpY };
                m_renderTarget->DrawLine(pIn, aIn1, guideArrowBrush.Get(), 1.4f * scale);
                m_renderTarget->DrawLine(pIn, aIn2, guideArrowBrush.Get(), 1.4f * scale);
            }

            // 外圈细光环 (白底 + 蓝描边)
            auto outerEllipse = D2D1::Ellipse(cpt, outerRingRadius, outerRingRadius);
            if (handleWhiteBrush) {
                m_renderTarget->FillEllipse(outerEllipse, handleWhiteBrush.Get());
            }
            if (cornerRingBrush) {
                m_renderTarget->DrawEllipse(outerEllipse, cornerRingBrush.Get(), 1.0f * scale);
            }
            m_renderTarget->DrawEllipse(outerEllipse, m_borderBrush.Get(), 1.0f * scale);

            // 中心极客黑曜石实心微圆点（精密高反差深黑点 #0F172A，小巧精致且与纯白外环形成强烈对比）
            auto innerEllipse = D2D1::Ellipse(cpt, innerDotRadius, innerDotRadius);
            ComPtr<ID2D1SolidColorBrush> handleDarkDotBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.09f, 0.16f, 0.95f), handleDarkDotBrush.GetAddressOf());
            if (handleDarkDotBrush) {
                m_renderTarget->FillEllipse(innerEllipse, handleDarkDotBrush.Get());
            }

            // 4. PixPin 同款实时圆角半径微胶囊 [ ╭ 42 ]（适度紧凑：留白减少一半，紧凑而绝不压叠）
            int radVal = static_cast<int>(std::round(state.cornerRadius));
            std::wstring rText = std::format(L"{}", radVal);
            float badgeW = (28.0f + rText.size() * 8.0f) * scale;
            float badgeH = 20.0f * scale;
            
            // 严密计算几何距离：胶囊中心 = 手柄点 + inDir * (箭头臂长 + 胶囊自身半对角线 + 8px 紧凑留白)
            float badgeHalfDiag = std::hypot(badgeW * 0.5f, badgeH * 0.5f);
            float badgeDist = armDist + badgeHalfDiag + 8.0f * scale;
            float bcX = cpt.x + inDirX * badgeDist;
            float bcY = cpt.y + inDirY * badgeDist;
            float bx = bcX - badgeW * 0.5f;
            float by = bcY - badgeH * 0.5f;

            // 智能选区边界保护：小选区下严防胶囊溢出或被裁切
            float minBx = rect.left + 4.0f * scale;
            float maxBx = rect.right - badgeW - 4.0f * scale;
            float minBy = rect.top + 4.0f * scale;
            float maxBy = rect.bottom - badgeH - 4.0f * scale;
            if (minBx <= maxBx) bx = std::clamp(bx, minBx, maxBx);
            if (minBy <= maxBy) by = std::clamp(by, minBy, maxBy);

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
    ComPtr<ID2D1SolidColorBrush> confirmBrush;
    ComPtr<ID2D1SolidColorBrush> confirmHoverBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    ComPtr<ID2D1SolidColorBrush> iconBrush;
    ComPtr<ID2D1SolidColorBrush> activeIconBrush;
    ComPtr<ID2D1SolidColorBrush> confirmIconBrush;
    ComPtr<ID2D1SolidColorBrush> dangerIconBrush;
    ComPtr<ID2D1SolidColorBrush> tipTextBrush;
    ComPtr<ID2D1SolidColorBrush> secondaryActiveBrush;
    ComPtr<ID2D1SolidColorBrush> separatorBrush;

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), buttonBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f), hoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.12f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.20f), secondaryActiveBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.85f), activeBorderBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 1.0f), activeIconBrush.GetAddressOf());
    
    // 完成按钮：高亮品牌/翡翠绿实心药丸
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.72f, 0.44f, 0.95f), confirmBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.62f, 0.38f, 1.0f), confirmHoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), confirmIconBrush.GetAddressOf());

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.26f, 0.26f, 0.12f), dangerHoverBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 1.0f), dangerIconBrush.GetAddressOf());

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.22f, 0.27f, 0.95f), iconBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.15f, 0.95f), tipTextBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f), separatorBrush.GetAddressOf());

    auto drawButtonGroup = [&](const std::vector<ToolbarButton>& btnList) {
        for (const auto& button : btnList) {
            // 绘制前置细腻分隔线
            if (button.isSeparatorBefore && separatorBrush) {
                float sepX = button.rect.left - 4.5f * scale;
                float sepTop = button.rect.top + 5.0f * scale;
                float sepBottom = button.rect.bottom - 5.0f * scale;
                m_renderTarget->DrawLine(D2D1::Point2F(sepX, sepTop), D2D1::Point2F(sepX, sepBottom), separatorBrush.Get(), 1.0f);
            }

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
            if (button.command == ToolbarCommand::SelectMosaicType) {
                isActiveTool = (button.intParam == 0); // 默认像素马赛克激活
            }

            bool isColor = button.command == ToolbarCommand::SelectColor;
            bool isDanger = button.command == ToolbarCommand::Cancel;
            bool isConfirm = button.command == ToolbarCommand::Confirm;
            bool isStepper = (button.command == ToolbarCommand::CycleStrokeWidth ||
                              button.command == ToolbarCommand::CycleElementCornerRadius ||
                              button.command == ToolbarCommand::ToggleCornerRadius);
            bool isHovered = state.currentCursor.x >= button.rect.left && state.currentCursor.x <= button.rect.right &&
                             state.currentCursor.y >= button.rect.top  && state.currentCursor.y <= button.rect.bottom;

            auto rounded = D2D1::RoundedRect(button.rect, 6.0f * scale, 6.0f * scale);

            if (isColor) {
                // 颜色色板: 圆形色球 + 双层高亮光环
                float cx = (button.rect.left + button.rect.right) * 0.5f;
                float cy = (button.rect.top + button.rect.bottom) * 0.5f;
                float r = std::min(button.rect.right - button.rect.left, button.rect.bottom - button.rect.top) * 0.38f;
                auto colorCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);

                ComPtr<ID2D1SolidColorBrush> swatch;
                m_renderTarget->CreateSolidColorBrush(
                    D2D1::ColorF(button.color.r / 255.0f, button.color.g / 255.0f,
                                 button.color.b / 255.0f, 1.0f),
                    swatch.GetAddressOf());
                if (swatch) m_renderTarget->FillEllipse(colorCircle, swatch.Get());

                // 白色色球增加 1px 浅灰色外边缘
                if (button.color.r > 240 && button.color.g > 240 && button.color.b > 240) {
                    m_renderTarget->DrawEllipse(colorCircle, separatorBrush.Get(), 1.0f);
                }

                bool isActiveColor = (button.color.r == state.currentColor.r &&
                                     button.color.g == state.currentColor.g &&
                                     button.color.b == state.currentColor.b);
                if (isActiveColor) {
                    // 内部白色间隙隔离环 + 外部同色扩散光晕
                    auto innerRing = D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 1.2f * scale, r + 1.2f * scale);
                    ComPtr<ID2D1SolidColorBrush> whiteRing;
                    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteRing.GetAddressOf());
                    if (whiteRing) m_renderTarget->DrawEllipse(innerRing, whiteRing.Get(), 1.2f * scale);

                    auto outerRing = D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 2.8f * scale, r + 2.8f * scale);
                    if (swatch) m_renderTarget->DrawEllipse(outerRing, swatch.Get(), 1.8f * scale);
                } else if (isHovered && hoverBrush) {
                    auto outerRing = D2D1::Ellipse(D2D1::Point2F(cx, cy), r + 2.2f * scale, r + 2.2f * scale);
                    m_renderTarget->DrawEllipse(outerRing, hoverBrush.Get(), 1.5f * scale);
                }
            } else {
                ComPtr<ID2D1SolidColorBrush> stepperBg;
                if (isStepper) {
                    m_renderTarget->CreateSolidColorBrush(
                        D2D1::ColorF(0.0f, 0.0f, 0.0f, isHovered ? 0.065f : 0.035f), stepperBg.GetAddressOf());
                }

                auto* fillBrush = isConfirm ? (isHovered ? confirmHoverBrush.Get() : confirmBrush.Get())
                                : isStepper ? stepperBg.Get()
                                : isActiveTool ? (button.isSecondary ? secondaryActiveBrush.Get() : activeBrush.Get())
                                : (isDanger && isHovered) ? dangerHoverBrush.Get()
                                : isHovered ? hoverBrush.Get()
                                : buttonBrush.Get();
                if (fillBrush) m_renderTarget->FillRoundedRectangle(rounded, fillBrush);

                if (isActiveTool && activeBorderBrush && !isConfirm && !isStepper) {
                    m_renderTarget->DrawRoundedRectangle(rounded, activeBorderBrush.Get(), 1.2f * scale);
                }

                auto* currentIconBrush = isConfirm ? confirmIconBrush.Get()
                                       : isActiveTool ? activeIconBrush.Get()
                                       : (isDanger && isHovered) ? dangerIconBrush.Get()
                                       : iconBrush.Get();
                drawVectorButtonIcon(button, button.rect, currentIconBrush, scale);
            }

            // 如果带有下拉三角指示器，绘制极精致微型实心倒三角 (3.0x2.0px)
            if (button.hasDropdown) {
                float tx = button.rect.right - 5.0f * scale;
                float ty = (button.rect.top + button.rect.bottom) * 0.5f;
                m_renderTarget->DrawLine(D2D1::Point2F(tx - 1.5f * scale, ty - 1.0f * scale),
                                         D2D1::Point2F(tx + 1.5f * scale, ty - 1.0f * scale), iconBrush.Get(), 1.0f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(tx - 1.5f * scale, ty - 1.0f * scale),
                                         D2D1::Point2F(tx, ty + 1.2f * scale), iconBrush.Get(), 1.0f * scale);
                m_renderTarget->DrawLine(D2D1::Point2F(tx + 1.5f * scale, ty - 1.0f * scale),
                                         D2D1::Point2F(tx, ty + 1.2f * scale), iconBrush.Get(), 1.0f * scale);
            }
        }
    };

    // 绘制主工具栏、二级属性栏与选区侧边浮动菜单
    drawButtonGroup(state.toolbarButtons);
    if (!state.secondaryToolbarButtons.empty()) {
        drawButtonGroup(state.secondaryToolbarButtons);
    }
    if (!state.selectionSideButtons.empty()) {
        drawGlassPanel(state.selectionSideRect, 8.0f * scale, false);
        drawButtonGroup(state.selectionSideButtons);
    }

    // 绘制展开的二级下拉悬浮菜单
    drawSubmenu(state);

    // 绘制无级滑块与预设悬浮弹窗
    drawSliderPopup(state);

    // 悬停浮层提示：把生僻字形翻译成「名称 + 快捷键」
    auto showTooltip = [&](const std::vector<ToolbarButton>& btnList) -> bool {
        for (const auto& button : btnList) {
            if (state.currentCursor.x < button.rect.left || state.currentCursor.x > button.rect.right ||
                state.currentCursor.y < button.rect.top  || state.currentCursor.y > button.rect.bottom) {
                continue;
            }
            std::wstring tip = tooltipForButton(button, state.toolbarLayoutChinese);
            std::wstring labelPart = tip;
            std::wstring kbdPart = L"";
            auto openPos = tip.find(L'(');
            auto closePos = tip.find(L')', openPos);
            if (openPos != std::wstring::npos && closePos != std::wstring::npos && closePos > openPos) {
                labelPart = tip.substr(0, openPos);
                while (!labelPart.empty() && labelPart.back() == L' ') labelPart.pop_back();
                kbdPart = tip.substr(openPos + 1, closePos - openPos - 1);
            }

            float labelW = static_cast<float>(labelPart.size()) * 11.5f * scale;
            float kbdW = kbdPart.empty() ? 0.0f : (static_cast<float>(kbdPart.size()) * 7.5f + 10.0f) * scale;
            float tw = (16.0f * scale) + labelW + (kbdPart.empty() ? 0.0f : (kbdW + 6.0f * scale));
            float th = 24.0f * scale;

            auto sz = m_renderTarget->GetSize();
            float bcx = (button.rect.left + button.rect.right) * 0.5f;
            float tx = std::clamp(bcx - tw * 0.5f, 8.0f * scale, sz.width - tw - 8.0f * scale);
            float ty = button.rect.top - th - 6.0f * scale;
            if (ty < 4.0f * scale) {
                ty = button.rect.bottom + 6.0f * scale;
            }

            auto tipRect = D2D1::RectF(tx, ty, tx + tw, ty + th);
            drawGlassPanel(tipRect, 6.0f * scale, false);

            ComPtr<ID2D1SolidColorBrush> textBrush, kbdBgBrush, kbdBorderBrush, kbdTextBrush;
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f), textBrush.GetAddressOf());
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f), kbdBgBrush.GetAddressOf());
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.12f), kbdBorderBrush.GetAddressOf());
            m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.40f, 0.48f, 0.95f), kbdTextBrush.GetAddressOf());

            float curLeft = tipRect.left + 8.0f * scale;
            if (m_infoTextFormat && textBrush) {
                m_renderTarget->DrawText(labelPart.c_str(), static_cast<UINT32>(labelPart.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(curLeft, tipRect.top + 4.0f * scale, curLeft + labelW, tipRect.bottom),
                    textBrush.Get());
            }

            if (!kbdPart.empty() && kbdBgBrush && kbdBorderBrush && kbdTextBrush && m_infoTextFormat) {
                float kx = curLeft + labelW + 6.0f * scale;
                float ky = tipRect.top + 4.0f * scale;
                float kh = 16.0f * scale;
                auto kbdRect = D2D1::RectF(kx, ky, kx + kbdW, ky + kh);
                auto kbdRound = D2D1::RoundedRect(kbdRect, 3.5f * scale, 3.5f * scale);
                m_renderTarget->FillRoundedRectangle(kbdRound, kbdBgBrush.Get());
                m_renderTarget->DrawRoundedRectangle(kbdRound, kbdBorderBrush.Get(), 1.0f * scale);
                m_renderTarget->DrawText(kbdPart.c_str(), static_cast<UINT32>(kbdPart.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(kx + 4.0f * scale, ky + 1.0f * scale, kx + kbdW - 4.0f * scale, ky + kh),
                    kbdTextBrush.Get());
            }
            return true;
        }
        return false;
    };

    if (state.openSubmenu == SubmenuType::None) {
        if (!showTooltip(state.toolbarButtons)) {
            if (!showTooltip(state.secondaryToolbarButtons)) {
                showTooltip(state.selectionSideButtons);
            }
        }
    }
}

void CaptureRenderer::drawSubmenu(CaptureState& state) {
    if (state.openSubmenu == SubmenuType::None || !m_renderTarget) return;

    float scale = (state.dpiScale > 0.1f) ? state.dpiScale : 1.0f;
    
    // 线条样式下拉菜单 (实线、长虚线、点虚线、点划线)
    if (state.openSubmenu == SubmenuType::LineStyle) {
        float menuW = 126.0f * scale;
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

        drawGlassPanel(menuRect, 9.0f * scale, false);

        ComPtr<ID2D1SolidColorBrush> itemHoverBg, itemActiveBg, textBrush, lineBrush, activeLineBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.045f), itemHoverBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.12f), itemActiveBg.GetAddressOf());
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
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.5f * scale, 4.5f * scale), itemActiveBg.Get());
            } else if (isHovered) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.5f * scale, 4.5f * scale), itemHoverBg.Get());
            }

            // 绘制线条样式矢量线段
            auto iconId = (items[i].style == LineStyle::Dashed) ? CaptureIconId::PropDashedLine
                        : (items[i].style == LineStyle::Dotted) ? CaptureIconId::PropDottedLine
                        : (items[i].style == LineStyle::DashDot) ? CaptureIconId::PropDashDotLine
                        : CaptureIconId::PropSolidLine;
            auto lineRect = D2D1::RectF(itemRect.left + 4.0f * scale, itemRect.top, itemRect.left + 46.0f * scale, itemRect.bottom);
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), iconId, lineRect,
                                           isSelected ? activeLineBrush.Get() : lineBrush.Get(), scale);

            // 绘制文字说明
            if (m_infoTextFormat && textBrush) {
                m_renderTarget->DrawText(items[i].label.c_str(), static_cast<UINT32>(items[i].label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(itemRect.left + 52.0f * scale, iy + 4.0f * scale, itemRect.right - 18.0f * scale, itemRect.bottom),
                    isSelected ? activeLineBrush.Get() : textBrush.Get());
            }

            // 选中项右侧微对勾 ✓
            if (isSelected) {
                float rcx = itemRect.right - 10.0f * scale;
                float rcy = iy + itemH * 0.5f;
                D2D1_POINT_2F p1{rcx - 3.5f * scale, rcy};
                D2D1_POINT_2F p2{rcx - 1.0f * scale, rcy + 2.8f * scale};
                D2D1_POINT_2F p3{rcx + 3.8f * scale, rcy - 3.2f * scale};
                m_renderTarget->DrawLine(p1, p2, activeLineBrush.Get(), 1.5f * scale);
                m_renderTarget->DrawLine(p2, p3, activeLineBrush.Get(), 1.5f * scale);
            }
        }
    }
    // 箭头样式下拉菜单 (标准单向、细线折角、双向箭头)
    else if (state.openSubmenu == SubmenuType::ArrowStyle) {
        float menuW = 126.0f * scale;
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

        drawGlassPanel(menuRect, 9.0f * scale, false);

        ComPtr<ID2D1SolidColorBrush> itemHoverBg, itemActiveBg, textBrush, lineBrush, activeLineBrush;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.045f), itemHoverBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.12f), itemActiveBg.GetAddressOf());
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
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.5f * scale, 4.5f * scale), itemActiveBg.Get());
            } else if (isHovered) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.5f * scale, 4.5f * scale), itemHoverBg.Get());
            }

            auto arrowIconId = (items[i].style == ArrowStyle::DoubleEnded) ? CaptureIconId::ToolArrowDouble
                             : (items[i].style == ArrowStyle::Thin) ? CaptureIconId::ToolArrowThin
                             : CaptureIconId::ToolArrow;
            auto arrowRect = D2D1::RectF(itemRect.left + 4.0f * scale, itemRect.top, itemRect.left + 36.0f * scale, itemRect.bottom);
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), arrowIconId, arrowRect,
                                           isSelected ? activeLineBrush.Get() : lineBrush.Get(), scale);

            if (m_infoTextFormat && textBrush) {
                m_renderTarget->DrawText(items[i].label.c_str(), static_cast<UINT32>(items[i].label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(itemRect.left + 42.0f * scale, iy + 4.0f * scale, itemRect.right - 18.0f * scale, itemRect.bottom),
                    isSelected ? activeLineBrush.Get() : textBrush.Get());
            }

            // 选中项右侧微对勾 ✓
            if (isSelected) {
                float rcx = itemRect.right - 10.0f * scale;
                float rcy = iy + itemH * 0.5f;
                D2D1_POINT_2F p1{rcx - 3.5f * scale, rcy};
                D2D1_POINT_2F p2{rcx - 1.0f * scale, rcy + 2.8f * scale};
                D2D1_POINT_2F p3{rcx + 3.8f * scale, rcy - 3.2f * scale};
                m_renderTarget->DrawLine(p1, p2, activeLineBrush.Get(), 1.5f * scale);
                m_renderTarget->DrawLine(p2, p3, activeLineBrush.Get(), 1.5f * scale);
            }
        }
    }
}

void CaptureRenderer::drawSliderPopup(CaptureState& state) {
    if (state.sliderPopup.type == SliderPopupType::None || !m_renderTarget) return;

    const float scale = std::clamp(state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    float popW = 200.0f * scale;
    float popH = 88.0f * scale;

    // 默认定位在二级属性栏的左侧或居中；若为选区侧边圆角则紧贴侧边栏
    float popX = state.secondaryToolbarRect.left + 12.0f * scale;
    float popY = state.secondaryToolbarRect.bottom + 8.0f * scale;

    if (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius) {
        if (state.selectionSideRect.left >= state.toolbarLayoutSelection.right) {
            popX = state.selectionSideRect.right + 8.0f * scale;
        } else {
            popX = state.selectionSideRect.left - popW - 8.0f * scale;
        }
        popY = state.selectionSideRect.top - 10.0f * scale;
    }

    auto sz = m_renderTarget->GetSize();
    if (popY + popH > sz.height - 8.0f * scale) {
        popY = sz.height - popH - 8.0f * scale;
    }
    if (popY < 8.0f * scale) {
        popY = 8.0f * scale;
    }
    if (popX + popW > sz.width - 8.0f * scale) {
        popX = sz.width - popW - 8.0f * scale;
    }
    if (popX < 8.0f * scale) {
        popX = 8.0f * scale;
    }

    auto popRect = D2D1::RectF(popX, popY, popX + popW, popY + popH);
    state.sliderPopup.popupRect = popRect;

    // 1. 磨砂亚克力微浮岛
    drawGlassPanel(popRect, 10.0f * scale, false);

    ComPtr<ID2D1SolidColorBrush> titleBrush, badgeBg, badgeText, trackBg, trackFill, thumbBg, thumbBorder, thumbGlow, presetBg, presetActiveBg, presetText, presetActiveText;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.22f, 0.26f, 0.95f), titleBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.05f), badgeBg.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), badgeText.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f), trackBg.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.95f), trackFill.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), thumbBg.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), thumbBorder.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.25f), thumbGlow.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.04f), presetBg.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 0.14f), presetActiveBg.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.28f, 0.35f, 0.95f), presetText.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), presetActiveText.GetAddressOf());

    std::wstring title = (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius)
        ? (state.toolbarLayoutChinese ? L"选区圆角半径" : L"Selection Radius")
        : (state.sliderPopup.type == SliderPopupType::CornerRadius)
        ? (state.toolbarLayoutChinese ? L"标注圆角" : L"Corner Radius")
        : (state.sliderPopup.type == SliderPopupType::MosaicBlockSize)
        ? (state.toolbarLayoutChinese ? L"马赛克粗细" : L"Mosaic Size")
        : (state.toolbarLayoutChinese ? L"标注线宽" : L"Stroke Width");

    int curVal = (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius)
        ? static_cast<int>(std::round(state.cornerRadius))
        : (state.sliderPopup.type == SliderPopupType::CornerRadius)
        ? static_cast<int>(std::round(state.currentElementCornerRadius))
        : state.currentStrokeWidth;

    state.sliderPopup.currentValue = curVal;
    int minV = (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius || state.sliderPopup.type == SliderPopupType::CornerRadius) ? 0 : 1;
    int maxV = (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius) ? 80 : (state.sliderPopup.type == SliderPopupType::CornerRadius) ? 40 : 28;
    state.sliderPopup.minValue = minV;
    state.sliderPopup.maxValue = maxV;

    // 1. 顶部标题与数值徽章
    float topY = popY + 8.0f * scale;
    if (m_infoTextFormat && titleBrush) {
        m_renderTarget->DrawText(title.c_str(), static_cast<UINT32>(title.size()), m_infoTextFormat.Get(),
            D2D1::RectF(popX + 12.0f * scale, topY, popX + 110.0f * scale, topY + 16.0f * scale),
            titleBrush.Get());
    }

    std::wstring valStr = std::format(L"{} px", curVal);
    float badgeW = (16.0f + valStr.size() * 7.5f) * scale;
    float badgeH = 18.0f * scale;
    auto valBadgeRect = D2D1::RectF(popX + popW - badgeW - 12.0f * scale, topY - 1.0f * scale, popX + popW - 12.0f * scale, topY + badgeH - 1.0f * scale);
    if (badgeBg) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(valBadgeRect, 4.0f * scale, 4.0f * scale), badgeBg.Get());
    if (m_infoTextFormat && badgeText) {
        m_renderTarget->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.size()), m_infoTextFormat.Get(),
            D2D1::RectF(valBadgeRect.left + 5.0f * scale, valBadgeRect.top + 1.0f * scale, valBadgeRect.right, valBadgeRect.bottom),
            badgeText.Get());
    }

    // 2. 中部无级滑动轨道与发光游标
    float trackX = popX + 12.0f * scale;
    float trackY = topY + 24.0f * scale;
    float trackW = popW - 24.0f * scale;
    float trackH = 5.0f * scale;
    auto trackRect = D2D1::RectF(trackX, trackY, trackX + trackW, trackY + trackH);
    state.sliderPopup.trackRect = trackRect;

    if (trackBg) m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackRect, 2.5f * scale, 2.5f * scale), trackBg.Get());

    float pct = (maxV > minV) ? std::clamp(static_cast<float>(curVal - minV) / static_cast<float>(maxV - minV), 0.0f, 1.0f) : 0.0f;
    float thumbX = trackX + pct * trackW;
    float thumbY = trackY + trackH * 0.5f;

    auto fillRect = D2D1::RectF(trackX, trackY, thumbX, trackY + trackH);
    if (trackFill && thumbX > trackX) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 2.5f * scale, 2.5f * scale), trackFill.Get());
    }

    // 发光游标 Handle
    float thumbR = (state.sliderPopup.isDragging ? 7.5f : 6.0f) * scale;
    auto thumbEllipse = D2D1::Ellipse(D2D1::Point2F(thumbX, thumbY), thumbR, thumbR);
    state.sliderPopup.thumbRect = D2D1::RectF(thumbX - thumbR - 2.0f * scale, thumbY - thumbR - 2.0f * scale, thumbX + thumbR + 2.0f * scale, thumbY + thumbR + 2.0f * scale);

    if (thumbGlow) m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, thumbY), thumbR + 3.0f * scale, thumbR + 3.0f * scale), thumbGlow.Get());
    if (thumbBg) m_renderTarget->FillEllipse(thumbEllipse, thumbBg.Get());
    if (thumbBorder) m_renderTarget->DrawEllipse(thumbEllipse, thumbBorder.Get(), 1.5f * scale);

    // 3. 底部快捷预设胶囊
    std::vector<int> presets = (state.sliderPopup.type == SliderPopupType::SelectionCornerRadius)
        ? std::vector<int>{ 0, 8, 14, 24, 40, 60 }
        : (state.sliderPopup.type == SliderPopupType::CornerRadius)
        ? std::vector<int>{ 0, 6, 12, 18, 24 }
        : (state.sliderPopup.type == SliderPopupType::MosaicBlockSize)
        ? std::vector<int>{ 4, 8, 12, 16, 24, 32 }
        : std::vector<int>{ 2, 4, 6, 8, 14, 20 };

    state.sliderPopup.presetButtons.clear();
    float presetY = trackY + 16.0f * scale;
    float presetH = 20.0f * scale;
    float slotW = trackW / static_cast<float>(presets.size());

    for (size_t i = 0; i < presets.size(); ++i) {
        int val = presets[i];
        float px = trackX + i * slotW;
        auto pRect = D2D1::RectF(px + 2.0f * scale, presetY, px + slotW - 2.0f * scale, presetY + presetH);
        state.sliderPopup.presetButtons.push_back({ val, pRect });

        bool isActivePreset = (val == curVal);
        auto pRound = D2D1::RoundedRect(pRect, 4.0f * scale, 4.0f * scale);
        auto* pBgBrush = isActivePreset ? presetActiveBg.Get() : presetBg.Get();
        auto* pTxBrush = isActivePreset ? presetActiveText.Get() : presetText.Get();

        if (pBgBrush) m_renderTarget->FillRoundedRectangle(pRound, pBgBrush);

        std::wstring pText = std::format(L"{}", val);
        if (m_infoTextFormat && pTxBrush) {
            m_renderTarget->DrawText(pText.c_str(), static_cast<UINT32>(pText.size()), m_infoTextFormat.Get(),
                D2D1::RectF(pRect.left, pRect.top + 2.0f * scale, pRect.right, pRect.bottom),
                pTxBrush);
        }
    }
}

void CaptureRenderer::drawVectorButtonIcon(const ToolbarButton& button, const D2D1_RECT_F& rect, ID2D1Brush* brush, float scale) {
    if (!m_renderTarget || !brush) return;

    float cy = (rect.top + rect.bottom) * 0.5f;

    switch (button.command) {
        case ToolbarCommand::Confirm:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionConfirm, rect, brush, scale);
            return;
        case ToolbarCommand::Cancel:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionCancel, rect, brush, scale);
            return;
        case ToolbarCommand::Undo:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionUndo, rect, brush, scale);
            return;
        case ToolbarCommand::Redo:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionRedo, rect, brush, scale);
            return;
        case ToolbarCommand::Clear:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionClear, rect, brush, scale);
            return;
        case ToolbarCommand::SideToggleCornerRadius:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::PropCornerRadius, rect, brush, scale);
            return;
        case ToolbarCommand::SideInvertSelection:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolArrowDouble, rect, brush, scale);
            return;
        case ToolbarCommand::SideResetSelection:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionUndo, rect, brush, scale);
            return;
        case ToolbarCommand::ExtractText:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionExtractText, rect, brush, scale);
            return;
        case ToolbarCommand::PinWindow:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionPinWindow, rect, brush, scale);
            return;
        case ToolbarCommand::ScrollCapture:
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ActionScrollCapture, rect, brush, scale);
            return;
        case ToolbarCommand::SelectTool: {
            switch (button.tool) {
                case MarkupTool::Rectangle:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolRectangle, rect, brush, scale);
                    return;
                case MarkupTool::Ellipse:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolEllipse, rect, brush, scale);
                    return;
                case MarkupTool::Arrow:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolArrow, rect, brush, scale);
                    return;
                case MarkupTool::Pen:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolPen, rect, brush, scale);
                    return;
                case MarkupTool::Highlight:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolHighlight, rect, brush, scale);
                    return;
                case MarkupTool::Mosaic:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolMosaic, rect, brush, scale);
                    return;
                case MarkupTool::Text:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolText, rect, brush, scale);
                    return;
                case MarkupTool::Number:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolNumber, rect, brush, scale);
                    return;
                case MarkupTool::Inpaint:
                    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::ToolInpaint, rect, brush, scale);
                    return;
                default: break;
            }
            break;
        }
        case ToolbarCommand::ToggleFill: {
            // 纯矢量填充/描边切换图标（居中方块）
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
                button.boolParam ? CaptureIconId::PropFillSolid : CaptureIconId::PropFillOutline,
                rect, brush, scale);
            return;
        }
        case ToolbarCommand::ToggleLineStyleDropdown: {
            auto iconId = (button.lineStyleParam == LineStyle::Dashed) ? CaptureIconId::PropDashedLine
                        : (button.lineStyleParam == LineStyle::Dotted) ? CaptureIconId::PropDottedLine
                        : (button.lineStyleParam == LineStyle::DashDot) ? CaptureIconId::PropDashDotLine
                        : CaptureIconId::PropSolidLine;
            auto lineRect = D2D1::RectF(rect.left + 3.0f * scale, rect.top, rect.right - 9.0f * scale, rect.bottom);
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), iconId, lineRect, brush, scale);
            return;
        }
        case ToolbarCommand::ToggleArrowStyleDropdown: {
            auto iconId = (button.arrowStyleParam == ArrowStyle::DoubleEnded) ? CaptureIconId::ToolArrowDouble
                        : (button.arrowStyleParam == ArrowStyle::Thin) ? CaptureIconId::ToolArrowThin
                        : CaptureIconId::ToolArrow;
            auto arrowRect = D2D1::RectF(rect.left + 3.0f * scale, rect.top, rect.right - 9.0f * scale, rect.bottom);
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), iconId, arrowRect, brush, scale);
            return;
        }
        case ToolbarCommand::CycleStrokeWidth: {
            auto iconRect = D2D1::RectF(rect.left + 3.0f * scale, rect.top, rect.left + 15.0f * scale, rect.bottom);
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::PropStrokeWidth, iconRect, brush, scale);
            if (!button.label.empty() && m_infoTextFormat) {
                m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                    m_infoTextFormat.Get(),
                    D2D1::RectF(rect.left + 17.0f * scale, cy - 8.0f * scale, rect.right - 2.0f * scale, cy + 8.0f * scale),
                    brush);
            }
            return;
        }
        case ToolbarCommand::ToggleCornerRadius:
        case ToolbarCommand::CycleElementCornerRadius: {
            if (button.label.empty()) {
                CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::PropCornerRadius, rect, brush, scale);
            } else {
                auto iconRect = D2D1::RectF(rect.left + 3.0f * scale, rect.top, rect.left + 15.0f * scale, rect.bottom);
                CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::PropCornerRadius, iconRect, brush, scale);
                if (m_infoTextFormat) {
                    m_renderTarget->DrawText(button.label.c_str(), static_cast<UINT32>(button.label.size()),
                        m_infoTextFormat.Get(),
                        D2D1::RectF(rect.left + 17.0f * scale, cy - 8.0f * scale, rect.right - 2.0f * scale, cy + 8.0f * scale),
                        brush);
                }
            }
            return;
        }
        case ToolbarCommand::SelectMosaicType: {
            CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(),
                button.intParam == 0 ? CaptureIconId::ToolMosaic : CaptureIconId::ToolBlur,
                rect, brush, scale);
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

    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        if (state.currentTool == MarkupTool::Rectangle || state.currentTool == MarkupTool::Ellipse ||
            state.currentTool == MarkupTool::Mosaic || state.currentTool == MarkupTool::Highlight) {
            float w = std::abs(x2 - x1);
            float h = std::abs(y2 - y1);
            float side = std::max(w, h);
            x2 = x1 + (x2 >= x1 ? side : -side);
            y2 = y1 + (y2 >= y1 ? side : -side);
        } else if (state.currentTool == MarkupTool::Arrow) {
            double dx = x2 - x1;
            double dy = y2 - y1;
            double dist = std::hypot(dx, dy);
            if (dist >= 1.0) {
                double angle = std::atan2(dy, dx);
                constexpr double step = 3.14159265358979323846 / 4.0;
                double snappedAngle = std::round(angle / step) * step;
                x2 = static_cast<float>(x1 + dist * std::cos(snappedAngle));
                y2 = static_cast<float>(y1 + dist * std::sin(snappedAngle));
            }
        }
    }

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

void CaptureRenderer::drawSmartAlignmentGuides(const D2D1_RECT_F& selRect, CaptureState& state) {
    if (!m_renderTarget) return;
    auto size = m_renderTarget->GetSize();
    float scale = (state.dpiScale > 0) ? state.dpiScale : 1.0f;

    ComPtr<ID2D1SolidColorBrush> guideBrush;
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.94f, 1.0f, 0.75f), guideBrush.GetAddressOf());
    if (!guideBrush) return;

    float cx = (selRect.left + selRect.right) * 0.5f;
    float cy = (selRect.top + selRect.bottom) * 0.5f;
    float midScreenX = size.width * 0.5f;
    float midScreenY = size.height * 0.5f;

    // 1. 水平中线对齐 (50% Screen Y)
    if (std::abs(cy - midScreenY) < 4.0f * scale) {
        float seg = 6.0f * scale, sp = 4.0f * scale;
        for (float x = 0; x < size.width; x += seg + sp) {
            m_renderTarget->DrawLine(D2D1::Point2F(x, midScreenY), D2D1::Point2F(std::min(size.width, x + seg), midScreenY), guideBrush.Get(), 1.0f * scale);
        }
    }

    // 2. 垂直中线对齐 (50% Screen X)
    if (std::abs(cx - midScreenX) < 4.0f * scale) {
        float seg = 6.0f * scale, sp = 4.0f * scale;
        for (float y = 0; y < size.height; y += seg + sp) {
            m_renderTarget->DrawLine(D2D1::Point2F(midScreenX, y), D2D1::Point2F(midScreenX, std::min(size.height, y + seg)), guideBrush.Get(), 1.0f * scale);
        }
    }
}

void CaptureRenderer::drawQrChip(const D2D1_RECT_F& selRect, CaptureState& state) {
    if (state.detectedQrText.empty() || !m_renderTarget || !m_dwriteFactory) {
        state.qrChipRect = D2D1::RectF(0, 0, 0, 0);
        return;
    }

    float scale = (state.dpiScale > 0.0f) ? state.dpiScale : 1.0f;
    float chipH = 26.0f * scale;
    float pad = 8.0f * scale;

    std::wstring displayMsg = L"二维码: " + easy::core::WinUtils::utf8ToWstring(state.detectedQrText);
    if (displayMsg.size() > 28) {
        displayMsg = displayMsg.substr(0, 26) + L"...";
    }

    // 测量文字宽度
    ComPtr<IDWriteTextLayout> layout;
    m_dwriteFactory->CreateTextLayout(displayMsg.c_str(), static_cast<UINT32>(displayMsg.size()),
                                      m_infoTextFormat.Get(), 600.0f, chipH, layout.GetAddressOf());
    float textW = 120.0f * scale;
    if (layout) {
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        textW = metrics.width;
    }

    float chipW = 24.0f * scale + textW + pad * 2;
    float cx2 = selRect.right;
    float cy1 = selRect.top - chipH - 6.0f * scale;
    if (cy1 < 10.0f * scale) {
        cy1 = selRect.top + 6.0f * scale; // 选区靠近屏幕顶端时移至内部
    }
    float cx1 = std::max(selRect.left, cx2 - chipW);
    state.qrChipRect = D2D1::RectF(cx1, cy1, cx1 + chipW, cy1 + chipH);

    // 绘制毛玻璃微胶囊底框与悬停高亮
    ComPtr<ID2D1SolidColorBrush> chipBg, chipBorder, iconBrush, textBrush;
    if (state.isQrChipHovered) {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.53f, 0.90f, 0.92f), chipBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), chipBorder.GetAddressOf());
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.14f, 0.88f), chipBg.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.42f, 0.80f), chipBorder.GetAddressOf());
    }
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), iconBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f), textBrush.GetAddressOf());

    if (chipBg && chipBorder) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(state.qrChipRect, 6.0f * scale, 6.0f * scale), chipBg.Get());
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(state.qrChipRect, 6.0f * scale, 6.0f * scale), chipBorder.Get(), 1.0f * scale);
    }

    // 绘制二维码纯矢量图标
    auto iconRect = D2D1::RectF(state.qrChipRect.left + 6.0f * scale, state.qrChipRect.top + 3.0f * scale,
                                state.qrChipRect.left + 22.0f * scale, state.qrChipRect.bottom - 3.0f * scale);
    CaptureVectorIcons::renderIcon(m_renderTarget.Get(), m_d2dFactory.Get(), CaptureIconId::PropQrCode, iconRect, iconBrush.Get(), scale);

    // 绘制文字
    if (m_infoTextFormat && textBrush) {
        m_infoTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        auto textRect = D2D1::RectF(state.qrChipRect.left + 25.0f * scale, state.qrChipRect.top + 4.0f * scale,
                                    state.qrChipRect.right - 6.0f * scale, state.qrChipRect.bottom);
        m_renderTarget->DrawTextW(displayMsg.c_str(), static_cast<UINT32>(displayMsg.size()),
                                  m_infoTextFormat.Get(), textRect, textBrush.Get());
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
        drawSmartAlignmentGuides(selRect, state);
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

        drawSmartAlignmentGuides(selRect, state);
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
