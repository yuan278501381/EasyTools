/* ─────────────────────────────────────────────────────────────────────────────
 * EasyToolsLogo.tsx — 极速能效品牌专属矢量 Logo
 *
 * 核心设计语言:
 *   • 几何动势: 45° 动力面前倾切角 + 棱镜折面 (Aero-chiseled Speed Geometry)
 *   • 质感层次: 极速电光双层渐变 + 3D 高光立体微折射面 (Hyper Electric Prism)
 *   • 动效反馈: 悬停电光脉冲与柔光弥散 (Energy Glow Aura)
 * ───────────────────────────────────────────────────────────────────────────── */

import type { FC, CSSProperties } from 'react';

interface EasyToolsLogoProps {
  size?: number;
  className?: string;
  style?: CSSProperties;
  variant?: 'primary' | 'monochrome' | 'gold';
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
        <linearGradient id="et-logo-primary-grad" x1="4" y1="2" x2="30" y2="34" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#38BDF8" />
          <stop offset="45%" stopColor="#4F46E5" />
          <stop offset="100%" stopColor="#7C3AED" />
        </linearGradient>

        {/* ── 顶部 3D 棱柱高光切面 ───────────────────────────────── */}
        <linearGradient id="et-logo-highlight-grad" x1="12" y1="2" x2="26" y2="18" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#BAE6FD" stopOpacity="0.95" />
          <stop offset="60%" stopColor="#60A5FA" stopOpacity="0.8" />
          <stop offset="100%" stopColor="#3B82F6" stopOpacity="0.2" />
        </linearGradient>

        {/* ── 底部疾速折射面 ─────────────────────────────────────── */}
        <linearGradient id="et-logo-facet-grad" x1="10" y1="16" x2="24" y2="34" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#6366F1" stopOpacity="0.9" />
          <stop offset="100%" stopColor="#9333EA" stopOpacity="1" />
        </linearGradient>

        {/* ── 能量脉冲辉光滤镜 ────────────────────────────────────── */}
        <filter id="et-logo-glow" x="-20%" y="-20%" width="140%" height="140%" filterUnits="userSpaceOnUse">
          <feGaussianBlur stdDeviation="2.2" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
      </defs>

      {/* ── 底部速度残影/微辉光衬底 ───────────────────────────────── */}
      <path
        d="M20.5 2.5 L9.5 16.5 L17.5 16.5 L12.5 33.5 L27.5 17.5 L19.5 17.5 Z"
        fill="url(#et-logo-primary-grad)"
        opacity="0.35"
        filter="url(#et-logo-glow)"
      />

      {/* ── 闪电主干主体 (锐利动势切角) ─────────────────────────── */}
      <path
        d="M21 2 L8.5 17 H17 L12 34 L28.5 16.5 H19.5 L23.5 2 Z"
        fill={variant === 'primary' ? 'url(#et-logo-primary-grad)' : 'currentColor'}
      />

      {/* ── 右上 3D 高光棱面 (增添速度流体与现代质感) ───────────── */}
      <path
        d="M21 2 L23.5 2 L19.5 16.5 H28.5 L16.5 29.5 L17.8 17 H9.5 L21 2 Z"
        fill="url(#et-logo-highlight-grad)"
        opacity="0.75"
        style={{ mixBlendMode: 'overlay' }}
      />

      {/* ── 左侧锋利破空刃面 (Aero-leading edge) ──────────────────── */}
      <path
        d="M21 2 L13.5 11 L17 17 H8.5 L21 2 Z"
        fill="#FFFFFF"
        opacity="0.28"
      />

      {/* ── 极速星芒粒子点缀 (Speed energy spark) ────────────────── */}
      <circle cx="28.5" cy="6.5" r="1.2" fill="#38BDF8" opacity="0.8" />
      <path
        d="M28.5 4.5 L28.5 8.5 M26.5 6.5 L30.5 6.5"
        stroke="#BAE6FD"
        strokeWidth="0.8"
        strokeLinecap="round"
        opacity="0.6"
      />
    </svg>
  );
};
