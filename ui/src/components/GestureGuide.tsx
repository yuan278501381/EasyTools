import React, { useEffect, useState } from 'react';
import './GestureGuide.css';

const GUIDES = [
  { code: 'U', name: '向上', path: 'M 50 80 L 50 20' },
  { code: 'D', name: '向下', path: 'M 50 20 L 50 80' },
  { code: 'L', name: '向左', path: 'M 80 50 L 20 50' },
  { code: 'R', name: '向右', path: 'M 20 50 L 80 50' },
  { code: 'DR', name: '右下', path: 'M 30 30 L 70 70' },
  { code: 'DL', name: '左下', path: 'M 70 30 L 30 70' },
  { code: 'UR', name: '右上', path: 'M 30 70 L 70 30' },
  { code: 'UL', name: '左上', path: 'M 70 70 L 30 30' },
  { code: 'RD', name: '先右后下', path: 'M 30 30 L 70 30 L 70 70' },
];

export const GestureGuide: React.FC = () => {
  const [index, setIndex] = useState(0);

  useEffect(() => {
    const timer = setInterval(() => {
      setIndex((i) => (i + 1) % GUIDES.length);
    }, 2500);
    return () => clearInterval(timer);
  }, []);

  const guide = GUIDES[index];

  return (
    <div className="gesture-guide">
      <div className="gesture-guide__info">
        <div className="gesture-guide__title">手势演示</div>
        <div className="gesture-guide__desc">按住右键滑动以触发操作</div>
        <div className="gesture-guide__badge">
          <span>{guide.code}</span>
          <span>{guide.name}</span>
        </div>
      </div>
      <div className="gesture-guide__canvas">
        <svg viewBox="0 0 100 100" width="100%" height="100%">
          <path
            key={guide.code}
            className="gesture-guide__path"
            d={guide.path}
            fill="none"
            stroke="var(--primary)"
            strokeWidth="6"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
          {/* Animated dot */}
          <circle 
            className="gesture-guide__dot" 
            r="4" 
            fill="var(--primary)" 
            style={{ offsetPath: `path('${guide.path}')` } as React.CSSProperties}
          />
        </svg>
      </div>
    </div>
  );
};
