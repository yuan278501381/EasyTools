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
import { Modal, Field, Button, Select, TextInput, HotkeyInput } from './UIKit';
import {
  type GestureMapping,
  ACTION_TYPE_OPTIONS,
  BUILTIN_COMMANDS,
  GESTURE_CODE_PATTERN,
  codeToArrows,
} from './gestureModel';

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
  const [draft, setDraft] = useState<GestureMapping>(() =>
    initial ? structuredClone(initial) : emptyMapping());

  const isEdit = initial !== null;
  const code = draft.gestureCode.trim().toUpperCase();

  // ── 校验 ──────────────────────────────────────────────────────────────────
  let codeError = '';
  if (!code) {
    codeError = '请填写手势编码';
  } else if (!GESTURE_CODE_PATTERN.test(code)) {
    codeError = '编码只能由 U/D/L/R 组成, 多段用 “-” 分隔, 如 U-R';
  } else if (!isEdit || code !== initial!.gestureCode) {
    if (existingCodes.includes(code)) codeError = `编码 ${code} 已存在`;
  }

  // 前缀冲突 (非阻塞警告)
  const prefixConflicts = code && !codeError
    ? existingCodes.filter(
        (c) => c !== (isEdit ? initial!.gestureCode : '') &&
               (c.startsWith(code) || code.startsWith(c)) && c !== code,
      )
    : [];

  const nameError = draft.action.name.trim() ? '' : '请填写动作名称';
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
    onSave({ gestureCode: code, action });
  };

  return (
    <Modal
      open
      title={isEdit ? '编辑手势' : '新增手势'}
      onClose={onClose}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>取消</Button>
          <Button variant="primary" onClick={handleSave} disabled={!canSave}>保存</Button>
        </>
      }
    >
      <Field
        label="手势编码"
        error={codeError}
        hint={prefixConflicts.length ? `提示: 与 ${prefixConflicts.join(', ')} 存在前缀冲突, 可能影响识别` : '方向序列, 如 L、UR、U-R'}
      >
        <TextInput
          value={draft.gestureCode}
          onChange={(v) => setDraft((d) => ({ ...d, gestureCode: v }))}
          placeholder="例如 U-R"
        />
        <div className="gesture-code-preview">{codeToArrows(code)}</div>
      </Field>

      <Field label="动作名称" error={nameError}>
        <TextInput
          value={draft.action.name}
          onChange={(v) => setAction({ name: v })}
          placeholder="例如 复制 / 后退"
        />
      </Field>

      <Field label="动作类型">
        <Select
          value={String(draft.action.type)}
          options={ACTION_TYPE_OPTIONS}
          onChange={(v) => setAction({ type: Number(v) })}
        />
      </Field>

      {draft.action.type === 0 && (
        <Field label="快捷键" hint="点击输入框后按下组合键">
          <HotkeyInput
            value={draft.action.keyStroke ?? ''}
            onChange={(v) => setAction({ keyStroke: v })}
          />
        </Field>
      )}

      {draft.action.type === 1 && (
        <Field label="Lua 脚本" hint="可调用 easy.* API, 见 docs/api/lua-api.md">
          <TextInput
            multiline
            value={draft.action.luaScript ?? ''}
            onChange={(v) => setAction({ luaScript: v })}
            placeholder={'easy.shell.open("https://example.com")'}
          />
        </Field>
      )}

      {draft.action.type === 2 && (
        <Field label="内置命令">
          <Select
            value={String(draft.action.builtinCmd ?? 0)}
            options={BUILTIN_COMMANDS.map((label, i) => ({ value: String(i), label }))}
            onChange={(v) => setAction({ builtinCmd: Number(v) })}
          />
        </Field>
      )}

      {draft.action.type === 3 && (
        <>
          <Field label="程序路径">
            <TextInput
              value={draft.action.programPath ?? ''}
              onChange={(v) => setAction({ programPath: v })}
              placeholder="C:\\Windows\\System32\\notepad.exe"
            />
          </Field>
          <Field label="启动参数" hint="可选">
            <TextInput
              value={draft.action.programArgs ?? ''}
              onChange={(v) => setAction({ programArgs: v })}
            />
          </Field>
        </>
      )}
    </Modal>
  );
};
