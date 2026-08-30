import { useCallback, useEffect, type KeyboardEvent } from 'react';
import type { CategoryFilter, SearchResult, SortField } from './searchTypes';

export interface UseSearchKeyboardProps {
  inputRef: React.RefObject<HTMLInputElement | null>;
  isComposing: boolean;
  sortedResults: SearchResult[];
  selectedIndex: number;
  setSelectedIndex: React.Dispatch<React.SetStateAction<number>>;
  query: string;
  renameTarget: { visible: boolean; newName: string; result?: SearchResult };
  setRenameTarget: React.Dispatch<React.SetStateAction<{ visible: boolean; newName: string; result?: SearchResult }>>;
  contextMenu: { visible: boolean; x: number; y: number; result?: SearchResult };
  setContextMenu: React.Dispatch<React.SetStateAction<{ visible: boolean; x: number; y: number; result?: SearchResult }>>;
  showSortMenu: boolean;
  setShowSortMenu: React.Dispatch<React.SetStateAction<boolean>>;
  showSyntaxHelp: boolean;
  setShowSyntaxHelp: React.Dispatch<React.SetStateAction<boolean>>;
  showViewSettings: boolean;
  setShowViewSettings: React.Dispatch<React.SetStateAction<boolean>>;
  hide: () => void;
  rebuildIndex: () => Promise<void>;
  togglePin: () => Promise<void>;
  startRename: (result: SearchResult) => void;
  handleSelectSort: (field: SortField) => void;
  openResult: (result?: SearchResult) => Promise<void>;
  openFolderResult: (result?: SearchResult) => Promise<void>;
  showFileProperties: (result?: SearchResult) => Promise<void>;
  copyPathResult: (result?: SearchResult) => void;
  exportResultsToCsv: () => void;
  selectCategory: (cat: CategoryFilter) => void;
  categories: CategoryFilter[];
}

export function useSearchKeyboard({
  inputRef,
  isComposing,
  sortedResults,
  selectedIndex,
  setSelectedIndex,
  query,
  renameTarget,
  setRenameTarget,
  contextMenu,
  setContextMenu,
  showSortMenu,
  setShowSortMenu,
  showSyntaxHelp,
  setShowSyntaxHelp,
  showViewSettings,
  setShowViewSettings,
  hide,
  rebuildIndex,
  togglePin,
  startRename,
  handleSelectSort,
  openResult,
  openFolderResult,
  showFileProperties,
  copyPathResult,
  exportResultsToCsv,
  selectCategory,
  categories,
}: UseSearchKeyboardProps) {
  const handleUnifiedKeyDown = useCallback((event: KeyboardEvent<HTMLElement> | globalThis.KeyboardEvent) => {
    const target = event.target as HTMLElement | null;
    const isSearchInput = target === inputRef.current;
    const isOtherInput = target && target !== inputRef.current && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable);

    // 1. Escape 拥有最高全局优先级（逐层退出浮层 -> 最终退出搜索窗口）
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopPropagation();
      if (renameTarget.visible) {
        setRenameTarget({ visible: false, newName: '' });
        inputRef.current?.focus();
      } else if (contextMenu.visible) {
        setContextMenu({ visible: false, x: 0, y: 0 });
        inputRef.current?.focus();
      } else if (showSortMenu) {
        setShowSortMenu(false);
        inputRef.current?.focus();
      } else if (showSyntaxHelp) {
        setShowSyntaxHelp(false);
        inputRef.current?.focus();
      } else if (showViewSettings) {
        setShowViewSettings(false);
        inputRef.current?.focus();
      } else {
        hide();
      }
      return;
    }

    // 2. F5 / Ctrl+R 全局重新构建全盘索引
    if (event.key === 'F5' || (event.ctrlKey && (event.key === 'r' || event.key === 'R'))) {
      event.preventDefault();
      void rebuildIndex();
      return;
    }

    // 3. F1 全局语法帮助
    if (event.key === 'F1') {
      event.preventDefault();
      setShowSyntaxHelp(prev => !prev);
      return;
    }

    // 4. Ctrl+P 全局切换窗口图钉固定状态 (Pin/Unpin)
    if ((event.ctrlKey || event.metaKey) && (event.key === 'p' || event.key === 'P')) {
      event.preventDefault();
      void togglePin();
      return;
    }

    if (isOtherInput) {
      return;
    }

    // 4. F2 全局重命名当前选中项
    if (event.key === 'F2') {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) startRename(current);
      return;
    }

    // 5. Ctrl+Shift+... 排序快捷键
    if (event.ctrlKey && event.shiftKey) {
      const k = event.key.toLowerCase();
      if (k === 'd') { event.preventDefault(); handleSelectSort('modified'); return; }
      if (k === 'n') { event.preventDefault(); handleSelectSort('name'); return; }
      if (k === 's') { event.preventDefault(); handleSelectSort('size'); return; }
      if (k === 'r') { event.preventDefault(); handleSelectSort('relevance'); return; }
    }

    if (event.key === 'ArrowDown') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => (prev + 1) % sortedResults.length);
      }
      return;
    }

    if (event.key === 'ArrowUp') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => (prev - 1 + sortedResults.length) % sortedResults.length);
      }
      return;
    }

    if (event.key === 'PageDown') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => Math.min(sortedResults.length - 1, prev + 10));
      }
      return;
    }

    if (event.key === 'PageUp') {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex((prev) => Math.max(0, prev - 10));
      }
      return;
    }

    if (event.key === 'Home' && !isSearchInput) {
      event.preventDefault();
      setSelectedIndex(0);
      return;
    }

    if (event.key === 'End' && !isSearchInput) {
      event.preventDefault();
      if (sortedResults.length > 0) {
        setSelectedIndex(sortedResults.length - 1);
      }
      return;
    }

    if (event.key === 'Enter' && event.ctrlKey) {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) {
        void openFolderResult(current);
      }
      return;
    }

    if (event.key === 'Enter' && event.altKey) {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) {
        void showFileProperties(current);
      }
      return;
    }

    if (event.key === 'Enter' && !isComposing) {
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) {
        void openResult(current);
      } else if (query.trim() && !query.trim().toLowerCase().startsWith('content:') && !query.trim().startsWith('内容:')) {
        const contentCat = categories.find(c => c.id === 'content') || categories[1];
        if (contentCat) selectCategory(contentCat);
      }
      return;
    }

    if ((event.ctrlKey || event.metaKey) && (event.key === 'c' || event.key === 'C')) {
      if (isSearchInput && window.getSelection()?.toString()) {
        return;
      }
      event.preventDefault();
      const current = sortedResults[selectedIndex];
      if (current) {
        copyPathResult(current);
      }
      return;
    }

    if ((event.ctrlKey || event.metaKey) && (event.key === 'e' || event.key === 'E')) {
      event.preventDefault();
      exportResultsToCsv();
      return;
    }

    if (event.altKey && (event.key === '1' || event.key === '2' || event.key === '3' || event.key === '4' || event.key === '5' || event.key === '6' || event.key === '7' || event.key === '8' || event.key === '9')) {
      event.preventDefault();
      const idx = parseInt(event.key, 10) - 1;
      if (idx < categories.length) {
        selectCategory(categories[idx]);
      }
      return;
    }

    if (!isSearchInput && event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) {
      inputRef.current?.focus();
    }
  }, [
    inputRef, isComposing, sortedResults, selectedIndex, setSelectedIndex, query,
    renameTarget.visible, setRenameTarget, contextMenu.visible, setContextMenu,
    showSortMenu, setShowSortMenu, showSyntaxHelp, setShowSyntaxHelp,
    showViewSettings, setShowViewSettings, hide, rebuildIndex, togglePin,
    startRename, handleSelectSort, openResult, openFolderResult,
    showFileProperties, copyPathResult, exportResultsToCsv, selectCategory, categories
  ]);

  useEffect(() => {
    const handleGlobal = (e: globalThis.KeyboardEvent) => {
      handleUnifiedKeyDown(e);
    };
    window.addEventListener('keydown', handleGlobal, true);
    return () => {
      window.removeEventListener('keydown', handleGlobal, true);
    };
  }, [handleUnifiedKeyDown]);

  return { handleUnifiedKeyDown };
}
