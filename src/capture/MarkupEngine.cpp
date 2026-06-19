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
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/MarkupEngine.h"
#include "core/logger/Logger.h"
#include "core/utils/TraceId.h"

#include <algorithm>
#include <cmath>

namespace easy::capture {

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

cv::Mat MarkupEngine::getCompositeImage() const {
    if (m_baseImage.empty()) return {};

    cv::Mat result = m_baseImage.clone();
    renderAll(result);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 元素操作
// ─────────────────────────────────────────────────────────────────────────────

void MarkupEngine::addElement(std::unique_ptr<MarkupElement> element) {
    m_undoStack.clear();  // 新增元素后清空重做栈
    m_elements.push_back(std::move(element));
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

                    // 边框
                    cv::circle(canvas, element.startPt, r - 1, cv::Scalar(200, 200, 200), 2, cv::LINE_AA);
                }
            }
            break;
        }
    }
}

}  // namespace easy::capture
