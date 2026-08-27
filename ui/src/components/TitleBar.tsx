/* ─────────────────────────────────────────────────────────────────────────────
 * TitleBar.tsx — 一体化沉浸式无缝标题栏组件 (Unified Frameless TitleBar)
 *
 * 世界级桌面端窗口交互标准:
 * 1. 标题栏双击极速切换最大化 / 向下还原 (双击防模态吞没保护)
 * 2. 标题栏按住平滑拖拽移动与 Windows Aero Snap 贴靠支持
 * 3. 标题栏右键呼出 Windows 原生系统窗口菜单 (System Menu)
 * 4. 全链路监听 Win32 WM_SIZE 最大化状态事件，保障 Win+Up/Down 与分屏双向联动
 * ───────────────────────────────────────────────────────────────────────────── */

import { type FC, type MouseEvent, useEffect, useState, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { Minus, Square, Copy, X } from 'lucide-react';
import { EasyToolsBolt } from './EasyToolsBolt';
import { bridgeRequest, useBridgeEvent } from '../hooks/useBridge';
import './TitleBar.css';

interface TitleBarProps {
  isElevated?: boolean;
}

export const TitleBar: FC<TitleBarProps> = ({ isElevated = false }) => {
  const { t } = useTranslation();
  const [isMaximized, setIsMaximized] = useState(false);

  // 初始化拉取当前窗口状态
  useEffect(() => {
    bridgeRequest<{ isMaximized: boolean }>('window.isMaximized')
      .then((res) => {
        if (res && typeof res.isMaximized === 'boolean') {
          setIsMaximized(res.isMaximized);
        }
      })
      .catch(() => {});
  }, []);

  // 监听 C++ 原生推送的最大化状态变化（覆盖 Win+方向键、Aero Snap 贴边、系统菜单等全部路径）
  useBridgeEvent('window:maximizedChanged', (data: unknown) => {
    if (data && typeof data === 'object' && 'isMaximized' in data) {
      setIsMaximized(Boolean((data as { isMaximized: boolean }).isMaximized));
    }
  });

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

  // 标题栏按下：区分单击拖拽与双击切换最大化
  const handleMouseDown = useCallback((e: MouseEvent<HTMLElement>) => {
    if (e.button !== 0) return;
    const target = e.target as HTMLElement | null;
    if (target && target.closest('.titlebar__controls, button, a, input, [role="button"]')) {
      return;
    }

    // 核心防御：当检测到双击（e.detail === 2）时，直接触发最大化切换，
    // 绝不进入 Win32 SC_MOVE 模态拖拽循环，彻底根治双击被系统拖拽吞没的问题！
    if (e.detail === 2) {
      handleToggleMaximize();
      return;
    }

    if (e.detail === 1) {
      bridgeRequest('window.dragMove').catch(console.error);
    }
  }, [handleToggleMaximize]);

  // 双击事件兜底
  const handleDoubleClick = useCallback((e: MouseEvent<HTMLElement>) => {
    const target = e.target as HTMLElement | null;
    if (target && target.closest('.titlebar__controls, button, a, input, [role="button"]')) {
      return;
    }
    handleToggleMaximize();
  }, [handleToggleMaximize]);

  // 标题栏右键弹出 Windows 原生系统窗口菜单
  const handleContextMenu = useCallback((e: MouseEvent<HTMLElement>) => {
    const target = e.target as HTMLElement | null;
    if (target && target.closest('.titlebar__controls')) {
      return;
    }
    e.preventDefault();
    bridgeRequest('window.showSystemMenu', { screenX: e.screenX, screenY: e.screenY }).catch(console.error);
  }, []);

  return (
    <header
      className="titlebar"
      onMouseDown={handleMouseDown}
      onDoubleClick={handleDoubleClick}
      onContextMenu={handleContextMenu}
    >
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
