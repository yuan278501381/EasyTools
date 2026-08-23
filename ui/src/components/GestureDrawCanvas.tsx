/* ─────────────────────────────────────────────────────────────────────────────
 * GestureDrawCanvas — 交互式手势录制与识别画板 (World-Class Interactive Drawing Pad)
 *
 * 核心交互哲学 (Zero-Cognition Auto-Detection & Display Only):
 *   - 用户无需手动配置任何选项，认知负荷归零！
 *   - 硬件按键自动感应: 在画板按住右键/中键/侧键1/侧键2/左键划动，全自动识别
 *   - 屏幕位置自动感应: 在画板顶部起笔 -> 自动识别为上边缘 (TopEdge+)
 *                     在画板底部起笔 -> 自动识别为底边缘 (BottomEdge+)
 *                     在画板左/右侧起笔 -> 自动识别为左/右边缘 (LeftEdge+/RightEdge+)
 *                     在画板中央起笔 -> 自动识别为全局常规手势 (none)
 *   - 物理修饰键自动侦测: 划动时按住 Ctrl/Shift/Alt，实时侦测并自动绑定
 *   - 顶部控制舱为「全自动感应监视仪 (Read-only Sensor Telemetry)」，纯状态展示不可编辑
 *   - 搭载 RDP + 转弯圆角折叠平滑消抖算法 (Corner Fillet Folding)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useRef, useCallback, useEffect, type FC } from 'react';
import { RotateCcw, MousePointer, Check } from 'lucide-react';
import {
  codeToArrows,
  parseGestureCode,
  assembleGestureCode,
  type TriggerButton,
  type ScreenEdge,
} from './gestureModel';
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
  
  const current: Direction[] = [];
  for (const d of raw) {
    if (current.length === 0 || current[current.length - 1] !== d) {
      current.push(d);
    }
  }

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

    current.length = 0;
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
  if (currentDir) rawDirs.push(currentDir);

  const simplified = simplifyDirections(rawDirs);
  return simplified.join('-');
}

function pointsToSvgPath(points: Point[]): string {
  if (points.length === 0) return '';
  if (points.length === 1) return `M ${points[0].x} ${points[0].y}`;
  if (points.length === 2) return `M ${points[0].x} ${points[0].y} L ${points[1].x} ${points[1].y}`;

  let d = `M ${points[0].x} ${points[0].y}`;
  for (let i = 1; i < points.length - 1; i++) {
    const p1 = points[i];
    const p2 = points[i + 1];
    const midX = (p1.x + p2.x) / 2;
    const midY = (p1.y + p2.y) / 2;
    d += ` Q ${p1.x} ${p1.y}, ${midX} ${midY}`;
  }
  const last = points[points.length - 1];
  d += ` L ${last.x} ${last.y}`;
  return d;
}

const RIGHT_PRESET_GESTURES = [
  { code: 'R', label: '前进' },
  { code: 'L', label: '后退' },
  { code: 'D-R', label: '关闭标签' },
  { code: 'R-D', label: '恢复标签' },
  { code: 'D-L', label: '关闭窗口' },
  { code: 'U', label: '最大化' },
  { code: 'D', label: '最小化' },
  { code: 'U-R', label: '下一标签' },
  { code: 'U-L', label: '上一标签' },
  { code: 'D-U', label: '刷新' },
  { code: 'L-D', label: '显示桌面' },
  { code: 'D-R-D', label: '截图' },
];

const MIDDLE_PRESET_GESTURES = [
  { code: 'Middle+L', label: '上一曲' },
  { code: 'Middle+R', label: '下一曲' },
  { code: 'Middle+U', label: '最大化' },
  { code: 'Middle+D', label: '最小化' },
  { code: 'Middle+D-R', label: '关闭标签' },
  { code: 'Middle+R-D', label: '恢复标签' },
  { code: 'Middle+D-U', label: '刷新' },
  { code: 'Middle+L-D', label: '显示桌面' },
];

const SIDE1_PRESET_GESTURES = [
  { code: 'X1+L', label: '后退' },
  { code: 'X1+R', label: '前进' },
  { code: 'X1+U', label: '下一标签' },
  { code: 'X1+D', label: '上一标签' },
  { code: 'X1+D-R', label: '关闭标签' },
  { code: 'X1+R-D', label: '恢复标签' },
  { code: 'X1+D-U', label: '刷新' },
];

const SIDE2_PRESET_GESTURES = [
  { code: 'X2+L', label: '上一曲' },
  { code: 'X2+R', label: '下一曲' },
  { code: 'X2+U', label: '音量增加' },
  { code: 'X2+D', label: '音量减少' },
  { code: 'X2+D-R', label: '关闭窗口' },
  { code: 'X2+L-D', label: '显示桌面' },
];

const TOPEDGE_PRESET_GESTURES = [
  { code: 'TopEdge+D', label: '任务视图 / 显示桌面' },
  { code: 'TopEdge+L', label: '左虚拟桌面' },
  { code: 'TopEdge+R', label: '右虚拟桌面' },
  { code: 'TopEdge+Left+D', label: '左键下拉关闭' },
  { code: 'TopEdge+Middle+D', label: '中键下拉最小化' },
];

interface Props {
  value: string;
  onChange: (code: string) => void;
  triggerButton?: TriggerButton;
  onTriggerButtonChange?: (trigger: TriggerButton) => void;
  screenEdge?: ScreenEdge;
  onScreenEdgeChange?: (edge: ScreenEdge) => void;
}

export const GestureDrawCanvas: FC<Props> = ({
  value,
  onChange,
  triggerButton = 'right',
  onTriggerButtonChange,
  screenEdge = 'none',
  onScreenEdgeChange,
}) => {
  const canvasRef = useRef<HTMLDivElement>(null);
  const [isDrawing, setIsDrawing] = useState(false);
  const [drawingButton, setDrawingButton] = useState<number | null>(null);
  const [drawingEdge, setDrawingEdge] = useState<ScreenEdge>('none');
  const [points, setPoints] = useState<Point[]>([]);

  // 实时监听键盘物理按键状态 (Ctrl / Shift / Alt)
  const [liveKeys, setLiveKeys] = useState<{ ctrl: boolean; shift: boolean; alt: boolean }>({
    ctrl: false,
    shift: false,
    alt: false,
  });

  // 解析当前手势编码自带的修饰与边缘状态
  const parsed = parseGestureCode(value);
  const currentCtrl = parsed.hasCtrl || liveKeys.ctrl;
  const currentShift = parsed.hasShift || liveKeys.shift;
  const currentAlt = parsed.hasAlt || liveKeys.alt;
  const currentTrigger = parsed.triggerButton !== 'right' ? parsed.triggerButton : triggerButton;
  const currentEdge = parsed.edge !== 'none' ? parsed.edge : (isDrawing ? drawingEdge : screenEdge);

  // 监听物理键盘修饰键实时按下/松开
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement)?.tagName)) return;
      if (e.key === 'Control') setLiveKeys((k) => ({ ...k, ctrl: true }));
      if (e.key === 'Shift') setLiveKeys((k) => ({ ...k, shift: true }));
      if (e.key === 'Alt') {
        e.preventDefault();
        setLiveKeys((k) => ({ ...k, alt: true }));
      }
    };
    const onKeyUp = (e: KeyboardEvent) => {
      if (['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement)?.tagName)) return;
      if (e.key === 'Control') setLiveKeys((k) => ({ ...k, ctrl: false }));
      if (e.key === 'Shift') setLiveKeys((k) => ({ ...k, shift: false }));
      if (e.key === 'Alt') setLiveKeys((k) => ({ ...k, alt: false }));
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  }, []);

  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    e.preventDefault();
    if (!canvasRef.current) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setPoints([{ x, y }]);
    setIsDrawing(true);
    setDrawingButton(e.button);

    // 智能识别落笔区域坐标 (自动感应上边缘/底边缘/左边缘/右边缘/全局)
    const relX = rect.width > 0 ? x / rect.width : 0.5;
    const relY = rect.height > 0 ? y / rect.height : 0.5;
    let detectedEdge: ScreenEdge;
    if (relY <= 0.25) {
      detectedEdge = 'top';
    } else if (relY >= 0.75) {
      detectedEdge = 'bottom';
    } else if (relX <= 0.15) {
      detectedEdge = 'left';
    } else if (relX >= 0.85) {
      detectedEdge = 'right';
    } else {
      detectedEdge = 'none';
    }

    setDrawingEdge(detectedEdge);
    if (onScreenEdgeChange && detectedEdge !== currentEdge) {
      onScreenEdgeChange(detectedEdge);
    }

    // 智能联动物理按键识别
    let detectedBtn: TriggerButton = currentTrigger;
    if (e.button === 1) detectedBtn = 'middle';
    else if (e.button === 2) detectedBtn = 'right';
    else if (e.button === 3) detectedBtn = 'x1';
    else if (e.button === 4) detectedBtn = 'x2';
    else if (e.button === 0 && (detectedEdge !== 'none' || liveKeys.ctrl || liveKeys.shift || liveKeys.alt || parsed.hasCtrl || parsed.hasShift || parsed.hasAlt)) {
      detectedBtn = 'left';
    }

    if (onTriggerButtonChange && detectedBtn !== currentTrigger) {
      onTriggerButtonChange(detectedBtn);
    }

    try {
      (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    } catch {
      // ignore
    }
  }, [currentTrigger, currentEdge, liveKeys, parsed, onTriggerButtonChange, onScreenEdgeChange]);

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
        if (dist < 4) return prev;
      }
      return [...prev, { x, y }];
    });
  }, [isDrawing]);

  const handlePointerUp = useCallback((e: React.PointerEvent) => {
    if (!isDrawing) return;
    e.preventDefault();
    setIsDrawing(false);

    // 基于起笔点的绝对坐标核实边缘归属
    let effectiveEdge: ScreenEdge = drawingEdge;
    if (points.length > 0 && canvasRef.current) {
      const startPt = points[0];
      const rect = canvasRef.current.getBoundingClientRect();
      const relX = rect.width > 0 ? startPt.x / rect.width : 0.5;
      const relY = rect.height > 0 ? startPt.y / rect.height : 0.5;
      if (relY <= 0.25) effectiveEdge = 'top';
      else if (relY >= 0.75) effectiveEdge = 'bottom';
      else if (relX <= 0.15) effectiveEdge = 'left';
      else if (relX >= 0.85) effectiveEdge = 'right';
      else effectiveEdge = 'none';
    }

    const effectiveButton = drawingButton ?? e.button;
    let effectiveTrigger: TriggerButton = currentTrigger;
    if (effectiveButton === 1) effectiveTrigger = 'middle';
    else if (effectiveButton === 2) effectiveTrigger = 'right';
    else if (effectiveButton === 3) effectiveTrigger = 'x1';
    else if (effectiveButton === 4) effectiveTrigger = 'x2';
    else if (effectiveButton === 0 && (effectiveEdge !== 'none' || liveKeys.ctrl || liveKeys.shift || liveKeys.alt || parsed.hasCtrl || parsed.hasShift || parsed.hasAlt)) {
      effectiveTrigger = 'left';
    }

    if (points.length >= 2) {
      const bare = recognizeStrokes(points);
      if (bare) {
        const full = assembleGestureCode({
          edge: effectiveEdge,
          triggerButton: effectiveTrigger,
          hasCtrl: e.ctrlKey || liveKeys.ctrl || parsed.hasCtrl,
          hasShift: e.shiftKey || liveKeys.shift || parsed.hasShift,
          hasAlt: e.altKey || liveKeys.alt || parsed.hasAlt,
          bareCode: bare,
        });
        onChange(full);
      }
    }
    setDrawingButton(null);
  }, [isDrawing, points, onChange, currentTrigger, drawingEdge, drawingButton, liveKeys, parsed]);

  const handleClear = () => {
    setPoints([]);
    setDrawingEdge('none');
    onChange('');
  };

  const handleSelectPreset = (presetCode: string) => {
    const presetParsed = parseGestureCode(presetCode);
    const full = assembleGestureCode({
      edge: presetParsed.edge !== 'none' ? presetParsed.edge : currentEdge,
      triggerButton: presetParsed.triggerButton !== 'right' ? presetParsed.triggerButton : currentTrigger,
      hasCtrl: currentCtrl || presetParsed.hasCtrl,
      hasShift: currentShift || presetParsed.hasShift,
      hasAlt: currentAlt || presetParsed.hasAlt,
      bareCode: presetParsed.bareCode,
    });
    onChange(full);
  };

  const svgPath = pointsToSvgPath(points);
  const lastPoint = points.length > 0 ? points[points.length - 1] : null;

  // 根据当前触发方式智能挑选预设列表
  const presets =
    currentEdge === 'top'
      ? TOPEDGE_PRESET_GESTURES
      : currentTrigger === 'middle'
      ? MIDDLE_PRESET_GESTURES
      : currentTrigger === 'x1'
      ? SIDE1_PRESET_GESTURES
      : currentTrigger === 'x2'
      ? SIDE2_PRESET_GESTURES
      : RIGHT_PRESET_GESTURES;

  return (
    <div className="gesture-draw-container">
      {/* 实时硬件与位置感应监视仪 (Live Sensor Cockpit - Zero Cognition Read-only Display) */}
      <div className="gesture-cockpit-bar" title="自动识别监视区：根据你在画板上按下的鼠标按键、起笔位置与键盘修饰键全自动识别">
        {/* 触发按键自动感应指示 */}
        <div className="gesture-cockpit-group">
          <span className="gesture-cockpit-label">触发键:</span>
          <span
            className={`gesture-cockpit-badge ${currentTrigger === 'right' ? 'active' : ''}`}
            title="鼠标右键 (按住右键划动手势)"
          >
            <span>◐</span>
            <span>右键</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentTrigger === 'middle' ? 'active' : ''}`}
            title="鼠标中键 (按住中键/滚轮划动手势)"
          >
            <span>◓</span>
            <span>中键</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentTrigger === 'x1' ? 'active' : ''}`}
            title="鼠标侧键1 (按住后退键划动手势)"
          >
            <span>◧</span>
            <span>侧键1</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentTrigger === 'x2' ? 'active' : ''}`}
            title="鼠标侧键2 (按住前进键划动手势)"
          >
            <span>◨</span>
            <span>侧键2</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentTrigger === 'left' ? 'active' : ''}`}
            title="鼠标左键 (在边缘起笔或配合修饰键生效)"
          >
            <span>◑</span>
            <span>左键</span>
          </span>
        </div>

        {/* 触发位置自动感应指示 */}
        <div className="gesture-cockpit-group">
          <span className="gesture-cockpit-label">位置:</span>
          <span
            className={`gesture-cockpit-badge ${currentEdge === 'none' ? 'active' : ''}`}
            title="画板中央起笔自动识别为全局手势"
          >
            <span>全局</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentEdge === 'top' ? 'active' : ''}`}
            title="画板顶部起笔自动识别为屏幕上边缘手势"
          >
            <span>◰ 上边缘</span>
          </span>
          <span
            className={`gesture-cockpit-badge ${currentEdge === 'bottom' ? 'active' : ''}`}
            title="画板底部起笔自动识别为屏幕底边缘手势"
          >
            <span>◲ 底边缘</span>
          </span>
        </div>

        {/* 物理修饰键自动侦测指示 */}
        <div className="gesture-cockpit-group">
          <span className="gesture-cockpit-label">修饰键:</span>
          <span
            className={`gesture-modifier-badge ${currentCtrl ? 'active' : ''} ${liveKeys.ctrl ? 'is-key-pressed' : ''}`}
            title="Ctrl 键 (按住键盘物理 Ctrl 自动捕获)"
          >
            Ctrl
          </span>
          <span
            className={`gesture-modifier-badge ${currentShift ? 'active' : ''} ${liveKeys.shift ? 'is-key-pressed' : ''}`}
            title="Shift 键 (按住键盘物理 Shift 自动捕获)"
          >
            Shift
          </span>
          <span
            className={`gesture-modifier-badge ${currentAlt ? 'active' : ''} ${liveKeys.alt ? 'is-key-pressed' : ''}`}
            title="Alt 键 (按住键盘物理 Alt 自动捕获)"
          >
            Alt
          </span>

          <button
            type="button"
            className="gesture-draw-clear-btn"
            onClick={handleClear}
            title="清空画板重新录制"
          >
            <RotateCcw size={12} />
            <span>清空重录</span>
          </button>
        </div>
      </div>

      {/* 交互式手势捕获板 (含顶部与底部边缘自动感应指示区) */}
      <div
        ref={canvasRef}
        className={`gesture-draw-pad ${isDrawing ? 'is-drawing' : ''} ${currentEdge === 'top' ? 'edge-active-top' : currentEdge === 'bottom' ? 'edge-active-bottom' : ''}`}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onContextMenu={(e) => e.preventDefault()}
        onAuxClick={(e) => e.preventDefault()}
      >
        {/* 背景辅助网格 */}
        <div className="gesture-draw-crosshair" />

        {/* 顶部屏幕上边缘感应区指示 */}
        <div className={`gesture-edge-sensor-zone gesture-edge-sensor-zone--top ${currentEdge === 'top' ? 'active' : ''}`}>
          <span className="gesture-edge-sensor-tag">◰ 顶部上边缘感应区</span>
        </div>

        {/* 底部屏幕底边缘感应区指示 */}
        <div className={`gesture-edge-sensor-zone gesture-edge-sensor-zone--bottom ${currentEdge === 'bottom' ? 'active' : ''}`}>
          <span className="gesture-edge-sensor-tag">◲ 底部底边缘感应区</span>
        </div>

        {points.length === 0 && (
          <div className="gesture-draw-watermark">
            <div className="gesture-draw-watermark-icon-box">
              <MousePointer size={20} strokeWidth={2} className="gesture-draw-mouse-icon" />
            </div>
            <div className="gesture-draw-watermark-text">
              直接在画板划动 · 按键、起笔位置与修饰键全自动识别
            </div>
            <div className="gesture-draw-watermark-hint">
              顶部起笔=上边缘 · 底部起笔=底边缘 · 中间起笔=全局 · 按哪个按键自动识别哪个
            </div>
          </div>
        )}

        {/* 实时平滑矢量轨迹 */}
        {points.length > 0 && (
          <svg className="gesture-draw-svg">
            <path
              d={svgPath}
              className="gesture-stroke-glow"
              fill="none"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
            <path
              d={svgPath}
              className="gesture-stroke-core"
              fill="none"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
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
        {value && !isDrawing && (
          <div className="gesture-draw-badge">
            <Check size={14} className="gesture-draw-badge-icon" />
            <span className="gesture-draw-badge-arrows">{codeToArrows(value)}</span>
          </div>
        )}
      </div>

      {/* 常用手势快捷预设栏 */}
      <div className="gesture-preset-tray">
        <span className="gesture-preset-label">快捷预设:</span>
        <div className="gesture-preset-chips">
          {presets.map((preset) => (
            <button
              key={preset.code}
              type="button"
              className={`gesture-preset-chip ${value === preset.code ? 'active' : ''}`}
              onClick={() => handleSelectPreset(preset.code)}
              title={preset.label}
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
