#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GestureRecognizer — 手势识别算法
//
// 核心算法: 方向编码法
//   1. 将鼠标轨迹实时分解为方向段 (U/D/L/R/UL/UR/DL/DR)
//   2. 每个方向段必须满足最小移动距离（防抖）
//   3. 角度容差 ±22.5° 范围内归类为同一方向
//   4. 最终生成方向编码字符串 (如 "L-U-R") 进行匹配
// ─────────────────────────────────────────────────────────────────────────────

#ifndef EASYTOOLS_GESTURE_GESTURERECOGNIZER_H
#define EASYTOOLS_GESTURE_GESTURERECOGNIZER_H

#include <string>
#include <vector>
#include <cmath>
#include <optional>

namespace easy::gesture {

/// 八方向枚举
enum class Direction : uint8_t {
    None = 0,
    Up,         // ↑
    Down,       // ↓
    Left,       // ←
    Right,      // →
    UpLeft,     // ↖
    UpRight,    // ↗
    DownLeft,   // ↙
    DownRight   // ↘
};

/// 方向 → 字符串编码
inline std::string directionToCode(Direction dir) {
    switch (dir) {
        case Direction::Up:        return "U";
        case Direction::Down:      return "D";
        case Direction::Left:      return "L";
        case Direction::Right:     return "R";
        case Direction::UpLeft:    return "UL";
        case Direction::UpRight:   return "UR";
        case Direction::DownLeft:  return "DL";
        case Direction::DownRight: return "DR";
        default:                   return "";
    }
}

/// 方向 → 显示用箭头符号
inline std::string directionToArrow(Direction dir) {
    switch (dir) {
        case Direction::Up:        return "↑";
        case Direction::Down:      return "↓";
        case Direction::Left:      return "←";
        case Direction::Right:     return "→";
        case Direction::UpLeft:    return "↖";
        case Direction::UpRight:   return "↗";
        case Direction::DownLeft:  return "↙";
        case Direction::DownRight: return "↘";
        default:                   return "";
    }
}

/// 轨迹点
struct TrackPoint {
    int x;
    int y;
};

/// 手势识别结果
struct GestureResult {
    std::string code;                          // 方向编码 (如 "L-U-R")
    std::vector<Direction> directions;          // 方向序列
    std::vector<TrackPoint> rawPoints;          // 原始轨迹点
    double totalDistance = 0.0;                 // 轨迹总长度（像素）

    bool isValid() const { return !directions.empty(); }

    /// 人类可读的箭头表示
    std::string toArrowString() const {
        std::string result;
        for (const auto& dir : directions) {
            result += directionToArrow(dir);
        }
        return result;
    }
};

/// 识别参数配置
struct RecognizerConfig {
    int minSegmentDistance   = 30;      // 最小方向段移动距离（像素），小于此值忽略
    int samplingInterval    = 5;       // 采样间隔（像素），过滤过于密集的点
    double angleToleranceDeg = 22.5;   // 角度容差（度），±容差内归类为同一方向
    int maxDirections       = 10;      // 最大方向段数量（超过视为无效手势）
};

class GestureRecognizer {
public:
    explicit GestureRecognizer(const RecognizerConfig& config = {});

    /// 重置识别器（新手势开始时调用）
    void reset();

    /// 添加轨迹点（鼠标移动时调用）
    void addPoint(int x, int y);

    /// 完成识别（鼠标释放时调用）
    /// @return 识别结果，如果轨迹太短或无效返回 nullopt
    std::optional<GestureResult> finalize();

    /// 获取当前已识别的方向序列（用于实时轨迹预览）
    std::vector<Direction> currentDirections() const;

    /// 获取当前轨迹点数量
    size_t pointCount() const { return m_points.size(); }

    /// 更新配置
    void setConfig(const RecognizerConfig& config) { m_config = config; }

private:
    /// 计算两点之间的角度（弧度，0 = 正右，逆时针为正）
    static double calculateAngle(int x1, int y1, int x2, int y2);

    /// 角度 → 方向
    Direction angleToDirection(double angleRad) const;

    /// 计算两点之间的距离
    static double calculateDistance(int x1, int y1, int x2, int y2);

    /// 处理累积的点，提取方向段
    void processPoints();

    RecognizerConfig m_config;
    std::vector<TrackPoint> m_points;              // 所有轨迹点
    std::vector<Direction> m_directions;           // 已识别的方向段序列

    // 当前方向段的累积状态
    TrackPoint m_segmentStart{0, 0};               // 当前段起点
    Direction m_currentDirection = Direction::None; // 当前段方向
    bool m_hasSegmentStart = false;
};

}  // namespace easy::gesture

#endif  // EASYTOOLS_GESTURE_GESTURERECOGNIZER_H
