/* ─────────────────────────────────────────────────────────────────────────────
 * 通用 UI 组件 — Card / Toggle / SettingGroup
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC, type ReactNode, type KeyboardEvent as ReactKeyboardEvent, useEffect, useCallback, useId, useRef } from 'react';
import { useTranslation } from 'react-i18next';
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
  icon?: ReactNode;
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
  id?: string;
  value: string;
  options: { value: string; label: string }[];
  onChange: (value: string) => void;
  disabled?: boolean;
}

export const Select: FC<SelectProps> = ({ id, value, options, onChange, disabled = false }) => (
  <select
    id={id}
    className="uikit-select"
    value={value}
    disabled={disabled}
    onChange={(e) => onChange(e.target.value)}
  >
    {options.map((opt) => (
      <option key={opt.value} value={opt.value}>
        {opt.label}
      </option>
    ))}
  </select>
);

/* ── Button ───────────────────────────────────────────────────────────────── */

interface ButtonProps {
  children: ReactNode;
  onClick?: () => void;
  variant?: 'primary' | 'ghost' | 'danger';
  size?: 'sm' | 'md';
  disabled?: boolean;
  type?: 'button' | 'submit';
  title?: string;
}

export const Button: FC<ButtonProps> = ({
  children, onClick, variant = 'primary', size = 'md', disabled = false, type = 'button', title,
}) => (
  <button
    type={type}
    className={`uikit-btn uikit-btn--${variant} uikit-btn--${size}`}
    onClick={onClick}
    disabled={disabled}
    title={title}
  >
    {children}
  </button>
);

/* ── TextInput ────────────────────────────────────────────────────────────── */

interface TextInputProps {
  id?: string;
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
  disabled?: boolean;
  multiline?: boolean;
  rows?: number;
}

export const TextInput: FC<TextInputProps> = ({
  id, value, onChange, placeholder, disabled = false, multiline = false, rows = 4,
}) =>
  multiline ? (
    <textarea
      id={id}
      className="uikit-input uikit-input--multiline"
      value={value}
      rows={rows}
      placeholder={placeholder}
      disabled={disabled}
      onChange={(e) => onChange(e.target.value)}
    />
  ) : (
    <input
      id={id}
      type="text"
      className="uikit-input"
      value={value}
      placeholder={placeholder}
      disabled={disabled}
      onChange={(e) => onChange(e.target.value)}
    />
  );

/* ── HotkeyInput — 按下组合键自动捕获 (输出如 "Ctrl+Shift+T") ──────────────── */

const KEY_NAME_MAP: Record<string, string> = {
  ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down',
  ' ': 'Space', Escape: 'Escape', Enter: 'Enter', Tab: 'Tab',
  Delete: 'Delete', Backspace: 'Backspace', Home: 'Home', End: 'End',
  PageUp: 'PageUp', PageDown: 'PageDown', Insert: 'Insert',
};

interface HotkeyInputProps {
  id?: string;
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
}

export const HotkeyInput: FC<HotkeyInputProps> = ({ id, value, onChange, placeholder }) => {
  const { t } = useTranslation();
  const onKeyDown = useCallback((e: ReactKeyboardEvent<HTMLInputElement>) => {
    e.preventDefault();
    const k = e.key;
    if (k === 'Control' || k === 'Alt' || k === 'Shift' || k === 'Meta') return;

    const mods: string[] = [];
    if (e.ctrlKey) mods.push('Ctrl');
    if (e.altKey) mods.push('Alt');
    if (e.shiftKey) mods.push('Shift');
    if (e.metaKey) mods.push('Win');

    const main = KEY_NAME_MAP[k] ?? (k.length === 1 ? k.toUpperCase() : k);
    onChange([...mods, main].join('+'));
  }, [onChange]);

  return (
    <input
      id={id}
      type="text"
      className="uikit-input uikit-input--hotkey"
      value={value}
      placeholder={placeholder ?? t('hotkey.pressKeys')}
      readOnly
      onKeyDown={onKeyDown}
    />
  );
};

/* ── Field — Modal 表单字段 (标签 + 控件 + 提示/错误) ─────────────────────── */

interface FieldProps {
  label: string;
  hint?: string;
  error?: string;
  children: ReactNode;
}

export const Field: FC<FieldProps> = ({ label, hint, error, children }) => (
  <div className="uikit-field">
    <label className="uikit-field__label">{label}</label>
    {children}
    {error ? <span className="uikit-field__error">{error}</span>
           : hint && <span className="uikit-field__hint">{hint}</span>}
  </div>
);

/* ── Modal ────────────────────────────────────────────────────────────────── */

interface ModalProps {
  open: boolean;
  title: string;
  onClose: () => void;
  children: ReactNode;
  footer?: ReactNode;
}

export const Modal: FC<ModalProps> = ({ open, title, onClose, children, footer }) => {
  const { t } = useTranslation();
  const titleId = useId();
  const modalRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!open) return;
    const previousFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    const focusableSelector = 'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
    const frame = requestAnimationFrame(() => {
      modalRef.current?.querySelector<HTMLElement>(focusableSelector)?.focus();
    });
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        onClose();
      } else if (e.key === 'Tab' && modalRef.current) {
        const focusable = Array.from(modalRef.current.querySelectorAll<HTMLElement>(focusableSelector));
        if (focusable.length === 0) return;
        const first = focusable[0];
        const last = focusable[focusable.length - 1];
        if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
        else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      cancelAnimationFrame(frame);
      window.removeEventListener('keydown', onKeyDown);
      previousFocus?.focus();
    };
  }, [open, onClose]);

  if (!open) return null;
  return (
    <div className="uikit-modal__overlay" onClick={onClose}>
      <div
        ref={modalRef}
        className="uikit-modal"
        role="dialog"
        aria-modal="true"
        aria-labelledby={titleId}
        onClick={(e) => e.stopPropagation()}
      >
        <div className="uikit-modal__header">
          <h3 id={titleId} className="uikit-modal__title">{title}</h3>
          <button className="uikit-modal__close" onClick={onClose} aria-label={t('common.close')}>×</button>
        </div>
        <div className="uikit-modal__body">{children}</div>
        {footer && <div className="uikit-modal__footer">{footer}</div>}
      </div>
    </div>
  );
};
