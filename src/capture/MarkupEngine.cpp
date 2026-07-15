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

#include "capture/MarkupEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

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

    cv::Size getGdiTextSize(const std::wstring& text, int fontSize) {
        Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
        Gdiplus::Font font(&fontFamily, static_cast<Gdiplus::REAL>(fontSize), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        HDC hdc = GetDC(nullptr);
        Gdiplus::Graphics graphics(hdc);
        Gdiplus::RectF boundRect;
        graphics.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &boundRect);
        ReleaseDC(nullptr, hdc);
        return cv::Size(static_cast<int>(boundRect.Width + 5), static_cast<int>(boundRect.Height + 5));
    }

    void renderTextToMat(cv::Mat& canvas, const std::wstring& text, cv::Point pt, const cv::Scalar& color, int fontSize) {
        cv::Size sz = getGdiTextSize(text, fontSize);
        if (sz.width <= 0 || sz.height <= 0) return;
        
        cv::Mat textMat(sz, CV_8UC4, cv::Scalar(0, 0, 0, 0));
        if (textMat.step > static_cast<size_t>(std::numeric_limits<INT>::max())) return;
        {
            Gdiplus::Bitmap bitmap(sz.width, sz.height, static_cast<INT>(textMat.step),
                                   PixelFormat32bppARGB, textMat.data);
            Gdiplus::Graphics graphics(&bitmap);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            
            Gdiplus::FontFamily fontFamily(L"Microsoft YaHei");
            Gdiplus::Font font(&fontFamily, static_cast<Gdiplus::REAL>(fontSize), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            
            Gdiplus::SolidBrush shadowBrush(Gdiplus::Color(128, 0, 0, 0));
            graphics.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(2.0f, 2.0f), &shadowBrush);
            
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, static_cast<BYTE>(color[2]), static_cast<BYTE>(color[1]), static_cast<BYTE>(color[0])));
            graphics.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(0.0f, 0.0f), &textBrush);
        }
        
        for (int y = 0; y < sz.height; ++y) {
            for (int x = 0; x < sz.width; ++x) {
                if (pt.y - sz.height + y < 0 || pt.y - sz.height + y >= canvas.rows || pt.x + x < 0 || pt.x + x >= canvas.cols) continue;
                cv::Vec4b& bg = canvas.at<cv::Vec4b>(pt.y - sz.height + y, pt.x + x);
                cv::Vec4b fg = textMat.at<cv::Vec4b>(y, x);
                float alpha = fg[3] / 255.0f;
                bg[0] = static_cast<uchar>(fg[0] * alpha + bg[0] * (1.0f - alpha));
                bg[1] = static_cast<uchar>(fg[1] * alpha + bg[1] * (1.0f - alpha));
                bg[2] = static_cast<uchar>(fg[2] * alpha + bg[2] * (1.0f - alpha));
                bg[3] = 255;
            }
        }
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
            // Text is anchored at startPt (bottom-left usually, but we draw background above it)
            if (textRenderSize.width == 0) {
                double fontScale = fontSize / 20.0;
                int baseline = 0;
                auto size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, fontScale, static_cast<int>(thickness), &baseline);
                size.height += baseline;
                const_cast<MarkupElement*>(this)->textRenderSize = size;
            }
            return cv::Rect(startPt.x - 4, startPt.y - textRenderSize.height - 4, textRenderSize.width + 8, textRenderSize.height + 8);
        }
        case MarkupTool::Magnifier: {
            return cv::Rect(startPt.x - magnifierRadius, startPt.y - magnifierRadius, magnifierRadius * 2, magnifierRadius * 2);
        }
        case MarkupTool::Number: {
            int r = 14;
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
    
    // 如果处于激活状态，优先测试缩放手柄
    if (isActive) {
        int hw = 6; // handle half-width
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

    // 然后测试主体区域
    bbox.x -= padding;
    bbox.y -= padding;
    bbox.width += padding * 2;
    bbox.height += padding * 2;
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
        fontSize += delta * 0.3f;
        if (fontSize < 10.0f) fontSize = 10.0f;
        if (fontSize > 200.0f) fontSize = 200.0f;
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

void MarkupEngine::drawRectangle(cv::Point p1, cv::Point p2, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Rectangle;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->thickness = thickness;
    addElement(std::move(elem));
}

void MarkupEngine::drawArrow(cv::Point from, cv::Point to, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Arrow;
    elem->startPt = from;
    elem->endPt = to;
    elem->color = color;
    elem->thickness = thickness;
    addElement(std::move(elem));
}

void MarkupEngine::drawEllipse(cv::Point p1, cv::Point p2, MarkupColor color, float thickness) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Ellipse;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->thickness = thickness;
    addElement(std::move(elem));
}

void MarkupEngine::drawPenStroke(const std::vector<cv::Point>& points, MarkupColor color, float thickness) {
    if (points.size() < 2) return;
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Pen;
    elem->penPoints = points;
    elem->color = color;
    elem->thickness = thickness;
    addElement(std::move(elem));
}

void MarkupEngine::drawHighlight(cv::Point p1, cv::Point p2, MarkupColor color) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Highlight;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->color = color;
    elem->color.a = 80;  // 半透明
    addElement(std::move(elem));
}

void MarkupEngine::applyMosaic(cv::Point p1, cv::Point p2, int blockSize) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Mosaic;
    elem->startPt = p1;
    elem->endPt = p2;
    elem->mosaicBlockSize = blockSize;
    addElement(std::move(elem));
}

void MarkupEngine::addText(cv::Point position, const std::string& text, MarkupColor color, float fontSize) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Text;
    elem->startPt = position;
    elem->text = text;
    elem->color = color;
    elem->fontSize = fontSize;
    addElement(std::move(elem));
}

int MarkupEngine::addNumberMark(cv::Point position, MarkupColor color) {
    auto elem = std::make_unique<MarkupElement>();
    elem->tool = MarkupTool::Number;
    elem->startPt = position;
    elem->color = color;
    elem->numberValue = m_nextNumber;
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

void MarkupEngine::renderAll(cv::Mat& canvas) const {
    for (const auto& elem : m_elements) {
        renderElement(canvas, *elem);
    }
}

void MarkupEngine::renderElement(cv::Mat& canvas, const MarkupElement& element) const {
    auto color = element.color.toCvScalar();
    int thick = static_cast<int>(element.thickness);

    switch (element.tool) {
        case MarkupTool::Rectangle: {
            cv::rectangle(canvas, element.startPt, element.endPt, color, thick, cv::LINE_AA);
            break;
        }

        case MarkupTool::Arrow: {
            cv::arrowedLine(canvas, element.startPt, element.endPt, color, thick, cv::LINE_AA, 0, 0.08);
            break;
        }

        case MarkupTool::Ellipse: {
            cv::Point center((element.startPt.x + element.endPt.x) / 2,
                             (element.startPt.y + element.endPt.y) / 2);
            cv::Size axes(std::abs(element.endPt.x - element.startPt.x) / 2,
                          std::abs(element.endPt.y - element.startPt.y) / 2);
            cv::ellipse(canvas, center, axes, 0, 0, 360, color, thick, cv::LINE_AA);
            break;
        }

        case MarkupTool::Pen: {
            if (element.penPoints.size() >= 2) {
                cv::polylines(canvas, element.penPoints, false, color, thick, cv::LINE_AA);
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
            int x1 = std::min(element.startPt.x, element.endPt.x);
            int y1 = std::min(element.startPt.y, element.endPt.y);
            int x2 = std::max(element.startPt.x, element.endPt.x);
            int y2 = std::max(element.startPt.y, element.endPt.y);

            // 边界裁剪
            x1 = std::max(0, x1);
            y1 = std::max(0, y1);
            x2 = std::min(canvas.cols, x2);
            y2 = std::min(canvas.rows, y2);

            int bs = element.mosaicBlockSize;
            for (int yy = y1; yy < y2; yy += bs) {
                for (int xx = x1; xx < x2; xx += bs) {
                    int bw = std::min(bs, x2 - xx);
                    int bh = std::min(bs, y2 - yy);
                    cv::Rect blockRect(xx, yy, bw, bh);
                    cv::Mat block = canvas(blockRect);
                    cv::Scalar meanColor = cv::mean(block);
                    block.setTo(meanColor);
                }
            }
            break;
        }

        case MarkupTool::Text: {
            double fontScale = element.fontSize / 20.0;
            int baseline = 0;
            cv::Size textSize = cv::getTextSize(element.text, cv::FONT_HERSHEY_SIMPLEX,
                                                fontScale, thick, &baseline);
            const_cast<MarkupElement*>(&element)->textRenderSize = cv::Size(textSize.width, textSize.height + baseline); // 更新包围盒尺寸

            // 文字背景（半透明黑色）
            cv::Rect bgRect(element.startPt.x - 4, element.startPt.y - textSize.height - 4,
                            textSize.width + 8, textSize.height + baseline + 8);
            // 裁剪到画布范围
            bgRect &= cv::Rect(0, 0, canvas.cols, canvas.rows);
            if (bgRect.area() > 0) {
                cv::Mat overlay = canvas.clone();
                cv::rectangle(overlay, bgRect, cv::Scalar(0, 0, 0), cv::FILLED);
                cv::addWeighted(overlay, 0.5, canvas, 0.5, 0, canvas);
            }

            cv::putText(canvas, element.text, element.startPt, cv::FONT_HERSHEY_SIMPLEX,
                        fontScale, color, thick, cv::LINE_AA);

            // 如果处于编辑状态，绘制光标
            if (element.isEditing && (GetTickCount() / 500) % 2 == 0) {
                cv::Point cursorStart(element.startPt.x + textSize.width + 2, element.startPt.y - textSize.height);
                cv::Point cursorEnd(element.startPt.x + textSize.width + 2, element.startPt.y + baseline);
                cv::line(canvas, cursorStart, cursorEnd, color, thick, cv::LINE_AA);
            }
            break;
        }

        case MarkupTool::Number: {
            // 圆形背景 + 数字
            int radius = 14;
            cv::circle(canvas, element.startPt, radius, color, cv::FILLED, cv::LINE_AA);

            // 白色数字
            std::string numStr = std::to_string(element.numberValue);
            double fontScale = 0.5;
            int baseline = 0;
            cv::Size textSize = cv::getTextSize(numStr, cv::FONT_HERSHEY_SIMPLEX,
                                                fontScale, 2, &baseline);
            cv::Point textOrg(element.startPt.x - textSize.width / 2,
                              element.startPt.y + textSize.height / 2);
            cv::putText(canvas, numStr, textOrg, cv::FONT_HERSHEY_SIMPLEX,
                        fontScale, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
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
        
        // 绘制 8 个把手
        int hw = 4;
        cv::Point handles[8] = {
            {bbox.x, bbox.y}, {bbox.x + bbox.width / 2, bbox.y}, {bbox.x + bbox.width, bbox.y},
            {bbox.x + bbox.width, bbox.y + bbox.height / 2},
            {bbox.x + bbox.width, bbox.y + bbox.height}, {bbox.x + bbox.width / 2, bbox.y + bbox.height},
            {bbox.x, bbox.y + bbox.height}, {bbox.x, bbox.y + bbox.height / 2}
        };

        for (const auto& pt : handles) {
            // 白色填充
            cv::rectangle(canvas, cv::Rect(pt.x - hw, pt.y - hw, hw * 2, hw * 2), cv::Scalar(255, 255, 255), cv::FILLED, cv::LINE_AA);
            // 蓝色描边
            cv::rectangle(canvas, cv::Rect(pt.x - hw, pt.y - hw, hw * 2, hw * 2), activeColor, 1, cv::LINE_AA);
        }
    }

    // 如果是文本且正在编辑，绘制闪烁光标
    if (element.tool == MarkupTool::Text && element.isEditing) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        if ((ms / 500) % 2 == 0) {
            int cx = element.startPt.x + element.textRenderSize.width + 2;
            int cy1 = element.startPt.y - element.textRenderSize.height + 8;
            int cy2 = element.startPt.y + 4;
            cv::line(canvas, cv::Point(cx, cy1), cv::Point(cx, cy2), color, thick, cv::LINE_AA);
        }
    }
}

}  // namespace easy::capture
