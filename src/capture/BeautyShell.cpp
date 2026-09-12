// ─────────────────────────────────────────────────────────────────────────────
// BeautyShell.cpp — 截图美化外壳导出引擎实现 (CleanShot X / PixPin 风格)
// ─────────────────────────────────────────────────────────────────────────────

#include "capture/BeautyShell.h"
#include "capture/MarkupEngine.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace tools3000::capture {
namespace {

void drawFilledRoundedRect(cv::Mat& mask, const cv::Rect& rect, int radius, uint8_t val = 255) {
    if (rect.width <= 0 || rect.height <= 0) return;
    int r = std::clamp(radius, 0, std::min(rect.width / 2, rect.height / 2));
    if (r <= 0) {
        cv::rectangle(mask, rect, cv::Scalar(val), cv::FILLED);
        return;
    }

    // 中间主要矩形块
    cv::rectangle(mask, cv::Rect(rect.x + r, rect.y, rect.width - 2 * r, rect.height),
                  cv::Scalar(val), cv::FILLED);
    cv::rectangle(mask, cv::Rect(rect.x, rect.y + r, r, rect.height - 2 * r),
                  cv::Scalar(val), cv::FILLED);
    cv::rectangle(mask, cv::Rect(rect.x + rect.width - r, rect.y + r, r, rect.height - 2 * r),
                  cv::Scalar(val), cv::FILLED);

    // 四个圆角
    cv::circle(mask, cv::Point(rect.x + r, rect.y + r), r, cv::Scalar(val), cv::FILLED, cv::LINE_AA);
    cv::circle(mask, cv::Point(rect.x + rect.width - r - 1, rect.y + r), r, cv::Scalar(val), cv::FILLED, cv::LINE_AA);
    cv::circle(mask, cv::Point(rect.x + r, rect.y + rect.height - r - 1), r, cv::Scalar(val), cv::FILLED, cv::LINE_AA);
    cv::circle(mask, cv::Point(rect.x + rect.width - r - 1, rect.y + rect.height - r - 1), r, cv::Scalar(val), cv::FILLED, cv::LINE_AA);
}

void getBackgroundPalette(BeautyBackgroundType type, cv::Vec3b& c1, cv::Vec3b& c2) {
    switch (type) {
        case BeautyBackgroundType::StudioSlate:
            // 摄影棚冷灰: 莫兰迪高阶冷灰 (#E2E8F0 -> BGR: 240, 232, 226) 至 (#CBD5E1 -> BGR: 225, 213, 203)
            c1 = cv::Vec3b(240, 232, 226);
            c2 = cv::Vec3b(225, 213, 203);
            break;
        case BeautyBackgroundType::PaperChalk:
            // 温润纸白: 纯净温润粉笔白 (#F8FAFC -> BGR: 252, 250, 248) 至 (#F1F5F9 -> BGR: 249, 245, 241)
            c1 = cv::Vec3b(252, 250, 248);
            c2 = cv::Vec3b(249, 245, 241);
            break;
        case BeautyBackgroundType::SilkMist:
            // 晨雾微光: 丝滑晨雾柔光 (#EEF2F6 -> BGR: 246, 242, 238) 至 (#E2E8F0 -> BGR: 240, 232, 226)
            c1 = cv::Vec3b(246, 242, 238);
            c2 = cv::Vec3b(240, 232, 226);
            break;
        case BeautyBackgroundType::MidnightGraphite:
            // 曜石深邃: 哑光黑曜石石墨黑 (#1E293B -> BGR: 59, 41, 30) 至 (#0F172A -> BGR: 42, 23, 15)
            c1 = cv::Vec3b(59, 41, 30);
            c2 = cv::Vec3b(42, 23, 15);
            break;
        case BeautyBackgroundType::PureMinimal:
        default:
            // 悬浮纯影: 极简纯白 (#FFFFFF -> BGR: 255, 255, 255) 衬底配合下沉微投影
            c1 = cv::Vec3b(255, 255, 255);
            c2 = cv::Vec3b(255, 255, 255);
            break;
    }
}

} // namespace

cv::Mat applyBeautyShell(const cv::Mat& src, const BeautyShellOptions& options) {
    if (src.empty() || src.cols <= 0 || src.rows <= 0) {
        return src.clone();
    }

    // 确保处理输入为 3 通道 BGR 格式
    cv::Mat inputBgr;
    if (src.channels() == 4) {
        cv::cvtColor(src, inputBgr, cv::COLOR_BGRA2BGR);
    } else if (src.channels() == 3) {
        inputBgr = src;
    } else {
        cv::cvtColor(src, inputBgr, cv::COLOR_GRAY2BGR);
    }

    const int pad = std::max(0, options.padding);
    const int outW = inputBgr.cols + pad * 2;
    const int outH = inputBgr.rows + pad * 2;

    // 1. 生成极具现代高级质感的双色对角平滑渐变衬底画布
    cv::Mat canvas(outH, outW, CV_8UC3);
    cv::Vec3b c1, c2;
    getBackgroundPalette(options.bgType, c1, c2);

    const float invOutW = 1.0f / static_cast<float>(std::max(1, outW));
    const float invOutH = 1.0f / static_cast<float>(std::max(1, outH));

    for (int y = 0; y < outH; ++y) {
        cv::Vec3b* row = canvas.ptr<cv::Vec3b>(y);
        float fy = y * invOutH;
        for (int x = 0; x < outW; ++x) {
            float fx = x * invOutW;
            float t = std::clamp((fx + fy) * 0.5f, 0.0f, 1.0f);
            float invT = 1.0f - t;
            row[x] = cv::Vec3b(
                static_cast<uint8_t>(std::clamp(c1[0] * invT + c2[0] * t, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(c1[1] * invT + c2[1] * t, 0.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(c1[2] * invT + c2[2] * t, 0.0f, 255.0f))
            );
        }
    }

    // 2. 渲染物理双层高斯弥散投影 (Key 接触阴影 + Ambient 空气感弥散阴影)
    if (pad > 0 && options.shadowRadius > 0.0f && options.shadowOpacity > 0.0f) {
        int maxK = std::min(outW, outH);
        if (maxK % 2 == 0) maxK--;
        if (maxK >= 3) {
            int shadowR = static_cast<int>(std::round(options.cornerRadius));

            auto renderShadowLayer = [&](float radiusMult, float offsetMult, float alphaMult) {
                float effectiveRadius = options.shadowRadius * radiusMult;
                if (effectiveRadius <= 0.5f) return;
                int sOffsetY = std::clamp(static_cast<int>(std::round(options.shadowOffsetY * offsetMult)), -pad, pad);
                cv::Rect shadowRect(pad, pad + sOffsetY, inputBgr.cols, inputBgr.rows);
                shadowRect &= cv::Rect(0, 0, outW, outH);
                if (shadowRect.width <= 0 || shadowRect.height <= 0) return;

                cv::Mat shadowMask(outH, outW, CV_8UC1, cv::Scalar(0));
                drawFilledRoundedRect(shadowMask, shadowRect, shadowR, 255);

                int ksize = static_cast<int>(std::round(effectiveRadius * 2.0f)) | 1;
                ksize = std::max(3, std::min(ksize, std::min(101, maxK)));
                double sigma = effectiveRadius * 0.55;
                try {
                    cv::GaussianBlur(shadowMask, shadowMask, cv::Size(ksize, ksize), sigma);
                } catch (...) {
                    return;
                }

                const float shadowAlpha = std::clamp(options.shadowOpacity * alphaMult, 0.0f, 1.0f);
                for (int y = 0; y < outH; ++y) {
                    const uint8_t* sRow = shadowMask.ptr<uint8_t>(y);
                    cv::Vec3b* cRow = canvas.ptr<cv::Vec3b>(y);
                    for (int x = 0; x < outW; ++x) {
                        if (sRow[x] > 0) {
                            float a = (sRow[x] / 255.0f) * shadowAlpha;
                            float invA = 1.0f - a;
                            cRow[x][0] = static_cast<uint8_t>(cRow[x][0] * invA);
                            cRow[x][1] = static_cast<uint8_t>(cRow[x][1] * invA);
                            cRow[x][2] = static_cast<uint8_t>(cRow[x][2] * invA);
                        }
                    }
                }
            };

            // Layer 1: Ambient 空气感大范围弥散阴影 (扩散大，半透明柔和)
            renderShadowLayer(1.25f, 1.0f, 0.55f);
            // Layer 2: Key 近距离边缘紧致接触阴影 (近边界沉底，边缘清晰)
            renderShadowLayer(0.35f, 0.35f, 0.45f);
        }
    }

    // 3. 内部截图圆角蒙版处理 (Anti-aliased Rounded Corners)
    cv::Mat innerMask(inputBgr.rows, inputBgr.cols, CV_8UC1, cv::Scalar(0));
    int innerRadius = static_cast<int>(std::round(options.cornerRadius));
    drawFilledRoundedRect(innerMask, cv::Rect(0, 0, inputBgr.cols, inputBgr.rows), innerRadius, 255);

    // 4. 将截图平滑贴入衬底画布目标区域
    cv::Rect targetRoi(pad, pad, inputBgr.cols, inputBgr.rows);
    cv::Mat canvasRoi = canvas(targetRoi);

    for (int y = 0; y < inputBgr.rows; ++y) {
        const uint8_t* mRow = innerMask.ptr<uint8_t>(y);
        const cv::Vec3b* sRow = inputBgr.ptr<cv::Vec3b>(y);
        cv::Vec3b* dRow = canvasRoi.ptr<cv::Vec3b>(y);
        for (int x = 0; x < inputBgr.cols; ++x) {
            uint8_t m = mRow[x];
            if (m == 255) {
                dRow[x] = sRow[x];
            } else if (m > 0) {
                float a = m / 255.0f;
                float invA = 1.0f - a;
                dRow[x][0] = static_cast<uint8_t>(sRow[x][0] * a + dRow[x][0] * invA);
                dRow[x][1] = static_cast<uint8_t>(sRow[x][1] * a + dRow[x][1] * invA);
                dRow[x][2] = static_cast<uint8_t>(sRow[x][2] * a + dRow[x][2] * invA);
            }
        }
    }

    // 5. 叠加高质感微晶高光内边框 (1px 半透明亮白描边，凸显现代玻璃通透感)
    if (innerRadius > 0 && pad > 0 && inputBgr.cols >= 4 && inputBgr.rows >= 4) {
        cv::Mat borderMask(inputBgr.rows, inputBgr.cols, CV_8UC1, cv::Scalar(0));
        drawFilledRoundedRect(borderMask, cv::Rect(0, 0, inputBgr.cols, inputBgr.rows), innerRadius, 255);
        cv::Mat erodedMask;
        cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::erode(borderMask, erodedMask, element);
        cv::subtract(borderMask, erodedMask, borderMask);

        for (int y = 0; y < inputBgr.rows; ++y) {
            const uint8_t* bRow = borderMask.ptr<uint8_t>(y);
            cv::Vec3b* dRow = canvasRoi.ptr<cv::Vec3b>(y);
            for (int x = 0; x < inputBgr.cols; ++x) {
                if (bRow[x] > 0) {
                    float a = (bRow[x] / 255.0f) * 0.28f; // 28% 微晶高光白内描边
                    float invA = 1.0f - a;
                    dRow[x][0] = static_cast<uint8_t>(255 * a + dRow[x][0] * invA);
                    dRow[x][1] = static_cast<uint8_t>(255 * a + dRow[x][1] * invA);
                    dRow[x][2] = static_cast<uint8_t>(255 * a + dRow[x][2] * invA);
                }
            }
        }
    }

    cv::Mat outBgra;
    cv::cvtColor(canvas, outBgra, cv::COLOR_BGR2BGRA);
    if (pad > 0 && options.cornerRadius > 0.0f) {
        outBgra = applyRoundedCorners(outBgra, options.cornerRadius + 4.0f);
    }
    return outBgra;
}

} // namespace tools3000::capture
