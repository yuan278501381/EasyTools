import { useState, useEffect, useRef, useLayoutEffect, useCallback } from 'react';
import { Settings, Camera, Video, Search, Shield, ShieldCheck, LogOut, RotateCw } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import { TRAY_CONTROL_REGISTRY, type TrayControlItem } from './tray/trayRegistry';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const menuRef = useRef<HTMLDivElement>(null);
  const [gesturePaused, setGesturePaused] = useState(false);
  const [elevated, setElevated] = useState(false);
  const [elevating, setElevating] = useState(false);
  const [busy, setBusy] = useState(false);
  const [pendingRestart, setPendingRestart] = useState(false);
  const pendingRestartRef = useRef(false);
  const [activePlugins, setActivePlugins] = useState<Set<string>>(() => new Set([
    'capture', 'search', 'gesture', 'keycast', 'spotlight', 'dialogenhancer', 'dialog_enhancer'
  ]));

    // 动态上报真实尺寸给 C++ 宿主窗口（宽度严格锁定 220px，仅高度自适应内容）
    const reportSize = useCallback(() => {
    if (!menuRef.current) return;
    const rect = menuRef.current.getBoundingClientRect();
    // 加上 #root padding (4px * 2 = 8px) 阴影容差
    const totalHeight = Math.ceil(rect.height + 8);
    const fixedWidth = 220;
    if (totalHeight > 20) {
      void bridgeRequest('tray.resize', { width: fixedWidth, height: totalHeight }).catch(() => {});
    }
  }, []);

  useLayoutEffect(() => {
    reportSize();
    if (!menuRef.current || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(() => {
      reportSize();
    });
    ro.observe(menuRef.current);
    return () => ro.disconnect();
  }, [reportSize, activePlugins, gesturePaused, pendingRestart]);

  const refreshState = useCallback(() => {
    void bridgeRequest<{ elevated?: boolean }>('general.getSettings')
      .then((res) => setElevated(Boolean(res?.elevated)))
      .catch(() => {});
    void bridgeRequest<Array<{ id: string; active: boolean }>>('plugins.getAll')
      .then((plugins) => {
        const active = new Set(plugins.filter((plugin) => plugin.active).map((plugin) => plugin.id));
        setActivePlugins(active);
        if (active.has('gesture')) {
          void bridgeRequest<{ paused: boolean }>('gesture.getState')
            .then((state) => setGesturePaused(Boolean(state?.paused)))
            .catch(() => {});
        }
      })
      .catch(() => {});
  }, []);

  const applyPendingRestart = useCallback(async () => {
    if (!pendingRestartRef.current) return;
    pendingRestartRef.current = false;
    setPendingRestart(false);
    void bridgeRequest('tray.hide').catch(() => {});
    await bridgeRequest('app.restart').catch(() => {});
  }, []);

  useEffect(() => {
    document.documentElement.dataset.surface = 'tray';
    refreshState();
    const handleShow = () => {
      refreshState();
      reportSize();
    };
    const handleVisibility = () => {
      if (document.hidden && pendingRestartRef.current) {
        // 用户调整完多个插件后收起托盘，后台静默自动重启生效
        pendingRestartRef.current = false;
        setPendingRestart(false);
        void bridgeRequest('app.restart').catch(() => {});
      } else {
        handleShow();
      }
    };
    window.addEventListener('tray:show', handleShow);
    window.addEventListener('focus', handleShow);
    window.addEventListener('visibilitychange', handleVisibility);
    return () => {
      window.removeEventListener('tray:show', handleShow);
      window.removeEventListener('focus', handleShow);
      window.removeEventListener('visibilitychange', handleVisibility);
      delete document.documentElement.dataset.surface;
    };
  }, [refreshState, reportSize]);

  // 键盘快捷导航 (Esc 关闭，方向键切换焦点)
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        if (pendingRestartRef.current) {
          pendingRestartRef.current = false;
          void bridgeRequest('app.restart').catch(() => {});
        } else {
          void bridgeRequest('tray.hide').catch(() => {});
        }
        return;
      }
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp' || e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
        e.preventDefault();
        const buttons = Array.from(menuRef.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? []);
        if (buttons.length === 0) return;
        const currentIndex = buttons.indexOf(document.activeElement as HTMLButtonElement);
        let nextIndex: number;
        if (currentIndex === -1) {
          nextIndex = (e.key === 'ArrowDown' || e.key === 'ArrowRight') ? 0 : buttons.length - 1;
        } else {
          const delta = (e.key === 'ArrowDown' || e.key === 'ArrowRight') ? 1 : -1;
          nextIndex = (currentIndex + delta + buttons.length) % buttons.length;
        }
        buttons[nextIndex]?.focus();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, []);

  // 快捷胶囊开关：手势特殊暂停/恢复处理
  const toggleGesture = async (e: React.MouseEvent) => {
    e.stopPropagation();
    if (!activePlugins.has('gesture')) {
      setActivePlugins((prev) => new Set(prev).add('gesture'));
      setGesturePaused(false);
      try {
        const res = await bridgeRequest<{ success: boolean; restartRequired?: boolean }>('plugins.setEnabled', { id: 'gesture', enabled: true });
        if (res?.restartRequired) {
          setPendingRestart(true);
          pendingRestartRef.current = true;
        }
      } catch {
        setActivePlugins((prev) => {
          const next = new Set(prev);
          next.delete('gesture');
          return next;
        });
      }
      return;
    }
    const nextPaused = !gesturePaused;
    setGesturePaused(nextPaused);
    try {
      await bridgeRequest('gesture.updateSettings', { paused: nextPaused });
    } catch {
      setGesturePaused(!nextPaused);
    }
  };

  // 通用插件开关逻辑（含别名同步与乐观回滚机制）
  const togglePlugin = async (e: React.MouseEvent, id: string, aliases?: string[]) => {
    e.stopPropagation();
    const allIds = [id, ...(aliases || [])];
    const currentlyActive = allIds.some((k) => activePlugins.has(k));
    const willEnable = !currentlyActive;

    setActivePlugins((prev) => {
      const next = new Set(prev);
      if (willEnable) {
        allIds.forEach((k) => next.add(k));
      } else {
        allIds.forEach((k) => next.delete(k));
      }
      return next;
    });

    try {
      const res = await bridgeRequest<{ success: boolean; restartRequired?: boolean }>('plugins.setEnabled', { id, enabled: willEnable });
      if (res?.restartRequired) {
        setPendingRestart(true);
        pendingRestartRef.current = true;
      }
    } catch {
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (willEnable) {
          allIds.forEach((k) => next.delete(k));
        } else {
          allIds.forEach((k) => next.add(k));
        }
        return next;
      });
    }
  };

  // 框架化判定某项是否处于活跃状态
  const isItemActive = useCallback((item: TrayControlItem): boolean => {
    if (item.getCustomActive) {
      return item.getCustomActive(activePlugins, { gesturePaused });
    }
    if (activePlugins.has(item.pluginId)) return true;
    if (item.aliasPluginIds?.some((id) => activePlugins.has(id))) return true;
    return false;
  }, [activePlugins, gesturePaused]);

  // 框架化分发点击切换
  const handleToggleItem = async (e: React.MouseEvent, item: TrayControlItem) => {
    e.stopPropagation();
    if (item.id === 'gesture') {
      await toggleGesture(e);
      return;
    }
    await togglePlugin(e, item.pluginId, item.aliasPluginIds);
  };

  const captureActive = activePlugins.has('capture');
  const searchActive = activePlugins.has('search');

  // 触发全局命令动作（立即乐观收起托盘，带来零延迟体验）
  const handleAction = async (action: string) => {
    if (busy) return;
    setBusy(true);
    void bridgeRequest('tray.hide').catch(() => {});
    try {
      const result = await bridgeRequest<{ success: boolean }>('tray.action', { action });
      if (!result.success) throw new Error('Tray action failed');
    } catch (error) {
      console.error(error);
    } finally {
      setBusy(false);
    }
  };

  // 触发管理员提权或降权重启
  const handleToggleElevated = async () => {
    if (busy || elevating) return;
    setElevating(true);
    void bridgeRequest('tray.hide').catch(() => {});
    try {
      if (!elevated) {
        const result = await bridgeRequest<{ success: boolean }>('tray.action', { action: 'restartElevated' });
        if (result && !result.success) {
          setElevating(false);
        }
      } else {
        await bridgeRequest('general.updateSettings', { runAsAdmin: false });
        await bridgeRequest('app.restart');
      }
    } catch {
      setElevating(false);
    }
  };

  return (
    <div ref={menuRef} className="tray-menu" role="menu">
      {/* 顶部 Mini Control Center 快捷胶囊栏 (基于数据驱动注册表自适应网格) */}
      <div className="tray-control-center" role="group" aria-label={t('tray.quickControls', 'Quick Controls')}>
        {TRAY_CONTROL_REGISTRY.map((item) => {
          const active = isItemActive(item);
          const IconComponent = item.icon;
          const label = t(item.labelKey as never, item.fallbackLabel);
          const desc = t(item.descKey as never, item.fallbackDesc);
          const statusText = active ? t('tray.enabled', 'Enabled') : t('tray.disabled', 'Disabled');

          return (
            <button
              key={item.id}
              type="button"
              className={`tray-pill ${active ? 'tray-pill--active' : ''}`}
              onClick={(e) => void handleToggleItem(e, item)}
              title={`${desc}: ${statusText}`}
              aria-label={`${desc}: ${statusText}`}
            >
              <IconComponent size={13} className="tray-pill__icon" />
              <span className="tray-pill__label">{label}</span>
              <span className="tray-pill__dot" />
            </button>
          );
        })}
      </div>

      {/* 待生效智能提示条 (支持多选连续切换，点击立即重启生效或收起托盘后后台自动生效) */}
      {pendingRestart && (
        <button
          type="button"
          className="tray-restart-banner"
          onClick={() => void applyPendingRestart()}
          title={t('tray.restartToApply', 'Plugin settings updated')}
        >
          <span className="tray-restart-banner__info">
            <RotateCw size={12} className="tray-menu__icon--spinning" />
            <span className="tray-restart-banner__text">{t('tray.restartToApply', 'Plugin settings updated')}</span>
          </span>
          <span className="tray-restart-banner__btn">
            {t('tray.restartNow', 'Restart')}
          </span>
        </button>
      )}

      <div className="tray-menu__divider" />

      {/* 核心操作项 */}
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('openSettings')}>
        <Settings size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.settings', 'Settings')}</span>
      </button>

      {captureActive && (
        <>
          <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('screenshot')}>
            <Camera size={15} className="tray-menu__icon" />
            <span className="tray-menu__label">{t('tray.capture', 'Capture')}</span>
          </button>
          <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('recording')}>
            <Video size={15} className="tray-menu__icon" />
            <span className="tray-menu__label">{t('tray.recording', 'Recording')}</span>
          </button>
        </>
      )}

      {searchActive && (
        <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('search')}>
          <Search size={15} className="tray-menu__icon" />
          <span className="tray-menu__label">{t('tray.search', 'File Search')}</span>
        </button>
      )}

      <div className="tray-menu__divider" />

      <button
        type="button"
        className={`tray-menu__item tray-menu__item--admin ${elevated ? 'tray-menu__item--admin-active' : ''} ${elevating ? 'tray-menu__item--elevating' : ''}`}
        disabled={busy || elevating}
        onClick={() => void handleToggleElevated()}
        title={elevated ? t('tray.adminActiveDesc', 'Running with highest privileges') : t('tray.restartElevated', 'Run as Administrator')}
      >
        {elevated ? (
          <ShieldCheck size={15} className="tray-menu__icon tray-menu__icon--admin" />
        ) : (
          <Shield size={15} className="tray-menu__icon tray-menu__icon--admin" />
        )}
        <span className="tray-menu__label">{t('tray.restartElevated', 'Run as Administrator')}</span>
        <span className={`tray-menu__dot ${elevated ? 'tray-menu__dot--active' : ''}`} />
      </button>

      <div className="tray-menu__divider" />

      <button type="button" className="tray-menu__item tray-menu__item--danger" disabled={busy} onClick={() => void handleAction('exit')}>
        <LogOut size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.exit', 'Exit')}</span>
      </button>
    </div>
  );
}
