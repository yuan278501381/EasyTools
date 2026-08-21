import React from 'react';

/**
 * 品牌图标。公开资源使用 1024 原图 `/Logo.png`，小尺寸走 256 的 srcset，
 * 避免把 128px 位图拉到关于页 / 高 DPI 侧边栏后发糊。
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
