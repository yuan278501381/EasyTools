import React from 'react';

/**
 * 品牌应用图标。源是 `Logo_Origin.png`（1254 透明立体 E）合成的圆角方标。
 * 小尺寸走 256 预缩放，避免浏览器直接把 1024 压成 32px 发糊。
 */
export const EasyToolsBolt: React.FC<{
  size?: number;
  className?: string;
  fill?: string;
} & React.ImgHTMLAttributes<HTMLImageElement>> = ({
  size = 24,
  className = '',
  fill,
  alt = 'EasyTools',
  ...props
}) => (
  <img
    src="/Logo.png"
    srcSet="/logo_active.png 256w, /Logo.png 1024w"
    sizes={`${size}px`}
    width={size}
    height={size}
    className={className}
    alt={alt}
    draggable={false}
    decoding="async"
    data-fill={fill || undefined}
    style={{
      display: 'inline-block',
      flexShrink: 0,
      width: size,
      height: size,
      objectFit: 'contain',
      imageRendering: 'auto',
    }}
    {...props}
  />
);
