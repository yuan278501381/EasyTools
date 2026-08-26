/* ─────────────────────────────────────────────────────────────────────────────
 * TitleBar.tsx — 一体化沉浸式无缝标题栏组件 (Unified Frameless TitleBar)
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC, type MouseEvent, useEffect, useState, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { Minus, Square, Copy, X } from 'lucide-react';
import { EasyToolsBolt } from './EasyToolsBolt';
import { bridgeRequest } from '../hooks/useBridge';
import './TitleBar.css';

interface TitleBarProps {
  isElevated?: boolean;
}

export const TitleBar: FC<TitleBarProps> = ({ isElevated = false }) => {
  const { t } = useTranslation();
  const [isMaximized, setIsMaximized] = useState(false);

  useEffect(() => {
    bridgeRequest<{ isMaximized: boolean }>('window.isMaximized')
      .then((res) => {
        if (res && typeof res.isMaximized === 'boolean') {
          setIsMaximized(res.isMaximized);
        }
      })
      .catch(() => {});
  }, []);

  const handleMinimize = useCallback(() => {
    bridgeRequest('window.minimize').catch(console.error);
  }, []);

  const handleToggleMaximize = useCallback(() => {
    bridgeRequest<{ isMaximized?: boolean }>('window.toggleMaximize')
      .then((res) => {
        if (res && typeof res.isMaximized === 'boolean') {
          setIsMaximized(res.isMaximized);
        } else {
          setIsMaximized((prev) => !prev);
        }
      })
      .catch(console.error);
  }, []);

  const handleClose = useCallback(() => {
    bridgeRequest('window.close').catch(console.error);
  }, []);

  const handleMouseDown = useCallback((e: MouseEvent<HTMLElement>) => {
    if (e.button !== 0) return;
    const target = e.target as HTMLElement | null;
    if (target && target.closest('.titlebar__controls')) {
      return;
    }
    bridgeRequest('window.dragMove').catch(console.error);
  }, []);

  const handleDoubleClick = useCallback((e: MouseEvent<HTMLElement>) => {
    const target = e.target as HTMLElement | null;
    if (target && target.closest('.titlebar__controls')) {
      return;
    }
    handleToggleMaximize();
  }, [handleToggleMaximize]);

  return (
    <header className="titlebar" onMouseDown={handleMouseDown} onDoubleClick={handleDoubleClick}>
      {/* ── 左侧品牌标识与标题 ────────────────────────────────────── */}
      <div className="titlebar__brand">
        <span className="titlebar__logo">
          <EasyToolsBolt size={18} fill="var(--primary)" />
        </span>
        <span className="titlebar__title">{t('app.title', 'EasyTools 设置中心')}</span>
        {isElevated && (
          <span className="titlebar__admin-badge" title={t('sidebar.adminTitle', '以系统管理员身份运行')}>
            {t('sidebar.adminBadge', '管理员')}
          </span>
        )}
      </div>

      {/* ── 中间可拖拽区域 ────────────────────────────────────────── */}
      <div className="titlebar__drag-region" />

      {/* ── 右侧窗口控制按钮 ──────────────────────────────────────── */}
      <div className="titlebar__controls">
        <button
          type="button"
          className="titlebar__btn titlebar__btn--minimize"
          onClick={handleMinimize}
          title={t('window.minimize', '最小化')}
          aria-label={t('window.minimize', '最小化')}
        >
          <Minus size={13} strokeWidth={2.2} />
        </button>

        <button
          type="button"
          className="titlebar__btn titlebar__btn--maximize"
          onClick={handleToggleMaximize}
          title={isMaximized ? t('window.restore', '向下还原') : t('window.maximize', '最大化')}
          aria-label={isMaximized ? t('window.restore', '向下还原') : t('window.maximize', '最大化')}
        >
          {isMaximized ? (
            <Copy size={11} strokeWidth={2.2} style={{ transform: 'rotate(90deg)' }} />
          ) : (
            <Square size={11} strokeWidth={2.2} />
          )}
        </button>

        <button
          type="button"
          className="titlebar__btn titlebar__btn--close"
          onClick={handleClose}
          title={t('window.close', '关闭')}
          aria-label={t('window.close', '关闭')}
        >
          <X size={13} strokeWidth={2.2} />
        </button>
      </div>
    </header>
  );
};
