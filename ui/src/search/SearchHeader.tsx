import React from 'react';
import { useTranslation } from 'react-i18next';
import {
  Search,
  Pin,
  Move,
  SlidersHorizontal,
  HelpCircle,
} from 'lucide-react';
import { bridgeRequest } from '../hooks/useBridge';
import type { SearchMode } from './searchTypes';

export interface SearchHeaderProps {
  inputRef: React.RefObject<HTMLInputElement | null>;
  query: string;
  onUpdateQuery: (val: string) => void;
  activeCategory: string;
  searchMode: SearchMode;
  loading: boolean;
  isInitialIndexing: boolean;
  isServiceStarting: boolean;
  isPinned: boolean;
  onTogglePin: () => void;
  showViewSettings: boolean;
  onToggleViewSettings: () => void;
  showSyntaxHelp: boolean;
  onToggleSyntaxHelp: () => void;
  onResetPlacement: () => void;
  resultsCount: number;
  hasSelectedResult: boolean;
  selectedIndex: number;
  setIsComposing: (val: boolean) => void;
}

export const SearchHeader: React.FC<SearchHeaderProps> = ({
  inputRef,
  query,
  onUpdateQuery,
  activeCategory,
  searchMode,
  loading,
  isInitialIndexing,
  isServiceStarting,
  isPinned,
  onTogglePin,
  showViewSettings,
  onToggleViewSettings,
  showSyntaxHelp,
  onToggleSyntaxHelp,
  onResetPlacement,
  resultsCount,
  hasSelectedResult,
  selectedIndex,
  setIsComposing,
}) => {
  const { t } = useTranslation();

  return (
    <div 
      className="search-input-wrapper"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) {
          void bridgeRequest('search.startDrag');
        }
      }}
    >
      <Search className="search-icon" size={22} aria-hidden="true" />
      <input
        ref={inputRef}
        autoFocus
        className="search-input"
        placeholder={
          activeCategory === 'content'
            ? t('search.placeholderContent', 'Search document and code full text (Word/Excel/PDF/Code/Text)... [F1 Syntax]')
            : searchMode === 'both'
            ? t('search.placeholderHybrid', 'Hybrid Search: Match both filename and document contents... [F1 Syntax]')
            : searchMode === 'content'
            ? t('search.placeholderContentOnly', 'Full-Text Search: Search inside documents and code... [F1 Syntax]')
            : t('search.placeholder', 'Search filename, wildcard (*.txt), ext:png, or keywords... [Use content: for full-text]')
        }
        value={query}
        onChange={(event) => onUpdateQuery(event.target.value)}
        onCompositionStart={() => setIsComposing(true)}
        onCompositionEnd={(event) => {
          setIsComposing(false);
          onUpdateQuery(event.currentTarget.value);
        }}
        role="combobox"
        aria-expanded={resultsCount > 0}
        aria-controls="search-results"
        aria-activedescendant={hasSelectedResult ? `search-result-${selectedIndex}` : undefined}
        spellCheck={false}
      />
      {(loading || isInitialIndexing || isServiceStarting) && <span className="search-loading" aria-label={t('common.loading', 'Loading...')} />}
      
      <button
        className={`search-help-btn ${isPinned ? 'search-help-btn--pinned' : ''}`}
        onClick={onTogglePin}
        title={isPinned ? t('search.unpinTitle', 'Unpin Window (Ctrl+P · Restore auto-hide on blur)') : t('search.pinTitle', 'Pin Window (Ctrl+P · Keep always on top & never auto-hide)')}
        type="button"
      >
        <Pin size={17} className="search-pin-icon" />
      </button>

      <button
        className="search-help-btn search-drag-btn"
        onMouseDown={() => void bridgeRequest('search.startDrag')}
        onDoubleClick={onResetPlacement}
        title={t('search.dragMoveTitle', 'Hold and drag to move window · Double click to reset center')}
        type="button"
      >
        <Move size={17} />
      </button>

      <button
        className={`search-help-btn ${showViewSettings ? 'search-help-btn--active' : ''}`}
        onClick={onToggleViewSettings}
        title={t('search.viewPrefTitle', 'View and column preferences (Window size, density, columns)')}
        type="button"
      >
        <SlidersHorizontal size={18} />
      </button>

      <button
        className={`search-help-btn ${showSyntaxHelp ? 'search-help-btn--active' : ''}`}
        onClick={onToggleSyntaxHelp}
        title={t('search.syntaxGuideTitle', 'Search syntax and expression guide (F1)')}
        type="button"
      >
        <HelpCircle size={18} />
      </button>
    </div>
  );
};
