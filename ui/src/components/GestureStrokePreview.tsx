/* ─────────────────────────────────────────────────────────────────────────────
 * GestureStrokePreview.tsx — 动态手势矢量轨迹微缩预览 (Animated Vector Gesture Preview)
 *
 * 功能:
 *   - 将手势编码 (如 "R", "D-R", "U-R-D", "UL") 转换为精美自适应的 SVG 矢量轨迹
 *   - 自动居中与等比缩放，适应任何长度的手势序列
 *   - 包含起点发光微光圈与终点实心方向箭头
 *   - 支持鼠标悬停触发运笔轨迹生长与流光循环动效 (WGestures 2 动效复刻)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useMemo, useState, useRef, useEffect, type FC } from 'react';
import { codeToArrows } from './gestureModel';
import './GestureStrokePreview.css';

interface Vector {
  dx: number;
  dy: number;
}

const DIR_VECTORS: Record<string, Vector> = {
  R: { dx: 1, dy: 0 },
  L: { dx: -1, dy: 0 },
  U: { dx: 0, dy: -1 },
  D: { dx: 0, dy: 1 },
  UR: { dx: 0.707, dy: -0.707 },
  RU: { dx: 0.707, dy: -0.707 },
  UL: { dx: -0.707, dy: -0.707 },
  LU: { dx: -0.707, dy: -0.707 },
  DR: { dx: 0.707, dy: 0.707 },
  RD: { dx: 0.707, dy: 0.707 },
  DL: { dx: -0.707, dy: 0.707 },
  LD: { dx: -0.707, dy: 0.707 },
};

function parseSegments(rawCode: string): string[] {
  if (!rawCode) return [];
  const clean = rawCode.trim().toUpperCase();
  if (clean.includes('-')) {
    return clean.split('-').filter(Boolean);
  }
  if (DIR_VECTORS[clean]) {
    return [clean];
  }
  // 逐字符回退 (如 "DR" -> ["D", "R"])
  return clean.split('').filter((c) => DIR_VECTORS[c]);
}

interface Props {
  code: string;
  size?: number;
  strokeWidth?: number;
  interactive?: boolean;
  autoAnimate?: boolean;
  className?: string;
  title?: string;
}

export const GestureStrokePreview: FC<Props> = ({
  code,
  size = 36,
  strokeWidth = 3,
  interactive = true,
  autoAnimate = false,
  className = '',
  title,
}) => {
  const [isHovered, setIsHovered] = useState(false);
  const pathRef = useRef<SVGPathElement>(null);
  const [pathLength, setPathLength] = useState(100);

  const segments = useMemo(() => parseSegments(code), [code]);

  // 计算几何点坐标与自适应视图
  const geometry = useMemo(() => {
    if (segments.length === 0) return null;

    const points: { x: number; y: number }[] = [{ x: 0, y: 0 }];
    let curX = 0;
    let curY = 0;
    const step = 30;

    for (const seg of segments) {
      const vec = DIR_VECTORS[seg] || { dx: 1, dy: 0 };
      curX += vec.dx * step;
      curY += vec.dy * step;
      points.push({ x: curX, y: curY });
    }

    let minX = Infinity;
    let maxX = -Infinity;
    let minY = Infinity;
    let maxY = -Infinity;

    for (const p of points) {
      if (p.x < minX) minX = p.x;
      if (p.x > maxX) maxX = p.x;
      if (p.y < minY) minY = p.y;
      if (p.y > maxY) maxY = p.y;
    }

    const rawW = maxX - minX || 1;
    const rawH = maxY - minY || 1;

    const padding = 7;
    const availW = size - padding * 2;
    const availH = size - padding * 2;

    const scale = Math.min(availW / rawW, availH / rawH, 1.0);

    const offsetX = (size - rawW * scale) / 2 - minX * scale;
    const offsetY = (size - rawH * scale) / 2 - minY * scale;

    const scaledPoints = points.map((p) => ({
      x: Number((p.x * scale + offsetX).toFixed(2)),
      y: Number((p.y * scale + offsetY).toFixed(2)),
    }));

    // 构建 SVG 路径指令
    let d = `M ${scaledPoints[0].x} ${scaledPoints[0].y}`;
    for (let i = 1; i < scaledPoints.length; i++) {
      d += ` L ${scaledPoints[i].x} ${scaledPoints[i].y}`;
    }

    const startPoint = scaledPoints[0];
    const endPoint = scaledPoints[scaledPoints.length - 1];

    // 计算终点箭头的朝向角度
    const prevPoint = scaledPoints[scaledPoints.length - 2] || startPoint;
    const angleRad = Math.atan2(endPoint.y - prevPoint.y, endPoint.x - prevPoint.x);
    const angleDeg = (angleRad * 180) / Math.PI;

    return {
      d,
      startPoint,
      endPoint,
      angleDeg,
    };
  }, [segments, size]);

  // 测量路径实际长度以驱动完美的 SVG 描边生长动画
  useEffect(() => {
    if (pathRef.current) {
      const len = pathRef.current.getTotalLength();
      if (len > 0) setPathLength(Math.ceil(len));
    }
  }, [geometry]);

  const fallbackArrows = useMemo(() => codeToArrows(code) || code, [code]);
  const tooltipText = title ?? `手势: ${fallbackArrows} (编码: ${code})`;

  if (!geometry) {
    return (
      <span className={`gesture-arrow ${className}`} title={tooltipText}>
        {fallbackArrows}
      </span>
    );
  }

  const { d, startPoint, endPoint, angleDeg } = geometry;
  const isAnimating = autoAnimate || isHovered;

  return (
    <div
      className={`gesture-stroke-preview ${isAnimating ? 'gesture-stroke-preview--animating' : ''} ${className}`}
      style={{
        width: `${size}px`,
        height: `${size}px`,
        // @ts-expect-error CSS variable
        '--path-length': `${pathLength}px`,
      }}
      title={tooltipText}
      onMouseEnter={interactive ? () => setIsHovered(true) : undefined}
      onMouseLeave={interactive ? () => setIsHovered(false) : undefined}
    >
      <svg
        width={size}
        height={size}
        viewBox={`0 0 ${size} ${size}`}
        className="gesture-stroke-preview__svg"
      >
        {/* 1. 底层半透明轨迹轨道 */}
        <path
          d={d}
          className="gesture-stroke-preview__track"
          strokeWidth={strokeWidth + 1}
        />

        {/* 2. 顶层动态高亮流光路径 */}
        <path
          ref={pathRef}
          d={d}
          className="gesture-stroke-preview__active-path"
          strokeWidth={strokeWidth}
          style={{
            strokeDasharray: pathLength,
            strokeDashoffset: isAnimating ? undefined : 0,
          }}
        />

        {/* 3. 起点微光圈 */}
        <circle
          cx={startPoint.x}
          cy={startPoint.y}
          r={strokeWidth * 0.75}
          className="gesture-stroke-preview__start-dot"
        />

        {/* 4. 终点方向箭头 */}
        <g transform={`translate(${endPoint.x}, ${endPoint.y}) rotate(${angleDeg})`}>
          <polygon
            points={`0,0 -${strokeWidth * 2.2},-${strokeWidth * 1.3} -${strokeWidth * 1.5},0 -${strokeWidth * 2.2},${strokeWidth * 1.3}`}
            className="gesture-stroke-preview__arrowhead"
          />
        </g>
      </svg>
    </div>
  );
};
