import React from 'react';
import { useTranslation } from 'react-i18next';
import {
  Zap,
  Clock,
  ArrowDownAZ,
  HardDrive,
  Calendar,
  ChevronDown,
  Check,
  Folder,
  Tag,
} from 'lucide-react';
import type { SortDirection, SortField } from './searchTypes';

export interface SearchSortDropdownProps {
  sortDropdownRef: React.RefObject<HTMLDivElement | null>;
  showSortMenu: boolean;
  onToggleSortMenu: () => void;
  sortField: SortField;
  sortDirection: SortDirection;
  onToggleSortDirection: () => void;
  onSetSortDirect: (field: SortField, dir: SortDirection) => void;
  foldersFirst: boolean;
  onToggleFoldersFirst: () => void;
  groupByType: boolean;
  onToggleGroupByType: () => void;
}

export const SearchSortDropdown: React.FC<SearchSortDropdownProps> = ({
  sortDropdownRef,
  showSortMenu,
  onToggleSortMenu,
  sortField,
  sortDirection,
  onToggleSortDirection,
  onSetSortDirect,
  foldersFirst,
  onToggleFoldersFirst,
  groupByType,
  onToggleGroupByType,
}) => {
  const { t } = useTranslation();

  return (
    <div className="search-sort-wrapper" ref={sortDropdownRef}>
      <div className={`sort-split-pill ${sortField !== 'relevance' ? 'sort-split-pill--active' : ''}`}>
        <button
          type="button"
          className="sort-split-main"
          onClick={onToggleSortMenu}
          title={t('search.sortTitle', 'Sort By (Ctrl+Shift+D Date / N Name / S Size / R Relevance)')}
        >
          {sortField === 'modified' ? <Clock size={12} /> :
           sortField === 'name' ? <ArrowDownAZ size={12} /> :
           sortField === 'size' ? <HardDrive size={12} /> :
           sortField === 'created' ? <Calendar size={12} /> :
           <Zap size={12} />}
          <span>
            {sortField === 'relevance' ? t('search.sortRelevance', 'Smart Relevance') :
             sortField === 'modified' ? t('search.sortModified', 'Modified Time') :
             sortField === 'name' ? t('search.sortName', 'File Name') :
             sortField === 'size' ? t('search.sortSize', 'File Size') :
             t('search.sortCreated', 'Created Time')}
          </span>
          <ChevronDown size={11} className={`sort-chevron ${showSortMenu ? 'sort-chevron--open' : ''}`} />
        </button>
        {sortField !== 'relevance' && (
          <button
            type="button"
            className="sort-split-dir-toggle"
            onClick={onToggleSortDirection}
            title={t('search.sortDirectionTip', 'Currently sorted in {{dir}}, click to toggle', { dir: sortDirection === 'desc' ? t('search.sortDescName', 'descending (newest / largest first)') : t('search.sortAscName', 'ascending (oldest / smallest first)') })}
          >
            <span className="sort-dir-icon">{sortDirection === 'desc' ? '↓' : '↑'}</span>
          </button>
        )}
      </div>

      {showSortMenu && (
        <div className="sort-dropdown-menu">
          <div className="sort-menu-header">
            <span>{t('search.sortMenuTitle', 'Sort Column & Direction')}</span>
          </div>
          <div className="sort-menu-items-group">
            {/* 智能相关度 */}
            <div
              className={`sort-menu-row ${sortField === 'relevance' ? 'sort-menu-row--active' : ''}`}
              onClick={() => onSetSortDirect('relevance', 'desc')}
            >
              <div className="sort-row-left">
                <Zap size={13} />
                <span>{t('search.sortRelevance', 'Relevance')}</span>
              </div>
              {sortField === 'relevance' && <Check size={12} className="sort-active-check" />}
            </div>

            {/* 修改时间 */}
            <div className={`sort-menu-row ${sortField === 'modified' ? 'sort-menu-row--active' : ''}`}>
              <div className="sort-row-left" onClick={() => onSetSortDirect('modified', sortDirection)}>
                <Clock size={13} />
                <span>{t('search.colModified', 'Modified Time')}</span>
              </div>
              <div className="sort-dir-subpills">
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('modified', 'desc'); }}
                  title={t('search.sortNewest', 'Newest to oldest')}
                >
                  {t('search.sortNewestFirst', 'Newest First ↓')}
                </button>
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'modified' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('modified', 'asc'); }}
                  title={t('search.sortOldest', 'Oldest to newest')}
                >
                  {t('search.sortOldestFirst', 'Oldest First ↑')}
                </button>
              </div>
            </div>

            {/* 文件名 */}
            <div className={`sort-menu-row ${sortField === 'name' ? 'sort-menu-row--active' : ''}`}>
              <div className="sort-row-left" onClick={() => onSetSortDirect('name', sortDirection)}>
                <ArrowDownAZ size={13} />
                <span>{t('search.sortName', 'Filename')}</span>
              </div>
              <div className="sort-dir-subpills">
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('name', 'asc'); }}
                  title={t('search.sortAz', 'A to Z')}
                >
                  A→Z ↓
                </button>
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'name' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('name', 'desc'); }}
                  title={t('search.sortZa', 'Z to A')}
                >
                  Z→A ↑
                </button>
              </div>
            </div>

            {/* 文件大小 */}
            <div className={`sort-menu-row ${sortField === 'size' ? 'sort-menu-row--active' : ''}`}>
              <div className="sort-row-left" onClick={() => onSetSortDirect('size', sortDirection)}>
                <HardDrive size={13} />
                <span>{t('search.colSize', 'Size')}</span>
              </div>
              <div className="sort-dir-subpills">
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('size', 'desc'); }}
                  title={t('search.sortLargest', 'Largest first')}
                >
                  {t('search.sortLargestFirst', 'Largest First ↓')}
                </button>
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'size' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('size', 'asc'); }}
                  title={t('search.sortSmallest', 'Smallest first')}
                >
                  {t('search.sortSmallestFirst', 'Smallest First ↑')}
                </button>
              </div>
            </div>

            {/* 创建时间 */}
            <div className={`sort-menu-row ${sortField === 'created' ? 'sort-menu-row--active' : ''}`}>
              <div className="sort-row-left" onClick={() => onSetSortDirect('created', sortDirection)}>
                <Calendar size={13} />
                <span>{t('search.colCreated', 'Created Time')}</span>
              </div>
              <div className="sort-dir-subpills">
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'desc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('created', 'desc'); }}
                  title={t('search.sortNewest', 'Newest to oldest')}
                >
                  {t('search.sortNewestFirst', 'Newest First ↓')}
                </button>
                <button
                  type="button"
                  className={`sort-dir-subpill ${sortField === 'created' && sortDirection === 'asc' ? 'sort-dir-subpill--active' : ''}`}
                  onClick={(e) => { e.stopPropagation(); onSetSortDirect('created', 'asc'); }}
                  title={t('search.sortOldest', 'Oldest to newest')}
                >
                  {t('search.sortOldestFirst', 'Oldest First ↑')}
                </button>
              </div>
            </div>
          </div>

          <div className="sort-menu-divider" />

          {/* 多维组合排序微开关 */}
          <div className="sort-composite-section">
            <div className="sort-composite-header">
              <span>{t('search.sortComboRules', 'Combined Sorting Rules')}</span>
            </div>
            <label className="sort-composite-option">
              <input
                type="checkbox"
                checked={foldersFirst}
                onChange={onToggleFoldersFirst}
              />
              <Folder size={13} className="sort-option-icon" />
              <span>{t('search.foldersFirst', 'Folders Always On Top')}</span>
            </label>
            <label className="sort-composite-option">
              <input
                type="checkbox"
                checked={groupByType}
                onChange={onToggleGroupByType}
              />
              <Tag size={13} className="sort-option-icon" />
              <span>{t('search.groupByType', 'Group by File Extension/Type')}</span>
            </label>
          </div>
        </div>
      )}
    </div>
  );
};
