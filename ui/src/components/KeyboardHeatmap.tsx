import { type FC, useMemo } from 'react';
import './KeyboardHeatmap.css';
import { useTranslation } from 'react-i18next';
import { Flame } from 'lucide-react';

interface KeyboardHeatmapProps {
  keyMap: Record<number, number>;
}

// Definition of a single key in the visual layout
interface KeyDef {
  label: string;
  fullName?: string;
  vkCode: number | number[];
  width?: string;
}

// Standard Windows VK Codes mapping to standard layout
const LAYOUT: KeyDef[][] = [
  // Row 1
  [
    { label: '`', fullName: '反引号 / 波浪号', vkCode: 0xC0 },
    { label: '1', vkCode: 0x31 }, { label: '2', vkCode: 0x32 }, { label: '3', vkCode: 0x33 },
    { label: '4', vkCode: 0x34 }, { label: '5', vkCode: 0x35 }, { label: '6', vkCode: 0x36 },
    { label: '7', vkCode: 0x37 }, { label: '8', vkCode: 0x38 }, { label: '9', vkCode: 0x39 },
    { label: '0', vkCode: 0x30 }, { label: '-', fullName: '减号', vkCode: 0xBD },
    { label: '=', fullName: '等号', vkCode: 0xBB },
    { label: 'Backspace', fullName: '退格键', vkCode: 0x08, width: 'key-width-200' }
  ],
  // Row 2
  [
    { label: 'Tab', fullName: '制表键', vkCode: 0x09, width: 'key-width-150' },
    { label: 'Q', vkCode: 0x51 }, { label: 'W', vkCode: 0x57 }, { label: 'E', vkCode: 0x45 },
    { label: 'R', vkCode: 0x52 }, { label: 'T', vkCode: 0x54 }, { label: 'Y', vkCode: 0x59 },
    { label: 'U', vkCode: 0x55 }, { label: 'I', vkCode: 0x49 }, { label: 'O', vkCode: 0x4F },
    { label: 'P', vkCode: 0x50 }, { label: '[', fullName: '左方括号', vkCode: 0xDB },
    { label: ']', fullName: '右方括号', vkCode: 0xDD },
    { label: '\\', fullName: '反斜杠', vkCode: 0xDC, width: 'key-width-150' }
  ],
  // Row 3
  [
    { label: 'Caps', fullName: '大写锁定', vkCode: 0x14, width: 'key-width-175' },
    { label: 'A', vkCode: 0x41 }, { label: 'S', vkCode: 0x53 }, { label: 'D', vkCode: 0x44 },
    { label: 'F', vkCode: 0x46 }, { label: 'G', vkCode: 0x47 }, { label: 'H', vkCode: 0x48 },
    { label: 'J', vkCode: 0x4A }, { label: 'K', vkCode: 0x4B }, { label: 'L', vkCode: 0x4C },
    { label: ';', fullName: '分号', vkCode: 0xBA }, { label: "'", fullName: '单引号', vkCode: 0xDE },
    { label: 'Enter', fullName: '回车换行', vkCode: 0x0D, width: 'key-width-225' }
  ],
  // Row 4
  [
    { label: 'Shift', fullName: '左 Shift', vkCode: [0xA0, 0x10], width: 'key-width-225' },
    { label: 'Z', vkCode: 0x5A }, { label: 'X', vkCode: 0x58 }, { label: 'C', vkCode: 0x43 },
    { label: 'V', vkCode: 0x56 }, { label: 'B', vkCode: 0x42 }, { label: 'N', vkCode: 0x4E },
    { label: 'M', vkCode: 0x4D }, { label: ',', fullName: '逗号', vkCode: 0xBC },
    { label: '.', fullName: '句号', vkCode: 0xBE }, { label: '/', fullName: '斜杠', vkCode: 0xBF },
    { label: 'Shift', fullName: '右 Shift', vkCode: [0xA1], width: 'key-width-275' }
  ],
  // Row 5
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

  // 计算今日总击键数
  const totalKeystrokes = useMemo(() => {
    return Object.values(keyMap).reduce((sum, v) => sum + v, 0);
  }, [keyMap]);

  // 计算最高频按键
  const { maxCount, topKey } = useMemo(() => {
    let max = 0;
    let best = { label: '', fullName: '', count: 0 };

    LAYOUT.forEach(row => {
      row.forEach(k => {
        const count = Array.isArray(k.vkCode)
          ? k.vkCode.reduce((sum, code) => sum + (keyMap[code] || 0), 0)
          : keyMap[k.vkCode] || 0;
        if (count > max) max = count;
        if (count > best.count) {
          best = { label: k.label, fullName: k.fullName || k.label, count };
        }
      });
    });

    return { maxCount: Math.max(max, 5), topKey: best };
  }, [keyMap]);

  const getHeatData = (keyDef: KeyDef) => {
    const count = Array.isArray(keyDef.vkCode)
      ? keyDef.vkCode.reduce((sum, code) => sum + (keyMap[code] || 0), 0)
      : keyMap[keyDef.vkCode] || 0;
    const intensity = count / maxCount;
    const percentage = totalKeystrokes > 0 ? ((count / totalKeystrokes) * 100).toFixed(1) : '0.0';
    const style = computeKeyHeatStyle(count, intensity);
    return { count, intensity, percentage, style };
  };

  return (
    <div className="keyboard-heatmap-wrapper">
      <div className="keyboard-heatmap">
        {LAYOUT.map((row, rowIndex) => (
          <div key={rowIndex} className="keyboard-heatmap__row">
            {row.map((keyDef, keyIndex) => {
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
            })}
          </div>
        ))}
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
