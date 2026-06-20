/* ─────────────────────────────────────────────────────────────────────────────
 * ScopeRuleModal — 新增 / 编辑作用域规则
 *
 * 匹配目标二选一: 进程名 或 窗口类名 (句柄需原生十字准星捕获, 暂不在此处)。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Modal, Field, Button, Select, TextInput } from './UIKit';
import {
  type ScopeRule,
  MATCH_MODE_OPTIONS,
  EFFECT_OPTIONS,
  emptyRule,
} from './scopeModel';

type TargetKind = 'process' | 'class';

interface Props {
  initial: ScopeRule | null;
  /** 可选的配置集名称, 供 effect===使用配置集 时选择 */
  profileNames: string[];
  onSave: (rule: ScopeRule) => void;
  onClose: () => void;
}

export const ScopeRuleModal: FC<Props> = ({ initial, profileNames, onSave, onClose }) => {
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

  const nameError = draft.name.trim() ? '' : '请填写规则名称';
  const targetError = targetValue.trim() ? '' : '请填写匹配的进程名或窗口类名';
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
      title={isEdit ? '编辑作用域规则' : '新增作用域规则'}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>取消</Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>保存</Button>
        </>
      }
    >
      <Field label="规则名称" error={nameError}>
        <TextInput value={draft.name} onChange={(v) => set({ name: v })} placeholder="例如 Chrome 浏览器" />
      </Field>

      <Field label="匹配目标">
        <Select
          value={targetKind}
          options={[
            { value: 'process', label: '按进程名' },
            { value: 'class', label: '按窗口类名' },
          ]}
          onChange={(v) => setTargetKind(v as TargetKind)}
        />
      </Field>

      <Field
        label={targetKind === 'process' ? '进程名' : '窗口类名'}
        error={targetError}
        hint={targetKind === 'process' ? '如 chrome.exe，可配合通配符 *.exe' : '如 Chrome_WidgetWin_1'}
      >
        <TextInput
          value={targetValue}
          onChange={setTargetValue}
          placeholder={targetKind === 'process' ? 'chrome.exe' : 'Chrome_WidgetWin_1'}
        />
      </Field>

      <Field label="匹配方式">
        <Select
          value={String(draft.matchMode)}
          options={MATCH_MODE_OPTIONS}
          onChange={(v) => set({ matchMode: Number(v) })}
        />
      </Field>

      <Field label="作用效果">
        <Select
          value={String(draft.effect)}
          options={EFFECT_OPTIONS}
          onChange={(v) => set({ effect: Number(v) })}
        />
      </Field>

      {draft.effect === 2 && (
        <Field label="使用的配置集" hint="匹配窗口时切换到该手势配置集">
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
