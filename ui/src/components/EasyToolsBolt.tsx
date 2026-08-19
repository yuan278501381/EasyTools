import React from 'react';

interface EasyToolsBoltProps {
  size?: number;
  className?: string;
  fill?: string;
}

/**
 * 官方标准平顶极速破空闪电 (The Aero Bolt)
 */
export const EasyToolsBolt: React.FC<EasyToolsBoltProps> = ({
  size = 24,
  className = '',
  fill = 'var(--primary)',
}) => {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 100 100"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      className={className}
      style={{ display: 'inline-block', verticalAlign: 'middle', flexShrink: 0 }}
      aria-hidden="true"
    >
      <polygon
        points="44,4 72,4 54,44 82,44 33,100 48,62 20,62"
        fill={fill}
      />
    </svg>
  );
};
