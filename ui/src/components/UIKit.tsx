/* ─────────────────────────────────────────────────────────────────────────────
 * 通用 UI 组件 — Card / Toggle / SettingGroup
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, useId, useRef, type FC, type ReactNode, type CSSProperties, type KeyboardEvent as ReactKeyboardEvent } from 'react';
import { createPortal } from 'react-dom';
import { useTranslation } from 'react-i18next';
import { ChevronDown, ChevronUp, Check } from 'lucide-react';
import { ControlA11yContext, useControlA11y } from './ControlA11yContext';
import './UIKit.css';

/* ── Card ─────────────────────────────────────────────────────────────────── */

interface CardProps {
  title?: string;
  subtitle?: string;
  headerAction?: ReactNode;
  children: ReactNode;
  className?: string;
}

export const Card: FC<CardProps> = ({ title, subtitle, headerAction, children, className = '' }) => (
  <div className={`uikit-card ${className}`}>
    {(title || subtitle || headerAction) && (
      <div className="uikit-card__header">
        <div className="uikit-card__titles">
          {title && <h3 className="uikit-card__title">{title}</h3>}
          {subtitle && <p className="uikit-card__subtitle">{subtitle}</p>}
        </div>
        {headerAction && <div className="uikit-card__action">{headerAction}</div>}
      </div>
    )}
    <div className="uikit-card__body">{children}</div>
  </div>
);

/* ── Toggle Switch ────────────────────────────────────────────────────────── */

export interface ToggleProps {
  id: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
  label?: ReactNode;
  description?: ReactNode;
  disabled?: boolean;
  size?: 'sm' | 'md' | 'lg';
  variant?: 'primary' | 'success';
  className?: string;
}

export const Toggle: FC<ToggleProps> = ({
  id,
  checked,
  onChange,
  label,
  description,
  disabled = false,
  size = 'md',
  variant = 'primary',
  className = '',
}) => {
  const hasText = Boolean(label || description);
  return (
    <div
      className={`uikit-toggle uikit-toggle--${size} uikit-toggle--${variant} ${hasText ? 'uikit-toggle--with-text' : 'uikit-toggle--standalone'} ${disabled ? 'uikit-toggle--disabled' : ''} ${className}`.trim()}
    >
      {hasText && (
        <div className="uikit-toggle__text">
          {label && <label id={`${id}-label`} htmlFor={id} className="uikit-toggle__label">{label}</label>}
          {description && <span id={`${id}-description`} className="uikit-toggle__desc">{description}</span>}
        </div>
      )}
      <label className="uikit-toggle__switch" htmlFor={id}>
        <input
          type="checkbox"
          id={id}
          checked={checked}
          aria-labelledby={label ? `${id}-label` : undefined}
          aria-describedby={description ? `${id}-description` : undefined}
          onChange={(e) => onChange(e.target.checked)}
          disabled={disabled}
        />
        <span className="uikit-toggle__slider">
          <span className="uikit-toggle__thumb" />
        </span>
      </label>
    </div>
  );
};

/* ── Setting Row ──────────────────────────────────────────────────────────── */

interface SettingRowProps {
  label: ReactNode;
  description?: string;
  children: ReactNode;
  layout?: 'horizontal' | 'vertical' | 'auto';
  className?: string;
}

export const SettingRow: FC<SettingRowProps> = ({
  label,
  description,
  children,
  layout = 'auto',
  className = ''
}) => {
  const id = useId();
  const labelId = `${id}-label`;
  const descriptionId = description ? `${id}-description` : undefined;
  return (
    <ControlA11yContext.Provider value={{ labelledBy: labelId, describedBy: descriptionId }}>
      <div
        className={`uikit-setting-row uikit-setting-row--${layout} ${className}`}
        role="group"
        aria-labelledby={labelId}
        aria-describedby={descriptionId}
      >
        <div className="uikit-setting-row__info">
          <span id={labelId} className="uikit-setting-row__label">{label}</span>
          {description && <span id={descriptionId} className="uikit-setting-row__desc">{description}</span>}
        </div>
        <div className="uikit-setting-row__control">{children}</div>
      </div>
    </ControlA11yContext.Provider>
  );
};

/* ── Setting Group ────────────────────────────────────────────────────────── */

interface SettingGroupProps {
  title: string;
  icon?: ReactNode;
  children: ReactNode;
}

export const SettingGroup: FC<SettingGroupProps> = ({ title, icon, children }) => {
  const titleId = useId();
  return (
    <section className="uikit-group" aria-labelledby={titleId}>
      <div className="uikit-group__header">
        {icon && <span className="uikit-group__icon" aria-hidden="true">{icon}</span>}
        <h2 id={titleId} className="uikit-group__title">{title}</h2>
      </div>
      <div className="uikit-group__content">{children}</div>
    </section>
  );
};

/* ── Badge ────────────────────────────────────────────────────────────────── */

interface BadgeProps {
  text: string;
  variant?: 'primary' | 'success' | 'warning' | 'danger' | 'muted';
}

export const Badge: FC<BadgeProps> = ({ text, variant = 'primary' }) => (
  <span className={`uikit-badge uikit-badge--${variant}`}>{text}</span>
);

/* ── Select 下拉 ──────────────────────────────────────────────────────────── */

export interface SelectOption {
  value: string;
  label: string;
}

export interface SelectProps {
  id?: string;
  value: string;
  options: SelectOption[];
  onChange: (value: string) => void;
  disabled?: boolean;
  ariaLabel?: string;
  placeholder?: string;
  className?: string;
}

export const Select: FC<SelectProps> = ({
  id,
  value,
  options,
  onChange,
  disabled = false,
  ariaLabel,
  placeholder,
  className = '',
}) => {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const triggerRef = useRef<HTMLButtonElement>(null);
  const listboxRef = useRef<HTMLDivElement>(null);
  const a11y = useControlA11y();

  const selectedOption = options.find((opt) => opt.value === value) || options[0];

  useEffect(() => {
    if (!isOpen) return;
    const handleClickOutside = (event: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    };
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setIsOpen(false);
        triggerRef.current?.focus();
      }
    };
    document.addEventListener('mousedown', handleClickOutside);
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [isOpen]);

  const handleSelect = (val: string) => {
    onChange(val);
    setIsOpen(false);
    triggerRef.current?.focus();
  };

  return (
    <div
      ref={containerRef}
      className={`uikit-select-wrapper ${isOpen ? 'uikit-select-wrapper--open' : ''} ${className}`}
    >
      <button
        ref={triggerRef}
        id={id}
        type="button"
        className="uikit-select-trigger"
        disabled={disabled}
        aria-haspopup="listbox"
        aria-expanded={isOpen}
        aria-label={ariaLabel}
        aria-labelledby={ariaLabel ? undefined : a11y.labelledBy}
        aria-describedby={a11y.describedBy}
        onClick={() => !disabled && setIsOpen((prev) => !prev)}
      >
        <span className="uikit-select-trigger__label">
          {selectedOption ? selectedOption.label : placeholder || ''}
        </span>
        <ChevronDown
          size={14}
          className={`uikit-select-trigger__icon ${isOpen ? 'uikit-select-trigger__icon--rotated' : ''}`}
        />
      </button>

      {isOpen && (
        <div ref={listboxRef} className="uikit-select-dropdown" role="listbox">
          {options.map((opt) => {
            const isSelected = opt.value === value;
            return (
              <div
                key={opt.value}
                role="option"
                aria-selected={isSelected}
                tabIndex={0}
                className={`uikit-select-option ${isSelected ? 'uikit-select-option--selected' : ''}`}
                onClick={() => handleSelect(opt.value)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    handleSelect(opt.value);
                  }
                }}
              >
                <span className="uikit-select-option__label">{opt.label}</span>
                {isSelected && (
                  <Check size={14} strokeWidth={2.4} className="uikit-select-option__check" />
                )}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};

/* ── Button ───────────────────────────────────────────────────────────────── */

interface ButtonProps {
  children: ReactNode;
  onClick?: () => void;
  variant?: 'primary' | 'secondary' | 'ghost' | 'danger';
  size?: 'sm' | 'md';
  disabled?: boolean;
  type?: 'button' | 'submit';
  title?: string;
  className?: string;
  style?: CSSProperties;
}

export const Button: FC<ButtonProps> = ({
  children, onClick, variant = 'primary', size = 'md', disabled = false, type = 'button', title, className = '', style,
}) => (
  <button
    type={type}
    className={`uikit-btn uikit-btn--${variant} uikit-btn--${size} ${className}`}
    onClick={onClick}
    disabled={disabled}
    title={title}
    style={style}
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
  readOnly?: boolean;
  ariaLabel?: string;
}

export const TextInput: FC<TextInputProps> = ({
  id, value, onChange, placeholder, disabled = false, multiline = false, rows = 4,
  readOnly = false, ariaLabel,
}) => {
  const a11y = useControlA11y();
  const accessibility = {
    'aria-label': ariaLabel,
    'aria-labelledby': ariaLabel ? undefined : a11y.labelledBy,
    'aria-describedby': a11y.describedBy,
  };
  return multiline ? (
    <textarea
      id={id}
      className="uikit-input uikit-input--multiline"
      value={value}
      rows={rows}
      placeholder={placeholder}
      disabled={disabled}
      readOnly={readOnly}
      {...accessibility}
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
      readOnly={readOnly}
      {...accessibility}
      onChange={(e) => onChange(e.target.value)}
    />
  );
};

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

export const Field: FC<FieldProps> = ({ label, hint, error, children }) => {
  const id = useId();
  const labelId = `${id}-label`;
  const helpId = error || hint ? `${id}-help` : undefined;
  return (
    <ControlA11yContext.Provider value={{ labelledBy: labelId, describedBy: helpId }}>
      <div className="uikit-field" role="group" aria-labelledby={labelId} aria-describedby={helpId}>
        <span id={labelId} className="uikit-field__label">{label}</span>
        {children}
        {error ? <span id={helpId} className="uikit-field__error" role="alert">{error}</span>
               : hint && <span id={helpId} className="uikit-field__hint">{hint}</span>}
      </div>
    </ControlA11yContext.Provider>
  );
};

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
  return createPortal(
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
    </div>,
    document.body
  );
};

/* ── Tabs / Segmented Control ─────────────────────────────────────────────── */

export interface TabItem<T extends string = string> {
  id: T;
  label: ReactNode;
  icon?: ReactNode;
  badge?: ReactNode;
  disabled?: boolean;
}

interface TabsProps<T extends string = string> {
  tabs: TabItem<T>[];
  activeId: T;
  onChange: (id: T) => void;
  className?: string;
  ariaLabel?: string;
}

export function Tabs<T extends string = string>({ tabs, activeId, onChange, className = '', ariaLabel }: TabsProps<T>) {
  return (
    <div className={`uikit-tabs ${className}`} role="tablist" aria-label={ariaLabel}>
      {tabs.map((tab) => {
        const isActive = activeId === tab.id;
        return (
          <button
            key={tab.id}
            role="tab"
            type="button"
            aria-selected={isActive}
            disabled={tab.disabled}
            className={`uikit-tab ${isActive ? 'uikit-tab--active' : ''}`}
            onClick={() => !tab.disabled && onChange(tab.id)}
          >
            {tab.icon && <span className="uikit-tab__icon">{tab.icon}</span>}
            <span className="uikit-tab__label">{tab.label}</span>
            {tab.badge && <span className="uikit-tab__badge">{tab.badge}</span>}
          </button>
        );
      })}
    </div>
  );
}

/* ── CodeBadge (世界级微晶代码/类名胶囊组件) ─────────────────────────────────── */

export interface CodeBadgeProps {
  children: ReactNode;
  variant?: 'default' | 'primary' | 'muted';
  className?: string;
  style?: CSSProperties;
  title?: string;
}

export const CodeBadge: FC<CodeBadgeProps> = ({
  children,
  variant = 'default',
  className = '',
  style,
  title,
}) => (
  <code
    className={`uikit-code-badge uikit-code-badge--${variant} ${className}`}
    style={style}
    title={title}
  >
    {children}
  </code>
);

/* ── NumberInput (世界级数字微调输入组件) ─────────────────────────────────── */

export interface NumberInputProps {
  id?: string;
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  unit?: string;
  disabled?: boolean;
  className?: string;
  ariaLabel?: string;
}

export const NumberInput: FC<NumberInputProps> = ({
  id,
  value,
  onChange,
  min = -Infinity,
  max = Infinity,
  step = 1,
  unit,
  disabled = false,
  className = '',
  ariaLabel,
}) => {
  const [draft, setDraft] = useState<string>(() => String(value ?? 0));
  const isFocusedRef = useRef(false);

  useEffect(() => {
    if (!isFocusedRef.current) {
      setDraft(String(value ?? 0));
    }
  }, [value]);

  const commit = (text: string) => {
    let num = Number(text.trim());
    if (isNaN(num) || text.trim() === '') {
      num = value ?? 0;
    }
    if (min !== undefined && num < min) num = min;
    if (max !== undefined && num > max) num = max;
    setDraft(String(num));
    if (num !== value) {
      onChange(num);
    }
  };

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const raw = e.target.value;
    setDraft(raw);
    const num = Number(raw);
    if (!isNaN(num) && raw.trim() !== '') {
      if (num >= min && num <= max) {
        onChange(num);
      }
    }
  };

  const handleBlur = () => {
    isFocusedRef.current = false;
    commit(draft);
  };

  const handleFocus = (e: React.FocusEvent<HTMLInputElement>) => {
    isFocusedRef.current = true;
    e.target.select();
  };

  const stepUp = () => {
    if (disabled) return;
    const current = Number(draft) || value || 0;
    const next = Math.min(max, current + step);
    setDraft(String(next));
    onChange(next);
  };

  const stepDown = () => {
    if (disabled) return;
    const current = Number(draft) || value || 0;
    const next = Math.max(min, current - step);
    setDraft(String(next));
    onChange(next);
  };

  const handleKeyDown = (e: ReactKeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      commit(draft);
      (e.target as HTMLInputElement).blur();
    } else if (e.key === 'Escape') {
      setDraft(String(value ?? 0));
      (e.target as HTMLInputElement).blur();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      stepUp();
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      stepDown();
    }
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    if (isFocusedRef.current && !disabled) {
      e.preventDefault();
      if (e.deltaY < 0) {
        stepUp();
      } else {
        stepDown();
      }
    }
  };

  return (
    <div
      className={`uikit-number-input-wrap ${disabled ? 'uikit-number-input-wrap--disabled' : ''} ${className}`.trim()}
      onWheel={handleWheel}
    >
      <input
        id={id}
        type="text"
        inputMode="numeric"
        pattern="[0-9]*"
        className="uikit-number-input"
        value={draft}
        onChange={handleChange}
        onFocus={handleFocus}
        onBlur={handleBlur}
        onKeyDown={handleKeyDown}
        disabled={disabled}
        aria-label={ariaLabel}
      />
      {unit && <span className="uikit-number-unit">{unit}</span>}
      <div className="uikit-number-steppers">
        <button
          type="button"
          tabIndex={-1}
          className="uikit-number-stepper-btn uikit-number-stepper-btn--up"
          onClick={stepUp}
          disabled={disabled || (Number(draft) || value) >= max}
          aria-label="Increase"
        >
          <ChevronUp size={11} strokeWidth={2.8} />
        </button>
        <button
          type="button"
          tabIndex={-1}
          className="uikit-number-stepper-btn uikit-number-stepper-btn--down"
          onClick={stepDown}
          disabled={disabled || (Number(draft) || value) <= min}
          aria-label="Decrease"
        >
          <ChevronDown size={11} strokeWidth={2.8} />
        </button>
      </div>
    </div>
  );
};


