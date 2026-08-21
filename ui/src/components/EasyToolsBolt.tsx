import React from 'react';

/** Reuses the packaged asset instead of embedding a 398KB base64 image in every WebView bundle. */
export const EasyToolsBolt: React.FC<{
  size?: number;
  className?: string;
  fill?: string;
} & React.ImgHTMLAttributes<HTMLImageElement>> = ({
  size = 24,
  className = '',
  fill,
  alt = '',
  ...props
}) => (
  <img
    src="/logo_active.png"
    width={size}
    height={size}
    className={className}
    alt={alt}
    draggable={false}
    data-fill={fill || undefined}
    style={{ display: 'inline-block', flexShrink: 0 }}
    {...props}
  />
);
