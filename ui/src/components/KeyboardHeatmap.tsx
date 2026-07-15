import { type FC, useMemo } from 'react';
import './KeyboardHeatmap.css';
import { useTranslation } from 'react-i18next';

interface KeyboardHeatmapProps {
  keyMap: Record<number, number>;
}

// Definition of a single key in the visual layout
interface KeyDef {
  label: string;
  vkCode: number | number[]; // Can map to multiple VKs (e.g. both LShift and RShift if we want, or just specific ones)
  width?: string;
}

// Standard Windows VK Codes mapping to a 60% layout + some extras
const LAYOUT: KeyDef[][] = [
  // Row 1
  [
    { label: '`', vkCode: 0xC0 }, { label: '1', vkCode: 0x31 }, { label: '2', vkCode: 0x32 }, { label: '3', vkCode: 0x33 },
    { label: '4', vkCode: 0x34 }, { label: '5', vkCode: 0x35 }, { label: '6', vkCode: 0x36 }, { label: '7', vkCode: 0x37 },
    { label: '8', vkCode: 0x38 }, { label: '9', vkCode: 0x39 }, { label: '0', vkCode: 0x30 }, { label: '-', vkCode: 0xBD },
    { label: '=', vkCode: 0xBB }, { label: 'Backspace', vkCode: 0x08, width: 'key-width-200' }
  ],
  // Row 2
  [
    { label: 'Tab', vkCode: 0x09, width: 'key-width-150' }, { label: 'Q', vkCode: 0x51 }, { label: 'W', vkCode: 0x57 },
    { label: 'E', vkCode: 0x45 }, { label: 'R', vkCode: 0x52 }, { label: 'T', vkCode: 0x54 }, { label: 'Y', vkCode: 0x59 },
    { label: 'U', vkCode: 0x55 }, { label: 'I', vkCode: 0x49 }, { label: 'O', vkCode: 0x4F }, { label: 'P', vkCode: 0x50 },
    { label: '[', vkCode: 0xDB }, { label: ']', vkCode: 0xDD }, { label: '\\', vkCode: 0xDC, width: 'key-width-150' }
  ],
  // Row 3
  [
    { label: 'Caps', vkCode: 0x14, width: 'key-width-175' }, { label: 'A', vkCode: 0x41 }, { label: 'S', vkCode: 0x53 },
    { label: 'D', vkCode: 0x44 }, { label: 'F', vkCode: 0x46 }, { label: 'G', vkCode: 0x47 }, { label: 'H', vkCode: 0x48 },
    { label: 'J', vkCode: 0x4A }, { label: 'K', vkCode: 0x4B }, { label: 'L', vkCode: 0x4C }, { label: ';', vkCode: 0xBA },
    { label: "'", vkCode: 0xDE }, { label: 'Enter', vkCode: 0x0D, width: 'key-width-225' }
  ],
  // Row 4
  [
    { label: 'Shift', vkCode: [0xA0, 0x10], width: 'key-width-225' }, { label: 'Z', vkCode: 0x5A }, { label: 'X', vkCode: 0x58 },
    { label: 'C', vkCode: 0x43 }, { label: 'V', vkCode: 0x56 }, { label: 'B', vkCode: 0x42 }, { label: 'N', vkCode: 0x4E },
    { label: 'M', vkCode: 0x4D }, { label: ',', vkCode: 0xBC }, { label: '.', vkCode: 0xBE }, { label: '/', vkCode: 0xBF },
    { label: 'Shift', vkCode: [0xA1], width: 'key-width-275' }
  ],
  // Row 5
  [
    { label: 'Ctrl', vkCode: [0xA2, 0x11], width: 'key-width-125' }, { label: 'Win', vkCode: 0x5B, width: 'key-width-125' },
    { label: 'Alt', vkCode: [0xA4, 0x12], width: 'key-width-125' }, { label: 'Space', vkCode: 0x20, width: 'key-width-space' },
    { label: 'Alt', vkCode: 0xA5, width: 'key-width-125' }, { label: 'Win', vkCode: 0x5C, width: 'key-width-125' },
    { label: 'Menu', vkCode: 0x5D, width: 'key-width-125' }, { label: 'Ctrl', vkCode: 0xA3, width: 'key-width-125' }
  ]
];

export const KeyboardHeatmap: FC<KeyboardHeatmapProps> = ({ keyMap }) => {
  const { t } = useTranslation();

  // Find max count to normalize heat intensity
  const maxCount = useMemo(() => {
    let max = 0;
    Object.values(keyMap).forEach(v => {
      if (v > max) max = v;
    });
    // Add a minimum threshold so the highest isn't always 100% if it's just 1 click
    return Math.max(max, 5); 
  }, [keyMap]);

  const getHeatIntensity = (vkCode: number | number[]) => {
    const count = Array.isArray(vkCode)
      ? vkCode.reduce((sum, code) => sum + (keyMap[code] || 0), 0)
      : keyMap[vkCode] || 0;
    return { count, intensity: count / maxCount };
  };

  return (
    <div className="keyboard-heatmap">
      {LAYOUT.map((row, rowIndex) => (
        <div key={rowIndex} className="keyboard-heatmap__row">
          {row.map((keyDef, keyIndex) => {
            const { count, intensity } = getHeatIntensity(keyDef.vkCode);
            // Non-linear scaling for better visibility of low-frequency keys
            const scaledIntensity = Math.pow(intensity, 0.6);
            
            // Map intensity (0..1) to Hue (240..0, Blue to Red)
            let heatColor = 'transparent';
            let heatGlow = 'none';
            if (count > 0) {
              const hue = Math.max(0, (1 - scaledIntensity) * 240);
              const alpha = Math.min(0.8, 0.3 + scaledIntensity * 0.5);
              heatColor = `hsla(${hue}, 100%, 50%, ${alpha})`;
              heatGlow = `inset 0 0 12px hsla(${hue}, 100%, 50%, ${alpha * 0.5})`;
            }

            return (
              <div 
                key={keyIndex} 
                className={`keyboard-heatmap__key ${keyDef.width || ''}`}
                style={{ 
                  '--heat-color': heatColor,
                  '--heat-glow': heatGlow
                } as React.CSSProperties}
              >
                <div className="keyboard-heatmap__heat" />
                <span className="keyboard-heatmap__label">{keyDef.label}</span>
                <div className="tooltip">
                  {keyDef.label}: {count} {t('stats.clicks', '次')}
                </div>
              </div>
            );
          })}
        </div>
      ))}
    </div>
  );
};
