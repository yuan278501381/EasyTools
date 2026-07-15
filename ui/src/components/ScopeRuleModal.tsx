/* ─────────────────────────────────────────────────────────────────────────────
 * ScopeRuleModal — 新增 / 编辑作用域规则
 *
 * 匹配目标二选一: 进程名 或 窗口类名 (句柄需原生十字准星捕获, 暂不在此处)。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Modal, Field, Button, Select, TextInput } from './UIKit';
import {
  type ScopeRule,
  MATCH_MODE_KEYS,
  EFFECT_KEYS,
  emptyRule,
} from './scopeModel';
import { useTranslation } from 'react-i18next';

type TargetKind = 'process' | 'class';

interface Props {
  initial: ScopeRule | null;
  /** 可选的配置集名称, 供 effect===使用配置集 时选择 */
  profileNames: string[];
  onSave: (rule: ScopeRule) => void;
  onClose: () => void;
}

export const ScopeRuleModal: FC<Props> = ({ initial, profileNames, onSave, onClose }) => {
  const { t } = useTranslation();
  const [draft, setDraft] = useState<ScopeRule>(() => (initial ? structuredClone(initial) : emptyRule()));
  const [targetKind, setTargetKind] = useState<TargetKind>(
    initial && initial.windowClass && !initial.processName ? 'class' : 'process',
  );

  const isEdit = initial !== null;
  const set = (patch: Partial<ScopeRule>) => setDraft((d) => ({ ...d, ...patch }));

  const targetValue = targetKind === 'process' ? draft.processName : draft.windowClass;
  const setTargetValue = (v: string) =>
    set(targetKind === 'process' ? { processName: v, windowClass: '' }
                                 : { windowClass: v, processName: '' });

  const nameError = draft.name.trim() ? '' : t('scope.nameRequired');
  const targetError = targetValue.trim() ? '' : t('scope.targetRequired');
  const canSave = !nameError && !targetError;

  const handleSave = () => {
    if (!canSave) return;
    const rule: ScopeRule = {
      ...draft,
      name: draft.name.trim(),
      processName: targetKind === 'process' ? targetValue.trim() : '',
      windowClass: targetKind === 'class' ? targetValue.trim() : '',
      profileName: draft.effect === 2 ? draft.profileName : '',
    };
    onSave(rule);
  };

  return (
    <Modal
      open
      title={isEdit ? t('scope.editRule') : t('scope.newRule')}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>{t('common.cancel')}</Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>{t('common.save')}</Button>
        </>
      }
    >
      <Field label={t('scope.ruleName')} error={nameError}>
        <TextInput value={draft.name} onChange={(v) => set({ name: v })} placeholder={t('scope.ruleNamePlaceholder')} />
      </Field>

      <Field label={t('scope.matchTarget')}>
        <Select
          value={targetKind}
          options={[
            { value: 'process', label: t('scope.byProcess') },
            { value: 'class', label: t('scope.byClass') },
          ]}
          onChange={(v) => setTargetKind(v as TargetKind)}
        />
      </Field>

      <Field
        label={targetKind === 'process' ? t('scope.processName') : t('scope.windowClass')}
        error={targetError}
        hint={targetKind === 'process' ? t('scope.processHint') : t('scope.classHint')}
      >
        <TextInput
          value={targetValue}
          onChange={setTargetValue}
          placeholder={targetKind === 'process' ? 'chrome.exe' : 'Chrome_WidgetWin_1'}
        />
      </Field>

      <Field label={t('scope.matchMode')}>
        <Select
          value={String(draft.matchMode)}
          options={MATCH_MODE_KEYS.map((key, index) => ({ value: String(index), label: t(key) }))}
          onChange={(v) => set({ matchMode: Number(v) })}
        />
      </Field>

      <Field label={t('scope.effect')}>
        <Select
          value={String(draft.effect)}
          options={EFFECT_KEYS.map((key, index) => ({ value: String(index), label: t(key) }))}
          onChange={(v) => set({ effect: Number(v) })}
        />
      </Field>

      {draft.effect === 2 && (
        <Field label={t('scope.profile')} hint={t('scope.profileHint')}>
          <Select
            value={draft.profileName || (profileNames[0] ?? '')}
            options={profileNames.map((n) => ({ value: n, label: n }))}
            onChange={(v) => set({ profileName: v })}
          />
        </Field>
      )}
    </Modal>
  );
};
