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

export const ACTION_TYPE_OPTIONS = [
  { value: '0', label: '快捷键' },
  { value: '1', label: 'Lua 脚本' },
  { value: '2', label: '内置命令' },
  { value: '3', label: '运行程序' },
];

// 顺序必须与 C++ BuiltinCommand 枚举一致 (gesture/GestureAction.h)
export const BUILTIN_COMMANDS = [
  '关闭窗口', '关闭标签页', '最大化窗口', '最小化窗口', '还原窗口',
  '显示桌面', '切换虚拟桌面', '任务视图', '锁屏', '暂停手势',
  '截图', '开始录屏', '恢复关闭的标签页', '窗口置顶切换',
];

export const CODE_TO_ARROWS: Record<string, string> = {
  L: '←', R: '→', U: '↑', D: '↓', UL: '↖', UR: '↗', DL: '↙', DR: '↘',
};

/** 把方向编码 (如 "U-R") 渲染为箭头串, 用于实时预览。 */
export function codeToArrows(code: string): string {
  if (!code) return '';
  return code.split('-').map((seg) => CODE_TO_ARROWS[seg] ?? seg).join(' ');
}

export const GESTURE_CODE_PATTERN = /^[UDLR]{1,3}(-[UDLR]{1,3})*$/;
