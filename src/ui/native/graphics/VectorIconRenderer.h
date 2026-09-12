#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// VectorIconRenderer.h — Tools3000 零 Emoji · 纯 Lucide 几何矢量微图标引擎
// ─────────────────────────────────────────────────────────────────────────────

#ifndef TOOLS3000_UI_NATIVE_GRAPHICS_VECTORICONRENDERER_H
#define TOOLS3000_UI_NATIVE_GRAPHICS_VECTORICONRENDERER_H

#include "ui/native/graphics/UIRenderContext.h"

namespace tools3000::ui::native {

enum class IconType {
    None = 0,
    // 核心导航图标
    Settings,
    Puzzle,        // 插件中心
    Search,        // 全局搜索
    Hand,          // 鼠标手势
    Maximize2,     // 屏幕热区
    Crop,          // 截图标注
    Clock,         // 截图历史
    FileText,      // 文字识别 OCR
    Keyboard,      // 按键播报 Keycast
    Compass,       // 聚合高亮 Spotlight
    FolderOpen,    // 对话框增强
    Cpu,           // 远控加速 RemoteBoost
    BarChart3,     // 按键统计 Stats
    Info,          // 关于产品 About

    // 通用交互与状态控制图标
    Check,
    X,
    ChevronDown,
    ChevronUp,
    ChevronRight,
    ChevronLeft,
    Plus,
    Trash2,
    Edit3,
    Power,
    RefreshCw,
    Play,
    Pause,
    Copy,
    Sliders,
    Sun,
    Moon,
    Monitor,
    Shield,
    Zap,
    Sparkles,
    Minimize,
    Maximize,
    Restore,
    Folder,
    Palette,       // 屏幕拾色器
    Camera,        // 屏幕截图
    Video,         // 屏幕录制
    Music,         // 音频播放
    FileDigit,     // 二进制 Hex
    ExternalLink,  // 打开外部文件
    Crosshair,     // 窗口准星拾取
    Flame,         // 热力图高频
    LogOut,        // 托盘退出
    Bot,           // AI 助手
    HelpCircle,    // 语法帮助与提示
    Ban,           // 禁用免打扰
    CheckCircle,   // 圆形确认徽章
    Code,          // 代码查看
    Pin            // 窗口固定置顶
};

class VectorIconRenderer {
public:
    /// 在指定区域内绘制居中缩放的高精度矢量图标
    static void drawIcon(
        UIRenderContext& ctx,
        IconType type,
        const Rect& bounds,
        const Color& color,
        float strokeWidth = 1.8f
    );
};

} // namespace tools3000::ui::native

#endif // TOOLS3000_UI_NATIVE_GRAPHICS_VECTORICONRENDERER_H
