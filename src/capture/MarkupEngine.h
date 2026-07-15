#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MarkupEngine — 截图标注引擎
//
// 职责:
//   1. 管理所有标注工具 (矩形/箭头/椭圆/画笔/高亮/马赛克/文本/放大镜/序号/聚光灯/水印/智能消除)
//   2. 维护标注元素列表（支持撤销/重做）
//   3. 使用 OpenCV 将标注渲染到截图上
//   4. 自动序号递增管理
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_CAPTURE_MARKUPENGINE_H
#define EASYTOOLS_CAPTURE_MARKUPENGINE_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <deque>

namespace easy::capture {

/// GDI+ must be started and stopped explicitly while the capture DLL is fully
/// loaded. Starting it from a DLL global constructor makes GdiplusShutdown run
/// under the Windows loader lock and can deadlock on its background thread.
bool initializeMarkupTextRenderer();
void shutdownMarkupTextRenderer();

/// 标注工具类型
enum class MarkupTool {
    Rectangle,   // 矩形
    Arrow,       // 箭头
    Ellipse,     // 椭圆
    Pen,         // 画笔
    Highlight,   // 高亮
    Mosaic,      // 马赛克
    Text,        // 文本
    Magnifier,   // 放大镜
    Number,      // 序列号标记
    Spotlight,   // 聚光灯（暗化选区外区域）
    Watermark,   // 水印叠加
    Inpaint,     // 智能消除（背景重建）
};

/// 颜色预设
struct MarkupColor {
    uint8_t r, g, b, a;

    cv::Scalar toCvScalar() const { return cv::Scalar(b, g, r, a); }

    static MarkupColor Red()    { return {255, 68, 68, 255}; }
    static MarkupColor Green()  { return {52, 211, 153, 255}; }
    static MarkupColor Blue()   { return {96, 165, 250, 255}; }
    static MarkupColor Yellow() { return {251, 191, 36, 255}; }
    static MarkupColor Purple() { return {139, 92, 246, 255}; }
    static MarkupColor White()  { return {255, 255, 255, 255}; }
    static MarkupColor Black()  { return {0, 0, 0, 255}; }
};

enum class HitArea {
    None = -1,
    Body = 0,
    LT = 1, T = 2, RT = 3, R = 4, RB = 5, B = 6, LB = 7, L = 8
};

struct MarkupElement;

struct HitResult {
    MarkupElement* element = nullptr;
    HitArea area = HitArea::None;
};

/// 标注元素基类
struct MarkupElement {
    MarkupTool tool;
    MarkupColor color = MarkupColor::Red();
    float thickness = 2.0f;

    // 起点/终点（矩形/箭头/椭圆用）
    cv::Point startPt{0, 0};
    cv::Point endPt{0, 0};

    // 画笔轨迹点
    std::vector<cv::Point> penPoints;

    // 文本内容
    std::string text;
    float fontSize = 16.0f;
    cv::Size textRenderSize{0, 0}; // 记录最后一次渲染的文本尺寸，用于包围盒

    // 序列号值
    int numberValue = 0;

    // 马赛克块大小
    int mosaicBlockSize = 12;

    // 放大镜倍率
    float magnifierScale = 2.0f;
    int magnifierRadius = 60;

    // 聚光灯参数
    float spotlightDimAlpha = 0.6f;  // 暗化区域的不透明度
    bool spotlightEllipse = true;     // true=椭圆区域, false=矩形区域

    // 水印参数
    std::string watermarkText;        // 水印文字
    float watermarkOpacity = 0.15f;   // 水印透明度
    float watermarkAngle = -30.0f;    // 旋转角度（度）
    int watermarkSpacing = 120;       // 水印间距

    // 智能消除（Inpaint）参数
    int inpaintRadius = 5;            // 修复半径

    uint32_t id = 0; // 唯一标识

    // 交互状态
    bool isActive = false;
    bool isEditing = false;

    virtual ~MarkupElement() = default;

    cv::Rect getBoundingBox() const;
    HitArea hitTestEx(cv::Point pt, int padding = 5) const;
    bool hitTest(cv::Point pt, int padding = 5) const { return hitTestEx(pt, padding) != HitArea::None; }
    void moveBy(int dx, int dy);
    void resize(int dx, int dy, HitArea handle);
};

/// 标注引擎
class MarkupEngine {
public:
    MarkupEngine() = default;

    /// 设置底图（截图原图）。会清空所有标注与撤销栈（用于全新选区）。
    void setBaseImage(const cv::Mat& image);

    /// 仅替换底图，保留现有标注与撤销栈（用于选区二次调整后重裁底图）。
    void updateBaseImage(const cv::Mat& image);

    /// 平移所有标注（含撤销栈）。用于选区移动/缩放时让标注跟随屏幕内容。
    void translateAll(int dx, int dy);

    /// 获取当前合成图（底图 + 所有标注）
    cv::Mat getCompositeImage() const;

    // ── 元素操作 ─────────────────────────────────────────────────────────

    /// 添加标注元素
    MarkupElement* addElement(std::unique_ptr<MarkupElement> element);

    /// 撤销最后一个标注
    bool undo();

    /// 重做
    bool redo();

    /// 清除所有标注
    void clearAll();

    /// 获取当前标注数量
    size_t elementCount() const { return m_elements.size(); }

    /// 是否存在任何标注（含撤销栈中已撤销但可重做的元素）
    bool hasAnyMarkup() const { return !m_elements.empty() || !m_undoStack.empty(); }

    /// 删除指定元素
    void removeElement(uint32_t id);

    /// 获取位于某点的元素及其命中区域（倒序查找，即最顶层的元素）
    HitResult getElementAtEx(cv::Point pt, int padding = 5) const;

    /// 获取位于某点的元素
    MarkupElement* getElementAt(cv::Point pt, int padding = 5) const {
        return getElementAtEx(pt, padding).element;
    }

    /// 获取指定 ID 的元素
    MarkupElement* getElementById(uint32_t id) const;

    // ── 工具快捷方法 ─────────────────────────────────────────────────────

    /// 画矩形
    void drawRectangle(cv::Point p1, cv::Point p2, MarkupColor color, float thickness = 2.0f);

    /// 画箭头
    void drawArrow(cv::Point from, cv::Point to, MarkupColor color, float thickness = 2.0f);

    /// 画椭圆
    void drawEllipse(cv::Point p1, cv::Point p2, MarkupColor color, float thickness = 2.0f);

    /// 画笔自由绘制
    void drawPenStroke(const std::vector<cv::Point>& points, MarkupColor color, float thickness = 2.0f);

    /// 高亮（半透明矩形）
    void drawHighlight(cv::Point p1, cv::Point p2, MarkupColor color);

    /// 马赛克区域
    void applyMosaic(cv::Point p1, cv::Point p2, int blockSize = 12);

    /// 添加文本
    void addText(cv::Point position, const std::string& text, MarkupColor color, float fontSize = 16.0f);

    /// 添加序列号标记
    int addNumberMark(cv::Point position, MarkupColor color);

    /// 添加放大镜
    void addMagnifier(cv::Point center, float scale = 2.0f, int radius = 60);

    /// 添加聚光灯（暗化选区外区域，突出选区内容）
    void addSpotlight(cv::Point p1, cv::Point p2, MarkupColor color, float dimAlpha = 0.6f, bool ellipse = true);

    /// 添加水印叠加（在选区内重复绘制旋转水印文字）
    void addWatermark(cv::Point p1, cv::Point p2, const std::string& text, float opacity = 0.15f, float angle = -30.0f);

    /// 智能消除（利用 cv::inpaint 重建选区背景）
    void applyInpaint(cv::Point p1, cv::Point p2, int radius = 5);

    /// 获取当前序列号
    int currentNumber() const { return m_nextNumber; }

    /// 重置序列号
    void resetNumber() { m_nextNumber = 1; }

private:
    /// 渲染单个元素到图像上
    void renderElement(cv::Mat& canvas, const MarkupElement& element) const;

    /// 渲染所有元素
    void renderAll(cv::Mat& canvas) const;

    cv::Mat m_baseImage;                                    // 底图
    std::vector<std::unique_ptr<MarkupElement>> m_elements; // 标注元素
    std::deque<std::unique_ptr<MarkupElement>> m_undoStack; // 撤销栈
    int m_nextNumber = 1;                                   // 下一个序列号
};

}  // namespace easy::capture

#endif  // EASYTOOLS_CAPTURE_MARKUPENGINE_H
