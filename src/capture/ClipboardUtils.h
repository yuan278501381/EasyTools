#pragma once

#include <opencv2/opencv.hpp>
#include <windows.h>
#include <string>

namespace tools3000::capture {

/**
 * @brief 世界级多格式剪贴板写入引擎 (ClipboardUtils)
 * 
 * 解决 Windows 桌面端由于单一格式导致应用兼容性受限的行业通病，
 * 采用全矩阵剪贴板格式注入架构：
 * 1. PNG: 保留透明通道与最高画质，现代社交与协同办公软件首选 (微信、QQ、钉钉、飞书、Slack、Chrome/Edge、VSCode)；
 * 2. CF_DIBV5: BITMAPV5HEADER 32 位标准 BGRA，保留透明通道与 sRGB 色彩空间，现代 Office/GDI+ 原生高保真读取；
 * 3. CF_DIB: BITMAPINFOHEADER 24 位标准底向上 (Bottom-up) DIB，自动合并不透明纯白底消除黑边，老旧系统与经典 Win32 软件 100% 兼容；
 * 4. CF_BITMAP: 设备相关位图 (HBITMAP)，为显式探测 CF_BITMAP 的系统组件与图形编辑工具兜底；
 * 5. CF_HDROP: 文件拖放结构体 (DROPFILES)，将截图图片文件句柄注入剪贴板，彻底实现用户在 Windows 桌面或资源管理器中按 Ctrl+V 直接粘贴为真实 PNG 图片文件。
 */
class ClipboardUtils {
public:
    /**
     * @brief 将图像写入系统剪贴板 (全矩阵多格式并发写入)
     * 
     * @param image 待复制的图像数据 (支持 CV_8UC3 BGR 与 CV_8UC4 BGRA)
     * @param preferredFilePath 已保存到本地的真实图片路径 (若为空，自动生成高可用临时文件供 CF_HDROP 桌面粘贴)
     * @param ownerHwnd 剪贴板宿主窗口句柄 (nullptr 表示由当前线程创建独立消息窗口作为合规宿主)
     * @return true 写入成功, false 写入失败
     */
    static bool copyImageToClipboard(const cv::Mat& image,
                                     const std::wstring& preferredFilePath = L"",
                                     HWND ownerHwnd = nullptr);
};

/// 兼容原有函数签名的内联转发接口
inline bool copyMatToClipboard(const cv::Mat& image) {
    return ClipboardUtils::copyImageToClipboard(image);
}

} // namespace tools3000::capture
