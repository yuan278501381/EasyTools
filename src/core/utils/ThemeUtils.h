#pragma once

#include <string>

namespace easy::core {

struct AccentColorRGB {
    float r;
    float g;
    float b;
};

/**
 * 获取强调色的归一化 RGB 浮点数值 (0.0f - 1.0f)
 */
inline AccentColorRGB getAccentColorRGB(const std::string& accent) noexcept {
    if (accent == "cyan")   return { 0.024f, 0.714f, 0.831f }; // #06b6d4 极光青
    if (accent == "amber")  return { 0.961f, 0.620f, 0.043f }; // #f59e0b 曜石金
    if (accent == "blue")   return { 0.231f, 0.510f, 0.965f }; // #3b82f6 深海蓝
    if (accent == "mint")   return { 0.063f, 0.725f, 0.506f }; // #10b981 薄荷绿
    if (accent == "coral")  return { 0.957f, 0.247f, 0.369f }; // #f43f5e 暮霞珊瑚
    return { 0.545f, 0.361f, 0.965f };                         // #8b5cf6 经典魅紫 (默认)
}

}  // namespace easy::core
