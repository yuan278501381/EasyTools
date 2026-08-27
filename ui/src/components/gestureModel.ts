/* ─────────────────────────────────────────────────────────────────────────────
 * gestureModel — 手势相关的共享类型 / 常量 / 纯函数
 * (与组件分离, 以满足 react-refresh 的「文件仅导出组件」约束)
 * ───────────────────────────────────────────────────────────────────────────── */

export type TriggerState = 'default' | 'enabled' | 'disabled';

export interface TriggerItemDef {
  key: string;
  name: string;
  nameKey?: string;
  category: 'mouse' | 'edge';
  iconType: 'rclick' | 'mclick' | 'lclick' | 'xbutton1' | 'xbutton2' | 'edge_slide' | 'edge_wheel' | 'edge_rclick' | 'edge_mclick' | 'edge_lclick';
}

export const TRIGGER_ITEM_DEFINITIONS: TriggerItemDef[] = [
  { key: 'right', name: 'Right Mouse Button', nameKey: 'gesture.triggerRightMouse', category: 'mouse', iconType: 'rclick' },
  { key: 'middle', name: 'Middle Mouse Button', nameKey: 'gesture.triggerMiddleMouse', category: 'mouse', iconType: 'mclick' },
  { key: 'xbutton1', name: 'Mouse Side Button 1', category: 'mouse', iconType: 'xbutton1' },
  { key: 'xbutton2', name: 'Mouse Side Button 2', category: 'mouse', iconType: 'xbutton2' },
  { key: 'left', name: 'Left Mouse Button', nameKey: 'gesture.triggerLeftMouse', category: 'mouse', iconType: 'lclick' },
  { key: 'edge_top_slide', name: 'Top Screen Edge + Mouse Slide', nameKey: 'gesture.triggerEdgeTopSlide', category: 'edge', iconType: 'edge_slide' },
  { key: 'edge_bottom_slide', name: 'Bottom Screen Edge + Mouse Slide', nameKey: 'gesture.triggerEdgeBottomSlide', category: 'edge', iconType: 'edge_slide' },
  { key: 'edge_left_slide', name: 'Left Screen Edge + Mouse Slide', nameKey: 'gesture.triggerEdgeLeftSlide', category: 'edge', iconType: 'edge_slide' },
  { key: 'edge_right_slide', name: 'Right Screen Edge + Mouse Slide', nameKey: 'gesture.triggerEdgeRightSlide', category: 'edge', iconType: 'edge_slide' },
  { key: 'edge_top_wheel', name: 'Top Screen Edge + Wheel', nameKey: 'gesture.triggerEdgeTopWheel', category: 'edge', iconType: 'edge_wheel' },
  { key: 'edge_bottom_wheel', name: 'Bottom Screen Edge + Wheel', nameKey: 'gesture.triggerEdgeBottomWheel', category: 'edge', iconType: 'edge_wheel' },
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

export type TriggerButton = 'right' | 'middle' | 'x1' | 'x2' | 'left';
export type ScreenEdge = 'none' | 'top' | 'bottom' | 'left' | 'right';

export interface ParsedGestureCode {
  edge: ScreenEdge;
  triggerButton: TriggerButton;
  isMiddle: boolean;
  isX1: boolean;
  isX2: boolean;
  isLeft: boolean;
  isTopEdge: boolean;
  hasCtrl: boolean;
  hasAlt: boolean;
  hasShift: boolean;
  bareCode: string;
}

export function parseGestureCode(code: string): ParsedGestureCode {
  let upper = (code || '').trim().toUpperCase();
  let edge: ScreenEdge = 'none';
  let triggerButton: TriggerButton = 'right';
  let hasCtrl = false;
  let hasAlt = false;
  let hasShift = false;

  let matched = true;
  while (matched) {
    matched = false;
    if (upper.startsWith('TOPEDGE+')) {
      edge = 'top';
      upper = upper.slice(8);
      matched = true;
    } else if (upper.startsWith('BOTTOMEDGE+')) {
      edge = 'bottom';
      upper = upper.slice(11);
      matched = true;
    } else if (upper.startsWith('LEFTEDGE+')) {
      edge = 'left';
      upper = upper.slice(9);
      matched = true;
    } else if (upper.startsWith('RIGHTEDGE+')) {
      edge = 'right';
      upper = upper.slice(10);
      matched = true;
    } else if (upper.startsWith('CTRL+')) {
      hasCtrl = true;
      upper = upper.slice(5);
      matched = true;
    } else if (upper.startsWith('ALT+')) {
      hasAlt = true;
      upper = upper.slice(4);
      matched = true;
    } else if (upper.startsWith('SHIFT+')) {
      hasShift = true;
      upper = upper.slice(6);
      matched = true;
    } else if (upper.startsWith('MIDDLE+')) {
      triggerButton = 'middle';
      upper = upper.slice(7);
      matched = true;
    } else if (upper.startsWith('X1+') || upper.startsWith('SIDE1+') || upper.startsWith('XBUTTON1+')) {
      triggerButton = 'x1';
      upper = upper.replace(/^(X1|SIDE1|XBUTTON1)\+/, '');
      matched = true;
    } else if (upper.startsWith('X2+') || upper.startsWith('SIDE2+') || upper.startsWith('XBUTTON2+')) {
      triggerButton = 'x2';
      upper = upper.replace(/^(X2|SIDE2|XBUTTON2)\+/, '');
      matched = true;
    } else if (upper.startsWith('LEFT+')) {
      triggerButton = 'left';
      upper = upper.slice(5);
      matched = true;
    }
  }

  return {
    edge,
    triggerButton,
    isMiddle: triggerButton === 'middle',
    isX1: triggerButton === 'x1',
    isX2: triggerButton === 'x2',
    isLeft: triggerButton === 'left',
    isTopEdge: edge === 'top',
    hasCtrl,
    hasAlt,
    hasShift,
    bareCode: upper,
  };
}

export function assembleGestureCode(params: {
  edge?: ScreenEdge;
  triggerButton?: TriggerButton;
  isMiddle?: boolean;
  isX1?: boolean;
  isX2?: boolean;
  isLeft?: boolean;
  isTopEdge?: boolean;
  hasCtrl?: boolean;
  hasAlt?: boolean;
  hasShift?: boolean;
  bareCode: string;
}): string {
  let prefix = '';

  // 1. Edge prefix
  if (params.edge === 'top' || params.isTopEdge) prefix += 'TopEdge+';
  else if (params.edge === 'bottom') prefix += 'BottomEdge+';
  else if (params.edge === 'left') prefix += 'LeftEdge+';
  else if (params.edge === 'right') prefix += 'RightEdge+';

  // 2. Modifier prefix
  if (params.hasCtrl) prefix += 'Ctrl+';
  if (params.hasAlt) prefix += 'Alt+';
  if (params.hasShift) prefix += 'Shift+';

  // 3. Trigger button prefix
  if (params.triggerButton === 'middle' || params.isMiddle) prefix += 'Middle+';
  else if (params.triggerButton === 'x1' || params.isX1) prefix += 'X1+';
  else if (params.triggerButton === 'x2' || params.isX2) prefix += 'X2+';
  else if (params.triggerButton === 'left' || params.isLeft) prefix += 'Left+';

  return prefix + (params.bareCode || '').toUpperCase();
}

/** 把完整手势编码渲染为箭头与文字提示串, 用于实时预览与列表清晰展示。 */
export function codeToArrows(code: string): string {
  if (!code) return '';
  const parsed = parseGestureCode(code);
  let prefix = '';

  if (parsed.edge === 'top') prefix += '[Top Edge] ';
  else if (parsed.edge === 'bottom') prefix += '[Bottom Edge] ';
  else if (parsed.edge === 'left') prefix += '[Left Edge] ';
  else if (parsed.edge === 'right') prefix += '[Right Edge] ';

  if (parsed.hasCtrl) prefix += 'Ctrl+';
  if (parsed.hasAlt) prefix += 'Alt+';
  if (parsed.hasShift) prefix += 'Shift+';

  if (parsed.triggerButton === 'middle') prefix += '[Middle] ';
  else if (parsed.triggerButton === 'x1') prefix += '[X1] ';
  else if (parsed.triggerButton === 'x2') prefix += '[X2] ';
  else if (parsed.triggerButton === 'left') prefix += '[Left] ';

  const bareCode = parsed.bareCode;
  if (!bareCode) return prefix.trim();

  if (bareCode.includes('-')) {
    return prefix + bareCode.split('-').map((seg) => CODE_TO_ARROWS[seg] ?? seg).join(' ');
  }
  if (CODE_TO_ARROWS[bareCode]) {
    return prefix + CODE_TO_ARROWS[bareCode];
  }
  const chars = bareCode.split('');
  if (chars.length > 0 && chars.every((c) => CODE_TO_ARROWS[c])) {
    return prefix + chars.map((c) => CODE_TO_ARROWS[c]).join(' ');
  }
  return prefix + bareCode;
}

export const GESTURE_CODE_PATTERN = /^((TopEdge|BottomEdge|LeftEdge|RightEdge|Middle|X1|X2|Left|Ctrl|Alt|Shift)\+)*[UDLR]{1,3}(-[UDLR]{1,3})*$/i;

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
