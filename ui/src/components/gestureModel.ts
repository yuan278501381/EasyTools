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
  L: '←', R: '→', U: '↑', D: '↓', UL: '↖', UR: '↗', DL: '↙', DR: '↘',
};

/** 把方向编码 (如 "U-R") 渲染为箭头串, 用于实时预览。 */
export function codeToArrows(code: string): string {
  if (!code) return '';
  return code.split('-').map((seg) => CODE_TO_ARROWS[seg] ?? seg).join(' ');
}

export const GESTURE_CODE_PATTERN = /^[UDLR]{1,3}(-[UDLR]{1,3})*$/;
