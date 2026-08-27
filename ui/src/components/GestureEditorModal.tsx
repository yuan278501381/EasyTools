/* ─────────────────────────────────────────────────────────────────────────────
 * GestureEditorModal — 手势动作编辑器弹窗 (World-Class Interactive Gesture Editor)
 *
 * 核心交互架构:
 *   - 零认知负荷: 无冗长说明文字，界面高度紧凑，一屏尽览
 *   - 一体化手势控制舱: 触发按键 (右键/中键) 与键盘修饰键 (Ctrl/Shift/Alt) 实时联动
 *   - 紧凑网格动作表单: 动作名称 + 类型与参数 2 列并排，最大化垂直空间利用率
 *   - 替换相似手势设置: 智能检测冲突并提供一键覆盖
 *   - 录制免打扰保护: 挂载时通知后端进入 recordingMode，拦截所有手势动作触发
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useRef, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Sliders,
  ChevronDown,
  ChevronUp,
  Zap,
  VolumeX,
  Lightbulb,
} from 'lucide-react';
import { Modal, Field, TextInput, Select, Toggle, Button } from './UIKit';
import { HotkeyRecorder } from './HotkeyRecorder';
import { GestureDrawCanvas } from './GestureDrawCanvas';
import { GestureStrokePreview } from './GestureStrokePreview';
import {
  type GestureMapping,
  ACTION_TYPE_KEYS,
  BUILTIN_COMMAND_KEYS,
  GESTURE_CODE_PATTERN,
  codeToArrows,
  parseGestureCode,
  assembleGestureCode,
} from './gestureModel';
import { getLocalizedGestureName } from '../utils/gestureI18n';
import { bridgeRequest } from '../hooks/useBridge';
import './GestureEditorModal.css';

interface Props {
  mapping?: GestureMapping | null;
  initial?: GestureMapping | null;
  existingCodes?: string[];
  existingMappings?: GestureMapping[];
  initialFocusTarget?: 'instant' | 'silent' | 'action_type' | 'action_detail' | 'hotkey' | 'lua' | 'builtin' | 'program' | null;
  onSave: (mapping: GestureMapping) => void;
  onClose: () => void;
}

const DEFAULT_MAPPING: GestureMapping = {
  gestureCode: '',
  enabled: true,
  instantExecute: false,
  silentToast: false,
  action: {
    type: 0,
    name: '',
  },
};

export const GestureEditorModal: FC<Props> = ({
  mapping,
  initial,
  existingCodes,
  existingMappings,
  initialFocusTarget,
  onSave,
  onClose,
}) => {
  const { t } = useTranslation();
  const initialMap = mapping ?? initial ?? null;

  const [draft, setDraft] = useState<GestureMapping>(() => ({
    ...(initialMap ?? DEFAULT_MAPPING),
    action: { ...(initialMap?.action ?? DEFAULT_MAPPING.action) },
  }));

  const initialCode = (initialMap?.gestureCode ?? '').trim().toUpperCase();
  const initialId = initialMap?.id;
  const isEdit = Boolean(initialMap);

  const [advancedOpen, setAdvancedOpen] = useState(Boolean(initialFocusTarget));
  const [replaceSimilarTracks, setReplaceSimilarTracks] = useState(true);

  const actionTypeRef = useRef<HTMLDivElement>(null);
  const actionDetailRef = useRef<HTMLDivElement>(null);
  const advancedRef = useRef<HTMLDivElement>(null);
  const instantCardRef = useRef<HTMLDivElement>(null);
  const silentCardRef = useRef<HTMLDivElement>(null);

  const code = draft.gestureCode.trim().toUpperCase();

  // 弹窗挂载时开启录制免打扰保护模式，关闭时自动恢复全局拦截
  useEffect(() => {
    void bridgeRequest('gesture.setRecordingMode', { recording: true });
    return () => {
      void bridgeRequest('gesture.setRecordingMode', { recording: false });
    };
  }, []);

  const handleToggleAdvanced = () => {
    const next = !advancedOpen;
    setAdvancedOpen(next);
    if (next) {
      requestAnimationFrame(() => {
        setTimeout(() => {
          advancedRef.current?.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }, 40);
      });
    }
  };

  // 校验与智能冲突识别
  let codeError = '';
  if (!code) {
    codeError = t('gestureEditor.gestureCodeRequired', 'Please draw or select a gesture on the canvas');
  } else if (!GESTURE_CODE_PATTERN.test(code)) {
    codeError = t('gestureEditor.gestureCodeInvalid', 'Invalid gesture track, please redraw on canvas');
  }

  // 检测是否与其它已有手势存在编码冲突
  const conflictingMapping = (existingMappings ?? []).find(
    (m) =>
      m.gestureCode.trim().toUpperCase() === code &&
      (m.id && initialId ? m.id !== initialId : m.gestureCode.trim().toUpperCase() !== initialCode)
  );

  const isDuplicateOfOther = Boolean(
    conflictingMapping ||
      ((existingCodes ?? []).map((c) => c.trim().toUpperCase()).includes(code) && code !== initialCode)
  );

  const conflictWarning =
    isDuplicateOfOther && !codeError
      ? conflictingMapping?.action?.name
        ? t('gestureEditor.gestureConflictOverwriteNamed', {
            name: getLocalizedGestureName(conflictingMapping.action.name, t),
            code: `「${codeToArrows(code)}」`,
            defaultValue: t('gesture.conflictReplaceWithAction', 'Gesture 「{{arrows}}」 is already bound to 「{{action}}」. Saving will replace it.', { arrows: codeToArrows(code), action: getLocalizedGestureName(conflictingMapping.action.name, t) }),
          })
        : t('gestureEditor.gestureConflictOverwrite', {
            code: `「${codeToArrows(code)}」`,
            defaultValue: t('gesture.conflictReplaceSimple', 'Gesture 「{{arrows}}」 already exists. Saving will replace it.', { arrows: codeToArrows(code) }),
          })
      : '';

  const allOtherCodes = (existingMappings ? existingMappings.map((m) => m.gestureCode) : (existingCodes ?? []))
    .map((c) => c.trim().toUpperCase())
    .filter((c) => c !== initialCode && c !== code);

  const prefixConflicts =
    code && !codeError && !conflictWarning
      ? allOtherCodes.filter((c) => (c.startsWith(code) || code.startsWith(c)) && c !== code)
      : [];

  const nameError = draft.action.name.trim() ? '' : t('gestureEditor.actionNameRequired', 'Action name is required');
  const canSave = !codeError && !nameError;

  const setAction = (patch: Partial<GestureMapping['action']>) =>
    setDraft((d) => ({ ...d, action: { ...d.action, ...patch } }));

  const handleSave = () => {
    if (!canSave) return;
    const tVal = draft.action.type;
    const action: GestureMapping['action'] = {
      type: tVal,
      name: draft.action.name.trim(),
      description: draft.action.description,
    };
    if (tVal === 0) action.keyStroke = draft.action.keyStroke ?? '';
    else if (tVal === 1) action.luaScript = draft.action.luaScript ?? '';
    else if (tVal === 2) action.builtinCmd = draft.action.builtinCmd ?? 0;
    else if (tVal === 3) {
      action.programPath = draft.action.programPath ?? '';
      action.programArgs = draft.action.programArgs ?? '';
    }
    onSave({
      id: draft.id || generateMappingId(code),
      enabled: draft.enabled ?? true,
      instantExecute: draft.instantExecute ?? false,
      silentToast: draft.silentToast ?? false,
      gestureCode: code,
      action,
    });
  };

  const handleGestureCodeChange = (newCode: string) => {
    const clean = newCode.trim().toUpperCase();
    setDraft((prev) => {
      const isCleanEmpty = !prev.action.name.trim();
      const presets: Record<
        string,
        { name: string; type: number; keyStroke?: string; builtinCmd?: number; desc?: string }
      > = {
        L: { name: 'Back', type: 0, keyStroke: 'Alt+Left', desc: 'Browser / Explorer back' },
        R: { name: 'Forward', type: 0, keyStroke: 'Alt+Right', desc: 'Browser / Explorer forward' },
        'MIDDLE+L': { name: 'Previous Track', type: 0, keyStroke: 'MediaPrev', desc: 'Media previous track' },
        'MIDDLE+R': { name: 'Next Track', type: 0, keyStroke: 'MediaNext', desc: 'Media next track' },
        'X1+L': { name: 'Back', type: 0, keyStroke: 'Alt+Left', desc: 'Browser / Explorer back' },
        'X1+R': { name: 'Forward', type: 0, keyStroke: 'Alt+Right', desc: 'Browser / Explorer forward' },
        'X1+U': { name: 'Next Tab', type: 0, keyStroke: 'Ctrl+Tab', desc: 'Switch to next tab' },
        'X1+D': { name: 'Previous Tab', type: 0, keyStroke: 'Ctrl+Shift+Tab', desc: 'Switch to previous tab' },
        'X1+D-R': { name: 'Close Tab', type: 0, keyStroke: 'Ctrl+W', desc: 'Close current tab' },
        'X2+L': { name: 'Previous Track', type: 0, keyStroke: 'MediaPrev', desc: 'Media previous track' },
        'X2+R': { name: 'Next Track', type: 0, keyStroke: 'MediaNext', desc: 'Media next track' },
        'X2+U': { name: 'Volume Up', type: 2, builtinCmd: 22, desc: 'Increase master volume' },
        'X2+D': { name: 'Volume Down', type: 2, builtinCmd: 23, desc: 'Decrease master volume' },
        'TOPEDGE+D': { name: 'Task View', type: 2, builtinCmd: 7, desc: 'Open Windows task view' },
        'TOPEDGE+L': { name: 'Switch Left Desktop', type: 2, builtinCmd: 25, desc: 'Switch to left virtual desktop' },
        'TOPEDGE+R': { name: 'Switch Right Desktop', type: 2, builtinCmd: 26, desc: 'Switch to right virtual desktop' },
        'TOPEDGE+LEFT+D': { name: 'Close Window', type: 0, keyStroke: 'Alt+F4', desc: 'Close active window' },
        U: { name: 'Maximize/Restore', type: 2, builtinCmd: 2, desc: 'Maximize or restore window' },
        D: { name: 'Minimize', type: 2, builtinCmd: 3, desc: 'Minimize active window' },
        'D-R': { name: 'Close Tab', type: 0, keyStroke: 'Ctrl+W', desc: 'Close current tab' },
        'R-D': { name: 'Reopen Closed Tab', type: 0, keyStroke: 'Ctrl+Shift+T', desc: 'Reopen last closed tab' },
        'D-L': { name: 'Close Window', type: 0, keyStroke: 'Alt+F4', desc: 'Exit active application window' },
        'L-U-R': { name: 'Reload Page', type: 0, keyStroke: 'F5', desc: 'Reload current web page' },
        'D-U': { name: 'Reload Page', type: 0, keyStroke: 'F5', desc: 'Reload current web page' },
        'L-D': { name: 'Show Desktop', type: 2, builtinCmd: 5, desc: 'Minimize all and show desktop' },
        'D-R-D': { name: 'Quick Screenshot', type: 2, builtinCmd: 10, desc: 'Launch EasyTools screenshot' },
      };

      const matched = presets[clean];
      if (matched && isCleanEmpty) {
        return {
          ...prev,
          gestureCode: newCode,
          action: {
            ...prev.action,
            name: matched.name,
            type: matched.type,
            keyStroke: matched.keyStroke,
            builtinCmd: matched.builtinCmd,
            description: matched.desc,
          },
        };
      }
      return { ...prev, gestureCode: newCode };
    });
  };

  const handleTriggerChange = (targetTrigger: import('./gestureModel').TriggerButton) => {
    const parsed = parseGestureCode(draft.gestureCode);
    const newCode = assembleGestureCode({
      edge: parsed.edge,
      triggerButton: targetTrigger,
      hasCtrl: parsed.hasCtrl,
      hasShift: parsed.hasShift,
      hasAlt: parsed.hasAlt,
      bareCode: parsed.bareCode,
    });
    handleGestureCodeChange(newCode);
  };

  const handleEdgeChange = (targetEdge: import('./gestureModel').ScreenEdge) => {
    const parsed = parseGestureCode(draft.gestureCode);
    const newCode = assembleGestureCode({
      edge: targetEdge,
      triggerButton: parsed.triggerButton,
      hasCtrl: parsed.hasCtrl,
      hasShift: parsed.hasShift,
      hasAlt: parsed.hasAlt,
      bareCode: parsed.bareCode,
    });
    handleGestureCodeChange(newCode);
  };

  const activeAdvancedCount = (draft.instantExecute ? 1 : 0) + (draft.silentToast ? 1 : 0);
  const parsed = parseGestureCode(draft.gestureCode);

  return (
    <Modal
      open
      title={isEdit ? t('gestureEditor.titleEdit') : t('gestureEditor.titleAdd')}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            {t('common.cancel')}
          </Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>
            {t('common.save')}
          </Button>
        </>
      }
    >
      <Field
        label={t('gestureEditor.gestureDraw')}
        error={codeError}
        hint={
          prefixConflicts.length
            ? t('gestureEditor.prefixConflict', {
                codes: prefixConflicts.map((c) => `「${codeToArrows(c)}」`).join(', '),
              })
            : undefined
        }
      >
        <GestureDrawCanvas
          value={draft.gestureCode}
          onChange={handleGestureCodeChange}
          triggerButton={parsed.triggerButton}
          onTriggerButtonChange={handleTriggerChange}
          screenEdge={parsed.edge}
          onScreenEdgeChange={handleEdgeChange}
        />
        {code && (
          <div style={{ marginTop: '10px', display: 'flex', gap: '12px', alignItems: 'center' }}>
            <span style={{ fontSize: '0.82rem', color: 'var(--text-muted)' }}>
              {t('gestureEditor.recognizedGesture')}:
            </span>
            <GestureStrokePreview code={code} width={64} height={38} autoAnimate />
            <span style={{ fontSize: '0.88rem', fontWeight: 600, color: 'var(--primary)' }}>
              {codeToArrows(code)}
            </span>
          </div>
        )}
        {conflictWarning && (
          <div className="gesture-conflict-warning">
            <Lightbulb size={14} className="gesture-conflict-icon" />
            <span>{conflictWarning}</span>
          </div>
        )}
      </Field>

      <Field label={t('gestureEditor.actionName')} error={nameError}>
        <TextInput
          value={draft.action.name}
          onChange={(v: string) => setAction({ name: v })}
          placeholder={t('gestureEditor.actionNamePlaceholder')}
        />
      </Field>

      {/* 动作类型与参数配置 (紧凑 2 列并排网格) */}
      <div className="gesture-action-config-grid">
        <div ref={actionTypeRef}>
          <Field label={t('gestureEditor.actionType')}>
            <Select
              value={String(draft.action.type)}
              options={ACTION_TYPE_KEYS.map((key, index) => ({ value: String(index), label: t(key) }))}
              onChange={(v: string) => setAction({ type: Number(v) })}
            />
          </Field>
        </div>

        <div ref={actionDetailRef}>
          {draft.action.type === 0 && (
            <Field label={t('gestureEditor.hotkey')} hint={t('gestureEditor.hotkeyHint')}>
              <HotkeyRecorder
                id="gesture-hotkey-recorder"
                value={draft.action.keyStroke ?? ''}
                onChange={(v: string) => setAction({ keyStroke: v })}
              />
            </Field>
          )}

          {draft.action.type === 1 && (
            <Field label={t('gestureEditor.luaScript')} hint={t('gestureEditor.luaScriptHint')}>
              <TextInput
                multiline
                value={draft.action.luaScript ?? ''}
                onChange={(v: string) => setAction({ luaScript: v })}
                placeholder={'easy.shell.open("https://example.com")'}
              />
            </Field>
          )}

          {draft.action.type === 2 && (
            <Field label={t('gestureEditor.builtinCommand')}>
              <Select
                value={String(draft.action.builtinCmd ?? 0)}
                options={BUILTIN_COMMAND_KEYS.map((key, i) => ({ value: String(i), label: t(key) }))}
                onChange={(v: string) => setAction({ builtinCmd: Number(v) })}
              />
            </Field>
          )}

          {draft.action.type === 3 && (
            <>
              <Field label={t('gestureEditor.programPath')}>
                <TextInput
                  value={draft.action.programPath ?? ''}
                  onChange={(v: string) => setAction({ programPath: v })}
                  placeholder="C:\\Windows\\System32\\notepad.exe"
                />
              </Field>
              <Field label={t('gestureEditor.programArgs')} hint={t('gestureEditor.programArgsHint')}>
                <TextInput
                  value={draft.action.programArgs ?? ''}
                  onChange={(v: string) => setAction({ programArgs: v })}
                />
              </Field>
            </>
          )}
        </div>
      </div>

      {/* 替换所有相似手势轨迹配置卡片 */}
      <div className="gesture-replace-tracks-card">
        <label className="gesture-replace-tracks-label">
          <input
            type="checkbox"
            className="gesture-replace-tracks-checkbox"
            checked={replaceSimilarTracks}
            onChange={(e: React.ChangeEvent<HTMLInputElement>) => setReplaceSimilarTracks(e.target.checked)}
          />
          <span className="gesture-replace-tracks-title">
            {t('gestureEditor.replaceSimilarTracks', 'Replace all similar gesture tracks')}
          </span>
          <span className="gesture-replace-tracks-subtitle">
            ({t('gestureEditor.replaceSimilarHintAuto', 'Automatically overwrites duplicate tracks to maintain unique mappings')})
          </span>
        </label>
      </div>

      {/* 高级执行特性折叠面板 */}
      <div ref={advancedRef} className="gesture-editor-advanced-wrapper">
        <button
          type="button"
          className="gesture-editor-advanced-toggle"
          onClick={handleToggleAdvanced}
        >
          <div className="gesture-editor-advanced-toggle__left">
            <Sliders size={16} className="gesture-editor-advanced-toggle__icon" />
            <span>{t('gestureEditor.advancedTitle')}</span>
          </div>
          <div className="gesture-editor-advanced-toggle__right">
            {activeAdvancedCount > 0 && (
              <span className="gesture-editor-advanced-badge">
                {t('gestureEditor.advancedActiveCount', { count: activeAdvancedCount })}
              </span>
            )}
            {advancedOpen ? <ChevronUp size={16} /> : <ChevronDown size={16} />}
          </div>
        </button>

        {advancedOpen && (
          <div className="gesture-editor-advanced-content">
            {/* 即时执行 */}
            <div
              ref={instantCardRef}
              className={`gesture-scenario-card ${draft.instantExecute ? 'gesture-scenario-card--active' : ''} ${initialFocusTarget === 'instant' ? 'gesture-scenario-card--focused' : ''}`}
            >
              <div
                className="gesture-scenario-card__header"
                onClick={() => setDraft((d) => ({ ...d, instantExecute: !d.instantExecute }))}
              >
                <div className="gesture-scenario-card__main">
                  <div className="gesture-scenario-card__icon-box gesture-scenario-card__icon-box--instant">
                    <Zap size={16} />
                  </div>
                  <div className="gesture-scenario-card__text-group">
                    <span className="gesture-scenario-card__title">{t('gestureEditor.instantExecuteTitle')}</span>
                    <span className="gesture-scenario-card__desc">{t('gestureEditor.instantExecuteDesc')}</span>
                  </div>
                </div>
                <div onClick={(e) => e.stopPropagation()}>
                  <Toggle
                    id="gesture-instant-execute-toggle"
                    checked={draft.instantExecute ?? false}
                    onChange={(checked: boolean) => setDraft((d) => ({ ...d, instantExecute: checked }))}
                  />
                </div>
              </div>
              <div className="gesture-scenario-card__callout">
                <Lightbulb size={13} className="gesture-scenario-card__callout-icon" />
                <span>{t('gestureEditor.instantExecuteScenario')}</span>
              </div>
            </div>

            {/* 静默模式 */}
            <div
              ref={silentCardRef}
              className={`gesture-scenario-card ${draft.silentToast ? 'gesture-scenario-card--active' : ''} ${initialFocusTarget === 'silent' ? 'gesture-scenario-card--focused' : ''}`}
            >
              <div
                className="gesture-scenario-card__header"
                onClick={() => setDraft((d) => ({ ...d, silentToast: !d.silentToast }))}
              >
                <div className="gesture-scenario-card__main">
                  <div className="gesture-scenario-card__icon-box gesture-scenario-card__icon-box--silent">
                    <VolumeX size={16} />
                  </div>
                  <div className="gesture-scenario-card__text-group">
                    <span className="gesture-scenario-card__title">{t('gestureEditor.silentToastTitle')}</span>
                    <span className="gesture-scenario-card__desc">{t('gestureEditor.silentToastDesc')}</span>
                  </div>
                </div>
                <div onClick={(e) => e.stopPropagation()}>
                  <Toggle
                    id="gesture-silent-toast-toggle"
                    checked={draft.silentToast ?? false}
                    onChange={(checked: boolean) => setDraft((d) => ({ ...d, silentToast: checked }))}
                  />
                </div>
              </div>
              <div className="gesture-scenario-card__callout">
                <Lightbulb size={13} className="gesture-scenario-card__callout-icon" />
                <span>{t('gestureEditor.silentToastScenario')}</span>
              </div>
            </div>
          </div>
        )}
      </div>
    </Modal>
  );
};

function generateMappingId(code: string): string {
  return `custom-${code.toLowerCase().replace(/[^a-z0-9]/g, '_')}-${Date.now().toString(36)}`;
}
