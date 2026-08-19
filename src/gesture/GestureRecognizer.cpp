// ─────────────────────────────────────────────────────────────────────────────
// GestureRecognizer.cpp — 手势识别算法实现
// ─────────────────────────────────────────────────────────────────────────────

#include "gesture/GestureRecognizer.h"
#include "core/logger/Logger.h"

#include <cmath>
#include <numbers>

namespace easy::gesture {

namespace {

inline bool isCornerFillet(Direction a, Direction b, Direction c) noexcept {
    // b 是在直角转弯 (a -> c) 过程中由手腕/手指产生的自然圆角对角过渡
    if ((a == Direction::Down && c == Direction::Right && b == Direction::DownRight) ||
        (a == Direction::Right && c == Direction::Down && b == Direction::DownRight) ||
        (a == Direction::Down && c == Direction::Left  && b == Direction::DownLeft)  ||
        (a == Direction::Left  && c == Direction::Down && b == Direction::DownLeft)  ||
        (a == Direction::Up    && c == Direction::Right && b == Direction::UpRight)   ||
        (a == Direction::Right && c == Direction::Up    && b == Direction::UpRight)   ||
        (a == Direction::Up    && c == Direction::Left  && b == Direction::UpLeft)    ||
        (a == Direction::Left  && c == Direction::Up    && b == Direction::UpLeft)) {
        return true;
    }
    return false;
}

inline bool isJitterRebound(Direction a, Direction b, Direction c) noexcept {
    if (a != c) return false;
    // 1. 180度反向回弹抖动 (如 Down -> Up -> Down, Left -> Right -> Left)
    if ((a == Direction::Left && b == Direction::Right) ||
        (a == Direction::Right && b == Direction::Left) ||
        (a == Direction::Up && b == Direction::Down) ||
        (a == Direction::Down && b == Direction::Up)) {
        return true;
    }
    // 2. 45度微小对角抖动 (如 Down -> DownRight -> Down, Right -> UpRight -> Right)
    if ((a == Direction::Down && (b == Direction::DownRight || b == Direction::DownLeft)) ||
        (a == Direction::Up   && (b == Direction::UpRight   || b == Direction::UpLeft))   ||
        (a == Direction::Left && (b == Direction::UpLeft    || b == Direction::DownLeft)) ||
        (a == Direction::Right&& (b == Direction::UpRight   || b == Direction::DownRight))) {
        return true;
    }
    return false;
}

} // namespace

bool GestureRecognizer::isAdvancing(Direction dir, const TrackPoint& peak, const TrackPoint& current) noexcept {
    switch (dir) {
        case Direction::Right:     return current.x > peak.x;
        case Direction::Left:      return current.x < peak.x;
        case Direction::Up:        return current.y < peak.y;
        case Direction::Down:      return current.y > peak.y;
        case Direction::UpRight:   return (current.x - current.y) > (peak.x - peak.y);
        case Direction::DownRight: return (current.x + current.y) > (peak.x + peak.y);
        case Direction::UpLeft:    return (-current.x - current.y) > (-peak.x - peak.y);
        case Direction::DownLeft:  return (-current.x + current.y) > (-peak.x + peak.y);
        default:                   return false;
    }
}

GestureRecognizer::GestureRecognizer(const RecognizerConfig& config)
    : m_config(config) {}

void GestureRecognizer::reset() {
    m_points.clear();
    m_directions.clear();
    m_currentDirection = Direction::None;
    m_hasSegmentStart = false;
    m_segmentStart = {0, 0};
    m_peakPoint = {0, 0};
}

void GestureRecognizer::addPoint(int x, int y) {
    // 采样过滤: 与上一个点距离太近则跳过 (默认 2px)
    if (!m_points.empty()) {
        double dist = calculateDistance(m_points.back().x, m_points.back().y, x, y);
        if (dist < m_config.samplingInterval) {
            return;
        }
    }

    m_points.push_back({x, y});

    // 初始化段起点
    if (!m_hasSegmentStart) {
        m_segmentStart = {x, y};
        m_peakPoint = {x, y};
        m_hasSegmentStart = true;
        return;
    }

    // 实时处理方向段
    processPoints();
}

void GestureRecognizer::processPoints() {
    if (m_points.size() < 2 || !m_hasSegmentStart) return;

    const auto& current = m_points.back();

    // 阶段 1: 尚未确定初始方向段
    if (m_currentDirection == Direction::None) {
        double dist = calculateDistance(m_segmentStart.x, m_segmentStart.y, current.x, current.y);
        if (dist < m_config.minSegmentDistance) return;

        double angle = calculateAngle(m_segmentStart.x, m_segmentStart.y, current.x, current.y);
        Direction dir = angleToDirection(angle);
        if (dir != Direction::None) {
            m_currentDirection = dir;
            m_peakPoint = current;
        }
        return;
    }

    // 阶段 2: 已有当前方向，判断是否沿当前方向继续推进
    if (isAdvancing(m_currentDirection, m_peakPoint, current)) {
        m_peakPoint = current;
        return;
    }

    // 阶段 3: 偏折拐弯检测 (从最远拐点计算偏折矢量)
    double distFromPeak = calculateDistance(m_peakPoint.x, m_peakPoint.y, current.x, current.y);
    if (distFromPeak >= m_config.minSegmentDistance) {
        double turnAngle = calculateAngle(m_peakPoint.x, m_peakPoint.y, current.x, current.y);
        Direction turnDir = angleToDirection(turnAngle);

        if (turnDir != Direction::None && turnDir != m_currentDirection) {
            // 确认拐角发生，锁定前一段并将极值拐点作为新段起点
            if (m_directions.size() < static_cast<size_t>(m_config.maxDirections)) {
                m_directions.push_back(m_currentDirection);
            }
            m_segmentStart = m_peakPoint;
            m_currentDirection = turnDir;
            m_peakPoint = current;
        }
    }
}

std::vector<Direction> GestureRecognizer::simplifyDirections(const std::vector<Direction>& raw) {
    if (raw.empty()) return {};

    // 步骤 1: 压缩连续重复方向 [A, A] -> [A]
    std::vector<Direction> current;
    for (auto d : raw) {
        if (d != Direction::None && (current.empty() || current.back() != d)) {
            current.push_back(d);
        }
    }

    // 步骤 2: 循环消除回弹微抖动 [A, B, A] -> [A] 以及转弯过渡圆角 [A, B(对角), C(垂直正交)] -> [A, C]
    bool modified = true;
    while (modified && current.size() >= 3) {
        modified = false;
        std::vector<Direction> next;
        for (size_t i = 0; i < current.size(); ) {
            if (i + 2 < current.size()) {
                Direction a = current[i];
                Direction b = current[i + 1];
                Direction c = current[i + 2];

                // 规则 1: 消除孤立抖动回弹 (反向回弹或微小对角抖动)
                if (isJitterRebound(a, b, c)) {
                    next.push_back(a);
                    i += 3;
                    modified = true;
                    continue;
                }

                // 规则 2: 转角圆弧消除 [A, B, C] -> [A, C]
                if (isCornerFillet(a, b, c)) {
                    next.push_back(a);
                    next.push_back(c);
                    i += 3;
                    modified = true;
                    continue;
                }
            }

            next.push_back(current[i]);
            ++i;
        }

        // 重新压缩连续重复
        current.clear();
        for (auto d : next) {
            if (d != Direction::None && (current.empty() || current.back() != d)) {
                current.push_back(d);
            }
        }
    }

    return current;
}

bool GestureRecognizer::isScribbleCanceled() const {
    if (!m_config.enableScribbleCancel || m_points.size() < 6) return false;

    // 1. 计算总移动路程与包围盒大小
    double totalDist = 0.0;
    int minX = m_points[0].x, maxX = m_points[0].x;
    int minY = m_points[0].y, maxY = m_points[0].y;

    for (size_t i = 1; i < m_points.size(); ++i) {
        totalDist += calculateDistance(m_points[i - 1].x, m_points[i - 1].y, m_points[i].x, m_points[i].y);
        minX = std::min(minX, m_points[i].x);
        maxX = std::max(maxX, m_points[i].x);
        minY = std::min(minY, m_points[i].y);
        maxY = std::max(maxY, m_points[i].y);
    }

    double bboxDiag = calculateDistance(minX, minY, maxX, maxY);

    // 2. 检测方向频繁反转次数（乱晃：左<->右 或 上<->下 往复振荡）
    int reversals = 0;
    for (size_t i = 2; i < m_directions.size(); ++i) {
        Direction prev = m_directions[i - 2];
        Direction curr = m_directions[i - 1];
        Direction next = m_directions[i];
        if (prev == next && prev != curr) {
            reversals++;
        }
    }

    // 乱晃反悔判定准则：
    // 条件 A: 往复反转 >= 3 次 (如左-右-左-右)
    // 条件 B: 轨迹总距离长 (>100px) 但局限在极小包围盒内 (<50px) 且有反转 >= 2 次 (原地乱涂/打圈)
    if (reversals >= 3) return true;
    if (totalDist > 100.0 && bboxDiag < 50.0 && reversals >= 2) return true;

    return false;
}

std::optional<GestureResult> GestureRecognizer::finalize() {
    // 0. 乱晃反悔检测：若检测到快速乱晃擦除行为，自动放弃识别
    if (isScribbleCanceled()) {
        LOG_INFO("手势识别: 检测到乱晃/原地反悔操作，已自动取消手势执行");
        return std::nullopt;
    }

    // 将最后一个正在累积的方向段加入
    auto rawDirs = m_directions;
    if (m_currentDirection != Direction::None) {
        if (rawDirs.empty() || rawDirs.back() != m_currentDirection) {
            rawDirs.push_back(m_currentDirection);
        }
    }

    // 运行世界级转弯圆角平滑与防抖算法
    auto simplified = simplifyDirections(rawDirs);

    if (simplified.empty()) {
        LOG_TRACE("手势识别: 轨迹太短或无有效方向段, 点数={}", m_points.size());
        return std::nullopt;
    }

    // 构建结果
    GestureResult result;
    result.directions = simplified;
    result.rawPoints = m_points;

    // 计算总距离
    for (size_t i = 1; i < m_points.size(); ++i) {
        result.totalDistance += calculateDistance(
            m_points[i - 1].x, m_points[i - 1].y,
            m_points[i].x, m_points[i].y
        );
    }

    // 生成方向编码字符串 (如 "L-U-R")
    for (size_t i = 0; i < simplified.size(); ++i) {
        if (i > 0) result.code += "-";
        result.code += directionToCode(simplified[i]);
    }

    LOG_DEBUG("手势识别完成: code={}, arrows={}, 点数={}, 总距离={:.1f}px",
              result.code, result.toArrowString(), m_points.size(), result.totalDistance);

    return result;
}

std::vector<Direction> GestureRecognizer::currentDirections() const {
    auto rawDirs = m_directions;
    if (m_currentDirection != Direction::None) {
        if (rawDirs.empty() || rawDirs.back() != m_currentDirection) {
            rawDirs.push_back(m_currentDirection);
        }
    }
    return simplifyDirections(rawDirs);
}

double GestureRecognizer::calculateAngle(int x1, int y1, int x2, int y2) {
    // atan2 返回 [-π, π]，以正右方为 0，逆时针为正
    // 注意: 屏幕坐标 Y 轴向下，所以 y 取反
    return std::atan2(-(y2 - y1), x2 - x1);
}

Direction GestureRecognizer::angleToDirection(double angleRad) const {
    // 将角度归一化到 [0, 2π)
    if (angleRad < 0) angleRad += 2 * std::numbers::pi;

    // 每个方向占 45°（π/4 弧度），允许 ±22.5° 容差
    // 方向划分 (以正右方 0° 为起点，逆时针):
    //   Right:     [-22.5°, 22.5°)    → [337.5°, 360°) ∪ [0°, 22.5°)
    //   UpRight:   [22.5°, 67.5°)
    //   Up:        [67.5°, 112.5°)
    //   UpLeft:    [112.5°, 157.5°)
    //   Left:      [157.5°, 202.5°)
    //   DownLeft:  [202.5°, 247.5°)
    //   Down:      [247.5°, 292.5°)
    //   DownRight: [292.5°, 337.5°)

    double angleDeg = angleRad * 180.0 / std::numbers::pi;

    if (angleDeg >= 337.5 || angleDeg < 22.5)   return Direction::Right;
    if (angleDeg >= 22.5  && angleDeg < 67.5)    return Direction::UpRight;
    if (angleDeg >= 67.5  && angleDeg < 112.5)   return Direction::Up;
    if (angleDeg >= 112.5 && angleDeg < 157.5)   return Direction::UpLeft;
    if (angleDeg >= 157.5 && angleDeg < 202.5)   return Direction::Left;
    if (angleDeg >= 202.5 && angleDeg < 247.5)   return Direction::DownLeft;
    if (angleDeg >= 247.5 && angleDeg < 292.5)   return Direction::Down;
    if (angleDeg >= 292.5 && angleDeg < 337.5)   return Direction::DownRight;

    return Direction::None;
}

double GestureRecognizer::calculateDistance(int x1, int y1, int x2, int y2) {
    double dx = static_cast<double>(x2 - x1);
    double dy = static_cast<double>(y2 - y1);
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace easy::gesture
