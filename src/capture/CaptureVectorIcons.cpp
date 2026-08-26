#include "capture/CaptureVectorIcons.h"
#include <algorithm>
#include <cmath>

namespace easy::capture {

void CaptureVectorIcons::renderIcon(ID2D1RenderTarget* rt, ID2D1Factory* factory,
                                    CaptureIconId iconId, const D2D1_RECT_F& rect,
                                    ID2D1Brush* brush, float scale) {
    if (!rt || !brush) return;

    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float btnW = rect.right - rect.left;
    const float btnH = rect.bottom - rect.top;

    // 基于标准 Lucide 24x24 视口网格系统 (原点在 12, 12)，等比缩放至当前按钮中心
    const float s = std::min(btnW, btnH) / 24.0f * 0.68f;
    const float stroke = std::max(1.3f, 1.6f * scale);

    auto p = [&](float x, float y) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + (x - 12.0f) * s, cy + (y - 12.0f) * s);
    };

    switch (iconId) {
        // ── 1. 矩形工具 (Lucide: Square / rect x=3, y=3, w=18, h=18, rx=2) ───────────
        case CaptureIconId::ToolRectangle: {
            auto box = D2D1::RectF(cx - 8.5f * s, cy - 8.5f * s, cx + 8.5f * s, cy + 8.5f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.5f * s, 2.5f * s), brush, stroke);
            break;
        }

        // ── 2. 椭圆工具 (Lucide: Circle / cx=12, cy=12, r=10) ────────────────────────
        case CaptureIconId::ToolEllipse: {
            auto ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.0f * s, 9.0f * s);
            rt->DrawEllipse(ellipse, brush, stroke);
            break;
        }

        // ── 3. 45° 标注箭头 (Lucide: ArrowUpRight / M7 7h10v10 + M7 17 17 7) ─────────
        case CaptureIconId::ToolArrow: {
            rt->DrawLine(p(7, 17), p(17, 7), brush, stroke);
            rt->DrawLine(p(7, 7), p(17, 7), brush, stroke);
            rt->DrawLine(p(17, 17), p(17, 7), brush, stroke);
            break;
        }

        // ── 4. 水平细线箭头 (Lucide: ArrowRight / M5 12h14 + m12 5 7 7-7 7) ───────────
        case CaptureIconId::ToolArrowThin: {
            rt->DrawLine(p(5, 12), p(19, 12), brush, stroke);
            rt->DrawLine(p(13, 6), p(19, 12), brush, stroke);
            rt->DrawLine(p(13, 18), p(19, 12), brush, stroke);
            break;
        }

        // ── 5. 水平双向箭头 (Lucide: MoveHorizontal / M2 12h20 + arrows) ───────────────
        case CaptureIconId::ToolArrowDouble: {
            rt->DrawLine(p(3, 12), p(21, 12), brush, stroke);
            rt->DrawLine(p(8, 7), p(3, 12), brush, stroke);
            rt->DrawLine(p(8, 17), p(3, 12), brush, stroke);
            rt->DrawLine(p(16, 7), p(21, 12), brush, stroke);
            rt->DrawLine(p(16, 17), p(21, 12), brush, stroke);
            break;
        }

        // ── 6. 铅笔 / 画笔 (Lucide: Pencil / M17 3... + tip) ──────────────────────────
        case CaptureIconId::ToolPen: {
            rt->DrawLine(p(17, 3), p(21, 7), brush, stroke);
            rt->DrawLine(p(21, 7), p(7.5f, 20.5f), brush, stroke);
            rt->DrawLine(p(7.5f, 20.5f), p(2, 22), brush, stroke);
            rt->DrawLine(p(2, 22), p(3.5f, 16.5f), brush, stroke);
            rt->DrawLine(p(3.5f, 16.5f), p(17, 3), brush, stroke);
            rt->DrawLine(p(14, 6), p(18, 10), brush, stroke * 0.85f);
            break;
        }

        // ── 7. 荧光笔 (Lucide: Highlighter) ─────────────────────────────────────────
        case CaptureIconId::ToolHighlight: {
            rt->DrawLine(p(9, 11), p(3, 17), brush, stroke);
            rt->DrawLine(p(3, 17), p(3, 20), brush, stroke);
            rt->DrawLine(p(3, 20), p(12, 20), brush, stroke);
            rt->DrawLine(p(12, 20), p(15, 17), brush, stroke);
            rt->DrawLine(p(9, 11), p(15, 17), brush, stroke);
            rt->DrawLine(p(14, 4), p(20, 10), brush, stroke);
            rt->DrawLine(p(14, 4), p(11.4f, 6.6f), brush, stroke);
            rt->DrawLine(p(20, 10), p(17.4f, 12.6f), brush, stroke);
            break;
        }

        // ── 8. 马赛克 (Lucide: Grid2x2 / 2x2 网格) ──────────────────────────────────
        case CaptureIconId::ToolMosaic: {
            auto box = D2D1::RectF(cx - 8.5f * s, cy - 8.5f * s, cx + 8.5f * s, cy + 8.5f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.5f * s, 2.5f * s), brush, stroke);
            rt->DrawLine(p(12, 3.5f), p(12, 20.5f), brush, stroke);
            rt->DrawLine(p(3.5f, 12), p(20.5f, 12), brush, stroke);
            // 左上与右下填充精美半透实心微格
            auto tl = D2D1::RectF(cx - 8.0f * s, cy - 8.0f * s, cx - 0.5f * s, cy - 0.5f * s);
            auto brBox = D2D1::RectF(cx + 0.5f * s, cy + 0.5f * s, cx + 8.0f * s, cy + 8.0f * s);
            rt->FillRectangle(tl, brush);
            rt->FillRectangle(brBox, brush);
            break;
        }

        // ── 8.5. 模糊滤镜 (Lucide: Droplet / 柔和水滴) ──────────────────────────────
        case CaptureIconId::ToolBlur: {
            rt->DrawLine(p(12, 3), p(6.5f, 13), brush, stroke);
            rt->DrawLine(p(12, 3), p(17.5f, 13), brush, stroke);
            rt->DrawLine(p(6.5f, 13), p(8, 18), brush, stroke);
            rt->DrawLine(p(17.5f, 13), p(16, 18), brush, stroke);
            rt->DrawLine(p(8, 18), p(12, 21), brush, stroke);
            rt->DrawLine(p(16, 18), p(12, 21), brush, stroke);
            break;
        }

        // ── 9. 文字 T (Lucide: Type / M12 4v16 + M4 7V5... + M9 20h6) ─────────────────
        case CaptureIconId::ToolText: {
            rt->DrawLine(p(12, 4), p(12, 20), brush, stroke);
            rt->DrawLine(p(4, 7), p(4, 5), brush, stroke);
            rt->DrawLine(p(4, 5), p(20, 5), brush, stroke);
            rt->DrawLine(p(20, 5), p(20, 7), brush, stroke);
            rt->DrawLine(p(9, 20), p(15, 20), brush, stroke);
            break;
        }

        // ── 10. 序号气泡 ① (Circle + Clean 1) ───────────────────────────────────────
        case CaptureIconId::ToolNumber: {
            auto ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.0f * s, 9.0f * s);
            rt->DrawEllipse(ellipse, brush, stroke);
            rt->DrawLine(p(10, 9), p(12, 7.5f), brush, stroke);
            rt->DrawLine(p(12, 7.5f), p(12, 16.5f), brush, stroke);
            rt->DrawLine(p(9.5f, 16.5f), p(14.5f, 16.5f), brush, stroke);
            break;
        }

        // ── 11. 智能消除 (Lucide: Wand2 / 魔法棒 + 4 芒星) ──────────────────────────
        case CaptureIconId::ToolInpaint: {
            // 魔棒棒身
            rt->DrawLine(p(3.5f, 20.5f), p(18.5f, 5.5f), brush, stroke * 1.6f);
            rt->DrawLine(p(13.5f, 8.5f), p(15.5f, 10.5f), brush, stroke * 0.8f);
            // 右上 4 芒星光斑
            rt->DrawLine(p(19, 2), p(19, 6), brush, stroke * 0.9f);
            rt->DrawLine(p(17, 4), p(21, 4), brush, stroke * 0.9f);
            // 左上小星光
            rt->DrawLine(p(7, 3), p(7, 5), brush, stroke * 0.75f);
            rt->DrawLine(p(6, 4), p(8, 4), brush, stroke * 0.75f);
            break;
        }

        // ── 12. 撤销 (Lucide: Undo2 / M9 14 4 9l5-5 + M4 9h10.5a5.5...) ───────────────
        case CaptureIconId::ActionUndo: {
            rt->DrawLine(p(9, 14), p(4, 9), brush, stroke);
            rt->DrawLine(p(4, 9), p(9, 4), brush, stroke);
            rt->DrawLine(p(4, 9), p(14.5f, 9), brush, stroke);
            // 右侧顺滑圆弧回折 (从 (14.5, 9) 弯至 (14.5, 20) 到 (11, 20))
            rt->DrawLine(p(14.5f, 9), p(18.5f, 11), brush, stroke);
            rt->DrawLine(p(18.5f, 11), p(19.5f, 14.5f), brush, stroke);
            rt->DrawLine(p(19.5f, 14.5f), p(18.5f, 18), brush, stroke);
            rt->DrawLine(p(18.5f, 18), p(14.5f, 20), brush, stroke);
            rt->DrawLine(p(14.5f, 20), p(10, 20), brush, stroke);
            break;
        }

        // ── 13. 重做 (Lucide: Redo2 / m15 14 5-5-5-5 + M20 9H9.5a5.5...) ──────────────
        case CaptureIconId::ActionRedo: {
            rt->DrawLine(p(15, 14), p(20, 9), brush, stroke);
            rt->DrawLine(p(20, 9), p(15, 4), brush, stroke);
            rt->DrawLine(p(20, 9), p(9.5f, 9), brush, stroke);
            // 左侧顺滑圆弧回折
            rt->DrawLine(p(9.5f, 9), p(5.5f, 11), brush, stroke);
            rt->DrawLine(p(5.5f, 11), p(4.5f, 14.5f), brush, stroke);
            rt->DrawLine(p(4.5f, 14.5f), p(5.5f, 18), brush, stroke);
            rt->DrawLine(p(5.5f, 18), p(9.5f, 20), brush, stroke);
            rt->DrawLine(p(9.5f, 20), p(14, 20), brush, stroke);
            break;
        }

        // ── 14. 清空 / 垃圾桶 (Lucide: Trash2) ───────────────────────────────────────
        case CaptureIconId::ActionClear: {
            rt->DrawLine(p(3, 6), p(21, 6), brush, stroke);
            rt->DrawLine(p(8, 6), p(8, 4), brush, stroke);
            rt->DrawLine(p(8, 4), p(16, 4), brush, stroke);
            rt->DrawLine(p(16, 4), p(16, 6), brush, stroke);
            auto bin = D2D1::RectF(cx - 6.5f * s, cy - 6.0f * s, cx + 6.5f * s, cy + 9.0f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(bin, 2.0f * s, 2.0f * s), brush, stroke);
            rt->DrawLine(p(10, 10), p(10, 16), brush, stroke);
            rt->DrawLine(p(14, 10), p(14, 16), brush, stroke);
            break;
        }

        // ── 15. OCR 文本提取 (Lucide: ScanText / 四角对焦 + 3 道扫描文本线) ────────────
        case CaptureIconId::ActionExtractText: {
            // 四角对焦
            rt->DrawLine(p(3, 8), p(3, 5), brush, stroke);
            rt->DrawLine(p(3, 5), p(6, 5), brush, stroke);
            rt->DrawLine(p(18, 5), p(21, 5), brush, stroke);
            rt->DrawLine(p(21, 5), p(21, 8), brush, stroke);
            rt->DrawLine(p(3, 16), p(3, 19), brush, stroke);
            rt->DrawLine(p(3, 19), p(6, 19), brush, stroke);
            rt->DrawLine(p(18, 19), p(21, 19), brush, stroke);
            rt->DrawLine(p(21, 19), p(21, 16), brush, stroke);

            // 内部 3 条优雅排版横线
            rt->DrawLine(p(7, 8), p(15, 8), brush, stroke * 0.9f);
            rt->DrawLine(p(7, 12), p(17, 12), brush, stroke * 0.9f);
            rt->DrawLine(p(7, 16), p(13, 16), brush, stroke * 0.9f);
            break;
        }

        // ── 16. 图钉 (Lucide: Pin) ──────────────────────────────────────────────────
        case CaptureIconId::ActionPinWindow: {
            rt->DrawLine(p(12, 16), p(12, 22), brush, stroke);
            rt->DrawLine(p(5, 15), p(19, 15), brush, stroke);
            rt->DrawLine(p(7, 15), p(8.5f, 11), brush, stroke);
            rt->DrawLine(p(17, 15), p(15.5f, 11), brush, stroke);
            rt->DrawLine(p(8.5f, 11), p(8.5f, 6), brush, stroke);
            rt->DrawLine(p(15.5f, 11), p(15.5f, 6), brush, stroke);
            rt->DrawLine(p(7, 6), p(17, 6), brush, stroke);
            rt->DrawLine(p(9, 6), p(9, 3), brush, stroke);
            rt->DrawLine(p(15, 6), p(15, 3), brush, stroke);
            rt->DrawLine(p(9, 3), p(15, 3), brush, stroke);
            break;
        }

        // ── 17. 长截图 (Lucide: ArrowDownToLine / 窗口 + 贯穿向下长箭头) ──────────────
        case CaptureIconId::ActionScrollCapture: {
            rt->DrawLine(p(12, 3), p(12, 17), brush, stroke);
            rt->DrawLine(p(6, 11), p(12, 17), brush, stroke);
            rt->DrawLine(p(18, 11), p(12, 17), brush, stroke);
            rt->DrawLine(p(5, 21), p(19, 21), brush, stroke);
            break;
        }

        // ── 18. 取消 (Lucide: X / M18 6 6 18 + m6 6 12 12) ───────────────────────────
        case CaptureIconId::ActionCancel: {
            rt->DrawLine(p(18, 6), p(6, 18), brush, stroke);
            rt->DrawLine(p(6, 6), p(18, 18), brush, stroke);
            break;
        }

        // ── 19. 确认 (Lucide: Check / M20 6 9 17l-5-5) ──────────────────────────────
        case CaptureIconId::ActionConfirm: {
            rt->DrawLine(p(4, 12), p(9.5f, 17.5f), brush, stroke * 1.35f);
            rt->DrawLine(p(9.5f, 17.5f), p(20, 6.5f), brush, stroke * 1.35f);
            break;
        }

        // ── 19.1 录屏开始 (实心正红高亮圆) ──────────────────────────────────────────
        case CaptureIconId::ActionRecordStart: {
            auto circle = D2D1::Ellipse(D2D1::Point2F(cx, cy), 6.5f * s, 6.5f * s);
            rt->FillEllipse(circle, brush);
            break;
        }

        // ── 19.2 录屏暂停 (经典双竖线) ──────────────────────────────────────────────
        case CaptureIconId::ActionRecordPause: {
            rt->DrawLine(p(9, 6), p(9, 18), brush, stroke * 1.6f);
            rt->DrawLine(p(15, 6), p(15, 18), brush, stroke * 1.6f);
            break;
        }

        // ── 19.3 录屏停止 (实心停止方块) ────────────────────────────────────────────
        case CaptureIconId::ActionRecordStop: {
            auto box = D2D1::RectF(cx - 5.5f * s, cy - 5.5f * s, cx + 5.5f * s, cy + 5.5f * s);
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, 2.0f * s, 2.0f * s), brush);
            break;
        }

        // ── 19.4 麦克风 (Lucide: Mic) ───────────────────────────────────────────────
        case CaptureIconId::ActionToggleMic: {
            auto micCap = D2D1::RectF(cx - 3.5f * s, cy - 8.0f * s, cx + 3.5f * s, cy + 2.0f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(micCap, 3.5f * s, 3.5f * s), brush, stroke);
            // U 型托架
            rt->DrawLine(p(6, 10), p(6, 12), brush, stroke);
            rt->DrawLine(p(6, 12), p(12, 17), brush, stroke);
            rt->DrawLine(p(18, 12), p(12, 17), brush, stroke);
            rt->DrawLine(p(18, 10), p(18, 12), brush, stroke);
            // 立柱与底座
            rt->DrawLine(p(12, 17), p(12, 21), brush, stroke);
            rt->DrawLine(p(8, 21), p(16, 21), brush, stroke);
            break;
        }

        // ── 19.5 扬声器 (Lucide: Volume2) ───────────────────────────────────────────
        case CaptureIconId::ActionToggleSpeaker: {
            rt->DrawLine(p(4, 9), p(8, 9), brush, stroke);
            rt->DrawLine(p(8, 9), p(13, 5), brush, stroke);
            rt->DrawLine(p(13, 5), p(13, 19), brush, stroke);
            rt->DrawLine(p(13, 19), p(8, 15), brush, stroke);
            rt->DrawLine(p(8, 15), p(4, 15), brush, stroke);
            rt->DrawLine(p(4, 15), p(4, 9), brush, stroke);
            // 声波弧线
            rt->DrawLine(p(16, 9), p(18, 12), brush, stroke);
            rt->DrawLine(p(18, 12), p(16, 15), brush, stroke);
            rt->DrawLine(p(19, 6), p(22, 12), brush, stroke);
            rt->DrawLine(p(22, 12), p(19, 18), brush, stroke);
            break;
        }

        // ── 19.6 复制 (Lucide: Copy) ────────────────────────────────────────────────
        case CaptureIconId::ActionCopy: {
            // 后层矩形 (右上折线)
            rt->DrawLine(p(8, 4), p(19, 4), brush, stroke);
            rt->DrawLine(p(19, 4), p(19, 15), brush, stroke);
            // 前层圆角矩形
            auto frontBox = D2D1::RectF(p(4, 8).x, p(4, 8).y, p(15, 19).x, p(15, 19).y);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(frontBox, 2.0f * s, 2.0f * s), brush, stroke);
            break;
        }

        // ── 19.7 保存 / 下载 (Lucide: Download) ─────────────────────────────────────
        case CaptureIconId::ActionSave: {
            // 下箭头
            rt->DrawLine(p(12, 4), p(12, 15), brush, stroke);
            rt->DrawLine(p(7, 10), p(12, 15), brush, stroke);
            rt->DrawLine(p(17, 10), p(12, 15), brush, stroke);
            // 底部托盘
            rt->DrawLine(p(4, 17), p(4, 20), brush, stroke);
            rt->DrawLine(p(4, 20), p(20, 20), brush, stroke);
            rt->DrawLine(p(20, 20), p(20, 17), brush, stroke);
            break;
        }

        // ── 20. 实线样式 ────────────────────────────────────────────────────────────
        case CaptureIconId::PropSolidLine: {
            float lx1 = rect.left + 5.0f * scale;
            float lx2 = rect.right - 5.0f * scale;
            rt->DrawLine(D2D1::Point2F(lx1, cy), D2D1::Point2F(lx2, cy), brush, stroke);
            break;
        }

        // ── 21. 长虚线样式 ──────────────────────────────────────────────────────────
        case CaptureIconId::PropDashedLine: {
            float lx1 = rect.left + 4.0f * scale;
            float lx2 = rect.right - 4.0f * scale;
            float seg = 5.0f * scale, sp = 3.0f * scale;
            for (float sx = lx1; sx < lx2; sx += seg + sp) {
                rt->DrawLine(D2D1::Point2F(sx, cy), D2D1::Point2F(std::min(lx2, sx + seg), cy), brush, stroke);
            }
            break;
        }

        // ── 22. 点虚线样式 ──────────────────────────────────────────────────────────
        case CaptureIconId::PropDottedLine: {
            float lx1 = rect.left + 4.0f * scale;
            float lx2 = rect.right - 4.0f * scale;
            float seg = 1.8f * scale, sp = 2.6f * scale;
            for (float sx = lx1; sx < lx2; sx += seg + sp) {
                rt->DrawLine(D2D1::Point2F(sx, cy), D2D1::Point2F(std::min(lx2, sx + seg), cy), brush, stroke);
            }
            break;
        }

        // ── 23. 点划线样式 ──────────────────────────────────────────────────────────
        case CaptureIconId::PropDashDotLine: {
            float lx1 = rect.left + 4.0f * scale;
            float lx2 = rect.right - 4.0f * scale;
            float sx = lx1;
            while (sx < lx2) {
                rt->DrawLine(D2D1::Point2F(sx, cy), D2D1::Point2F(std::min(lx2, sx + 5.0f * scale), cy), brush, stroke);
                sx += 7.5f * scale;
                if (sx < lx2) {
                    rt->DrawLine(D2D1::Point2F(sx, cy), D2D1::Point2F(std::min(lx2, sx + 1.8f * scale), cy), brush, stroke);
                    sx += 4.0f * scale;
                }
            }
            break;
        }

        // ── 24. 线宽步进器图标 (三级递增阶梯横线) ──────────────────────────────────
        case CaptureIconId::PropStrokeWidth: {
            rt->DrawLine(p(4, 6), p(20, 6), brush, stroke * 0.7f);
            rt->DrawLine(p(4, 12), p(20, 12), brush, stroke * 1.15f);
            rt->DrawLine(p(4, 18), p(20, 18), brush, stroke * 1.75f);
            break;
        }

        // ── 25. 圆角步进器图标 (优雅 90° 圆弧切角) ──────────────────────────────────
        case CaptureIconId::PropCornerRadius: {
            rt->DrawLine(p(5, 19), p(5, 12), brush, stroke);
            // 90° 顺滑圆弧
            rt->DrawLine(p(5, 12), p(6.5f, 8.5f), brush, stroke);
            rt->DrawLine(p(6.5f, 8.5f), p(8.5f, 6.5f), brush, stroke);
            rt->DrawLine(p(8.5f, 6.5f), p(12, 5), brush, stroke);
            rt->DrawLine(p(12, 5), p(19, 5), brush, stroke);
            break;
        }

        // ── 26. 描边模式 (空心框) ───────────────────────────────────────────────────
        case CaptureIconId::PropFillOutline: {
            auto box = D2D1::RectF(cx - 7.5f * s, cy - 7.5f * s, cx + 7.5f * s, cy + 7.5f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 2.0f * s, 2.0f * s), brush, stroke);
            break;
        }

        // ── 27. 实心填充模式 (实心框) ───────────────────────────────────────────────
        case CaptureIconId::PropFillSolid: {
            auto box = D2D1::RectF(cx - 7.5f * s, cy - 7.5f * s, cx + 7.5f * s, cy + 7.5f * s);
            rt->FillRoundedRectangle(D2D1::RoundedRect(box, 2.0f * s, 2.0f * s), brush);
            break;
        }

        // ── 28. 取色吸管 (Lucide: Pipette) ──────────────────────────────────────────
        case CaptureIconId::PropPipette: {
            // 吸管体
            rt->DrawLine(p(19, 3), p(21, 5), brush, stroke);
            rt->DrawLine(p(21, 5), p(12, 14), brush, stroke);
            rt->DrawLine(p(19, 3), p(10, 12), brush, stroke);
            // 尖嘴
            rt->DrawLine(p(10, 12), p(6, 16), brush, stroke);
            rt->DrawLine(p(6, 16), p(3, 21), brush, stroke);
            rt->DrawLine(p(3, 21), p(8, 18), brush, stroke);
            rt->DrawLine(p(8, 18), p(12, 14), brush, stroke);
            // 微液滴
            auto drop = D2D1::Ellipse(p(2, 22), 1.0f * s, 1.0f * s);
            rt->FillEllipse(drop, brush);
            break;
        }

        // ── 29. 调色板 (Lucide: Palette) ────────────────────────────────────────────
        case CaptureIconId::PropPalette: {
            auto palBox = D2D1::RectF(cx - 8.0f * s, cy - 8.0f * s, cx + 8.0f * s, cy + 8.0f * s);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(palBox, 5.0f * s, 5.0f * s), brush, stroke);
            // 3 个色彩微孔
            rt->FillEllipse(D2D1::Ellipse(p(9, 9), 1.3f * s, 1.3f * s), brush);
            rt->FillEllipse(D2D1::Ellipse(p(15, 9), 1.3f * s, 1.3f * s), brush);
            rt->FillEllipse(D2D1::Ellipse(p(12, 15), 1.3f * s, 1.3f * s), brush);
            break;
        }

        // ── 30. 二维码 (Lucide: QrCode) ─────────────────────────────────────────────
        case CaptureIconId::PropQrCode: {
            // 定位框 1 (左上)
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p(3, 3).x, p(3, 3).y, p(9, 9).x, p(9, 9).y), 1.0f * s, 1.0f * s), brush, stroke * 0.9f);
            rt->FillRectangle(D2D1::RectF(p(5, 5).x, p(5, 5).y, p(7, 7).x, p(7, 7).y), brush);
            // 定位框 2 (右上)
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p(15, 3).x, p(15, 3).y, p(21, 9).x, p(21, 9).y), 1.0f * s, 1.0f * s), brush, stroke * 0.9f);
            rt->FillRectangle(D2D1::RectF(p(17, 5).x, p(17, 5).y, p(19, 7).x, p(19, 7).y), brush);
            // 定位框 3 (左下)
            rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p(3, 15).x, p(3, 15).y, p(9, 21).x, p(9, 21).y), 1.0f * s, 1.0f * s), brush, stroke * 0.9f);
            rt->FillRectangle(D2D1::RectF(p(5, 17).x, p(5, 17).y, p(7, 19).x, p(7, 19).y), brush);
            // 数据微像素
            rt->FillRectangle(D2D1::RectF(p(15, 15).x, p(15, 15).y, p(17, 17).x, p(17, 17).y), brush);
            rt->FillRectangle(D2D1::RectF(p(19, 15).x, p(19, 15).y, p(21, 17).x, p(21, 17).y), brush);
            rt->FillRectangle(D2D1::RectF(p(15, 19).x, p(15, 19).y, p(17, 21).x, p(17, 21).y), brush);
            rt->FillRectangle(D2D1::RectF(p(19, 19).x, p(19, 19).y, p(21, 21).x, p(21, 21).y), brush);
            break;
        }

        default:
            break;
    }
}

} // namespace easy::capture
