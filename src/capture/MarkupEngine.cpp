// ─────────────────────────────────────────────────────────────────────────────
// MarkupEngine.cpp — 截图标注引擎实现
//
// 所有标注元素使用 OpenCV 绘制:
//   - 矩形: cv::rectangle
//   - 箭头: cv::arrowedLine
//   - 椭圆: cv::ellipse
//   - 画笔: cv::polylines
//   - 高亮: cv::Mat ROI + alpha blending
//   - 马赛克: 区域像素块化
//   - 文本: cv::putText
//   - 序号: 圆形背景 + 数字
//   - 放大镜: ROI 放大 + 圆形裁剪
//   - 聚光灯: 半透明黑色遮罩 + 选区孔洞
//   - 水印: 旋转文字平铺 + alpha 混合
//   - 智能消除: cv::inpaint (TELEA) 背景重建
// ─────────────────────────────────────────────────────────────────────────────

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "capture/MarkupEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <algorithm>
#include <windows.h>

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <cmath>
#include <limits>
#include <mutex>

namespace {
    std::vector<cv::Point> generateSmoothSpline(const std::vector<cv::Point>& pts, int stepsPerSegment = 10) {
        if (pts.size() < 3) return pts;
        
        std::vector<cv::Point> extended(pts.size() + 2);
        extended[0] = pts[0] - (pts[1] - pts[0]);
        for (size_t i = 0; i < pts.size(); ++i) extended[i+1] = pts[i];
        extended[extended.size()-1] = pts.back() + (pts.back() - pts[pts.size()-2]);

        std::vector<cv::Point> smoothPts;
        smoothPts.reserve((pts.size() - 1) * stepsPerSegment + 1);

        for (size_t i = 1; i < extended.size() - 2; ++i) {
            cv::Point2f p0 = extended[i-1], p1 = extended[i], p2 = extended[i+1], p3 = extended[i+2];
            for (int t_i = 0; t_i < (i == extended.size()-3 ? stepsPerSegment + 1 : stepsPerSegment); ++t_i) {
                float t = static_cast<float>(t_i) / stepsPerSegment;
                float t2 = t * t;
                float t3 = t2 * t;
                float x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
                float y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
                smoothPts.push_back(cv::Point(static_cast<int>(x), static_cast<int>(y)));
            }
        }
        return smoothPts;
    }

    std::mutex g_gdiPlusMutex;
    ULONG_PTR g_gdiPlusToken = 0;

    std::wstring utf8ToWide(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring wstr(len - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], len);
        return wstr;
    }

    void blendOverlay(cv::Mat& canvas, const cv::Mat& fgMat, int startX, int startY) {
        if (canvas.empty() || fgMat.empty()) return;
        int szH = fgMat.rows;
        int szW = fgMat.cols;

        if (canvas.channels() == 3) {
            for (int y = 0; y < szH; ++y) {
                int targetY = startY + y;
                if (targetY < 0 || targetY >= canvas.rows) continue;
                cv::Vec3b* rowBg = canvas.ptr<cv::Vec3b>(targetY);
                const cv::Vec4b* rowFg = fgMat.ptr<cv::Vec4b>(y);
                for (int x = 0; x < szW; ++x) {
                    int targetX = startX + x;
                    if (targetX < 0 || targetX >= canvas.cols) continue;

                    const cv::Vec4b& fg = rowFg[x];
                    float alpha = fg[3] / 255.0f;
                    if (alpha > 0.001f) {
                        cv::Vec3b& bg = rowBg[targetX];
                        bg[0] = static_cast<uchar>(fg[0] * alpha + bg[0] * (1.0f - alpha));
                        bg[1] = static_cast<uchar>(fg[1] * alpha + bg[1] * (1.0f - alpha));
                        bg[2] = static_cast<uchar>(fg[2] * alpha + bg[2] * (1.0f - alpha));
                    }
                }
            }
        } else if (canvas.channels() == 4) {
            for (int y = 0; y < szH; ++y) {
                int targetY = startY + y;
                if (targetY < 0 || targetY >= canvas.rows) continue;
                cv::Vec4b* rowBg = canvas.ptr<cv::Vec4b>(targetY);
                const cv::Vec4b* rowFg = fgMat.ptr<cv::Vec4b>(y);
                for (int x = 0; x < szW; ++x) {
                    int targetX = startX + x;
                    if (targetX < 0 || targetX >= canvas.cols) continue;

                    const cv::Vec4b& fg = rowFg[x];
                    float alpha = fg[3] / 255.0f;
                    if (alpha > 0.001f) {
                        cv::Vec4b& bg = rowBg[targetX];
                        bg[0] = static_cast<uchar>(fg[0] * alpha + bg[0] * (1.0f - alpha));
                        bg[1] = static_cast<uchar>(fg[1] * alpha + bg[1] * (1.0f - alpha));
                        bg[2] = static_cast<uchar>(fg[2] * alpha + bg[2] * (1.0f - alpha));
                        bg[3] = 255;
                    }
                }
            }
        }
    }

    cv::Size measureSnipasteText(const std::wstring& wtext, int fontSize) {
        if (wtext.empty()) return {24, fontSize + 8};
        Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
        const Gdiplus::FontFamily* activeFamily = &fontFamily;
        Gdiplus::FontFamily fallbackFamily(L"Segoe UI");
        if (!fontFamily.IsAvailable()) {
            activeFamily = &fallbackFamily;
        }

        if (wtext.empty()) {
            // 空文本预留舒适编辑框尺寸 (支持用户在输入内容前拖拽 8 手柄调节大小)
            int minW = static_cast<int>(std::max(90.0f, fontSize * 4.2f));
            int minH = static_cast<int>(std::max(26.0f, fontSize * 1.4f));
            return cv::Size(minW, minH);
        }

        Gdiplus::Font font(activeFamily, static_cast<Gdiplus::REAL>(fontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        HDC hdc = GetDC(nullptr);
        Gdiplus::Graphics graphics(hdc);
        Gdiplus::RectF boundRect;
        Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
        graphics.MeasureString(wtext.c_str(), -1, &font, Gdiplus::PointF(0, 0), &format, &boundRect);
        ReleaseDC(nullptr, hdc);
        int w = static_cast<int>(std::ceil(boundRect.Width)) + 8;
        int h = static_cast<int>(std::ceil(boundRect.Height)) + 6;
        return cv::Size(std::max(w, 24), std::max(h, fontSize + 8));
    }

    void renderSnipasteStyleText(cv::Mat& canvas, const std::string& text, cv::Point pt, const cv::Scalar& color, int fontSize, bool isEditing, bool withBackdrop, cv::Size& outSize) {
        std::wstring wtext = utf8ToWide(text);
        if (wtext.empty() && !isEditing) {
            outSize = cv::Size(0, 0);
            return;
        }

        cv::Size sz = measureSnipasteText(wtext, fontSize);
        outSize = sz;
        if (sz.width <= 0 || sz.height <= 0) return;

        cv::Mat textMat(sz, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        {
            Gdiplus::Bitmap bitmap(sz.width, sz.height, static_cast<INT>(textMat.step), PixelFormat32bppARGB, textMat.data);
            Gdiplus::Graphics g(&bitmap);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            // 采用纯净灰度抗锯齿 (TextRenderingHintAntiAliasGridFit)，严禁在透明图层上使用 ClearType 造成彩色/黑白栅格条纹
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

            BYTE rVal = static_cast<BYTE>(color[2]);
            BYTE gVal = static_cast<BYTE>(color[1]);
            BYTE bVal = static_cast<BYTE>(color[0]);
            float luminance = (0.299f * rVal + 0.587f * gVal + 0.114f * bVal) / 255.0f;

            // 1. 文字绘制 (纯透明底衬 + 高对比描边 + 矢量文字填充)
            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei UI");
            const Gdiplus::FontFamily* activeFamily = &fontFamily;
            Gdiplus::FontFamily fallbackFamily(L"Segoe UI");
            if (!fontFamily.IsAvailable()) {
                activeFamily = &fallbackFamily;
            }

            Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
            format.SetAlignment(Gdiplus::StringAlignmentNear);
            format.SetLineAlignment(Gdiplus::StringAlignmentNear);

            float textX = 4.0f;
            float textY = 3.0f;

            if (withBackdrop) {
                Gdiplus::SolidBrush bgBrush(luminance > 0.45f ? Gdiplus::Color(210, 15, 23, 42) : Gdiplus::Color(210, 255, 255, 255));
                Gdiplus::GraphicsPath bgPath;
                float padX = 2.0f, padY = 1.0f;
                float r = 4.0f;
                float bx = textX - padX, by = textY - padY, bw = sz.width - 4.0f, bh = sz.height - 2.0f;
                bgPath.AddArc(bx, by, r * 2, r * 2, 180, 90);
                bgPath.AddArc(bx + bw - r * 2, by, r * 2, r * 2, 270, 90);
                bgPath.AddArc(bx + bw - r * 2, by + bh - r * 2, r * 2, r * 2, 0, 90);
                bgPath.AddArc(bx, by + bh - r * 2, r * 2, r * 2, 90, 90);
                bgPath.CloseFigure();
                g.FillPath(&bgBrush, &bgPath);
            }

            if (!wtext.empty()) {
                Gdiplus::GraphicsPath textPath;
                textPath.AddString(wtext.c_str(), -1, activeFamily, Gdiplus::FontStyleBold, static_cast<Gdiplus::REAL>(fontSize), Gdiplus::PointF(textX, textY), &format);

                // 双层高对比抗锯齿描边（浅色文字配深色阴影描边，深色文字配浅色高光描边，无需灰黑底框）
                Gdiplus::Color strokeColor = (luminance > 0.45f) ? Gdiplus::Color(220, 0, 0, 0) : Gdiplus::Color(220, 255, 255, 255);
                Gdiplus::Pen strokePen(strokeColor, std::max(2.0f, fontSize * 0.10f));
                strokePen.SetLineJoin(Gdiplus::LineJoinRound);
                g.DrawPath(&strokePen, &textPath);

                // 正文矢量文字填充
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, rVal, gVal, bVal));
                g.FillPath(&textBrush, &textPath);
            } else if (isEditing) {
                // 空文本编辑态：绘制柔和的极细占位微虚线边框
                Gdiplus::Pen dashPen(Gdiplus::Color(120, 59, 130, 246), 1.0f);
                dashPen.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&dashPen, textX, textY, static_cast<float>(sz.width - 8), static_cast<float>(sz.height - 6));
            }

            // 2. 编辑态光标绘制
            if (isEditing) {
                bool showCursor = ((GetTickCount() / 450) % 2 == 0) || wtext.empty();
                if (showCursor) {
                    float cursorX = textX + 2.0f;
                    if (!wtext.empty()) {
                        Gdiplus::Font font(activeFamily, static_cast<Gdiplus::REAL>(fontSize), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                        Gdiplus::RectF measured;
                        g.MeasureString(wtext.c_str(), -1, &font, Gdiplus::PointF(0, 0), &format, &measured);
                        cursorX = textX + measured.Width + 2.0f;
                    }
                    Gdiplus::Pen cursorPen(Gdiplus::Color(255, rVal, gVal, bVal), 2.0f);
                    g.DrawLine(&cursorPen, cursorX, textY + 2.0f, cursorX, textY + static_cast<float>(fontSize) + 2.0f);
                }
            }
        }

        // 4. 高性能自适应 Alpha 混合到 canvas (精准处理 3 通道 BGR 与 4 通道 BGRA，杜绝跨步错位栅格)
        blendOverlay(canvas, textMat, pt.x, pt.y);
    }

    void renderSnipasteStyleNumberBadge(cv::Mat& canvas, int number, cv::Point center, const cv::Scalar& color, bool fill = true, float dpiScale = 1.0f) {
        float scale = dpiScale > 0.0f ? dpiScale : 1.0f;
        int radius = static_cast<int>(std::round(15.0f * scale));
        int sz = radius * 2 + static_cast<int>(std::round(10.0f * scale));
        cv::Mat badgeMat(sz, sz, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        {
            Gdiplus::Bitmap bitmap(sz, sz, static_cast<INT>(badgeMat.step), PixelFormat32bppARGB, badgeMat.data);
            Gdiplus::Graphics g(&bitmap);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

            float cx = sz / 2.0f;
            float cy = sz / 2.0f;

            BYTE rVal = static_cast<BYTE>(color[2]);
            BYTE gVal = static_cast<BYTE>(color[1]);
            BYTE bVal = static_cast<BYTE>(color[0]);

            if (fill) {
                // 1. 实心模式：柔和外层投影 + 主题色彩圆盘 + 1.5px 白色高光内圈
                Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(100, 0, 0, 0));
                g.FillEllipse(&shadowBrush, cx - radius + 1.5f * scale, cy - radius + 2.5f * scale, radius * 2.0f, radius * 2.0f);

                Gdiplus::SolidBrush circleBrush(Gdiplus::Color(255, rVal, gVal, bVal));
                g.FillEllipse(&circleBrush, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

                Gdiplus::Pen ringPen(Gdiplus::Color(220, 255, 255, 255), 1.5f * scale);
                g.DrawEllipse(&ringPen, cx - radius + 1.0f * scale, cy - radius + 1.0f * scale, (radius - 1.0f * scale) * 2.0f, (radius - 1.0f * scale) * 2.0f);
            } else {
                // 2. 空心模式：半透明纯白底 + 主题色彩外环
                Gdiplus::SolidBrush bgBrush(Gdiplus::Color(230, 255, 255, 255));
                g.FillEllipse(&bgBrush, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

                Gdiplus::Pen outlinePen(Gdiplus::Color(255, rVal, gVal, bVal), 2.2f * scale);
                g.DrawEllipse(&outlinePen, cx - radius + 1.0f * scale, cy - radius + 1.0f * scale, (radius - 1.0f * scale) * 2.0f, (radius - 1.0f * scale) * 2.0f);
            }

            // 4. 数字矢量排版
            std::wstring numWStr = std::to_wstring(number);
            Gdiplus::FontFamily fontFamily(L"Segoe UI");
            float fontSize = ((number >= 100) ? 11.0f : (number >= 10 ? 13.0f : 15.0f)) * scale;
            Gdiplus::Font font(&fontFamily, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::SolidBrush numBrush(fill ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, rVal, gVal, bVal));
            Gdiplus::RectF layoutRect(cx - radius, cy - radius - 0.5f * scale, radius * 2.0f, radius * 2.0f);
            g.DrawString(numWStr.c_str(), -1, &font, layoutRect, &format, &numBrush);
        }

        int startX = center.x - sz / 2;
        int startY = center.y - sz / 2;
        blendOverlay(canvas, badgeMat, startX, startY);
    }
}

#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>

namespace easy::capture {

bool initializeMarkupTextRenderer() {
    std::lock_guard lock(g_gdiPlusMutex);
    if (g_gdiPlusToken != 0) return true;

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    const auto status = Gdiplus::GdiplusStartup(&token, &input, nullptr);
    if (status != Gdiplus::Ok) {
        LOG_ERROR("GDI+ 文本渲染器初始化失败, status={}", static_cast<int>(status));
        return false;
    }
    g_gdiPlusToken = token;
    LOG_DEBUG("GDI+ 文本渲染器已初始化");
    return true;
}

void shutdownMarkupTextRenderer() {
    ULONG_PTR token = 0;
    {
        std::lock_guard lock(g_gdiPlusMutex);
        token = g_gdiPlusToken;
        g_gdiPlusToken = 0;
    }
    if (token != 0) {
        // Deliberately before FreeLibrary and without the mutex: GDI+ waits for
        // its helper thread during shutdown.
        Gdiplus::GdiplusShutdown(token);
        LOG_DEBUG("GDI+ 文本渲染器已关闭");
    }
}

static std::atomic<uint32_t> g_elementIdCounter{1};

// ─────────────────────────────────────────────────────────────────────────────
// MarkupElement 矢量操作
// ─────────────────────────────────────────────────────────────────────────────

cv::Rect MarkupElement::getBoundingBox() const {
    switch (tool) {
        case MarkupTool::Rectangle:
        case MarkupTool::Highlight:
        case MarkupTool::Mosaic:
        case MarkupTool::Spotlight:
        case MarkupTool::Watermark:
        case MarkupTool::Inpaint: {
            int x1 = std::min(startPt.x, endPt.x);
            int y1 = std::min(startPt.y, endPt.y);
            int x2 = std::max(startPt.x, endPt.x);
            int y2 = std::max(startPt.y, endPt.y);
            return cv::Rect(x1, y1, x2 - x1, y2 - y1);
        }
        case MarkupTool::Arrow: {
            int x1 = std::min(startPt.x, endPt.x);
            int y1 = std::min(startPt.y, endPt.y);
            int x2 = std::max(startPt.x, endPt.x);
            int y2 = std::max(startPt.y, endPt.y);
            return cv::Rect(x1, y1, x2 - x1, y2 - y1);
        }
        case MarkupTool::Ellipse: {
            int rx = std::abs(endPt.x - startPt.x) / 2;
            int ry = std::abs(endPt.y - startPt.y) / 2;
            int cx = (startPt.x + endPt.x) / 2;
            int cy = (startPt.y + endPt.y) / 2;
            return cv::Rect(cx - rx, cy - ry, rx * 2, ry * 2);
        }
        case MarkupTool::Text: {
            if (textRenderSize.width <= 0 || textRenderSize.height <= 0) {
                std::wstring wtext = utf8ToWide(text);
                const_cast<MarkupElement*>(this)->textRenderSize = measureSnipasteText(wtext, static_cast<int>(fontSize));
            }
            int bw = textRenderSize.width;
            int bh = textRenderSize.height;
            if (bw < 80) bw = static_cast<int>(std::max(80.0f, fontSize * 4.0f));
            if (bh < static_cast<int>(fontSize * 1.3f)) bh = static_cast<int>(std::max(26.0f, fontSize * 1.3f));
            return cv::Rect(startPt.x, startPt.y, bw, bh);
        }
        case MarkupTool::Magnifier: {
            return cv::Rect(startPt.x - magnifierRadius, startPt.y - magnifierRadius, magnifierRadius * 2, magnifierRadius * 2);
        }
        case MarkupTool::Number: {
            int r = static_cast<int>(std::round(16.0f * (dpiScale > 0.0f ? dpiScale : 1.0f)));
            return cv::Rect(startPt.x - r, startPt.y - r, r * 2, r * 2);
        }
        case MarkupTool::Pen: {
            if (penPoints.empty()) return cv::Rect();
            int minX = penPoints[0].x, minY = penPoints[0].y;
            int maxX = minX, maxY = minY;
            for (const auto& pt : penPoints) {
                minX = std::min(minX, pt.x); minY = std::min(minY, pt.y);
                maxX = std::max(maxX, pt.x); maxY = std::max(maxY, pt.y);
            }
            return cv::Rect(minX, minY, maxX - minX, maxY - minY);
        }
        default: return cv::Rect();
    }
}

HitArea MarkupElement::hitTestEx(cv::Point pt, int padding) const {
    cv::Rect bbox = getBoundingBox();
    
    // 如果处于激活状态，优先测试缩放手柄 (8 个方向控制点)
    if (isActive) {
        int hw = 7; // 手柄响应半宽 (14x14px 灵敏命中区)
        cv::Point handles[8] = {
            {bbox.x, bbox.y}, {bbox.x + bbox.width / 2, bbox.y}, {bbox.x + bbox.width, bbox.y},
            {bbox.x + bbox.width, bbox.y + bbox.height / 2},
            {bbox.x + bbox.width, bbox.y + bbox.height}, {bbox.x + bbox.width / 2, bbox.y + bbox.height},
            {bbox.x, bbox.y + bbox.height}, {bbox.x, bbox.y + bbox.height / 2}
        };
        HitArea areas[8] = { HitArea::LT, HitArea::T, HitArea::RT, HitArea::R, HitArea::RB, HitArea::B, HitArea::LB, HitArea::L };
        for (int i = 0; i < 8; ++i) {
            cv::Rect hrect(handles[i].x - hw, handles[i].y - hw, hw * 2, hw * 2);
            if (hrect.contains(pt)) return areas[i];
        }
    }

    // 然后测试主体区域（扩大命中容差，避免边缘漏点）
    int p = std::max(padding, 6);
    bbox.x -= p;
    bbox.y -= p;
    bbox.width += p * 2;
    bbox.height += p * 2;
    if (bbox.contains(pt)) return HitArea::Body;
    
    return HitArea::None;
}

void MarkupElement::moveBy(int dx, int dy) {
    startPt.x += dx; startPt.y += dy;
    endPt.x += dx; endPt.y += dy;
    for (auto& pt : penPoints) {
        pt.x += dx; pt.y += dy;
    }
}

void MarkupElement::resize(int dx, int dy, HitArea handle) {
    if (tool == MarkupTool::Text) {
        // 改善文本框缩放交互: 无论抓取哪个手柄，向外拖拽变大，向内变小
        int delta = 0;
        switch (handle) {
            case HitArea::LT: delta = -dx - dy; startPt.x += dx; startPt.y += dy; break;
            case HitArea::T:  delta = -dy; startPt.y += dy; break;
            case HitArea::RT: delta = dx - dy; startPt.y += dy; break;
            case HitArea::R:  delta = dx; break;
            case HitArea::RB: delta = dx + dy; break;
            case HitArea::B:  delta = dy; break;
            case HitArea::LB: delta = -dx + dy; startPt.x += dx; break;
            case HitArea::L:  delta = -dx; startPt.x += dx; break;
            default: break;
        }
        fontSize += delta * 0.35f;
        if (fontSize < 12.0f) fontSize = 12.0f;
        if (fontSize > 180.0f) fontSize = 180.0f;
        const_cast<MarkupElement*>(this)->textRenderSize = cv::Size(0,0); // 强制重算
        return;
    }
    if (tool == MarkupTool::Magnifier) {
        // 改善放大镜缩放交互
        int delta = 0;
        switch (handle) {
            case HitArea::LT: delta = -dx - dy; break;
            case HitArea::T:  delta = -dy; break;
            case HitArea::RT: delta = dx - dy; break;
            case HitArea::R:  delta = dx; break;
            case HitArea::RB: delta = dx + dy; break;
            case HitArea::B:  delta = dy; break;
            case HitArea::LB: delta = -dx + dy; break;
            case HitArea::L:  delta = -dx; break;
            default: break;
        }
        magnifierRadius += delta / 2;
        if (magnifierRadius < 20) magnifierRadius = 20;
        if (magnifierRadius > 500) magnifierRadius = 500;
        return;
    }
    
    // 基于拖拽手柄调整边界
    switch (handle) {
        case HitArea::LT: startPt.x += dx; startPt.y += dy; break;
        case HitArea::T:  startPt.y += dy; break;
        case HitArea::RT: endPt.x += dx; startPt.y += dy; break;
        case HitArea::R:  endPt.x += dx; break;
        case HitArea::RB: endPt.x += dx; endPt.y += dy; break;
        case HitArea::B:  endPt.y += dy; break;
        case HitArea::LB: startPt.x += dx; endPt.y += dy; break;
        case HitArea::L:  startPt.x += dx; break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 底图管理
// ─────────────────────────────────────────────────────────────────────────────

void MarkupEngine::setBaseImage(const cv::Mat& image) {
    m_baseImage = image.clone();
    m_elements.clear();
    m_undoStack.clear();
    m_nextNumber = 1;
    LOG_DEBUG("标注引擎: 设置底图 {}x{}", image.cols, image.rows);
}

void MarkupEngine::updateBaseImage(const cv::Mat& image) {
    // 仅替换底图，保留标注/撤销栈/序号状态（选区二次调整后重裁用）
    m_baseImage = image.clone();
}

void MarkupEngine::translateAll(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    for (auto& e : m_elements) e->moveBy(dx, dy);
    for (auto& e : m_undoStack) e->moveBy(dx, dy);  // 撤销栈一并平移，保证 redo 位置正确
}

cv::Mat MarkupEngine::getCompositeImage() const {
    if (m_baseImage.empty()) return {};

    cv::Mat result = m_baseImage.clone();
    renderAll(result);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 元素操作
// ─────────────────────────────────────────────────────────────────────────────

MarkupElement* MarkupEngine::addElement(std::unique_ptr<MarkupElement> element) {
    m_undoStack.clear();  // 新增元素后清空重做栈
    element->id = g_elementIdCounter++;
    m_elements.push_back(std::move(element));
    return m_elements.back().get();
}

bool MarkupEngine::undo() {
    if (m_elements.empty()) return false;

    m_undoStack.push_back(std::move(m_elements.back()));
    m_elements.pop_back();
    LOG_DEBUG("标注引擎: 撤销, 剩余元素数={}", m_elements.size());
    return true;
}

bool MarkupEngine::redo() {
    if (m_undoStack.empty()) return false;

    m_elements.push_back(std::move(m_undoStack.back()));
    m_undoStack.pop_back();
    LOG_DEBUG("标注引擎: 重做, 剩余元素数={}", m_elements.size());
    return true;
}

void MarkupEngine::clearAll() {
    m_elements.clear();
    m_undoStack.clear();
    m_nextNumber = 1;
}

void MarkupEngine::removeElement(uint32_t id) {
    m_elements.erase(std::remove_if(m_elements.begin(), m_elements.end(),
        [id](const std::unique_ptr<MarkupElement>& e) { return e->id == id; }),
        m_elements.end());
}

HitResult MarkupEngine::getElementAtEx(cv::Point pt, int padding) const {
    // 倒序查找，优先命中上层元素
    for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it) {
        HitArea area = (*it)->hitTestEx(pt, padding);
        if (area != HitArea::None) {
            return { it->get(), area };
        }
    }
    return { nullptr, HitArea::None };
}

MarkupElement* MarkupEngine::getElementById(uint32_t id) const {
    for (const auto& elem : m_elements) {
        if (elem->id == id) return elem.get();
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 工具快捷方法
// ─────────────────────────────────────────────────────────────────────────────

MarkupElement* MarkupEngine::drawRectangle(cv::Point p1, cv::Point p2, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Rectangle;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->thickness = thickness;
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::drawArrow(cv::Point from, cv::Point to, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Arrow;
    elem->startPt = from;
    elem->endPt = to;
    elem->color = color;
    elem->thickness = thickness;
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::drawEllipse(cv::Point p1, cv::Point p2, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Ellipse;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->thickness = thickness;
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::drawPenStroke(const std::vector<cv::Point>& points, MarkupColor color, float thickness) {
    if (points.size() < 2) return nullptr;
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Pen;
    elem->penPoints = points;
    elem->color = color;
    elem->thickness = thickness;
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::drawHighlight(cv::Point p1, cv::Point p2, MarkupColor color) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Highlight;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->color.a = 80;  // 半透明
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::applyMosaic(cv::Point p1, cv::Point p2, int blockSize) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Mosaic;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->mosaicBlockSize = blockSize;
    return addElement(std::move(elem));
}

MarkupElement* MarkupEngine::addText(cv::Point position, const std::string& text, MarkupColor color, float fontSize) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Text;
    elem->startPt = position;
    elem->text = text;
    elem->color = color;
    elem->fontSize = fontSize;
    return addElement(std::move(elem));
}

int MarkupEngine::addNumberMark(cv::Point position, MarkupColor color, float dpiScale) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Number;
    elem->startPt = position;
    elem->color = color;
    elem->numberValue = m_nextNumber;
    elem->dpiScale = dpiScale;
    addElement(std::move(elem));
    return m_nextNumber++;
}

void MarkupEngine::addMagnifier(cv::Point center, float scale, int radius) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Magnifier;
    elem->startPt = center;
    elem->magnifierScale = scale;
    elem->magnifierRadius = radius;
    addElement(std::move(elem));
}

void MarkupEngine::addSpotlight(cv::Point p1, cv::Point p2, MarkupColor color, float dimAlpha, bool ellipse) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Spotlight;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->spotlightDimAlpha = dimAlpha;
    elem->spotlightEllipse = ellipse;
    addElement(std::move(elem));
    LOG_DEBUG("标注引擎: 添加聚光灯 ({},{})→({},{}) dimAlpha={:.2f} ellipse={}",
              p1.x, p1.y, p2.x, p2.y, dimAlpha, ellipse);
}

void MarkupEngine::addWatermark(cv::Point p1, cv::Point p2, const std::string& text, float opacity, float angle) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Watermark;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->watermarkText = text;
    elem->watermarkOpacity = opacity;
    elem->watermarkAngle = angle;
    addElement(std::move(elem));
    LOG_DEBUG("标注引擎: 添加水印 ({},{})→({},{}) text='{}' opacity={:.2f} angle={:.1f}",
              p1.x, p1.y, p2.x, p2.y, text, opacity, angle);
}

void MarkupEngine::applyInpaint(cv::Point p1, cv::Point p2, int radius) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Inpaint;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->inpaintRadius = radius;
    addElement(std::move(elem));
    LOG_DEBUG("标注引擎: 添加智能消除 ({},{})→({},{}) radius={}",
              p1.x, p1.y, p2.x, p2.y, radius);
}

// ─────────────────────────────────────────────────────────────────────────────
// 渲染
// ─────────────────────────────────────────────────────────────────────────────

namespace {

void drawPatternedLine(cv::Mat& canvas, cv::Point p1, cv::Point p2, const cv::Scalar& color, int thick, LineStyle style) {
    if (style == LineStyle::Solid) {
        cv::line(canvas, p1, p2, color, thick, cv::LINE_AA);
        return;
    }

    float dx = static_cast<float>(p2.x - p1.x);
    float dy = static_cast<float>(p2.y - p1.y);
    float dist = std::hypot(dx, dy);
    if (dist < 1.0f) return;

    float ux = dx / dist;
    float uy = dy / dist;

    std::vector<float> pattern;
    if (style == LineStyle::Dashed) {
        pattern = { 10.0f, 6.0f }; // 10px 实线, 6px 空白
    } else if (style == LineStyle::Dotted) {
        pattern = { 3.0f, 4.0f };  // 3px 实线点, 4px 空白
    } else if (style == LineStyle::DashDot) {
        pattern = { 10.0f, 4.0f, 3.0f, 4.0f }; // 线 - 间隙 - 点 - 间隙
    } else {
        pattern = { dist, 0.0f };
    }

    float cur = 0.0f;
    size_t patIdx = 0;
    while (cur < dist) {
        float segLen = pattern[patIdx % pattern.size()];
        bool isDraw = (patIdx % 2 == 0);
        float nextCur = (std::min)(dist, cur + segLen);
        if (isDraw) {
            cv::Point ptA(static_cast<int>(std::round(p1.x + ux * cur)), static_cast<int>(std::round(p1.y + uy * cur)));
            cv::Point ptB(static_cast<int>(std::round(p1.x + ux * nextCur)), static_cast<int>(std::round(p1.y + uy * nextCur)));
            cv::line(canvas, ptA, ptB, color, thick, cv::LINE_AA);
        }
        cur = nextCur;
        ++patIdx;
    }
}

void drawPatternedRect(cv::Mat& canvas, cv::Point p1, cv::Point p2, const cv::Scalar& color, int thick, LineStyle style, bool fill) {
    int x1 = (std::min)(p1.x, p2.x);
    int y1 = (std::min)(p1.y, p2.y);
    int x2 = (std::max)(p1.x, p2.x);
    int y2 = (std::max)(p1.y, p2.y);

    if (fill) {
        cv::rectangle(canvas, cv::Point(x1, y1), cv::Point(x2, y2), color, cv::FILLED);
    }
    if (style == LineStyle::Solid) {
        if (!fill || thick > 1) {
            cv::rectangle(canvas, cv::Point(x1, y1), cv::Point(x2, y2), color, thick, cv::LINE_AA);
        }
    } else {
        drawPatternedLine(canvas, cv::Point(x1, y1), cv::Point(x2, y1), color, thick, style);
        drawPatternedLine(canvas, cv::Point(x2, y1), cv::Point(x2, y2), color, thick, style);
        drawPatternedLine(canvas, cv::Point(x2, y2), cv::Point(x1, y2), color, thick, style);
        drawPatternedLine(canvas, cv::Point(x1, y2), cv::Point(x1, y1), color, thick, style);
    }
}

}  // namespace

void MarkupEngine::renderAll(cv::Mat& canvas) const {
    for (const auto& elem : m_elements) {
        renderElement(canvas, *elem);
    }
}

void MarkupEngine::renderElement(cv::Mat& canvas, const MarkupElement& element) const {
    auto color = element.color.toCvScalar();
    int thick = std::max(1, static_cast<int>(element.thickness));

    switch (element.tool) {
        case MarkupTool::Rectangle: {
            drawPatternedRect(canvas, element.startPt, element.endPt, color, thick, element.lineStyle, element.fill);
            break;
        }

        case MarkupTool::Arrow: {
            if (element.arrowStyle == ArrowStyle::DoubleEnded) {
                cv::arrowedLine(canvas, element.startPt, element.endPt, color, thick, cv::LINE_AA, 0, 0.08);
                cv::arrowedLine(canvas, element.endPt, element.startPt, color, thick, cv::LINE_AA, 0, 0.08);
            } else {
                if (element.lineStyle == LineStyle::Solid) {
                    cv::arrowedLine(canvas, element.startPt, element.endPt, color, thick, cv::LINE_AA, 0, 0.08);
                } else {
                    drawPatternedLine(canvas, element.startPt, element.endPt, color, thick, element.lineStyle);
                    double angle = std::atan2(element.endPt.y - element.startPt.y, element.endPt.x - element.startPt.x);
                    double arrowLen = std::max(12.0, thick * 3.5);
                    cv::Point arrowP1(static_cast<int>(element.endPt.x - arrowLen * std::cos(angle - CV_PI / 6)),
                                      static_cast<int>(element.endPt.y - arrowLen * std::sin(angle - CV_PI / 6)));
                    cv::Point arrowP2(static_cast<int>(element.endPt.x - arrowLen * std::cos(angle + CV_PI / 6)),
                                      static_cast<int>(element.endPt.y - arrowLen * std::sin(angle + CV_PI / 6)));
                    cv::line(canvas, element.endPt, arrowP1, color, thick, cv::LINE_AA);
                    cv::line(canvas, element.endPt, arrowP2, color, thick, cv::LINE_AA);
                }
            }
            break;
        }

        case MarkupTool::Ellipse: {
            cv::Point center((element.startPt.x + element.endPt.x) / 2,
                             (element.startPt.y + element.endPt.y) / 2);
            cv::Size axes(std::abs(element.endPt.x - element.startPt.x) / 2,
                          std::abs(element.endPt.y - element.startPt.y) / 2);
            if (axes.width > 0 && axes.height > 0) {
                if (element.fill) {
                    cv::ellipse(canvas, center, axes, 0, 0, 360, color, cv::FILLED, cv::LINE_AA);
                }
                cv::ellipse(canvas, center, axes, 0, 0, 360, color, thick, cv::LINE_AA);
            }
            break;
        }

        case MarkupTool::Pen: {
            if (element.penPoints.size() >= 2) {
                if (element.lineStyle == LineStyle::Solid) {
                    cv::polylines(canvas, element.penPoints, false, color, thick, cv::LINE_AA);
                } else {
                    for (size_t i = 1; i < element.penPoints.size(); ++i) {
                        drawPatternedLine(canvas, element.penPoints[i - 1], element.penPoints[i], color, thick, element.lineStyle);
                    }
                }
            }
            break;
        }

        case MarkupTool::Highlight: {
            // 半透明矩形覆盖
            cv::Mat overlay = canvas.clone();
            cv::Scalar highlightColor(element.color.b, element.color.g, element.color.r);
            cv::rectangle(overlay, element.startPt, element.endPt, highlightColor, cv::FILLED);
            double alpha = element.color.a / 255.0;
            cv::addWeighted(overlay, alpha, canvas, 1.0 - alpha, 0, canvas);
            break;
        }

        case MarkupTool::Mosaic: {
            // 马赛克: 区域像素块化
            int x1 = (std::min)(element.startPt.x, element.endPt.x);
            int y1 = (std::min)(element.startPt.y, element.endPt.y);
            int x2 = (std::max)(element.startPt.x, element.endPt.x);
            int y2 = (std::max)(element.startPt.y, element.endPt.y);

            // 边界裁剪
            x1 = (std::max)(0, x1);
            y1 = (std::max)(0, y1);
            x2 = (std::min)(canvas.cols, x2);
            y2 = (std::min)(canvas.rows, y2);

            int bs = element.mosaicBlockSize;
            for (int yy = y1; yy < y2; yy += bs) {
                for (int xx = x1; xx < x2; xx += bs) {
                    int bw = (std::min)(bs, x2 - xx);
                    int bh = (std::min)(bs, y2 - yy);
                    cv::Rect blockRect(xx, yy, bw, bh);
                    cv::Mat block = canvas(blockRect);
                    cv::Scalar meanColor = cv::mean(block);
                    block.setTo(meanColor);
                }
            }
            break;
        }

        case MarkupTool::Text: {
            cv::Size renderedSize(0, 0);
            renderSnipasteStyleText(canvas, element.text, element.startPt, color, static_cast<int>(element.fontSize), element.isEditing, element.fill, renderedSize);
            const_cast<MarkupElement*>(&element)->textRenderSize = renderedSize;
            break;
        }

        case MarkupTool::Number: {
            renderSnipasteStyleNumberBadge(canvas, element.numberValue, element.startPt, color, element.fill, element.dpiScale);
            break;
        }

        case MarkupTool::Magnifier: {
            // 放大镜: 以 startPt 为中心，放大周围区域
            int r = element.magnifierRadius;
            float scale = element.magnifierScale;

            // 计算源区域（缩小后的区域）
            int srcR = static_cast<int>(r / scale);
            cv::Rect srcRect(element.startPt.x - srcR, element.startPt.y - srcR, srcR * 2, srcR * 2);
            srcRect &= cv::Rect(0, 0, canvas.cols, canvas.rows);

            if (srcRect.area() > 0) {
                cv::Mat srcROI = canvas(srcRect);
                cv::Mat enlarged;
                cv::resize(srcROI, enlarged, cv::Size(r * 2, r * 2), 0, 0, cv::INTER_CUBIC);

                // 创建圆形蒙版
                cv::Mat mask = cv::Mat::zeros(r * 2, r * 2, CV_8UC1);
                cv::circle(mask, cv::Point(r, r), r - 2, cv::Scalar(255), cv::FILLED, cv::LINE_AA);

                // 绘制到画布
                cv::Rect dstRect(element.startPt.x - r, element.startPt.y - r, r * 2, r * 2);
                dstRect &= cv::Rect(0, 0, canvas.cols, canvas.rows);
                if (dstRect.area() > 0) {
                    // 尺寸对齐
                    cv::Mat dstROI = canvas(dstRect);
                    cv::Mat croppedEnlarged = enlarged(cv::Rect(0, 0, dstRect.width, dstRect.height));
                    cv::Mat croppedMask = mask(cv::Rect(0, 0, dstRect.width, dstRect.height));
                    croppedEnlarged.copyTo(dstROI, croppedMask);

                    // 边框 - 双层高反差设计
                    cv::circle(canvas, element.startPt, r, cv::Scalar(0, 0, 0), 2, cv::LINE_AA); // 外层黑
                    cv::circle(canvas, element.startPt, r - 2, cv::Scalar(255, 255, 255), 2, cv::LINE_AA); // 内层白

                    // 中心精细准心
                    int ch = 4;
                    cv::line(canvas, cv::Point(element.startPt.x - ch, element.startPt.y), 
                                     cv::Point(element.startPt.x + ch, element.startPt.y), 
                                     cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
                    cv::line(canvas, cv::Point(element.startPt.x, element.startPt.y - ch), 
                                     cv::Point(element.startPt.x, element.startPt.y + ch), 
                                     cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
                }
            }
            break;
        }

        case MarkupTool::Spotlight: {
            // 聚光灯：暗化选区外区域，选区内保持原始亮度
            LOG_DEBUG("标注引擎: 渲染聚光灯 dimAlpha={:.2f} ellipse={}",
                      element.spotlightDimAlpha, element.spotlightEllipse);

            // 创建全画布暗化遮罩（黑色 + dimAlpha 不透明度）
            cv::Mat overlay = canvas.clone();
            cv::Mat darkLayer(canvas.size(), canvas.type(), cv::Scalar(0, 0, 0, 255));

            // 在选区位置留出透明孔洞 —— 用原始画布内容覆盖遮罩对应区域
            int x1 = std::min(element.startPt.x, element.endPt.x);
            int y1 = std::min(element.startPt.y, element.endPt.y);
            int x2 = std::max(element.startPt.x, element.endPt.x);
            int y2 = std::max(element.startPt.y, element.endPt.y);
            x1 = std::max(0, x1); y1 = std::max(0, y1);
            x2 = std::min(canvas.cols, x2); y2 = std::min(canvas.rows, y2);

            if (x2 > x1 && y2 > y1) {
                if (element.spotlightEllipse) {
                    // 椭圆孔洞：创建掩码然后将原始像素复制到暗层
                    cv::Point center((x1 + x2) / 2, (y1 + y2) / 2);
                    cv::Size axes((x2 - x1) / 2, (y2 - y1) / 2);
                    cv::Mat mask = cv::Mat::zeros(canvas.size(), CV_8UC1);
                    cv::ellipse(mask, center, axes, 0, 0, 360, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
                    canvas.copyTo(darkLayer, mask);
                } else {
                    // 矩形孔洞：直接复制 ROI
                    cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                    canvas(roi).copyTo(darkLayer(roi));
                }
            }

            // 暗化混合：canvas = darkLayer * dimAlpha + canvas * (1 - dimAlpha)
            double alpha = static_cast<double>(element.spotlightDimAlpha);
            cv::addWeighted(darkLayer, alpha, canvas, 1.0 - alpha, 0, canvas);
            break;
        }

        case MarkupTool::Watermark: {
            // 水印：在选区范围内以指定角度和间距重复绘制半透明文字
            LOG_DEBUG("标注引擎: 渲染水印 text='{}' opacity={:.2f} angle={:.1f} spacing={}",
                      element.watermarkText, element.watermarkOpacity, element.watermarkAngle, element.watermarkSpacing);

            if (element.watermarkText.empty()) break;

            int x1 = std::min(element.startPt.x, element.endPt.x);
            int y1 = std::min(element.startPt.y, element.endPt.y);
            int x2 = std::max(element.startPt.x, element.endPt.x);
            int y2 = std::max(element.startPt.y, element.endPt.y);
            x1 = std::max(0, x1); y1 = std::max(0, y1);
            x2 = std::min(canvas.cols, x2); y2 = std::min(canvas.rows, y2);
            if (x2 <= x1 || y2 <= y1) break;

            int roiW = x2 - x1, roiH = y2 - y1;

            // 测量单个水印文字尺寸
            int baseline = 0;
            double fontScale = 0.6;
            cv::Size textSz = cv::getTextSize(element.watermarkText, cv::FONT_HERSHEY_SIMPLEX,
                                               fontScale, 1, &baseline);

            // 在一块足够大的画布上绘制旋转平铺水印
            // 对角线长度保证旋转后仍能覆盖整个 ROI
            int diag = static_cast<int>(std::sqrt(roiW * roiW + roiH * roiH)) + textSz.width;
            cv::Mat wmCanvas(diag * 2, diag * 2, canvas.type(), cv::Scalar(0, 0, 0, 0));

            int spacing = std::max(element.watermarkSpacing, textSz.width + 20);
            int vSpacing = textSz.height + spacing / 2;
            cv::Scalar wmColor(200, 200, 200); // 浅灰色水印

            for (int y = 0; y < wmCanvas.rows; y += vSpacing) {
                for (int x = 0; x < wmCanvas.cols; x += spacing) {
                    cv::putText(wmCanvas, element.watermarkText, cv::Point(x, y + textSz.height),
                                cv::FONT_HERSHEY_SIMPLEX, fontScale, wmColor, 1, cv::LINE_AA);
                }
            }

            // 旋转水印画布
            cv::Point2f wmCenter(static_cast<float>(wmCanvas.cols) / 2.0f, static_cast<float>(wmCanvas.rows) / 2.0f);
            cv::Mat rotMat = cv::getRotationMatrix2D(wmCenter, element.watermarkAngle, 1.0);
            cv::Mat wmRotated;
            cv::warpAffine(wmCanvas, wmRotated, rotMat, wmCanvas.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT);

            // 从旋转后画布的中心裁剪出 ROI 大小的区域
            int cropX = wmRotated.cols / 2 - roiW / 2;
            int cropY = wmRotated.rows / 2 - roiH / 2;
            cropX = std::max(0, std::min(cropX, wmRotated.cols - roiW));
            cropY = std::max(0, std::min(cropY, wmRotated.rows - roiH));
            cv::Mat wmCrop = wmRotated(cv::Rect(cropX, cropY, roiW, roiH));

            // Alpha 混合叠加到画布的 ROI 区域
            cv::Rect dstRoi(x1, y1, roiW, roiH);
            cv::Mat canvasRoi = canvas(dstRoi);
            double wmAlpha = static_cast<double>(element.watermarkOpacity);
            cv::addWeighted(wmCrop, wmAlpha, canvasRoi, 1.0, 0, canvasRoi);
            break;
        }

        case MarkupTool::Inpaint: {
            // 智能消除：创建选区掩码并调用 cv::inpaint 重建背景
            LOG_DEBUG("标注引擎: 渲染智能消除 radius={}", element.inpaintRadius);

            int x1 = std::min(element.startPt.x, element.endPt.x);
            int y1 = std::min(element.startPt.y, element.endPt.y);
            int x2 = std::max(element.startPt.x, element.endPt.x);
            int y2 = std::max(element.startPt.y, element.endPt.y);
            x1 = std::max(0, x1); y1 = std::max(0, y1);
            x2 = std::min(canvas.cols, x2); y2 = std::min(canvas.rows, y2);
            if (x2 <= x1 || y2 <= y1) break;

            // 构建全图掩码：选区内白色（需要修复），其余黑色
            cv::Mat mask = cv::Mat::zeros(canvas.size(), CV_8UC1);
            cv::rectangle(mask, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255), cv::FILLED);

            // cv::inpaint 需要 3 通道 BGR 输入
            cv::Mat bgr;
            if (canvas.channels() == 4) {
                cv::cvtColor(canvas, bgr, cv::COLOR_BGRA2BGR);
            } else {
                bgr = canvas;
            }

            cv::Mat result;
            cv::inpaint(bgr, mask, result, element.inpaintRadius, cv::INPAINT_TELEA);

            // 将修复结果写回画布的选区区域
            if (canvas.channels() == 4) {
                cv::Mat result4;
                cv::cvtColor(result, result4, cv::COLOR_BGR2BGRA);
                cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                result4(roi).copyTo(canvas(roi));
            } else {
                cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                result(roi).copyTo(canvas(roi));
            }
            break;
        }
        }

    // 绘制被选中状态的高亮边框和把手
    if (element.isActive) {
        cv::Rect bbox = element.getBoundingBox();
        // 边框 - 现代 Figma 蓝
        cv::Scalar activeColor(255, 140, 0); // BGR: Deep Sky Blue / Azure style
        cv::rectangle(canvas, bbox, activeColor, 1, cv::LINE_AA);
        
        // 绘制 8 个把手 (5x5 像素纯白+高亮天蓝描边手柄)
        int hw = 5;
        cv::Point handles[8] = {
            {bbox.x, bbox.y}, {bbox.x + bbox.width / 2, bbox.y}, {bbox.x + bbox.width, bbox.y},
            {bbox.x + bbox.width, bbox.y + bbox.height / 2},
            {bbox.x + bbox.width, bbox.y + bbox.height}, {bbox.x + bbox.width / 2, bbox.y + bbox.height},
            {bbox.x, bbox.y + bbox.height}, {bbox.x, bbox.y + bbox.height / 2}
        };

        for (const auto& pt : handles) {
            // 白色实体填充
            cv::rectangle(canvas, cv::Rect(pt.x - hw, pt.y - hw, hw * 2, hw * 2), cv::Scalar(255, 255, 255), cv::FILLED, cv::LINE_AA);
            // 天蓝色清晰描边
            cv::rectangle(canvas, cv::Rect(pt.x - hw, pt.y - hw, hw * 2, hw * 2), activeColor, 1, cv::LINE_AA);
        }
    }
}

}  // namespace easy::capture
