/* ─────────────────────────────────────────────────────────────────────────────
 * gestureModel — 手势相关的共享类型 / 常量 / 纯函数
 * (与组件分离, 以满足 react-refresh 的「文件仅导出组件」约束)
 * ───────────────────────────────────────────────────────────────────────────── */

export interface GestureMapping {
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
  'gestureEditor.builtin.pasteAsPin',
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
