/* ─────────────────────────────────────────────────────────────────────────────
 * GestureDrawCanvas — 交互式手势录制与识别画板 (World-Class Interactive Drawing Pad)
 *
 * 功能:
 *   - 支持按住鼠标 (左键/右键/中键) 在画板上直观绘制手势轨迹
 *   - 拦截右键菜单，实现无阻碍的平滑鼠标轨迹捕获
 *   - 实时贝塞尔平滑渲染与双层发光霓虹流光特效
 *   - 搭载与 C++ 内核完全对齐的 RDP + 转弯圆角折叠平滑消抖算法 (Corner Fillet Folding)
 *   - 实时识别方向并自动回填方向编码 (如 D-R, L-D) 与大号箭头展示
 *   - 支持一键清空重绘与常用方向快速预设
 * ───────────────────────────────────────────────────────────────────────────── */

import React, { useRef, useState, useCallback, type FC } from 'react';
import { RotateCcw, PenTool, MousePointer, Check } from 'lucide-react';
import { codeToArrows } from './gestureModel';
import './GestureDrawCanvas.css';

interface Point {
  x: number;
  y: number;
}

type Direction = 'U' | 'D' | 'L' | 'R' | 'UL' | 'UR' | 'DL' | 'DR';

function calculateDistance(x1: number, y1: number, x2: number, y2: number): number {
  const dx = x2 - x1;
  const dy = y2 - y1;
  return Math.sqrt(dx * dx + dy * dy);
}

function angleToDirection(dx: number, dy: number): Direction | null {
  // 屏幕坐标 Y 轴向下，计算角度时 dy 取反
  const angleRad = Math.atan2(-dy, dx);
  let angleDeg = (angleRad * 180) / Math.PI;
  if (angleDeg < 0) angleDeg += 360;

  if (angleDeg >= 337.5 || angleDeg < 22.5) return 'R';
  if (angleDeg >= 22.5 && angleDeg < 67.5) return 'UR';
  if (angleDeg >= 67.5 && angleDeg < 112.5) return 'U';
  if (angleDeg >= 112.5 && angleDeg < 157.5) return 'UL';
  if (angleDeg >= 157.5 && angleDeg < 202.5) return 'L';
  if (angleDeg >= 202.5 && angleDeg < 247.5) return 'DL';
  if (angleDeg >= 247.5 && angleDeg < 292.5) return 'D';
  if (angleDeg >= 292.5 && angleDeg < 337.5) return 'DR';
  return null;
}

function isCornerFillet(a: Direction, b: Direction, c: Direction): boolean {
  if (
    (a === 'D' && c === 'R' && b === 'DR') ||
    (a === 'R' && c === 'D' && b === 'DR') ||
    (a === 'D' && c === 'L' && b === 'DL') ||
    (a === 'L' && c === 'D' && b === 'DL') ||
    (a === 'U' && c === 'R' && b === 'UR') ||
    (a === 'R' && c === 'U' && b === 'UR') ||
    (a === 'U' && c === 'L' && b === 'UL') ||
    (a === 'L' && c === 'U' && b === 'UL')
  ) {
    return true;
  }
  return false;
}

function simplifyDirections(raw: Direction[]): Direction[] {
  if (raw.length === 0) return [];
  
  // 步骤 1: 压缩连续重复方向 [A, A] -> [A]
  let current: Direction[] = [];
  for (const d of raw) {
    if (current.length === 0 || current[current.length - 1] !== d) {
      current.push(d);
    }
  }

  // 步骤 2: 消除孤立回弹微抖动 [A, B, A] -> [A] 与 转弯圆角折叠 [A, B, C] -> [A, C]
  let modified = true;
  while (modified && current.length >= 3) {
    modified = false;
    const next: Direction[] = [];
    for (let i = 0; i < current.length; ) {
      if (i + 2 < current.length) {
        const a = current[i];
        const b = current[i + 1];
        const c = current[i + 2];

        if (a === c) {
          next.push(a);
          i += 3;
          modified = true;
          continue;
        }

        if (isCornerFillet(a, b, c)) {
          next.push(a);
          next.push(c);
          i += 3;
          modified = true;
          continue;
        }
      }
      next.push(current[i]);
      i++;
    }

    current = [];
    for (const d of next) {
      if (current.length === 0 || current[current.length - 1] !== d) {
        current.push(d);
      }
    }
  }

  return current;
}

function recognizeStrokes(points: Point[]): string {
  if (points.length < 2) return '';

  const minSegmentDist = 24; // 像素
  const rawDirs: Direction[] = [];
  let segStart = points[0];
  let currentDir: Direction | null = null;

  for (let i = 1; i < points.length; i++) {
    const pt = points[i];
    const dist = calculateDistance(segStart.x, segStart.y, pt.x, pt.y);
    if (dist < minSegmentDist) continue;

    const dir = angleToDirection(pt.x - segStart.x, pt.y - segStart.y);
    if (!dir) continue;

    if (dir !== currentDir) {
      if (currentDir) rawDirs.push(currentDir);
      currentDir = dir;
      segStart = pt;
    } else {
      segStart = pt;
    }
  }

  if (currentDir) {
    if (rawDirs.length === 0 || rawDirs[rawDirs.length - 1] !== currentDir) {
      rawDirs.push(currentDir);
    }
  }

  const simplified = simplifyDirections(rawDirs);
  return simplified.join('-');
}

// 贝塞尔平滑样条生成
function pointsToSvgPath(points: Point[]): string {
  if (points.length === 0) return '';
  if (points.length === 1) return `M ${points[0].x} ${points[0].y}`;
  if (points.length === 2) return `M ${points[0].x} ${points[0].y} L ${points[1].x} ${points[1].y}`;

  let path = `M ${points[0].x} ${points[0].y}`;
  for (let i = 0; i < points.length - 2; i++) {
    const pNext = points[i + 1];
    const pAfter = points[i + 2];
    const midX = (pNext.x + pAfter.x) / 2;
    const midY = (pNext.y + pAfter.y) / 2;
    path += ` Q ${pNext.x} ${pNext.y}, ${midX} ${midY}`;
  }
  path += ` L ${points[points.length - 1].x} ${points[points.length - 1].y}`;
  return path;
}

const COMMON_PRESET_GESTURES = [
  { code: 'L', label: '后退' },
  { code: 'R', label: '前进' },
  { code: 'U', label: '最大化' },
  { code: 'D', label: '最小化' },
  { code: 'D-R', label: '关闭' },
  { code: 'R-U', label: '恢复标签' },
  { code: 'U-R', label: '下一标签' },
  { code: 'U-L', label: '上一标签' },
  { code: 'D-U', label: '刷新' },
  { code: 'L-D', label: '显示桌面' },
  { code: 'D-R-D', label: '截图' },
];

interface Props {
  value: string;
  onChange: (code: string) => void;
}

export const GestureDrawCanvas: FC<Props> = ({ value, onChange }) => {
  const canvasRef = useRef<HTMLDivElement>(null);
  const [isDrawing, setIsDrawing] = useState(false);
  const [points, setPoints] = useState<Point[]>([]);
  const recognizedCode = value;

  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    // 允许鼠标左键、右键或中键在画板内绘制
    e.preventDefault();
    if (!canvasRef.current) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setPoints([{ x, y }]);
    setIsDrawing(true);
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
  }, []);

  const handlePointerMove = useCallback((e: React.PointerEvent) => {
    if (!isDrawing || !canvasRef.current) return;
    e.preventDefault();
    const rect = canvasRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    setPoints((prev) => {
      if (prev.length > 0) {
        const last = prev[prev.length - 1];
        const dist = calculateDistance(last.x, last.y, x, y);
        if (dist < 4) return prev; // 过滤过密采样
      }
      return [...prev, { x, y }];
    });
  }, [isDrawing]);

  const handlePointerUp = useCallback((e: React.PointerEvent) => {
    if (!isDrawing) return;
    e.preventDefault();
    setIsDrawing(false);

    if (points.length >= 2) {
      const code = recognizeStrokes(points);
      if (code) {
        onChange(code);
      }
    }
  }, [isDrawing, points, onChange]);

  const handleClear = () => {
    setPoints([]);
    onChange('');
  };

  const handleSelectPreset = (code: string) => {
    onChange(code);
  };

  const svgPath = pointsToSvgPath(points);
  const lastPoint = points.length > 0 ? points[points.length - 1] : null;

  return (
    <div className="gesture-draw-container">
      <div className="gesture-draw-header">
        <div className="gesture-draw-title">
          <div className="gesture-draw-title-icon-badge">
            <PenTool size={12} strokeWidth={2.2} />
          </div>
          <span>手势录制画板 (按住鼠标右键或左键在下方划线)</span>
        </div>
        <button
          type="button"
          className="gesture-draw-clear-btn"
          onClick={handleClear}
          title="清空画板"
        >
          <RotateCcw size={13} />
          <span>清空重绘</span>
        </button>
      </div>

      {/* 交互式手势捕获板 */}
      <div
        ref={canvasRef}
        className={`gesture-draw-pad ${isDrawing ? 'is-drawing' : ''}`}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onContextMenu={(e) => e.preventDefault()} // 阻止右键弹出浏览器菜单
      >
        {/* 背景辅助网格与导引十字 */}
        <div className="gesture-draw-crosshair" />

        {points.length === 0 && (
          <div className="gesture-draw-watermark">
            <div className="gesture-draw-watermark-icon-box">
              <MousePointer size={22} strokeWidth={2} className="gesture-draw-mouse-icon" />
            </div>
            <div className="gesture-draw-watermark-text">按住鼠标在此处划出轨迹</div>
            <div className="gesture-draw-watermark-hint">系统将自动识别转角与方向</div>
          </div>
        )}

        {/* 实时平滑矢量轨迹 */}
        {points.length > 0 && (
          <svg className="gesture-draw-svg">
            {/* 外部霓虹光晕 */}
            <path
              d={svgPath}
              className="gesture-stroke-glow"
              fill="none"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
            {/* 内部高亮核心线条 */}
            <path
              d={svgPath}
              className="gesture-stroke-core"
              fill="none"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
            {/* 笔尖能量微粒 */}
            {lastPoint && (
              <>
                <circle cx={lastPoint.x} cy={lastPoint.y} r={8} className="gesture-point-halo" />
                <circle cx={lastPoint.x} cy={lastPoint.y} r={4.5} className="gesture-point-bead" />
                <circle cx={lastPoint.x} cy={lastPoint.y} r={2} className="gesture-point-core" />
              </>
            )}
          </svg>
        )}

        {/* 识别结果悬浮卡片 */}
        {recognizedCode && !isDrawing && (
          <div className="gesture-draw-badge">
            <Check size={14} className="gesture-draw-badge-icon" />
            <span className="gesture-draw-badge-arrows">{codeToArrows(recognizedCode)}</span>
            <span className="gesture-draw-badge-code">({recognizedCode})</span>
          </div>
        )}
      </div>

      {/* 常用手势快捷预设栏 */}
      <div className="gesture-preset-tray">
        <span className="gesture-preset-label">常用预设:</span>
        <div className="gesture-preset-chips">
          {COMMON_PRESET_GESTURES.map((preset) => (
            <button
              key={preset.code}
              type="button"
              className={`gesture-preset-chip ${recognizedCode === preset.code ? 'active' : ''}`}
              onClick={() => handleSelectPreset(preset.code)}
              title={`${preset.label} (${preset.code})`}
            >
              <span className="gesture-preset-arrows">{codeToArrows(preset.code)}</span>
              <span className="gesture-preset-name">{preset.label}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
};
