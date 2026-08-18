import { type FC, useMemo, useState } from 'react';
import './KeyboardHeatmap.css';
import { useTranslation } from 'react-i18next';
import { Flame, LayoutGrid, Keyboard } from 'lucide-react';

interface KeyboardHeatmapProps {
  keyMap: Record<number, number>;
}

// Definition of a single key in the visual layout
interface KeyDef {
  label: string;
  fullName?: string;
  vkCode: number | number[];
  width?: string;
  isSpacer?: boolean;
}

export type KeyboardLayoutMode = '104' | '87' | '60';

// ── 主键区 (Main Alphanumeric Cluster) ──────────────────────────────────────────
const MAIN_CLUSTER: KeyDef[][] = [
  // F-Row
  [
    { label: 'Esc', fullName: 'Escape', vkCode: 0x1B, width: 'key-width-100' },
    { label: '', isSpacer: true, vkCode: 0, width: 'key-spacer-100' },
    { label: 'F1', vkCode: 0x70 }, { label: 'F2', vkCode: 0x71 }, { label: 'F3', vkCode: 0x72 }, { label: 'F4', vkCode: 0x73 },
    { label: '', isSpacer: true, vkCode: 0, width: 'key-spacer-50' },
    { label: 'F5', vkCode: 0x74 }, { label: 'F6', vkCode: 0x75 }, { label: 'F7', vkCode: 0x76 }, { label: 'F8', vkCode: 0x77 },
    { label: '', isSpacer: true, vkCode: 0, width: 'key-spacer-50' },
    { label: 'F9', vkCode: 0x78 }, { label: 'F10', vkCode: 0x79 }, { label: 'F11', vkCode: 0x7A }, { label: 'F12', vkCode: 0x7B },
  ],
  // Row 1 (Numbers)
  [
    { label: '`', fullName: '反引号 / 波浪号', vkCode: 0xC0 },
    { label: '1', vkCode: 0x31 }, { label: '2', vkCode: 0x32 }, { label: '3', vkCode: 0x33 },
    { label: '4', vkCode: 0x34 }, { label: '5', vkCode: 0x35 }, { label: '6', vkCode: 0x36 },
    { label: '7', vkCode: 0x37 }, { label: '8', vkCode: 0x38 }, { label: '9', vkCode: 0x39 },
    { label: '0', vkCode: 0x30 }, { label: '-', fullName: '减号', vkCode: 0xBD },
    { label: '=', fullName: '等号', vkCode: 0xBB },
    { label: 'Backspace', fullName: '退格键', vkCode: 0x08, width: 'key-width-200' }
  ],
  // Row 2 (QWERTY)
  [
    { label: 'Tab', fullName: '制表键', vkCode: 0x09, width: 'key-width-150' },
    { label: 'Q', vkCode: 0x51 }, { label: 'W', vkCode: 0x57 }, { label: 'E', vkCode: 0x45 },
    { label: 'R', vkCode: 0x52 }, { label: 'T', vkCode: 0x54 }, { label: 'Y', vkCode: 0x59 },
    { label: 'U', vkCode: 0x55 }, { label: 'I', vkCode: 0x49 }, { label: 'O', vkCode: 0x4F },
    { label: 'P', vkCode: 0x50 }, { label: '[', fullName: '左方括号', vkCode: 0xDB },
    { label: ']', fullName: '右方括号', vkCode: 0xDD },
    { label: '\\', fullName: '反斜杠', vkCode: 0xDC, width: 'key-width-150' }
  ],
  // Row 3 (ASDF)
  [
    { label: 'Caps', fullName: '大写锁定', vkCode: 0x14, width: 'key-width-175' },
    { label: 'A', vkCode: 0x41 }, { label: 'S', vkCode: 0x53 }, { label: 'D', vkCode: 0x44 },
    { label: 'F', vkCode: 0x46 }, { label: 'G', vkCode: 0x47 }, { label: 'H', vkCode: 0x48 },
    { label: 'J', vkCode: 0x4A }, { label: 'K', vkCode: 0x4B }, { label: 'L', vkCode: 0x4C },
    { label: ';', fullName: '分号', vkCode: 0xBA }, { label: "'", fullName: '单引号', vkCode: 0xDE },
    { label: 'Enter', fullName: '回车换行', vkCode: 0x0D, width: 'key-width-225' }
  ],
  // Row 4 (ZXCV)
  [
    { label: 'Shift', fullName: '左 Shift', vkCode: [0xA0, 0x10], width: 'key-width-225' },
    { label: 'Z', vkCode: 0x5A }, { label: 'X', vkCode: 0x58 }, { label: 'C', vkCode: 0x43 },
    { label: 'V', vkCode: 0x56 }, { label: 'B', vkCode: 0x42 }, { label: 'N', vkCode: 0x4E },
    { label: 'M', vkCode: 0x4D }, { label: ',', fullName: '逗号', vkCode: 0xBC },
    { label: '.', fullName: '句号', vkCode: 0xBE }, { label: '/', fullName: '斜杠', vkCode: 0xBF },
    { label: 'Shift', fullName: '右 Shift', vkCode: [0xA1], width: 'key-width-275' }
  ],
  // Row 5 (Bottom)
  [
    { label: 'Ctrl', fullName: '左 Control', vkCode: [0xA2, 0x11], width: 'key-width-125' },
    { label: 'Win', fullName: 'Windows 徽标键', vkCode: 0x5B, width: 'key-width-125' },
    { label: 'Alt', fullName: '左 Alt', vkCode: [0xA4, 0x12], width: 'key-width-125' },
    { label: 'Space', fullName: '空格键', vkCode: 0x20, width: 'key-width-space' },
    { label: 'Alt', fullName: '右 Alt', vkCode: 0xA5, width: 'key-width-125' },
    { label: 'Win', fullName: '右 Windows', vkCode: 0x5C, width: 'key-width-125' },
    { label: 'Menu', fullName: '上下文菜单键', vkCode: 0x5D, width: 'key-width-125' },
    { label: 'Ctrl', fullName: '右 Control', vkCode: 0xA3, width: 'key-width-125' }
  ]
];

// ── 编辑功能与方向键区 (Navigation / Editing / Arrows Cluster) ────────────────
const NAV_CLUSTER: KeyDef[][] = [
  // F-Row align
  [
    { label: 'PrtSc', fullName: '屏幕快照 (Print Screen)', vkCode: 0x2C },
    { label: 'ScrLk', fullName: '滚动锁定 (Scroll Lock)', vkCode: 0x91 },
    { label: 'Pause', fullName: '暂停断点 (Pause Break)', vkCode: 0x13 }
  ],
  // Row 1 align
  [
    { label: 'Ins', fullName: '插入键 (Insert)', vkCode: 0x2D },
    { label: 'Home', fullName: '行首键 (Home)', vkCode: 0x24 },
    { label: 'PgUp', fullName: '上一页 (Page Up)', vkCode: 0x21 }
  ],
  // Row 2 align
  [
    { label: 'Del', fullName: '删除键 (Delete)', vkCode: 0x2E },
    { label: 'End', fullName: '行尾键 (End)', vkCode: 0x23 },
    { label: 'PgDn', fullName: '下一页 (Page Down)', vkCode: 0x22 }
  ],
  // Row 3 align (空行隔离)
  [
    { label: '', isSpacer: true, vkCode: 0, width: 'key-width-300' }
  ],
  // Row 4 align (上方向键)
  [
    { label: '', isSpacer: true, vkCode: 0 },
    { label: '↑', fullName: '向上方向键', vkCode: 0x26 },
    { label: '', isSpacer: true, vkCode: 0 }
  ],
  // Row 5 align (左、下、右方向键)
  [
    { label: '←', fullName: '向左方向键', vkCode: 0x25 },
    { label: '↓', fullName: '向下方向键', vkCode: 0x28 },
    { label: '→', fullName: '向右方向键', vkCode: 0x27 }
  ]
];

// ── 数字小键盘区 (Numpad Cluster) ──────────────────────────────────────────
const NUMPAD_CLUSTER: KeyDef[][] = [
  // F-Row align (小键盘指示/留白)
  [
    { label: '', isSpacer: true, vkCode: 0, width: 'key-width-400' }
  ],
  // Row 1 align
  [
    { label: 'Num', fullName: '小键盘数字锁定 (Num Lock)', vkCode: 0x90 },
    { label: '/', fullName: '数字除号 (Numpad /)', vkCode: 0x6F },
    { label: '*', fullName: '数字乘号 (Numpad *)', vkCode: 0x6A },
    { label: '-', fullName: '数字减号 (Numpad -)', vkCode: 0x6D }
  ],
  // Row 2 align
  [
    { label: '7', fullName: '数字 7 (Home)', vkCode: 0x67 },
    { label: '8', fullName: '数字 8 (Up)', vkCode: 0x68 },
    { label: '9', fullName: '数字 9 (PgUp)', vkCode: 0x69 },
    { label: '+', fullName: '数字加号 (Numpad +)', vkCode: 0x6B }
  ],
  // Row 3 align
  [
    { label: '4', fullName: '数字 4 (Left)', vkCode: 0x64 },
    { label: '5', fullName: '数字 5 (Center)', vkCode: 0x65 },
    { label: '6', fullName: '数字 6 (Right)', vkCode: 0x66 },
    { label: '', isSpacer: true, vkCode: 0 }
  ],
  // Row 4 align
  [
    { label: '1', fullName: '数字 1 (End)', vkCode: 0x61 },
    { label: '2', fullName: '数字 2 (Down)', vkCode: 0x62 },
    { label: '3', fullName: '数字 3 (PgDn)', vkCode: 0x63 },
    { label: 'Enter', fullName: '数字确认键 (Numpad Enter)', vkCode: [0x0D] }
  ],
  // Row 5 align
  [
    { label: '0', fullName: '数字 0 (Insert)', vkCode: 0x60, width: 'key-width-200' },
    { label: '.', fullName: '数字小数点 (Numpad .)', vkCode: 0x6E },
    { label: '', isSpacer: true, vkCode: 0 }
  ]
];

function formatMicroCount(num: number): string {
  if (num <= 0) return '';
  if (num >= 10000) return `${(num / 1000).toFixed(0)}k`;
  if (num >= 1000) return `${(num / 1000).toFixed(1)}k`;
  return String(num);
}

interface KeyHeatStyle {
  background: string;
  borderColor: string;
  color: string;
  subColor: string;
  boxShadow: string;
  level: number;
}

function computeKeyHeatStyle(count: number, intensity: number): KeyHeatStyle {
  if (count <= 0) {
    return {
      background: 'var(--key-bg-idle)',
      borderColor: 'var(--key-border-idle)',
      color: 'var(--key-text-idle)',
      subColor: 'transparent',
      boxShadow: 'var(--key-shadow-idle)',
      level: 0,
    };
  }

  // 感官平方根非线性映射，低频键也能产生优雅的淡紫呼吸感
  const s = Math.min(1, Math.max(0, Math.pow(intensity, 0.48)));

  if (s < 0.28) {
    const t = s / 0.28;
    return {
      background: `rgba(139, 92, 246, ${0.12 + t * 0.16})`,
      borderColor: `rgba(167, 139, 250, ${0.25 + t * 0.25})`,
      color: 'var(--key-text-level1)',
      subColor: 'var(--key-subtext-level1)',
      boxShadow: `0 2px 0 rgba(0,0,0,0.06), 0 0 ${4 + t * 6}px rgba(139, 92, 246, ${0.1 + t * 0.15})`,
      level: 1,
    };
  } else if (s < 0.60) {
    const t = (s - 0.28) / 0.32;
    return {
      background: `linear-gradient(135deg, rgba(147, 51, 234, ${0.38 + t * 0.22}), rgba(192, 38, 211, ${0.28 + t * 0.22}))`,
      borderColor: `rgba(216, 180, 254, ${0.5 + t * 0.3})`,
      color: 'var(--key-text-level2)',
      subColor: 'var(--key-subtext-level2)',
      boxShadow: `0 2px 0 rgba(0,0,0,0.1), 0 0 ${8 + t * 8}px rgba(168, 85, 247, ${0.25 + t * 0.2})`,
      level: 2,
    };
  } else if (s < 0.88) {
    const t = (s - 0.60) / 0.28;
    return {
      background: `linear-gradient(135deg, rgba(236, 72, 153, ${0.7 + t * 0.2}), rgba(249, 115, 22, ${0.75 + t * 0.2}))`,
      borderColor: `rgba(253, 186, 116, ${0.65 + t * 0.3})`,
      color: '#ffffff',
      subColor: 'rgba(255, 255, 255, 0.85)',
      boxShadow: `0 2px 0 rgba(0,0,0,0.14), 0 0 ${12 + t * 8}px rgba(249, 115, 22, ${0.35 + t * 0.2})`,
      level: 3,
    };
  } else {
    // 巅峰热力峰值 (Solar Gold / Flame)
    const t = (s - 0.88) / 0.12;
    return {
      background: `linear-gradient(135deg, #f43f5e, #f59e0b)`,
      borderColor: '#fef08a',
      color: '#ffffff',
      subColor: 'rgba(255, 255, 255, 0.95)',
      boxShadow: `0 2px 0 rgba(180, 83, 9, 0.6), 0 0 ${16 + t * 10}px rgba(245, 158, 11, 0.55)`,
      level: 4,
    };
  }
}

export const KeyboardHeatmap: FC<KeyboardHeatmapProps> = ({ keyMap }) => {
  const { t } = useTranslation();
  const [layoutMode, setLayoutMode] = useState<KeyboardLayoutMode>('104');

  // 计算今日总击键数
  const totalKeystrokes = useMemo(() => {
    return Object.values(keyMap).reduce((sum, v) => sum + v, 0);
  }, [keyMap]);

  // 计算最高频按键 (全键盘所有区域综合统计)
  const { maxCount, topKey } = useMemo(() => {
    let max = 0;
    let best = { label: '', fullName: '', count: 0 };

    const checkCluster = (cluster: KeyDef[][]) => {
      cluster.forEach(row => {
        row.forEach(k => {
          if (k.isSpacer) return;
          const count = Array.isArray(k.vkCode)
            ? k.vkCode.reduce((sum, code) => sum + (keyMap[code] || 0), 0)
            : keyMap[k.vkCode] || 0;
          if (count > max) max = count;
          if (count > best.count) {
            best = { label: k.label, fullName: k.fullName || k.label, count };
          }
        });
      });
    };

    checkCluster(MAIN_CLUSTER);
    checkCluster(NAV_CLUSTER);
    checkCluster(NUMPAD_CLUSTER);

    return { maxCount: Math.max(max, 5), topKey: best };
  }, [keyMap]);

  const getHeatData = (keyDef: KeyDef) => {
    if (keyDef.isSpacer) {
      return { count: 0, intensity: 0, percentage: '0.0', style: computeKeyHeatStyle(0, 0) };
    }
    const count = Array.isArray(keyDef.vkCode)
      ? keyDef.vkCode.reduce((sum, code) => sum + (keyMap[code] || 0), 0)
      : keyMap[keyDef.vkCode] || 0;
    const intensity = count / maxCount;
    const percentage = totalKeystrokes > 0 ? ((count / totalKeystrokes) * 100).toFixed(1) : '0.0';
    const style = computeKeyHeatStyle(count, intensity);
    return { count, intensity, percentage, style };
  };

  const renderKey = (keyDef: KeyDef, keyIndex: number) => {
    if (keyDef.isSpacer) {
      return (
        <div
          key={keyIndex}
          className={`keyboard-heatmap__spacer ${keyDef.width || ''}`}
        />
      );
    }

    const { count, percentage, style } = getHeatData(keyDef);
    const isPeak = topKey.count > 0 && topKey.label === keyDef.label;

    return (
      <div
        key={keyIndex}
        className={`keyboard-heatmap__key ${keyDef.width || ''} ${count > 0 ? 'has-heat' : ''} ${isPeak ? 'is-peak' : ''}`}
        style={{
          background: style.background,
          borderColor: style.borderColor,
          color: style.color,
          boxShadow: style.boxShadow,
        }}
      >
        <span className="keyboard-heatmap__label">{keyDef.label}</span>
        {count > 0 && (
          <span
            className="keyboard-heatmap__micro-count"
            style={{ color: style.subColor }}
          >
            {formatMicroCount(count)}
          </span>
        )}
        {isPeak && <span className="keyboard-heatmap__crown">🔥</span>}

        {/* 浮动玻璃微质感提示卡片 */}
        <div className="keyboard-heatmap__tooltip">
          <div className="tooltip-header">
            <span className="tooltip-key-name">{keyDef.fullName || keyDef.label}</span>
            {isPeak && <span className="tooltip-badge-peak">今日最高频</span>}
          </div>
          <div className="tooltip-body">
            <span className="tooltip-count">
              <strong>{count.toLocaleString()}</strong> 次击键
            </span>
            {count > 0 && (
              <span className="tooltip-pct">占比 {percentage}%</span>
            )}
          </div>
        </div>
      </div>
    );
  };

  return (
    <div className="keyboard-heatmap-wrapper">
      {/* 顶部工具栏: 布局规格切换器 */}
      <div className="keyboard-heatmap__header-bar">
        <div className="keyboard-heatmap__title-tag">
          <Keyboard size={15} className="keyboard-heatmap__title-icon" />
          <span>全键盘按键热力分布 ({layoutMode === '104' ? '104 键全尺寸' : layoutMode === '87' ? '87 键 TKL' : '60 键紧凑'})</span>
        </div>

        <div className="keyboard-layout-segmented">
          <button
            type="button"
            className={`keyboard-layout-btn ${layoutMode === '104' ? 'active' : ''}`}
            onClick={() => setLayoutMode('104')}
          >
            <LayoutGrid size={12} />
            <span>104 全键盘</span>
          </button>
          <button
            type="button"
            className={`keyboard-layout-btn ${layoutMode === '87' ? 'active' : ''}`}
            onClick={() => setLayoutMode('87')}
          >
            <span>87 键 TKL</span>
          </button>
          <button
            type="button"
            className={`keyboard-layout-btn ${layoutMode === '60' ? 'active' : ''}`}
            onClick={() => setLayoutMode('60')}
          >
            <span>60 键主键区</span>
          </button>
        </div>
      </div>

      {/* 机械键盘热力机体 */}
      <div className={`keyboard-heatmap keyboard-heatmap--${layoutMode}`}>
        {/* 主键区 (60% Block) */}
        <div className="keyboard-cluster keyboard-cluster--main">
          {MAIN_CLUSTER.map((row, rIdx) => (
            <div key={rIdx} className="keyboard-heatmap__row">
              {row.map((k, kIdx) => renderKey(k, kIdx))}
            </div>
          ))}
        </div>

        {/* 编辑功能与方向键区 (Nav Cluster - 87/104 模式下展示) */}
        {layoutMode !== '60' && (
          <div className="keyboard-cluster keyboard-cluster--nav">
            {NAV_CLUSTER.map((row, rIdx) => (
              <div key={rIdx} className="keyboard-heatmap__row">
                {row.map((k, kIdx) => renderKey(k, kIdx))}
              </div>
            ))}
          </div>
        )}

        {/* 数字小键盘区 (Numpad Cluster - 104 模式下展示) */}
        {layoutMode === '104' && (
          <div className="keyboard-cluster keyboard-cluster--numpad">
            {NUMPAD_CLUSTER.map((row, rIdx) => (
              <div key={rIdx} className="keyboard-heatmap__row">
                {row.map((k, kIdx) => renderKey(k, kIdx))}
              </div>
            ))}
          </div>
        )}
      </div>

      {/* 底部专业热力能谱图例栏 */}
      <div className="keyboard-heatmap__footer">
        <div className="keyboard-heatmap__legend">
          <span className="legend-label">{t('stats.lowHeat', '闲置 / 0')}</span>
          <div className="legend-spectrum">
            <div className="spectrum-bar" />
          </div>
          <span className="legend-label">{t('stats.highHeat', '极高频 (Peak)')}</span>
        </div>

        {topKey.count > 0 && (
          <div className="keyboard-heatmap__top-badge">
            <div className="top-badge-icon-box">
              <Flame size={12} strokeWidth={2.5} className="top-badge-flame-icon" />
            </div>
            <div className="top-badge-content">
              <span className="top-badge-label">{t('stats.topKey', '今日最高频')}:</span>
              <span className="top-badge-key-name">{topKey.fullName || topKey.label}</span>
              <span className="top-badge-stat">
                <span className="top-badge-count">{topKey.count.toLocaleString()}</span>
                <span className="top-badge-unit">{t('stats.times', '次')}</span>
                <span className="top-badge-divider">·</span>
                <span className="top-badge-pct">{((topKey.count / (totalKeystrokes || 1)) * 100).toFixed(1)}%</span>
              </span>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
