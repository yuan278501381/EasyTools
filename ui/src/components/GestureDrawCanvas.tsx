/* ─────────────────────────────────────────────────────────────────────────────
 * GestureDrawCanvas — 交互式手势录制与识别画板 (World-Class Interactive Drawing Pad)
 *
 * 功能:
 *   - 一体化手势录制控制舱 (Cockpit Bar): 触发模式切换 + Ctrl / Shift / Alt 修饰键联动
 *   - 实时监听键盘物理修饰键按下状态并高亮显示
 *   - 支持按住鼠标 (左键/右键/中键) 在画板上真实录制
 *   - 拦截右键菜单与中键滚动，实现平滑鼠标轨迹捕获
 *   - 实时贝塞尔平滑渲染与双层发光霓虹流光特效
 *   - 搭载 RDP + 转弯圆角折叠平滑消抖算法 (Corner Fillet Folding)
 *   - 实时识别方向并自动合成手势编码 (如 Ctrl+R, Middle+Shift+L)
 *   - 支持一键清空重绘与常用快捷预设
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useRef, useCallback, useEffect, type FC } from 'react';
import { RotateCcw, MousePointer, Check } from 'lucide-react';
import {
  codeToArrows,
  parseGestureCode,
  assembleGestureCode,
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

  if (currentDir) {
    if (rawDirs.length === 0 || rawDirs[rawDirs.length - 1] !== currentDir) {
      rawDirs.push(currentDir);
    }
  }

  const simplified = simplifyDirections(rawDirs);
  return simplified.join('-');
}

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

interface Props {
  value: string;
  onChange: (code: string) => void;
  triggerButton?: 'right' | 'middle';
  onTriggerButtonChange?: (trigger: 'right' | 'middle') => void;
}

export const GestureDrawCanvas: FC<Props> = ({
  value,
  onChange,
  triggerButton = 'right',
  onTriggerButtonChange,
}) => {
  const canvasRef = useRef<HTMLDivElement>(null);
  const [isDrawing, setIsDrawing] = useState(false);
  const [drawingButton, setDrawingButton] = useState<number | null>(null);
  const [points, setPoints] = useState<Point[]>([]);

  // 实时监听键盘物理按键状态 (Ctrl / Shift / Alt)
  const [liveKeys, setLiveKeys] = useState<{ ctrl: boolean; shift: boolean; alt: boolean }>({
    ctrl: false,
    shift: false,
    alt: false,
  });

  // 解析当前手势编码自带的修饰状态
  const parsed = parseGestureCode(value);
  const currentCtrl = parsed.hasCtrl || liveKeys.ctrl;
  const currentShift = parsed.hasShift || liveKeys.shift;
  const currentAlt = parsed.hasAlt || liveKeys.alt;
  const currentTrigger = parsed.isMiddle ? 'middle' : triggerButton;

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

  const handleToggleModifier = (mod: 'ctrl' | 'shift' | 'alt') => {
    const nextCtrl = mod === 'ctrl' ? !parsed.hasCtrl : parsed.hasCtrl;
    const nextShift = mod === 'shift' ? !parsed.hasShift : parsed.hasShift;
    const nextAlt = mod === 'alt' ? !parsed.hasAlt : parsed.hasAlt;

    const newCode = assembleGestureCode({
      isMiddle: currentTrigger === 'middle',
      hasCtrl: nextCtrl,
      hasShift: nextShift,
      hasAlt: nextAlt,
      bareCode: parsed.bareCode,
    });
    onChange(newCode);
  };

  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    e.preventDefault();
    if (!canvasRef.current) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setPoints([{ x, y }]);
    setIsDrawing(true);
    setDrawingButton(e.button);

    // 智能联动触发键
    if (e.button === 1 && onTriggerButtonChange) {
      onTriggerButtonChange('middle');
    } else if (e.button === 2 && onTriggerButtonChange) {
      onTriggerButtonChange('right');
    }

    try {
      (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    } catch {
      // ignore
    }
  }, [onTriggerButtonChange]);

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

    const effectiveButton = drawingButton ?? e.button;
    const isMiddle = effectiveButton === 1 ? true : effectiveButton === 2 ? false : currentTrigger === 'middle';

    if (points.length >= 2) {
      const bare = recognizeStrokes(points);
      if (bare) {
        const full = assembleGestureCode({
          isMiddle,
          hasCtrl: e.ctrlKey || liveKeys.ctrl || parsed.hasCtrl,
          hasShift: e.shiftKey || liveKeys.shift || parsed.hasShift,
          hasAlt: e.altKey || liveKeys.alt || parsed.hasAlt,
          bareCode: bare,
        });
        onChange(full);
      }
    }
    setDrawingButton(null);
  }, [isDrawing, points, onChange, currentTrigger, drawingButton, liveKeys, parsed]);

  const handleClear = () => {
    setPoints([]);
    onChange('');
  };

  const handleSelectPreset = (presetCode: string) => {
    const presetParsed = parseGestureCode(presetCode);
    const full = assembleGestureCode({
      isMiddle: currentTrigger === 'middle' || presetParsed.isMiddle,
      hasCtrl: currentCtrl || presetParsed.hasCtrl,
      hasShift: currentShift || presetParsed.hasShift,
      hasAlt: currentAlt || presetParsed.hasAlt,
      bareCode: presetParsed.bareCode,
    });
    onChange(full);
  };

  const svgPath = pointsToSvgPath(points);
  const lastPoint = points.length > 0 ? points[points.length - 1] : null;
  const presets = currentTrigger === 'middle' ? MIDDLE_PRESET_GESTURES : RIGHT_PRESET_GESTURES;

  return (
    <div className="gesture-draw-container">
      {/* 一体化手势录制控制舱 (Cockpit Bar) */}
      <div className="gesture-cockpit-bar">
        {/* 左侧：触发按键快速切换 */}
        <div className="gesture-cockpit-group">
          <span className="gesture-cockpit-label">触发键:</span>
          <button
            type="button"
            className={`gesture-cockpit-pill ${currentTrigger === 'right' ? 'active' : ''}`}
            onClick={() => onTriggerButtonChange?.('right')}
            title="按住鼠标右键滑动"
          >
            <span>◐</span>
            <span>鼠标右键</span>
          </button>
          <button
            type="button"
            className={`gesture-cockpit-pill ${currentTrigger === 'middle' ? 'active' : ''}`}
            onClick={() => onTriggerButtonChange?.('middle')}
            title="按住鼠标中键(滚轮)滑动"
          >
            <span>◓</span>
            <span>鼠标中键</span>
          </button>
        </div>

        {/* 右侧：修饰键与清空按钮 */}
        <div className="gesture-cockpit-group">
          <span className="gesture-cockpit-label">修饰键:</span>
          <button
            type="button"
            className={`gesture-modifier-pill ${currentCtrl ? 'active' : ''} ${liveKeys.ctrl ? 'is-key-pressed' : ''}`}
            onClick={() => handleToggleModifier('ctrl')}
            title="Ctrl 修饰键 (按住键盘 Ctrl 或点击开启)"
          >
            Ctrl
          </button>
          <button
            type="button"
            className={`gesture-modifier-pill ${currentShift ? 'active' : ''} ${liveKeys.shift ? 'is-key-pressed' : ''}`}
            onClick={() => handleToggleModifier('shift')}
            title="Shift 修饰键 (按住键盘 Shift 或点击开启)"
          >
            Shift
          </button>
          <button
            type="button"
            className={`gesture-modifier-pill ${currentAlt ? 'active' : ''} ${liveKeys.alt ? 'is-key-pressed' : ''}`}
            onClick={() => handleToggleModifier('alt')}
            title="Alt 修饰键 (按住键盘 Alt 或点击开启)"
          >
            Alt
          </button>

          <button
            type="button"
            className="gesture-draw-clear-btn"
            onClick={handleClear}
            title="清空画板"
          >
            <RotateCcw size={12} />
            <span>清空</span>
          </button>
        </div>
      </div>

      {/* 交互式手势捕获板 */}
      <div
        ref={canvasRef}
        className={`gesture-draw-pad ${isDrawing ? 'is-drawing' : ''}`}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onContextMenu={(e) => e.preventDefault()}
        onAuxClick={(e) => e.preventDefault()}
      >
        {/* 背景辅助网格 */}
        <div className="gesture-draw-crosshair" />

        {points.length === 0 && (
          <div className="gesture-draw-watermark">
            <div className="gesture-draw-watermark-icon-box">
              <MousePointer size={20} strokeWidth={2} className="gesture-draw-mouse-icon" />
            </div>
            <div className="gesture-draw-watermark-text">
              按住鼠标划出轨迹 · 支持按住 Ctrl / Shift / Alt 修饰键录制
            </div>
            <div className="gesture-draw-watermark-hint">
              按哪个键划动即自动绑定该键 · 也可点击上方药丸自由组合
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

