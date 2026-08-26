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
            case ToolbarCommand::ToggleCornerRadius: return L"Corner radius ([ / ])";
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
        case ToolbarCommand::ToggleCornerRadius: return L"调节选区圆角 ([ / ])";
        case ToolbarCommand::ExtractText: return L"提取文字 (OCR)";
        case ToolbarCommand::PinWindow: return L"贴图到屏幕";
        case ToolbarCommand::ScrollCapture: return L"长截图";
        case ToolbarCommand::Confirm: return L"确认 (Enter)";
        case ToolbarCommand::Cancel: return L"取消 (ESC)";
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

    // 选区边框（支持直角与圆角）
    if (radius > 0.5f) {
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), m_borderBrush.Get(), 2.0f * scale);
    } else {
        m_renderTarget->DrawRectangle(rect, m_borderBrush.Get(), 2.0f * scale);
    }

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

    // 4 个 Figma 级内侧圆角调节手柄（精致实心小白圆点 + 主题色细描边）
    float selW = rect.right - rect.left;
    float selH = rect.bottom - rect.top;
    if (selW >= 36.0f * scale && selH >= 36.0f * scale) {
        float offset = std::clamp(std::max(12.0f, state.cornerRadius + 6.0f), 10.0f, std::min(selW, selH) * 0.45f) * scale;
        D2D1_POINT_2F cornerPts[4] = {
            {rect.left + offset,  rect.top + offset},
            {rect.right - offset, rect.top + offset},
            {rect.right - offset, rect.bottom - offset},
            {rect.left + offset,  rect.bottom - offset}
        };

        ComPtr<ID2D1SolidColorBrush> handleFill, handleBorder;
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), handleFill.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.85f, 1.0f), handleBorder.GetAddressOf());

        float dotRadius = (state.isAdjustingCornerRadius ? 4.5f : 3.5f) * scale;

        for (const auto& cpt : cornerPts) {
            auto ellipse = D2D1::Ellipse(cpt, dotRadius, dotRadius);
            if (handleFill) m_renderTarget->FillEllipse(ellipse, handleFill.Get());
            if (handleBorder) m_renderTarget->DrawEllipse(ellipse, handleBorder.Get(), 1.4f * scale);
        }
    }
}

void CaptureRenderer::drawSizeInfo(const D2D1_RECT_F& rect, CaptureState& state) {
    int w = static_cast<int>(rect.right - rect.left);
    int h = static_cast<int>(rect.bottom - rect.top);

    std::wstring info;
    if (state.cornerRadius > 0.5f) {
        info = std::format(L"{}×{} (R: {}px)", w, h, static_cast<int>(state.cornerRadius));
    } else {
        info = std::format(L"{}×{}", w, h);
    }

    const float scale = std::clamp(
        state.dpiScale > 0.0f ? state.dpiScale : 1.0f, 1.0f, 5.0f);
    float labelW = (state.cornerRadius > 0.5f ? 128.0f : 100.0f) * scale;
    float labelH = 24.0f * scale;
    float labelX = rect.left;
    float labelY = rect.top - labelH - 4.0f * scale;
    if (labelY < 0) labelY = rect.bottom + 4.0f * scale;

    drawGlassPanel(D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH), 5.0f * scale, false);
    m_renderTarget->DrawText(info.c_str(), static_cast<UINT32>(info.size()),
                             m_infoTextFormat.Get(),
                             D2D1::RectF(labelX, labelY, labelX + labelW, labelY + labelH),
                             m_infoTextBrush.Get());
}

static bool isDarkThemeActive() {
    auto& cfg = easy::core::ConfigManager::instance();
    const std::string theme = cfg.get<std::string>("/general/theme", "system");
    if (theme == "dark") return true;
    if (theme == "light") return false;
    return easy::core::WinUtils::isSystemTaskbarDark();
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
    
    const bool isDark = isDarkThemeActive();
    // 磨砂玻璃 HUD 面板（自适应全局深浅色）
    drawGlassPanel(bgRect, 9.0f * scale, false);

    // 分组分隔线（工具 | 颜色 | 命令），提升辨识度
    if (state.mode != OverlayMode::RecordRegion && state.toolbarButtons.size() >= 25) {
        ComPtr<ID2D1SolidColorBrush> sep;
        m_renderTarget->CreateSolidColorBrush(
            isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f),
            sep.GetAddressOf());
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
    ComPtr<ID2D1SolidColorBrush> confirmBrush;
    ComPtr<ID2D1SolidColorBrush> hoverBrush;
    ComPtr<ID2D1SolidColorBrush> iconBrush;
    ComPtr<ID2D1SolidColorBrush> activeIconBrush;
    ComPtr<ID2D1SolidColorBrush> tipTextBrush;

    if (isDark) {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), buttonBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f), hoverBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.97f, 0.95f), iconBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), activeIconBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.97f, 0.95f), tipTextBrush.GetAddressOf());
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.04f), buttonBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f), hoverBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.22f, 0.92f), iconBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), activeIconBrush.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.15f, 0.95f), tipTextBrush.GetAddressOf());
    }

    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(themeRgb.r, themeRgb.g, themeRgb.b, 0.95f), activeBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.88f), dangerBrush.GetAddressOf());
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.72f, 0.42f, 0.95f), confirmBrush.GetAddressOf());

    for (const auto& button : state.toolbarButtons) {
        bool isTool = button.command == ToolbarCommand::SelectTool;
        bool isActiveTool = isTool && button.tool == state.currentTool;
        bool isColor = button.command == ToolbarCommand::SelectColor;
        bool isDanger = button.command == ToolbarCommand::Cancel || button.command == ToolbarCommand::Clear;
        bool isConfirm = button.command == ToolbarCommand::Confirm;
        bool isHovered = state.currentCursor.x >= button.rect.left && state.currentCursor.x <= button.rect.right &&
                         state.currentCursor.y >= button.rect.top  && state.currentCursor.y <= button.rect.bottom;

        auto rounded = D2D1::RoundedRect(button.rect, 5.0f * scale, 5.0f * scale);

        if (isColor) {
            // 颜色色板: 用色块填充, 当前色加高对比描边, 悬停加淡描边
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
                m_renderTarget->DrawRoundedRectangle(rounded, activeBrush.Get(), 2.0f);
            } else if (isHovered && hoverBrush) {
                m_renderTarget->DrawRoundedRectangle(rounded, hoverBrush.Get(), 1.5f);
            }
            continue;
        }

        auto* fillBrush = isActiveTool ? activeBrush.Get()
                        : (isConfirm && isHovered) ? confirmBrush.Get()
                        : (isDanger && isHovered) ? dangerBrush.Get()
                        : isHovered ? hoverBrush.Get()
                        : buttonBrush.Get();
        m_renderTarget->FillRoundedRectangle(rounded, fillBrush);

        if (isActiveTool) {
            m_renderTarget->DrawRoundedRectangle(rounded, activeIconBrush.Get(), 1.2f);
        }

        // 矢量图标绘制（全 DPI 极致锐利，根据按钮状态动态使用纯白或炭黑画刷）
        auto* currentIconBrush = (isActiveTool || (isConfirm && isHovered) || (isDanger && isHovered))
                               ? activeIconBrush.Get() : iconBrush.Get();
        drawVectorButtonIcon(button, button.rect, currentIconBrush, scale);
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
                                 tipTextBrush.Get());
        break;
    }
}

void CaptureRenderer::drawVectorButtonIcon(const ToolbarButton& button, const D2D1_RECT_F& rect, ID2D1Brush* brush, float scale) {
    if (!m_renderTarget || !brush) return;

    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float half = 5.8f * scale;

    switch (button.command) {
        case ToolbarCommand::Confirm: {
            // 黄金比例平滑对勾 ✓
            D2D1_POINT_2F p1{cx - 5.5f * scale, cy - 0.2f * scale};
            D2D1_POINT_2F p2{cx - 1.8f * scale, cy + 4.2f * scale};
            D2D1_POINT_2F p3{cx + 5.8f * scale, cy - 4.8f * scale};
            m_renderTarget->DrawLine(p1, p2, brush, 2.2f * scale);
            m_renderTarget->DrawLine(p2, p3, brush, 2.2f * scale);
            return;
        }
        case ToolbarCommand::Cancel: {
            // 精致对称叉号 ✕
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.2f * scale, cy - 4.2f * scale),
                                     D2D1::Point2F(cx + 4.2f * scale, cy + 4.2f * scale), brush, 2.0f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.2f * scale, cy - 4.2f * scale),
                                     D2D1::Point2F(cx - 4.2f * scale, cy + 4.2f * scale), brush, 2.0f * scale);
            return;
        }
        case ToolbarCommand::Undo: {
            // 撤销平滑回折圆弧 ↩
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.0f * scale, cy + 4.0f * scale),
                                     D2D1::Point2F(cx + 4.0f * scale, cy - 0.5f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.0f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx - 3.5f * scale, cy - 0.5f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 3.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx - 0.5f * scale, cy - 3.8f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 3.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx - 0.5f * scale, cy + 2.8f * scale), brush, 1.8f * scale);
            return;
        }
        case ToolbarCommand::Redo: {
            // 重做平滑回折圆弧 ↪
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.0f * scale, cy + 4.0f * scale),
                                     D2D1::Point2F(cx - 4.0f * scale, cy - 0.5f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.0f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx + 3.5f * scale, cy - 0.5f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 3.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx + 0.5f * scale, cy - 3.8f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 3.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx + 0.5f * scale, cy + 2.8f * scale), brush, 1.8f * scale);
            return;
        }
        case ToolbarCommand::Clear: {
            // 精致垃圾桶 🗑️ (桶盖 + 桶身 + 竖向格栅)
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 5.0f * scale, cy - 3.5f * scale),
                                     D2D1::Point2F(cx + 5.0f * scale, cy - 3.5f * scale), brush, 1.5f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.8f * scale, cy - 5.0f * scale),
                                     D2D1::Point2F(cx + 1.8f * scale, cy - 5.0f * scale), brush, 1.4f * scale);
            auto bin = D2D1::RectF(cx - 4.0f * scale, cy - 2.5f * scale, cx + 4.0f * scale, cy + 5.2f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bin, 1.5f * scale, 1.5f * scale), brush, 1.4f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx - 1.5f * scale, cy + 3.5f * scale), brush, 1.2f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 1.5f * scale, cy - 0.5f * scale),
                                     D2D1::Point2F(cx + 1.5f * scale, cy + 3.5f * scale), brush, 1.2f * scale);
            return;
        }
        case ToolbarCommand::ToggleCornerRadius: {
            // 选区圆角调节图标 ╭╮ (外框带大圆角 + 中心定位点)
            auto box = D2D1::RectF(cx - 5.5f * scale, cy - 4.5f * scale, cx + 5.5f * scale, cy + 4.5f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.5f * scale, 3.5f * scale), brush, 1.6f * scale);
            m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 1.2f * scale, 1.2f * scale), brush);
            return;
        }
        case ToolbarCommand::ExtractText: {
            // OCR 提取文字 (四角扫描取景框 [ T ])
            float r = 5.2f * scale;
            float arm = 2.4f * scale;
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy - r + arm), D2D1::Point2F(cx - r, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx - r + arm, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r - arm, cy - r), D2D1::Point2F(cx + r, cy - r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx + r, cy - r + arm), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy + r - arm), D2D1::Point2F(cx - r, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx - r + arm, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r - arm, cy + r), D2D1::Point2F(cx + r, cy + r), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + r, cy + r), D2D1::Point2F(cx + r, cy + r - arm), brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.8f * scale, cy - 2.6f * scale),
                                     D2D1::Point2F(cx + 2.8f * scale, cy - 2.6f * scale), brush, 1.5f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 2.6f * scale),
                                     D2D1::Point2F(cx, cy + 3.0f * scale), brush, 1.5f * scale);
            return;
        }
        case ToolbarCommand::PinWindow: {
            // 现代 45° 办公金属图钉 📌 (针头圆盖 + 针身台阶 + 细长针尖)
            D2D1_POINT_2F pTip{cx - 5.0f * scale, cy + 5.0f * scale};
            D2D1_POINT_2F pBase{cx - 1.5f * scale, cy + 1.5f * scale};
            m_renderTarget->DrawLine(pTip, pBase, brush, 1.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(pBase.x - 3.0f * scale, pBase.y - 1.0f * scale),
                                     D2D1::Point2F(pBase.x + 1.0f * scale, pBase.y + 3.0f * scale), brush, 2.0f * scale);
            m_renderTarget->DrawLine(pBase, D2D1::Point2F(cx + 3.5f * scale, cy - 3.5f * scale), brush, 2.6f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx + 1.5f * scale, cy - 5.5f * scale),
                                     D2D1::Point2F(cx + 5.5f * scale, cy - 1.5f * scale), brush, 2.0f * scale);
            return;
        }
        case ToolbarCommand::ScrollCapture: {
            // 长截图 (双层滚动页面轮廓 + 向下贯穿展开箭头)
            auto page = D2D1::RectF(cx - 4.5f * scale, cy - 5.5f * scale, cx + 4.5f * scale, cy + 1.0f * scale);
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(page, 1.2f * scale, 1.2f * scale), brush, 1.2f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 2.0f * scale),
                                     D2D1::Point2F(cx, cy + 5.0f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy + 5.0f * scale),
                                     D2D1::Point2F(cx - 2.8f * scale, cy + 2.4f * scale), brush, 1.8f * scale);
            m_renderTarget->DrawLine(D2D1::Point2F(cx, cy + 5.0f * scale),
                                     D2D1::Point2F(cx + 2.8f * scale, cy + 2.4f * scale), brush, 1.8f * scale);
            return;
        }
        case ToolbarCommand::SelectTool: {
            switch (button.tool) {
                case MarkupTool::Rectangle: {
                    // 圆角规整矩形框
                    auto box = D2D1::RectF(cx - half, cy - half * 0.8f, cx + half, cy + half * 0.8f);
                    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush, 1.5f * scale);
                    return;
                }
                case MarkupTool::Ellipse: {
                    // 黄金比例优雅椭圆
                    auto ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), half, half * 0.8f);
                    m_renderTarget->DrawEllipse(ellipse, brush, 1.5f * scale);
                    return;
                }
                case MarkupTool::Arrow: {
                    // 45° 现代引导箭头 (主干 + 对称规整内折双翼)
                    D2D1_POINT_2F pStart{cx - 5.0f * scale, cy + 5.0f * scale};
                    D2D1_POINT_2F pEnd{cx + 4.5f * scale, cy - 4.5f * scale};
                    m_renderTarget->DrawLine(pStart, pEnd, brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 0.5f * scale, cy - 4.5f * scale), pEnd, brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.5f * scale, cy - 0.5f * scale), pEnd, brush, 1.8f * scale);
                    return;
                }
                case MarkupTool::Pen: {
                    // 45° 专业手绘铅笔 (三角笔尖 + 笔身矩形)
                    D2D1_POINT_2F tip{cx - 5.2f * scale, cy + 5.2f * scale};
                    D2D1_POINT_2F b1{cx - 3.2f * scale, cy + 5.2f * scale};
                    D2D1_POINT_2F b2{cx - 5.2f * scale, cy + 3.2f * scale};
                    m_renderTarget->DrawLine(tip, b1, brush, 1.5f * scale);
                    m_renderTarget->DrawLine(tip, b2, brush, 1.5f * scale);
                    m_renderTarget->DrawLine(b1, D2D1::Point2F(cx + 4.5f * scale, cy - 2.5f * scale), brush, 1.5f * scale);
                    m_renderTarget->DrawLine(b2, D2D1::Point2F(cx + 2.5f * scale, cy - 4.5f * scale), brush, 1.5f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 4.5f * scale, cy - 2.5f * scale),
                                             D2D1::Point2F(cx + 2.5f * scale, cy - 4.5f * scale), brush, 1.5f * scale);
                    return;
                }
                case MarkupTool::Highlight: {
                    // 荧光马克笔 (斜切笔头 + 笔身)
                    auto tipBox = D2D1::RectF(cx - 5.5f * scale, cy - 1.5f * scale, cx + 5.5f * scale, cy + 3.5f * scale);
                    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(tipBox, 1.0f * scale, 1.0f * scale), brush);
                    D2D1_POINT_2F p1{cx - 3.0f * scale, cy - 5.0f * scale};
                    D2D1_POINT_2F p2{cx + 4.5f * scale, cy - 2.0f * scale};
                    m_renderTarget->DrawLine(p1, p2, brush, 2.2f * scale);
                    return;
                }
                case MarkupTool::Mosaic: {
                    // 3x3 棋盘格像素矩阵 (9 像素交错实心与镂空)
                    float s = 2.8f * scale;
                    float g = 0.7f * scale;
                    for (int row = -1; row <= 1; ++row) {
                        for (int col = -1; col <= 1; ++col) {
                            float x = cx + col * (s + g) - s * 0.5f;
                            float y = cy + row * (s + g) - s * 0.5f;
                            auto cell = D2D1::RectF(x, y, x + s, y + s);
                            if ((row + col) % 2 == 0) {
                                m_renderTarget->FillRectangle(cell, brush);
                            } else {
                                m_renderTarget->DrawRectangle(cell, brush, 0.9f * scale);
                            }
                        }
                    }
                    return;
                }
                case MarkupTool::Text: {
                    // 世界级衬线大写「T」(横梁衬线 + 垂直立柱 + 稳固底座)
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 5.2f * scale, cy - 5.0f * scale),
                                             D2D1::Point2F(cx + 5.2f * scale, cy - 5.0f * scale), brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 5.2f * scale, cy - 5.0f * scale),
                                             D2D1::Point2F(cx - 5.2f * scale, cy - 3.2f * scale), brush, 1.4f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 5.2f * scale, cy - 5.0f * scale),
                                             D2D1::Point2F(cx + 5.2f * scale, cy - 3.2f * scale), brush, 1.4f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx, cy - 5.0f * scale),
                                             D2D1::Point2F(cx, cy + 5.0f * scale), brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.5f * scale, cy + 5.0f * scale),
                                             D2D1::Point2F(cx + 2.5f * scale, cy + 5.0f * scale), brush, 1.4f * scale);
                    return;
                }
                case MarkupTool::Number: {
                    // 圆形徽章序号「①」(圆环 + 居中清晰数字 1)
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 5.8f * scale, 5.8f * scale), brush, 1.4f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.8f * scale, cy - 2.0f * scale),
                                             D2D1::Point2F(cx + 0.2f * scale, cy - 3.6f * scale), brush, 1.6f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx + 0.2f * scale, cy - 3.6f * scale),
                                             D2D1::Point2F(cx + 0.2f * scale, cy + 3.6f * scale), brush, 1.8f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 1.8f * scale, cy + 3.6f * scale),
                                             D2D1::Point2F(cx + 2.2f * scale, cy + 3.6f * scale), brush, 1.4f * scale);
                    return;
                }
                case MarkupTool::Magnifier: {
                    // 拟物光学放大镜 (45° 镜片圆环 + 粗手柄)
                    D2D1_POINT_2F mCenter{cx - 1.6f * scale, cy - 1.6f * scale};
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(mCenter, 4.4f * scale, 4.4f * scale), brush, 1.6f * scale);
                    D2D1_POINT_2F hStart{cx + 1.8f * scale, cy + 1.8f * scale};
                    D2D1_POINT_2F hEnd{cx + 5.6f * scale, cy + 5.6f * scale};
                    m_renderTarget->DrawLine(hStart, hEnd, brush, 2.4f * scale);
                    return;
                }
                case MarkupTool::Watermark: {
                    // 倾斜印章图章 (圆角边框 + 排版双线)
                    auto box = D2D1::RectF(cx - 5.5f * scale, cy - 4.5f * scale, cx + 5.5f * scale, cy + 4.5f * scale);
                    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * scale, 2.0f * scale), brush, 1.4f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 3.2f * scale, cy - 1.2f * scale),
                                             D2D1::Point2F(cx + 3.2f * scale, cy - 1.2f * scale), brush, 1.2f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 2.2f * scale, cy + 1.8f * scale),
                                             D2D1::Point2F(cx + 2.2f * scale, cy + 1.8f * scale), brush, 1.2f * scale);
                    return;
                }
                case MarkupTool::Inpaint: {
                    // 魔法消除棒 ✨ (45° 魔法棒身 + 璀璨四芒星)
                    m_renderTarget->DrawLine(D2D1::Point2F(cx - 4.5f * scale, cy + 5.0f * scale),
                                             D2D1::Point2F(cx + 1.5f * scale, cy - 1.0f * scale), brush, 2.0f * scale);
                    D2D1_POINT_2F sCenter{cx + 3.5f * scale, cy - 3.5f * scale};
                    m_renderTarget->DrawLine(D2D1::Point2F(sCenter.x, sCenter.y - 3.0f * scale),
                                             D2D1::Point2F(sCenter.x, sCenter.y + 3.0f * scale), brush, 1.4f * scale);
                    m_renderTarget->DrawLine(D2D1::Point2F(sCenter.x - 3.0f * scale, sCenter.y),
                                             D2D1::Point2F(sCenter.x + 3.0f * scale, sCenter.y), brush, 1.4f * scale);
                    return;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    if (!button.label.empty()) {
        m_renderTarget->DrawText(
            button.label.c_str(), static_cast<UINT32>(button.label.size()),
            m_infoTextFormat.Get(), rect, brush);
    }
}

void CaptureRenderer::drawGlassPanel(const D2D1_RECT_F& rect, float radius, bool seeThrough) {
    if (!m_renderTarget) return;
    auto rr = D2D1::RoundedRect(rect, radius, radius);

    const bool isDark = isDarkThemeActive();
    ComPtr<ID2D1SolidColorBrush> tint, sheen, border;
    if (isDark) {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.11f, 0.15f, 0.88f), tint.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), sheen.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), border.GetAddressOf());
    } else {
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.97f, 0.97f, 0.99f, 0.92f), tint.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), sheen.GetAddressOf());
        m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.12f), border.GetAddressOf());
    }

    // 透视玻璃：圆角裁剪 → 透出未暗化的背后画面 → 深/浅色蒙版
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
        // 磨砂效果：半透明深/浅色直接叠加在当前帧上
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
