/* ─────────────────────────────────────────────────────────────────────────────
 * GestureEditorModal — 新增 / 编辑手势映射
 *
 * 字段随动作类型变化:
 *   0 快捷键   → keyStroke (HotkeyInput)
 *   1 Lua 脚本 → luaScript (多行)
 *   2 内置命令 → builtinCmd (下拉)
 *   3 运行程序 → programPath + programArgs
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Modal, Field, Button, Select, TextInput, Toggle } from './UIKit';
import { HotkeyRecorder } from './HotkeyRecorder';
import { GestureDrawCanvas } from './GestureDrawCanvas';
import { useTranslation } from 'react-i18next';
import { Zap, VolumeX, Sliders, ChevronDown, ChevronUp, Lightbulb } from 'lucide-react';
import {
  type GestureMapping,
  ACTION_TYPE_KEYS,
  BUILTIN_COMMAND_KEYS,
  GESTURE_CODE_PATTERN,
  codeToArrows,
} from './gestureModel';
import './GestureEditorModal.css';

let gMappingCounter = 0;
function generateMappingId(code: string): string {
  gMappingCounter = (gMappingCounter + 1) % 1000000;
  return `gm_${code.replace(/[^a-zA-Z0-9]/g, '_')}_${gMappingCounter}`;
}

function emptyMapping(): GestureMapping {
  return { gestureCode: '', action: { type: 0, name: '', keyStroke: '' } };
}

interface Props {
  /** 编辑时传入原映射; 新增时传 null。组件按 key 条件挂载, 故无需 open prop。 */
  initial: GestureMapping | null;
  /** 现有的全部编码, 用于冲突检测 */
  existingCodes: string[];
  onSave: (mapping: GestureMapping) => void;
  onClose: () => void;
}

export const GestureEditorModal: FC<Props> = ({ initial, existingCodes, onSave, onClose }) => {
  const { t } = useTranslation();
  const [draft, setDraft] = useState<GestureMapping>(() =>
    initial ? structuredClone(initial) : emptyMapping());

  const isEdit = initial !== null;
  const code = draft.gestureCode.trim().toUpperCase();
  const [advancedOpen, setAdvancedOpen] = useState(Boolean(initial?.instantExecute || initial?.silentToast));
  const activeAdvancedCount = (draft.instantExecute ? 1 : 0) + (draft.silentToast ? 1 : 0);

  // ── 校验 ──────────────────────────────────────────────────────────────────
  let codeError = '';
  if (!code) {
    codeError = t('gestureEditor.gestureCodeRequired');
  } else if (!GESTURE_CODE_PATTERN.test(code)) {
    codeError = t('gestureEditor.gestureCodeInvalid');
  } else if (!isEdit || code !== initial!.gestureCode) {
    if (existingCodes.includes(code)) codeError = t('gestureEditor.gestureCodeExists', { code });
  }

  // 前缀冲突 (非阻塞警告)
  const prefixConflicts = code && !codeError
    ? existingCodes.filter(
        (c) => c !== (isEdit ? initial!.gestureCode : '') &&
               (c.startsWith(code) || code.startsWith(c)) && c !== code,
      )
    : [];

  const nameError = draft.action.name.trim() ? '' : t('gestureEditor.actionNameRequired');
  const canSave = !codeError && !nameError;

  const setAction = (patch: Partial<GestureMapping['action']>) =>
    setDraft((d) => ({ ...d, action: { ...d.action, ...patch } }));

  const handleSave = () => {
    if (!canSave) return;
    // 按类型裁剪掉无关字段, 避免向后端发送脏数据
    const t = draft.action.type;
    const action: GestureMapping['action'] = {
      type: t,
      name: draft.action.name.trim(),
      description: draft.action.description,
    };
    if (t === 0) action.keyStroke = draft.action.keyStroke ?? '';
    else if (t === 1) action.luaScript = draft.action.luaScript ?? '';
    else if (t === 2) action.builtinCmd = draft.action.builtinCmd ?? 0;
    else if (t === 3) {
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

  return (
    <Modal
      open
      title={isEdit ? t('gestureEditor.titleEdit') : t('gestureEditor.titleAdd')}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>{t('common.cancel')}</Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>{t('common.save')}</Button>
        </>
      }
    >
      <Field
        label={t('gestureEditor.gestureDraw')}
        error={codeError}
        hint={prefixConflicts.length ? t('gestureEditor.prefixConflict', { codes: prefixConflicts.join(', ') }) : t('gestureEditor.gestureDrawHint')}
      >
        <GestureDrawCanvas
          value={draft.gestureCode}
          onChange={(v) => setDraft((d) => ({ ...d, gestureCode: v }))}
        />
        {code && (
          <div style={{ marginTop: '8px', display: 'flex', gap: '10px', alignItems: 'center' }}>
            <span style={{ fontSize: '0.82rem', color: 'var(--text-muted)' }}>{t('gestureEditor.recognizedGesture')}:</span>
            <span className="gesture-arrow" title={`底层编码: ${code}`}>{codeToArrows(code)}</span>
          </div>
        )}
      </Field>

      <Field label={t('gestureEditor.actionName')} error={nameError}>
        <TextInput
          value={draft.action.name}
          onChange={(v) => setAction({ name: v })}
          placeholder={t('gestureEditor.actionNamePlaceholder')}
        />
      </Field>

      <Field label={t('gestureEditor.actionType')}>
        <Select
          value={String(draft.action.type)}
          options={ACTION_TYPE_KEYS.map((key, index) => ({ value: String(index), label: t(key) }))}
          onChange={(v) => setAction({ type: Number(v) })}
        />
      </Field>

      {draft.action.type === 0 && (
        <Field label={t('gestureEditor.hotkey')} hint={t('gestureEditor.hotkeyHint')}>
          <HotkeyRecorder
            id="gesture-hotkey-recorder"
            value={draft.action.keyStroke ?? ''}
            onChange={(v) => setAction({ keyStroke: v })}
          />
        </Field>
      )}

      {draft.action.type === 1 && (
        <Field label={t('gestureEditor.luaScript')} hint={t('gestureEditor.luaScriptHint')}>
          <TextInput
            multiline
            value={draft.action.luaScript ?? ''}
            onChange={(v) => setAction({ luaScript: v })}
            placeholder={'easy.shell.open("https://example.com")'}
          />
        </Field>
      )}

      {draft.action.type === 2 && (
        <Field label={t('gestureEditor.builtinCommand')}>
          <Select
            value={String(draft.action.builtinCmd ?? 0)}
            options={BUILTIN_COMMAND_KEYS.map((key, i) => ({ value: String(i), label: t(key) }))}
            onChange={(v) => setAction({ builtinCmd: Number(v) })}
          />
        </Field>
      )}

      {draft.action.type === 3 && (
        <>
          <Field label={t('gestureEditor.programPath')}>
            <TextInput
              value={draft.action.programPath ?? ''}
              onChange={(v) => setAction({ programPath: v })}
              placeholder="C:\\Windows\\System32\\notepad.exe"
            />
          </Field>
          <Field label={t('gestureEditor.programArgs')} hint={t('gestureEditor.programArgsHint')}>
            <TextInput
              value={draft.action.programArgs ?? ''}
              onChange={(v) => setAction({ programArgs: v })}
            />
          </Field>
        </>
      )}

      {/* ── 高级执行特性折叠面板 ────────────────────────────────────── */}
      <div className="gesture-editor-advanced-wrapper">
        <button
          type="button"
          className="gesture-editor-advanced-toggle"
          onClick={() => setAdvancedOpen(!advancedOpen)}
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
            {/* 卡片 1: 即时执行 */}
            <div
              className={`gesture-scenario-card ${draft.instantExecute ? 'gesture-scenario-card--active' : ''}`}
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
                    onChange={(checked) => setDraft((d) => ({ ...d, instantExecute: checked }))}
                  />
                </div>
              </div>
              <div className="gesture-scenario-card__callout">
                <Lightbulb size={13} className="gesture-scenario-card__callout-icon" />
                <span>{t('gestureEditor.instantExecuteScenario')}</span>
              </div>
            </div>

            {/* 卡片 2: 静默模式 */}
            <div
              className={`gesture-scenario-card ${draft.silentToast ? 'gesture-scenario-card--active' : ''}`}
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
                    onChange={(checked) => setDraft((d) => ({ ...d, silentToast: checked }))}
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
