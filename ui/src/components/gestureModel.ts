/* ─────────────────────────────────────────────────────────────────────────────
 * gestureModel — 手势相关的共享类型 / 常量 / 纯函数
 * (与组件分离, 以满足 react-refresh 的「文件仅导出组件」约束)
 * ───────────────────────────────────────────────────────────────────────────── */

export type TriggerState = 'default' | 'enabled' | 'disabled';

export interface TriggerItemDef {
  key: string;
  name: string;
  category: 'mouse' | 'edge';
  iconType: 'rclick' | 'mclick' | 'lclick' | 'xbutton1' | 'xbutton2' | 'edge_slide' | 'edge_wheel' | 'edge_rclick' | 'edge_mclick' | 'edge_lclick';
}

export const TRIGGER_ITEM_DEFINITIONS: TriggerItemDef[] = [
  { key: 'right', name: '鼠标右键', category: 'mouse', iconType: 'rclick' },
  { key: 'middle', name: '鼠标中键', category: 'mouse', iconType: 'mclick' },
  { key: 'left', name: '鼠标左键', category: 'mouse', iconType: 'lclick' },
  { key: 'xbutton1', name: '鼠标侧键1', category: 'mouse', iconType: 'xbutton1' },
  { key: 'xbutton2', name: '鼠标侧键2', category: 'mouse', iconType: 'xbutton2' },
  { key: 'edge_top_slide', name: '屏幕上边缘 + 鼠标滑动', category: 'edge', iconType: 'edge_slide' },
  { key: 'edge_top_wheel', name: '屏幕上边缘 + 滚轮', category: 'edge', iconType: 'edge_wheel' },
  { key: 'edge_top_right', name: '屏幕上边缘 + 鼠标右键', category: 'edge', iconType: 'edge_rclick' },
  { key: 'edge_top_middle', name: '屏幕上边缘 + 鼠标中键', category: 'edge', iconType: 'edge_mclick' },
  { key: 'edge_top_left', name: '屏幕上边缘 + 鼠标左键', category: 'edge', iconType: 'edge_lclick' },
];

export interface GestureMapping {
  id?: string;
  enabled?: boolean;
  instantExecute?: boolean;
  silentToast?: boolean;
  gestureCode: string;
  action: {
    type: number;
    name: string;
    description?: string;
    keyStroke?: string;
    luaScript?: string;
    builtinCmd?: number;
    programPath?: string;
    programArgs?: string;
  };
}

export interface GestureProfileData {
  name: string;
  mappings: GestureMapping[];
  triggerStates?: Record<string, TriggerState>;
}

export const ACTION_TYPE_KEYS = [
  'gestureEditor.actionTypes.hotkey',
  'gestureEditor.actionTypes.lua',
  'gestureEditor.actionTypes.builtin',
  'gestureEditor.actionTypes.program',
] as const;

// 顺序必须与 C++ BuiltinCommand 枚举一致 (gesture/GestureAction.h)
export const BUILTIN_COMMAND_KEYS = [
  'gestureEditor.builtin.closeWindow', 'gestureEditor.builtin.closeTab',
  'gestureEditor.builtin.maximize', 'gestureEditor.builtin.minimize',
  'gestureEditor.builtin.restore', 'gestureEditor.builtin.showDesktop',
  'gestureEditor.builtin.switchDesktop', 'gestureEditor.builtin.taskView',
  'gestureEditor.builtin.lockScreen', 'gestureEditor.builtin.pauseGestures',
  'gestureEditor.builtin.screenshot', 'gestureEditor.builtin.record',
  'gestureEditor.builtin.restoreTab', 'gestureEditor.builtin.topmost',
  'gestureEditor.builtin.transparency', 'gestureEditor.builtin.webSearch',
  'gestureEditor.builtin.search', 'gestureEditor.builtin.radialMenu',
  'gestureEditor.builtin.pasteAsPin', 'gestureEditor.builtin.mediaNext',
  'gestureEditor.builtin.mediaPrev', 'gestureEditor.builtin.mediaPlayPause',
  'gestureEditor.builtin.volumeUp', 'gestureEditor.builtin.volumeDown',
  'gestureEditor.builtin.volumeMute', 'gestureEditor.builtin.prevDesktop',
  'gestureEditor.builtin.nextDesktop',
] as const;

export const CODE_TO_ARROWS: Record<string, string> = {
  L: '←', R: '→', U: '↑', D: '↓',
  UL: '↖', UR: '↗', DL: '↙', DR: '↘',
  LU: '↖', RU: '↗', LD: '↙', RD: '↘',
};

/** 把方向编码 (如 "Middle+L", "Ctrl+U-R", "LU") 渲染为箭头串, 用于实时预览与无障碍提示。 */
export function codeToArrows(code: string): string {
  if (!code) return '';
  let prefix = '';
  let upper = code.trim().toUpperCase();

  if (upper.startsWith('MIDDLE+')) {
    prefix = '[中键] ';
    upper = upper.slice(7);
  }
  if (upper.startsWith('CTRL+')) {
    prefix = 'Ctrl+' + prefix;
    upper = upper.slice(5);
  }
  if (upper.startsWith('ALT+')) {
    prefix = 'Alt+' + prefix;
    upper = upper.slice(4);
  }
  if (upper.startsWith('SHIFT+')) {
    prefix = 'Shift+' + prefix;
    upper = upper.slice(6);
  }

  if (upper.includes('-')) {
    return prefix + upper.split('-').map((seg) => CODE_TO_ARROWS[seg] ?? seg).join(' ');
  }
  if (CODE_TO_ARROWS[upper]) {
    return prefix + CODE_TO_ARROWS[upper];
  }
  // 逐字符解析 (如 "DR" -> "↓ →")
  const chars = upper.split('');
  if (chars.length > 0 && chars.every((c) => CODE_TO_ARROWS[c])) {
    return prefix + chars.map((c) => CODE_TO_ARROWS[c]).join(' ');
  }
  return prefix + upper;
}

export const GESTURE_CODE_PATTERN = /^((Middle|Ctrl|Alt|Shift)\+)*[UDLR]{1,3}(-[UDLR]{1,3})*$/i;

export function normalizeGestureCode(code: string): string {
  return code.trim().toUpperCase();
}

/**
 * 编辑已有手势时原地替换，保持列表顺序；新增则追加到末尾。
 * 若保存的方向码与另一项冲突，则覆盖那一项（编辑中的项仍留在原位）。
 */
export function upsertGestureMapping(
  list: readonly GestureMapping[],
  saved: GestureMapping,
  editing: GestureMapping | null,
): GestureMapping[] {
  const savedCode = normalizeGestureCode(saved.gestureCode);
  const editingId = editing?.id;
  const editingCode = editing ? normalizeGestureCode(editing.gestureCode) : '';

  const isEditingItem = (m: GestureMapping) => {
    if (!editing) return false;
    if (editingId && m.id) return m.id === editingId;
    return editingCode !== '' && normalizeGestureCode(m.gestureCode) === editingCode;
  };

  const next = list.slice();
  let editIdx = next.findIndex(isEditingItem);

  if (editIdx >= 0) {
    for (let i = next.length - 1; i >= 0; i--) {
      if (i === editIdx) continue;
      if (normalizeGestureCode(next[i].gestureCode) === savedCode) {
        next.splice(i, 1);
        if (i < editIdx) editIdx -= 1;
      }
    }
    next[editIdx] = saved;
    return next;
  }

  const conflictIdx = next.findIndex((m) => normalizeGestureCode(m.gestureCode) === savedCode);
  if (conflictIdx >= 0) {
    next[conflictIdx] = saved;
    return next;
  }

  next.push(saved);
  return next;
}
