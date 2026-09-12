#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BeautyShell.h — 截图美化外壳导出引擎 (CleanShot X / PixPin 风格)
//
// 职责:
//   1. 自动为截图添加优雅可调节衬底画布 Padding
//   2. 提供多款专业预设渐变/纯色/微晶毛玻璃背景 (极光紫蓝/晨曦珊瑚/翡翠深海/曜石黑晶/极简浅灰)
//   3. 为内部截图施加平滑抗锯齿圆角与多层柔和高斯弥散投影
//   4. 叠加高质感微晶高光内边框，生成世界级分享图片
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_CAPTURE_BEAUTYSHELL_H
#define TOOLS3000_CAPTURE_BEAUTYSHELL_H

#include <opencv2/core.hpp>

namespace tools3000::capture {

/// 美化外壳背景预设类型
enum class BeautyBackgroundType {
    StudioSlate      = 0, // 摄影棚冷灰 (Studio Slate: Morandi Cool Gray #E2E8F0 -> #CBD5E1)
    PaperChalk       = 1, // 温润纸白 (Paper Chalk: Subtle Warm Off-White #F8FAFC -> #F1F5F9)
    SilkMist         = 2, // 晨雾微光 (Silk Mist: Delicate Whisper Soft Gradient #EEF2F6 -> #E2E8F0)
    MidnightGraphite = 3, // 曜石深邃 (Midnight Graphite: Premium Matte Obsidian #1E293B -> #0F172A)
    PureMinimal      = 4, // 悬浮纯影 (Pure Minimal: Pure Studio White with deep floating shadow)
    COUNT,
    // 兼容历史命名别名
    AuroraPurple = StudioSlate,
    SunriseCoral = PaperChalk,
    EmeraldSea   = SilkMist,
    ObsidianDark = MidnightGraphite,
    MinimalLight = PureMinimal
};

/// 美化外壳配置参数
struct BeautyShellOptions {
    bool enabled = false;
    int padding = 48;                         // 衬底画布 Padding (px, 24~80)
    float cornerRadius = 14.0f;               // 截图圆角半径 (px)
    float shadowRadius = 22.0f;               // 柔和外阴影扩散半径 (px)
    float shadowOpacity = 0.38f;              // 阴影不透明度 (0.0~1.0)
    int shadowOffsetY = 10;                   // 阴影垂直自然下坠偏移 (px)
    BeautyBackgroundType bgType = BeautyBackgroundType::StudioSlate;
};

/// 对输入截图应用精美外壳合成
cv::Mat applyBeautyShell(const cv::Mat& src, const BeautyShellOptions& options);

} // namespace tools3000::capture

#endif // TOOLS3000_CAPTURE_BEAUTYSHELL_H
