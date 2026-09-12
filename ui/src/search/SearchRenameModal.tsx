import React, { useRef, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Pencil, X, Folder } from 'lucide-react';
import { CodeBadge } from '../components/UIKit';
import { extractParentFolder } from './searchUtils';
import type { SearchResult } from './searchTypes';

export interface SearchRenameModalProps {
  renameTarget: {
    visible: boolean;
    result?: SearchResult;
    newName: string;
  };
  onClose: () => void;
  onChangeName: (name: string) => void;
  onConfirm: () => void;
}

export const SearchRenameModal: React.FC<SearchRenameModalProps> = ({
  renameTarget,
  onClose,
  onChangeName,
  onConfirm,
}) => {
  const { t } = useTranslation();
  const renameInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (renameTarget.visible) {
      setTimeout(() => {
        renameInputRef.current?.focus();
        renameInputRef.current?.select();
      }, 50);
    }
  }, [renameTarget.visible]);

  if (!renameTarget.visible || !renameTarget.result) {
    return null;
  }

  const res = renameTarget.result;

  return (
    <div className="search-modal-overlay" onClick={onClose}>
      <div
        className="search-rename-card"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="search-rename-header">
          <div className="search-rename-title">
            <Pencil size={15} className="search-rename-icon" />
            <span>{t('search.renameTitle', 'Rename')} {res.isDirectory ? t('search.folderLabel', 'Folder') : t('search.fileLabel', 'File')}</span>
          </div>
          <button
            type="button"
            className="search-rename-close"
            onClick={onClose}
          >
            <X size={14} />
          </button>
        </div>

        <div className="search-rename-body">
          <div className="search-rename-path-hint" title={res.path}>
            <Folder size={12} />
            <CodeBadge variant="muted">{extractParentFolder(res.path) || res.path}</CodeBadge>
          </div>

          <div className="search-rename-input-wrap">
            <input
              ref={renameInputRef}
              type="text"
              className="search-rename-input"
              value={renameTarget.newName}
              onChange={(e) => onChangeName(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault();
                  onConfirm();
                } else if (e.key === 'Escape') {
                  e.preventDefault();
                  onClose();
                }
              }}
              placeholder={t('search.renamePlaceholder', 'Enter new name...')}
              spellCheck={false}
            />
          </div>
        </div>

        <div className="search-rename-footer">
          <button
            type="button"
            className="search-rename-btn search-rename-btn--cancel"
            onClick={onClose}
          >
            {t('common.cancel', 'Cancel')}
          </button>
          <button
            type="button"
            className="search-rename-btn search-rename-btn--confirm"
            onClick={onConfirm}
            disabled={!renameTarget.newName.trim() || renameTarget.newName.trim() === res.name}
          >
            {t('search.confirmRename', 'Confirm Rename')}
          </button>
        </div>
      </div>
    </div>
  );
};
