#include "ui/native/graphics/VectorIconRenderer.h"
#include <algorithm>
#include <cmath>

namespace tools3000::ui::native {

using namespace Microsoft::WRL;

void VectorIconRenderer::drawIcon(
    UIRenderContext& ctx,
    IconType type,
    const Rect& bounds,
    const Color& color,
    float strokeWidth
) {
    auto rt = ctx.target();
    if (!rt || type == IconType::None || color.a <= 0.001f) return;

    const float iconSize = std::min(bounds.width(), bounds.height());
    if (iconSize <= 2.0f) return;

    // 居中偏移与 24x24 栅格比例映射
    const float scale = iconSize / 24.0f;
    const float offsetX = bounds.left + (bounds.width() - iconSize) * 0.5f;
    const float offsetY = bounds.top + (bounds.height() - iconSize) * 0.5f;

    auto brush = ctx.getSolidBrush(color);
    if (!brush) return;

    // 获取圆角描边样式 (Round Cap & Round Join)
    static ComPtr<ID2D1Factory> cachedFactory;
    static ComPtr<ID2D1StrokeStyle> strokeStyle;
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.GetAddressOf());
    if (!strokeStyle || factory.Get() != cachedFactory.Get()) {
        cachedFactory = factory;
        strokeStyle.Reset();
        if (factory) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND
            );
            factory->CreateStrokeStyle(props, nullptr, 0, strokeStyle.GetAddressOf());
        }
    }

    auto P = [&](float x, float y) -> D2D1_POINT_2F {
        return D2D1::Point2F(offsetX + x * scale, offsetY + y * scale);
    };

    const float strokeW = std::max(1.0f, strokeWidth * scale);

    auto line = [&](float x1, float y1, float x2, float y2) {
        rt->DrawLine(P(x1, y1), P(x2, y2), brush, strokeW, strokeStyle.Get());
    };

    auto circle = [&](float cx, float cy, float r) {
        D2D1_ELLIPSE ell = D2D1::Ellipse(P(cx, cy), r * scale, r * scale);
        rt->DrawEllipse(ell, brush, strokeW, strokeStyle.Get());
    };

    auto fillCircle = [&](float cx, float cy, float r) {
        D2D1_ELLIPSE ell = D2D1::Ellipse(P(cx, cy), r * scale, r * scale);
        rt->FillEllipse(ell, brush);
    };

    auto rect = [&](float l, float t, float r, float b, float rad = 0.0f) {
        D2D1_RECT_F drc = D2D1::RectF(offsetX + l * scale, offsetY + t * scale, offsetX + r * scale, offsetY + b * scale);
        if (rad > 0.0f) {
            rt->DrawRoundedRectangle(D2D1::RoundedRect(drc, rad * scale, rad * scale), brush, strokeW, strokeStyle.Get());
        } else {
            rt->DrawRectangle(drc, brush, strokeW, strokeStyle.Get());
        }
    };

    switch (type) {
    case IconType::Check:
        line(4.0f, 12.0f, 9.0f, 17.0f);
        line(9.0f, 17.0f, 20.0f, 6.0f);
        break;

    case IconType::X:
        line(18.0f, 6.0f, 6.0f, 18.0f);
        line(6.0f, 6.0f, 18.0f, 18.0f);
        break;

    case IconType::ChevronDown:
        line(6.0f, 9.0f, 12.0f, 15.0f);
        line(12.0f, 15.0f, 18.0f, 9.0f);
        break;

    case IconType::ChevronUp:
        line(18.0f, 15.0f, 12.0f, 9.0f);
        line(12.0f, 9.0f, 6.0f, 15.0f);
        break;

    case IconType::ChevronRight:
        line(9.0f, 18.0f, 15.0f, 12.0f);
        line(15.0f, 12.0f, 9.0f, 6.0f);
        break;

    case IconType::ChevronLeft:
        line(15.0f, 18.0f, 9.0f, 12.0f);
        line(9.0f, 12.0f, 15.0f, 6.0f);
        break;

    case IconType::Plus:
        line(12.0f, 5.0f, 12.0f, 19.0f);
        line(5.0f, 12.0f, 19.0f, 12.0f);
        break;

    case IconType::Trash2:
        line(3.0f, 6.0f, 21.0f, 6.0f);
        line(10.0f, 11.0f, 10.0f, 17.0f);
        line(14.0f, 11.0f, 14.0f, 17.0f);
        rect(5.0f, 6.0f, 19.0f, 21.0f, 2.0f);
        line(8.0f, 6.0f, 9.0f, 3.0f);
        line(9.0f, 3.0f, 15.0f, 3.0f);
        line(15.0f, 3.0f, 16.0f, 6.0f);
        break;

    case IconType::Edit3:
        line(12.0f, 20.0f, 20.0f, 20.0f);
        line(16.5f, 3.5f, 20.5f, 7.5f);
        line(4.0f, 16.0f, 16.5f, 3.5f);
        line(4.0f, 16.0f, 4.0f, 20.0f);
        line(4.0f, 20.0f, 8.0f, 20.0f);
        break;

    case IconType::Power:
        line(12.0f, 2.0f, 12.0f, 12.0f);
        circle(12.0f, 13.0f, 8.0f);
        break;

    case IconType::RefreshCw:
        circle(12.0f, 12.0f, 8.0f);
        line(20.0f, 4.0f, 20.0f, 9.0f);
        line(15.0f, 9.0f, 20.0f, 9.0f);
        break;

    case IconType::Play:
        line(5.0f, 3.0f, 19.0f, 12.0f);
        line(19.0f, 12.0f, 5.0f, 21.0f);
        line(5.0f, 21.0f, 5.0f, 3.0f);
        break;

    case IconType::Pause:
        line(6.0f, 4.0f, 6.0f, 20.0f);
        line(10.0f, 4.0f, 10.0f, 20.0f);
        line(14.0f, 4.0f, 14.0f, 20.0f);
        line(18.0f, 4.0f, 18.0f, 20.0f);
        break;

    case IconType::Copy:
        rect(8.0f, 8.0f, 20.0f, 20.0f, 2.0f);
        line(4.0f, 16.0f, 4.0f, 4.0f);
        line(4.0f, 4.0f, 16.0f, 4.0f);
        break;

    case IconType::Sliders:
        line(4.0f, 21.0f, 4.0f, 14.0f);
        line(4.0f, 10.0f, 4.0f, 3.0f);
        line(12.0f, 21.0f, 12.0f, 12.0f);
        line(12.0f, 8.0f, 12.0f, 3.0f);
        line(20.0f, 21.0f, 20.0f, 16.0f);
        line(20.0f, 12.0f, 20.0f, 3.0f);
        line(1.0f, 14.0f, 7.0f, 14.0f);
        line(9.0f, 8.0f, 15.0f, 8.0f);
        line(17.0f, 16.0f, 23.0f, 16.0f);
        break;

    case IconType::Sun:
        circle(12.0f, 12.0f, 4.0f);
        line(12.0f, 2.0f, 12.0f, 4.0f);
        line(12.0f, 20.0f, 12.0f, 22.0f);
        line(2.0f, 12.0f, 4.0f, 12.0f);
        line(20.0f, 12.0f, 22.0f, 12.0f);
        line(4.93f, 4.93f, 6.34f, 6.34f);
        line(17.66f, 17.66f, 19.07f, 19.07f);
        line(4.93f, 19.07f, 6.34f, 17.66f);
        line(17.66f, 6.34f, 19.07f, 4.93f);
        break;

    case IconType::Moon:
        circle(12.0f, 12.0f, 8.0f);
        fillCircle(14.0f, 10.0f, 6.5f);
        break;

    case IconType::Monitor:
        rect(2.0f, 3.0f, 22.0f, 17.0f, 2.0f);
        line(8.0f, 21.0f, 16.0f, 21.0f);
        line(12.0f, 17.0f, 12.0f, 21.0f);
        break;

    case IconType::Settings:
        circle(12.0f, 12.0f, 3.2f);
        circle(12.0f, 12.0f, 7.8f);
        line(12.0f, 2.0f, 12.0f, 4.5f);
        line(12.0f, 19.5f, 12.0f, 22.0f);
        line(2.0f, 12.0f, 4.5f, 12.0f);
        line(19.5f, 12.0f, 22.0f, 12.0f);
        break;

    case IconType::Puzzle:
        rect(4.0f, 4.0f, 20.0f, 20.0f, 2.5f);
        circle(12.0f, 4.0f, 2.5f);
        circle(20.0f, 12.0f, 2.5f);
        break;

    case IconType::Search:
        circle(11.0f, 11.0f, 6.5f);
        line(16.0f, 16.0f, 21.0f, 21.0f);
        break;

    case IconType::Hand:
        rect(6.0f, 11.0f, 18.0f, 21.0f, 3.0f);
        line(10.0f, 4.0f, 10.0f, 11.0f);
        line(14.0f, 5.0f, 14.0f, 11.0f);
        line(6.0f, 13.0f, 3.0f, 16.0f);
        break;

    case IconType::Maximize2:
        line(15.0f, 3.0f, 21.0f, 3.0f);
        line(21.0f, 3.0f, 21.0f, 9.0f);
        line(21.0f, 3.0f, 14.0f, 10.0f);
        line(9.0f, 21.0f, 3.0f, 21.0f);
        line(3.0f, 21.0f, 3.0f, 15.0f);
        line(3.0f, 21.0f, 10.0f, 14.0f);
        break;

    case IconType::Crop:
        line(6.0f, 2.0f, 6.0f, 18.0f);
        line(6.0f, 18.0f, 22.0f, 18.0f);
        line(18.0f, 6.0f, 2.0f, 6.0f);
        line(18.0f, 6.0f, 18.0f, 22.0f);
        break;

    case IconType::Clock:
        circle(12.0f, 12.0f, 8.5f);
        line(12.0f, 7.0f, 12.0f, 12.0f);
        line(12.0f, 12.0f, 16.0f, 14.0f);
        break;

    case IconType::FileText:
        rect(4.0f, 2.0f, 20.0f, 22.0f, 2.0f);
        line(8.0f, 8.0f, 16.0f, 8.0f);
        line(8.0f, 12.0f, 16.0f, 12.0f);
        line(8.0f, 16.0f, 12.0f, 16.0f);
        break;

    case IconType::Keyboard:
        rect(2.0f, 4.0f, 22.0f, 20.0f, 2.5f);
        line(6.0f, 8.0f, 7.0f, 8.0f);
        line(10.0f, 8.0f, 11.0f, 8.0f);
        line(14.0f, 8.0f, 15.0f, 8.0f);
        line(18.0f, 8.0f, 19.0f, 8.0f);
        line(6.0f, 12.0f, 7.0f, 12.0f);
        line(10.0f, 12.0f, 14.0f, 12.0f);
        line(17.0f, 12.0f, 19.0f, 12.0f);
        line(7.0f, 16.0f, 17.0f, 16.0f);
        break;

    case IconType::Compass:
        circle(12.0f, 12.0f, 9.0f);
        line(16.24f, 7.76f, 14.12f, 14.12f);
        line(14.12f, 14.12f, 7.76f, 16.24f);
        line(7.76f, 16.24f, 9.88f, 9.88f);
        line(9.88f, 9.88f, 16.24f, 7.76f);
        break;

    case IconType::FolderOpen:
        line(2.0f, 11.0f, 22.0f, 11.0f);
        line(2.0f, 11.0f, 4.0f, 21.0f);
        line(4.0f, 21.0f, 20.0f, 21.0f);
        line(20.0f, 21.0f, 22.0f, 11.0f);
        line(2.0f, 11.0f, 2.0f, 5.0f);
        line(2.0f, 5.0f, 8.0f, 5.0f);
        line(8.0f, 5.0f, 10.0f, 8.0f);
        line(10.0f, 8.0f, 20.0f, 8.0f);
        line(20.0f, 8.0f, 20.0f, 11.0f);
        break;

    case IconType::Cpu:
        rect(4.0f, 4.0f, 20.0f, 20.0f, 2.0f);
        rect(9.0f, 9.0f, 15.0f, 15.0f, 1.0f);
        line(9.0f, 1.0f, 9.0f, 4.0f);
        line(15.0f, 1.0f, 15.0f, 4.0f);
        line(9.0f, 20.0f, 9.0f, 23.0f);
        line(15.0f, 20.0f, 15.0f, 23.0f);
        line(1.0f, 9.0f, 4.0f, 9.0f);
        line(1.0f, 15.0f, 4.0f, 15.0f);
        line(20.0f, 9.0f, 23.0f, 9.0f);
        line(20.0f, 15.0f, 23.0f, 15.0f);
        break;

    case IconType::BarChart3:
        line(18.0f, 20.0f, 18.0f, 10.0f);
        line(12.0f, 20.0f, 12.0f, 4.0f);
        line(6.0f, 20.0f, 6.0f, 14.0f);
        break;

    case IconType::Info:
        circle(12.0f, 12.0f, 9.0f);
        line(12.0f, 16.0f, 12.0f, 12.0f);
        line(12.0f, 8.0f, 12.0f, 8.5f);
        break;

    case IconType::Shield:
        line(12.0f, 2.0f, 19.0f, 5.0f);
        line(19.0f, 5.0f, 19.0f, 11.0f);
        line(19.0f, 11.0f, 12.0f, 21.0f);
        line(12.0f, 21.0f, 5.0f, 11.0f);
        line(5.0f, 11.0f, 5.0f, 5.0f);
        line(5.0f, 5.0f, 12.0f, 2.0f);
        break;

    case IconType::Zap:
        line(13.0f, 2.0f, 3.0f, 14.0f);
        line(3.0f, 14.0f, 12.0f, 14.0f);
        line(12.0f, 14.0f, 11.0f, 22.0f);
        line(11.0f, 22.0f, 21.0f, 10.0f);
        line(21.0f, 10.0f, 12.0f, 10.0f);
        line(12.0f, 10.0f, 13.0f, 2.0f);
        break;

    case IconType::Sparkles:
        line(12.0f, 3.0f, 14.0f, 9.0f);
        line(14.0f, 9.0f, 20.0f, 11.0f);
        line(20.0f, 11.0f, 14.0f, 13.0f);
        line(14.0f, 13.0f, 12.0f, 19.0f);
        line(12.0f, 19.0f, 10.0f, 13.0f);
        line(10.0f, 13.0f, 4.0f, 11.0f);
        line(4.0f, 11.0f, 10.0f, 9.0f);
        line(10.0f, 9.0f, 12.0f, 3.0f);
        break;

    case IconType::Minimize:
        line(4.0f, 12.0f, 20.0f, 12.0f);
        break;

    case IconType::Maximize:
        rect(5.0f, 5.0f, 19.0f, 19.0f, 1.5f);
        break;

    case IconType::Restore:
        // 后方方框
        line(8.0f, 5.0f, 19.0f, 5.0f);
        line(19.0f, 5.0f, 19.0f, 16.0f);
        line(15.0f, 16.0f, 19.0f, 16.0f);
        line(8.0f, 5.0f, 8.0f, 9.0f);
        // 前方方框
        rect(5.0f, 9.0f, 15.0f, 19.0f, 1.2f);
        break;

    case IconType::Folder:
        line(2.0f, 6.0f, 9.0f, 6.0f);
        line(9.0f, 6.0f, 11.0f, 9.0f);
        line(11.0f, 9.0f, 22.0f, 9.0f);
        rect(2.0f, 9.0f, 22.0f, 20.0f, 2.0f);
        break;

    case IconType::Palette:
        circle(12.0f, 12.0f, 9.5f);
        fillCircle(8.0f, 10.0f, 1.2f);
        fillCircle(12.0f, 7.5f, 1.2f);
        fillCircle(16.0f, 10.0f, 1.2f);
        fillCircle(14.0f, 14.5f, 1.2f);
        break;

    case IconType::Camera:
        rect(3.0f, 7.0f, 21.0f, 20.0f, 2.5f);
        circle(12.0f, 13.5f, 3.5f);
        line(8.0f, 7.0f, 9.5f, 4.0f);
        line(9.5f, 4.0f, 14.5f, 4.0f);
        line(14.5f, 4.0f, 16.0f, 7.0f);
        break;

    case IconType::Video:
        rect(2.0f, 6.0f, 16.0f, 18.0f, 2.0f);
        line(16.0f, 10.0f, 22.0f, 7.0f);
        line(22.0f, 7.0f, 22.0f, 17.0f);
        line(22.0f, 17.0f, 16.0f, 14.0f);
        break;

    case IconType::Music:
        circle(9.0f, 18.0f, 3.0f);
        circle(18.0f, 15.0f, 3.0f);
        line(12.0f, 18.0f, 12.0f, 6.0f);
        line(21.0f, 15.0f, 21.0f, 3.0f);
        line(12.0f, 6.0f, 21.0f, 3.0f);
        break;

    case IconType::FileDigit:
        line(4.0f, 2.0f, 14.0f, 2.0f);
        line(14.0f, 2.0f, 20.0f, 8.0f);
        line(20.0f, 8.0f, 20.0f, 22.0f);
        line(20.0f, 22.0f, 4.0f, 22.0f);
        line(4.0f, 22.0f, 4.0f, 2.0f);
        line(14.0f, 2.0f, 14.0f, 8.0f);
        line(14.0f, 8.0f, 20.0f, 8.0f);
        line(9.0f, 12.0f, 9.0f, 17.0f);
        line(13.0f, 12.0f, 15.0f, 12.0f);
        line(15.0f, 12.0f, 15.0f, 17.0f);
        line(13.0f, 17.0f, 15.0f, 17.0f);
        break;

    case IconType::ExternalLink:
        line(18.0f, 13.0f, 18.0f, 20.0f);
        line(18.0f, 20.0f, 4.0f, 20.0f);
        line(4.0f, 20.0f, 4.0f, 6.0f);
        line(4.0f, 6.0f, 11.0f, 6.0f);
        line(15.0f, 3.0f, 21.0f, 3.0f);
        line(21.0f, 3.0f, 21.0f, 9.0f);
        line(10.0f, 14.0f, 21.0f, 3.0f);
        break;

    case IconType::Crosshair:
        circle(12.0f, 12.0f, 8.0f);
        line(12.0f, 2.0f, 12.0f, 6.0f);
        line(12.0f, 18.0f, 12.0f, 22.0f);
        line(2.0f, 12.0f, 6.0f, 12.0f);
        line(18.0f, 12.0f, 22.0f, 12.0f);
        break;

    case IconType::Flame:
        line(8.5f, 14.5f, 12.0f, 3.0f);
        line(12.0f, 3.0f, 16.5f, 10.0f);
        line(16.5f, 10.0f, 19.0f, 12.0f);
        line(19.0f, 12.0f, 17.0f, 19.5f);
        line(17.0f, 19.5f, 7.0f, 19.5f);
        line(7.0f, 19.5f, 5.0f, 14.0f);
        line(5.0f, 14.0f, 8.5f, 14.5f);
        circle(12.0f, 16.0f, 2.5f);
        break;

    case IconType::LogOut:
        line(9.0f, 21.0f, 4.0f, 21.0f);
        line(4.0f, 21.0f, 4.0f, 3.0f);
        line(4.0f, 3.0f, 9.0f, 3.0f);
        line(16.0f, 17.0f, 21.0f, 12.0f);
        line(21.0f, 12.0f, 16.0f, 7.0f);
        line(21.0f, 12.0f, 9.0f, 12.0f);
        break;

    case IconType::Bot:
        rect(4.0f, 8.0f, 20.0f, 20.0f, 3.0f);
        line(12.0f, 8.0f, 12.0f, 4.0f);
        fillCircle(12.0f, 3.0f, 1.2f);
        line(2.0f, 14.0f, 4.0f, 14.0f);
        line(20.0f, 14.0f, 22.0f, 14.0f);
        fillCircle(9.0f, 14.0f, 1.2f);
        fillCircle(15.0f, 14.0f, 1.2f);
        line(8.5f, 17.0f, 15.5f, 17.0f);
        break;

    case IconType::HelpCircle:
        circle(12.0f, 12.0f, 9.0f);
        line(9.09f, 9.0f, 12.0f, 9.0f);
        line(12.0f, 9.0f, 12.0f, 13.0f);
        fillCircle(12.0f, 16.0f, 1.0f);
        break;

    case IconType::Ban:
        circle(12.0f, 12.0f, 9.0f);
        line(5.64f, 5.64f, 18.36f, 18.36f);
        break;

    case IconType::CheckCircle:
        circle(12.0f, 12.0f, 9.0f);
        line(8.0f, 12.0f, 11.0f, 15.0f);
        line(11.0f, 15.0f, 16.0f, 9.0f);
        break;

    case IconType::Code:
        line(16.0f, 18.0f, 22.0f, 12.0f);
        line(22.0f, 12.0f, 16.0f, 6.0f);
        line(8.0f, 6.0f, 2.0f, 12.0f);
        line(2.0f, 12.0f, 8.0f, 18.0f);
        break;

    case IconType::Pin:
        line(12.0f, 17.0f, 12.0f, 22.0f);
        line(5.0f, 9.0f, 19.0f, 9.0f);
        line(9.0f, 4.0f, 15.0f, 4.0f);
        line(9.0f, 4.0f, 7.0f, 9.0f);
        line(15.0f, 4.0f, 17.0f, 9.0f);
        line(7.0f, 9.0f, 9.0f, 17.0f);
        line(17.0f, 9.0f, 15.0f, 17.0f);
        line(9.0f, 17.0f, 15.0f, 17.0f);
        break;

    default:
        break;
    }
}

} // namespace tools3000::ui::native
