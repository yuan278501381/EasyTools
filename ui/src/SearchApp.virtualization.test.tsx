/* @vitest-environment jsdom */

import { act, cleanup, render } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ColumnLayout, SearchResult } from './SearchApp';
import { VirtualSearchResults } from './SearchApp';

class ResizeObserverMock {
  static instances: ResizeObserverMock[] = [];
  private readonly callback: ResizeObserverCallback;
  readonly observed = new Set<Element>();
  readonly observe = vi.fn((element: Element) => this.observed.add(element));
  readonly unobserve = vi.fn((element: Element) => this.observed.delete(element));
  readonly disconnect = vi.fn(() => this.observed.clear());

  constructor(callback: ResizeObserverCallback) {
    this.callback = callback;
    ResizeObserverMock.instances.push(this);
  }

  emit(element: Element, height: number) {
    this.callback([{
      target: element,
      contentRect: { height },
      borderBoxSize: [{ blockSize: height }],
    } as unknown as ResizeObserverEntry], this as unknown as ResizeObserver);
  }
}

Object.defineProperty(globalThis, 'ResizeObserver', {
  writable: true,
  value: ResizeObserverMock,
});

const columns: ColumnLayout = {
  name: true, ext: false, parent: false, path: true, size: false,
  modified: false, created: false, snippets: false, nameFlex: 1, pathFlex: 1,
};

function results(count: number): SearchResult[] {
  return Array.from({ length: count }, (_, index) => ({
    name: `result-${index}`,
    path: `C:\\results\\${index}.txt`,
    isDirectory: false,
  }));
}

function props(items: SearchResult[], selectedIndex = 0) {
  return {
    results: items,
    selectedIndex,
    density: 'standard' as const,
    columns,
    queryKeywords: [],
    onHover: vi.fn(),
    onSelect: vi.fn(),
    onOpen: vi.fn(),
    onContextMenu: vi.fn(),
  };
}

afterEach(() => {
  cleanup();
  ResizeObserverMock.instances = [];
});

describe('VirtualSearchResults', () => {
  it('keeps a 100k result DOM window bounded and mounts the far active option', () => {
    const items = results(100_000);
    const view = render(<VirtualSearchResults {...props(items)} />);
    const list = view.getByRole('listbox');
    Object.defineProperty(list, 'clientHeight', { configurable: true, value: 480 });

    const initialCount = list.querySelectorAll('[role="option"]').length;
    expect(initialCount).toBeGreaterThan(0);
    expect(initialCount).toBeLessThanOrEqual(20);
    view.rerender(<VirtualSearchResults {...props(items, 99_999)} />);

    const options = list.querySelectorAll('[role="option"]');
    expect(options.length).toBeLessThanOrEqual(16);
    expect(document.getElementById('search-result-99999')).not.toBeNull();
    expect(document.getElementById('search-result-99999')?.getAttribute('data-virtual-index')).toBe('99999');
    expect(document.getElementById('search-result-99999')?.getAttribute('aria-selected')).toBe('true');
    // Selection changes from keyboard navigation must make the active option
    // visible without mounting every item between index 0 and index 99,999.
    expect(list.scrollTop).toBeGreaterThan(5_000_000);
  });

  it('applies ResizeObserver measurements and removes rows from observation on unmount', () => {
    const view = render(<VirtualSearchResults {...props(results(20))} />);
    const firstRow = document.getElementById('search-result-0');
    const secondRow = document.getElementById('search-result-1');
    expect(firstRow).not.toBeNull();
    expect(secondRow).not.toBeNull();

    const rowObserver = ResizeObserverMock.instances.find((observer) => observer.observed.has(firstRow!));
    expect(rowObserver).toBeDefined();
    act(() => rowObserver!.emit(firstRow!, 120));
    expect((secondRow as HTMLElement).style.top).toBe('120px');

    view.unmount();
    expect(rowObserver!.disconnect).toHaveBeenCalledTimes(1);
  });
});
