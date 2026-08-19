/* ─────────────────────────────────────────────────────────────────────────────
 * EasyToolsLogo.tsx — 极速能效品牌专属矢量 Logo
 *
 * 核心设计语言:
 *   • 几何动势: 经典 6 顶点高能极速闪电 (Pure 6-Point Aerodynamic Lightning)
 *   • 质感层次: 极速电光渐变 + 饱满力量感 (Hyper Electric Vector)
 *   • 动效反馈: 悬停电光脉冲与柔光弥散 (Energy Glow Aura)
 * ───────────────────────────────────────────────────────────────────────────── */

import type { FC, CSSProperties } from 'react';

interface EasyToolsLogoProps {
  size?: number;
  className?: string;
  style?: CSSProperties;
  variant?: 'primary' | 'monochrome' | 'white';
  animated?: boolean;
}

export const EasyToolsLogo: FC<EasyToolsLogoProps> = ({
  size = 24,
  className = '',
  style,
  variant = 'primary',
  animated = true,
}) => {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 36 36"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      className={`easytools-logo ${animated ? 'easytools-logo--animated' : ''} ${className}`}
      style={{
        display: 'inline-block',
        verticalAlign: 'middle',
        overflow: 'visible',
        ...style,
      }}
      aria-label="EasyTools Speed Logo"
    >
      <defs>
        {/* ── 极速科技蓝紫电光渐变 ─────────────────────────────────── */}
        <linearGradient id="et-logo-primary-grad" x1="4" y1="2" x2="32" y2="34" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#38BDF8" />
          <stop offset="45%" stopColor="#4F46E5" />
          <stop offset="100%" stopColor="#7C3AED" />
        </linearGradient>

        {/* ── 能量脉冲辉光滤镜 ────────────────────────────────────── */}
        <filter id="et-logo-glow" x="-20%" y="-20%" width="140%" height="140%" filterUnits="userSpaceOnUse">
          <feGaussianBlur stdDeviation="2.2" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
      </defs>

      {/* ── 底部速度残影/微辉光衬底 ───────────────────────────────── */}
      {variant === 'primary' && (
        <path
          d="M21.5 2 L5.5 18 H16.5 L14.5 34 L30.5 16.5 H19.5 Z"
          fill="url(#et-logo-primary-grad)"
          opacity="0.35"
          filter="url(#et-logo-glow)"
        />
      )}

      {/* ── 闪电主干主体 (标准 6 点尖锐极速折角，无缺角畸形) ──────── */}
      <path
        d="M21.5 2 L5.5 18 H16.5 L14.5 34 L30.5 16.5 H19.5 Z"
        fill={
          variant === 'primary'
            ? 'url(#et-logo-primary-grad)'
            : variant === 'white'
            ? '#FFFFFF'
            : 'currentColor'
        }
      />
    </svg>
  );
};
