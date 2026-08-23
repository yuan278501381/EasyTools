import { describe, expect, it } from 'vitest';
import { upsertGestureMapping, codeToArrows, GESTURE_CODE_PATTERN, type GestureMapping } from './gestureModel';

function mapping(code: string, name: string, id?: string): GestureMapping {
  return { id, gestureCode: code, action: { type: 0, name, keyStroke: 'Ctrl+A' } };
}

describe('upsertGestureMapping', () => {
  const list = [
    mapping('D-R', '关闭标签页', 'a'),
    mapping('LU', '剪切', 'b'),
    mapping('UR', '最大化', 'c'),
  ];

  it('keeps an edited mapping in its original slot', () => {
    const saved = mapping('D-R', '关标签', 'a');
    const next = upsertGestureMapping(list, saved, list[0]);
    expect(next.map((m) => m.id)).toEqual(['a', 'b', 'c']);
    expect(next[0].action.name).toBe('关标签');
  });

  it('matches items without an id by the original gesture code', () => {
    const noId = [mapping('U', '关闭窗口'), mapping('D', '新建标签页')];
    const saved = mapping('U', '关窗');
    const next = upsertGestureMapping(noId, saved, noId[0]);
    expect(next).toHaveLength(2);
    expect(next[0].action.name).toBe('关窗');
    expect(next[1].gestureCode).toBe('D');
  });

  it('appends a brand-new mapping', () => {
    const saved = mapping('R-L', '全选', 'd');
    const next = upsertGestureMapping(list, saved, null);
    expect(next.map((m) => m.id)).toEqual(['a', 'b', 'c', 'd']);
  });

  it('keeps position when the gesture stroke itself changes', () => {
    const saved = mapping('R-D', '关闭标签页', 'a');
    const next = upsertGestureMapping(list, saved, list[0]);
    expect(next.map((m) => m.gestureCode)).toEqual(['R-D', 'LU', 'UR']);
  });

  it('drops a colliding mapping but keeps the edited row in place', () => {
    const saved = mapping('LU', '关闭标签页', 'a');
    const next = upsertGestureMapping(list, saved, list[0]);
    expect(next.map((m) => m.id)).toEqual(['a', 'c']);
    expect(next[0].gestureCode).toBe('LU');
  });

  it('replaces an existing code in place when adding would collide', () => {
    const saved = mapping('LU', '新剪切', 'x');
    const next = upsertGestureMapping(list, saved, null);
    expect(next.map((m) => m.id)).toEqual(['a', 'x', 'c']);
    expect(next[1].action.name).toBe('新剪切');
  });

  it('keeps the edited row in place when a collision sits earlier in the list', () => {
    const saved = mapping('D-R', '最大化改关标签', 'c');
    const next = upsertGestureMapping(list, saved, list[2]);
    expect(next.map((m) => m.id)).toEqual(['b', 'c']);
    expect(next[1].action.name).toBe('最大化改关标签');
  });
});

describe('codeToArrows, parseGestureCode, assembleGestureCode and GESTURE_CODE_PATTERN', () => {
  it('correctly renders arrows with Middle+, X1+, X2+, TopEdge+ and modifier prefixes', () => {
    expect(codeToArrows('L')).toBe('←');
    expect(codeToArrows('R')).toBe('→');
    expect(codeToArrows('Middle+L')).toBe('[中键] ←');
    expect(codeToArrows('Middle+R')).toBe('[中键] →');
    expect(codeToArrows('X1+L')).toBe('[侧键1] ←');
    expect(codeToArrows('X2+R')).toBe('[侧键2] →');
    expect(codeToArrows('TopEdge+D')).toBe('[上边缘] ↓');
    expect(codeToArrows('TopEdge+Left+D')).toBe('[上边缘] [左键] ↓');
    expect(codeToArrows('Ctrl+U-R')).toBe('Ctrl+↑ →');
    expect(codeToArrows('Ctrl+X1+L')).toBe('Ctrl+[侧键1] ←');

    expect(GESTURE_CODE_PATTERN.test('L')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('R')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('Middle+L')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('X1+L')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('X2+R')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('TopEdge+D')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('TopEdge+Left+D')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('BottomEdge+X1+L')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('Ctrl+Shift+U-R')).toBe(true);
    expect(GESTURE_CODE_PATTERN.test('invalid_xyz')).toBe(false);
  });
});
