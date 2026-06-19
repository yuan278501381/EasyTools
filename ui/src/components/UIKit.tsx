/* ─────────────────────────────────────────────────────────────────────────────
 * 通用 UI 组件 — Card / Toggle / SettingGroup
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC, type ReactNode } from 'react';
import './UIKit.css';

/* ── Card ─────────────────────────────────────────────────────────────────── */

interface CardProps {
  title?: string;
  subtitle?: string;
  children: ReactNode;
  className?: string;
}

export const Card: FC<CardProps> = ({ title, subtitle, children, className = '' }) => (
  <div className={`uikit-card ${className}`}>
    {(title || subtitle) && (
      <div className="uikit-card__header">
        {title && <h3 className="uikit-card__title">{title}</h3>}
        {subtitle && <p className="uikit-card__subtitle">{subtitle}</p>}
      </div>
    )}
    <div className="uikit-card__body">{children}</div>
  </div>
);

/* ── Toggle Switch ────────────────────────────────────────────────────────── */

interface ToggleProps {
  id: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
  label?: string;
  description?: string;
  disabled?: boolean;
}

export const Toggle: FC<ToggleProps> = ({ id, checked, onChange, label, description, disabled = false }) => (
  <div className={`uikit-toggle ${disabled ? 'uikit-toggle--disabled' : ''}`}>
    <div className="uikit-toggle__text">
      {label && <span className="uikit-toggle__label">{label}</span>}
      {description && <span className="uikit-toggle__desc">{description}</span>}
    </div>
    <label className="uikit-toggle__switch" htmlFor={id}>
      <input
        type="checkbox"
        id={id}
        checked={checked}
        onChange={(e) => onChange(e.target.checked)}
        disabled={disabled}
      />
      <span className="uikit-toggle__slider" />
    </label>
  </div>
);

/* ── Setting Row ──────────────────────────────────────────────────────────── */

interface SettingRowProps {
  label: string;
  description?: string;
  children: ReactNode;
}

export const SettingRow: FC<SettingRowProps> = ({ label, description, children }) => (
  <div className="uikit-setting-row">
    <div className="uikit-setting-row__info">
      <span className="uikit-setting-row__label">{label}</span>
      {description && <span className="uikit-setting-row__desc">{description}</span>}
    </div>
    <div className="uikit-setting-row__control">{children}</div>
  </div>
);

/* ── Setting Group ────────────────────────────────────────────────────────── */

interface SettingGroupProps {
  title: string;
  icon?: string;
  children: ReactNode;
}

export const SettingGroup: FC<SettingGroupProps> = ({ title, icon, children }) => (
  <div className="uikit-group">
    <div className="uikit-group__header">
      {icon && <span className="uikit-group__icon">{icon}</span>}
      <h2 className="uikit-group__title">{title}</h2>
    </div>
    <div className="uikit-group__content">{children}</div>
  </div>
);

/* ── Badge ────────────────────────────────────────────────────────────────── */

interface BadgeProps {
  text: string;
  variant?: 'primary' | 'success' | 'warning' | 'danger' | 'muted';
}

export const Badge: FC<BadgeProps> = ({ text, variant = 'primary' }) => (
  <span className={`uikit-badge uikit-badge--${variant}`}>{text}</span>
);

/* ── Select 下拉 ──────────────────────────────────────────────────────────── */

interface SelectProps {
  id: string;
  value: string;
  options: { value: string; label: string }[];
  onChange: (value: string) => void;
}

export const Select: FC<SelectProps> = ({ id, value, options, onChange }) => (
  <select
    id={id}
    className="uikit-select"
    value={value}
    onChange={(e) => onChange(e.target.value)}
  >
    {options.map((opt) => (
      <option key={opt.value} value={opt.value}>
        {opt.label}
      </option>
    ))}
  </select>
);
