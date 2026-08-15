/* ─────────────────────────────────────────────────────────────────────────────
 * HotkeyRecorder — 交互式快捷键录入器
 *
 * 功能:
 *   - 点击进入录入模式（边框高亮 + 提示文字 "按下快捷键..."）
 *   - 实时监听键盘事件，显示当前按下的键组合
 *   - 支持 Ctrl / Alt / Shift / Win + 任意键
 *   - 每个修饰键用独立的圆角胶囊渲染（macOS 偏好风格）
 *   - Esc 取消录入，Backspace 清除当前绑定
 *   - 录入完成（松开所有键）后自动确认
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useCallback, useRef, useEffect, type FC, type KeyboardEvent as ReactKeyboardEvent } from 'react';
import { useTranslation } from 'react-i18next';
import { useControlA11y } from './ControlA11yContext';
import './HotkeyRecorder.css';

/** 特殊键名标准化映射 */
const KEY_NAME_MAP: Record<string, string> = {
  ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down',
  ' ': 'Space', Enter: 'Enter', Tab: 'Tab',
  Delete: 'Delete', Home: 'Home', End: 'End',
  PageUp: 'PageUp', PageDown: 'PageDown', Insert: 'Insert',
};

/** 修饰键集合 */
const MODIFIER_KEYS = new Set(['Control', 'Alt', 'Shift', 'Meta']);

interface HotkeyRecorderProps {
  /** 当前快捷键值, 格式如 "Ctrl+Shift+A" */
  value: string;
  /** 快捷键变更回调 */
  onChange: (value: string) => void;
  /** 未绑定时的占位文字 */
  placeholder?: string;
  /** 唯一标识 */
  id: string;
  ariaLabel?: string;
}

export const HotkeyRecorder: FC<HotkeyRecorderProps> = ({ value, onChange, placeholder, id, ariaLabel }) => {
  const { t } = useTranslation();
  const a11y = useControlA11y();
  const [recording, setRecording] = useState(false);
  const [activeKeys, setActiveKeys] = useState<string[]>([]);
  const containerRef = useRef<HTMLDivElement>(null);
  const pendingRef = useRef<string | null>(null);

  // ── 进入录入模式 ──────────────────────────────────────────────────────────
  const startRecording = useCallback(() => {
    setRecording(true);
    setActiveKeys([]);
    pendingRef.current = null;
  }, []);

  // ── 退出录入模式 (不保存) ─────────────────────────────────────────────────
  const cancelRecording = useCallback(() => {
    setRecording(false);
    setActiveKeys([]);
    pendingRef.current = null;
  }, []);

  // ── 确认录入结果 ──────────────────────────────────────────────────────────
  const confirmRecording = useCallback((hotkeyStr: string) => {
    setRecording(false);
    setActiveKeys([]);
    if (hotkeyStr && hotkeyStr !== value) {
      onChange(hotkeyStr);
    }
    pendingRef.current = null;
  }, [onChange, value]);

  // ── 键盘事件监听 ──────────────────────────────────────────────────────────
  useEffect(() => {
    if (!recording) return;

    const handleKeyDown = (e: KeyboardEvent) => {
      e.preventDefault();
      e.stopPropagation();

      const key = e.key;

      // Esc 取消
      if (key === 'Escape') {
        cancelRecording();
        return;
      }

      // Backspace 清除绑定
      if (key === 'Backspace' && !e.ctrlKey && !e.altKey && !e.shiftKey && !e.metaKey) {
        onChange('');
        setRecording(false);
        setActiveKeys([]);
        pendingRef.current = null;
        return;
      }

      // 收集当前修饰键
      const mods: string[] = [];
      if (e.ctrlKey) mods.push('Ctrl');
      if (e.altKey) mods.push('Alt');
      if (e.shiftKey) mods.push('Shift');
      if (e.metaKey) mods.push('Win');

      if (MODIFIER_KEYS.has(key)) {
        // 仅按下了修饰键，实时展示
        setActiveKeys([...mods]);
        pendingRef.current = null;
      } else {
        // 修饰键 + 主键 → 完成组合
        const mainKey = KEY_NAME_MAP[key] ?? (key.length === 1 ? key.toUpperCase() : key);
        const combo = [...mods, mainKey];
        setActiveKeys(combo);
        pendingRef.current = combo.join('+');
      }
    };

    const handleKeyUp = () => {
      // 当有挂起的组合键时，在松开任意键后确认
      if (pendingRef.current) {
        confirmRecording(pendingRef.current);
      }
    };

    window.addEventListener('keydown', handleKeyDown, true);
    window.addEventListener('keyup', handleKeyUp, true);

    return () => {
      window.removeEventListener('keydown', handleKeyDown, true);
      window.removeEventListener('keyup', handleKeyUp, true);
    };
  }, [recording, cancelRecording, confirmRecording, onChange]);

  // ── 点击外部退出录入 ──────────────────────────────────────────────────────
  useEffect(() => {
    if (!recording) return;

    const handleClickOutside = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        cancelRecording();
      }
    };

    // 延迟绑定避免触发录入的那次 click 也被捕获
    const timer = setTimeout(() => {
      document.addEventListener('mousedown', handleClickOutside);
    }, 0);

    return () => {
      clearTimeout(timer);
      document.removeEventListener('mousedown', handleClickOutside);
    };
  }, [recording, cancelRecording]);

  // ── 解析已绑定的快捷键字符串为 token 列表 ─────────────────────────────────
  const parsedKeys = value ? value.split('+') : [];
  const modifierSet = new Set(['Ctrl', 'Alt', 'Shift', 'Win']);

  // ── 渲染按键胶囊 ──────────────────────────────────────────────────────────
  const renderKeys = (keys: string[]) => {
    if (keys.length === 0) return null;

    return (
      <span className="hotkey-recorder__keys">
        {keys.map((key, idx) => (
          <span key={`${key}-${idx}`}>
            {idx > 0 && <span className="hotkey-recorder__separator">+</span>}
            <span
              className={`hotkey-recorder__key ${modifierSet.has(key) ? 'hotkey-recorder__key--modifier' : ''}`}
            >
              {key}
            </span>
          </span>
        ))}
      </span>
    );
  };

  const defaultPlaceholder = placeholder ?? t('capture.pressToBind');
  const recordingPlaceholder = t('hotkey.pressKeys');

  return (
    <div ref={containerRef} className="hotkey-recorder-wrap">
      <button
        id={id}
        type="button"
        className={`hotkey-recorder ${recording ? 'hotkey-recorder--recording' : ''}`}
        onClick={() => !recording && startRecording()}
        onKeyDown={(e: ReactKeyboardEvent) => {
          if (!recording && (e.key === 'Enter' || e.key === ' ')) {
            e.preventDefault();
            startRecording();
            return;
          }
          if (recording && e.key === 'Tab') e.preventDefault();
        }}
        aria-pressed={recording}
        aria-labelledby={!ariaLabel && a11y.labelledBy ? `${a11y.labelledBy} ${id}-value` : undefined}
        aria-describedby={a11y.describedBy}
        aria-label={ariaLabel
          ? `${ariaLabel}: ${recording ? recordingPlaceholder : (value || defaultPlaceholder)}`
          : a11y.labelledBy ? undefined : (value || defaultPlaceholder)}
      >
        <span id={`${id}-value`} className="sr-only">
          {recording ? recordingPlaceholder : (value || defaultPlaceholder)}
        </span>
        <span aria-hidden="true">
          {recording ? (
            activeKeys.length > 0 ? renderKeys(activeKeys) : (
              <span className="hotkey-recorder__placeholder">{recordingPlaceholder}</span>
            )
          ) : (
            parsedKeys.length > 0 ? renderKeys(parsedKeys) : (
              <span className="hotkey-recorder__placeholder">{defaultPlaceholder}</span>
            )
          )}
        </span>
      </button>
      {value && !recording && (
        <button
          type="button"
          className="hotkey-recorder__clear"
          onClick={() => onChange('')}
          aria-label={t('hotkey.clearBinding')}
        >
          ×
        </button>
      )}
    </div>
  );
};
