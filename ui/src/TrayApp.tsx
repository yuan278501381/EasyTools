import { useState, useEffect, useRef, useLayoutEffect, useCallback } from 'react';
import { Settings, Camera, Video, Search, Shield, ShieldCheck, LogOut, RotateCw, Zap } from 'lucide-react';
import { bridgeRequest, useBridgeEvent } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import { TRAY_CONTROL_REGISTRY, type TrayControlItem } from './tray/trayRegistry';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const menuRef = useRef<HTMLDivElement>(null);
  const [gesturePaused, setGesturePaused] = useState(false);
  const [remoteBoostEnabled, setRemoteBoostEnabled] = useState(true);
  const [elevated, setElevated] = useState(false);
  const [elevating, setElevating] = useState(false);
  const [busy, setBusy] = useState(false);
  const [pendingRestart, setPendingRestart] = useState(false);
  const pendingRestartRef = useRef(false);
  const lastReportedSizeRef = useRef({ width: 0, height: 0 });
  const sizeFrameRef = useRef<number | null>(null);
  const refreshInFlightRef = useRef(false);
  const [activePlugins, setActivePlugins] = useState<Set<string>>(() => new Set([
    'capture', 'search', 'gesture', 'keycast', 'spotlight', 'dialogenhancer', 'dialog_enhancer', 'remote_boost'
  ]));

  // 立即同步直接测量并上报尺寸 (force=true 时绕过重复判定)
  const reportSizeDirect = useCallback((force = false) => {
    if (!menuRef.current) return;
    const el = menuRef.current;
    const rect = el.getBoundingClientRect();
    const borderY = el.offsetHeight - el.clientHeight;
    let totalHeight = Math.ceil(Math.max(rect.height, el.scrollHeight + borderY));

    // 深度双保险：遍历直接子元素，通过最后一个子元素的物理布局位置交叉校验，杜绝任何容器死锁
    const children = Array.from(el.children) as HTMLElement[];
    if (children.length > 0) {
      const lastChild = children[children.length - 1];
      const lastChildRect = lastChild.getBoundingClientRect();
      const style = window.getComputedStyle(el);
      const padBottom = parseFloat(style.paddingBottom) || 0;
      const borderBottom = parseFloat(style.borderBottomWidth) || 0;
      const lastMarginBottom = parseFloat(window.getComputedStyle(lastChild).marginBottom) || 0;
      const fromChildren = Math.ceil(
        (lastChildRect.bottom - rect.top) + el.scrollTop + padBottom + borderBottom + lastMarginBottom
      );
      if (fromChildren > 20) {
        totalHeight = fromChildren;
      }
    }

    const fixedWidth = 220;
    const previous = lastReportedSizeRef.current;
    if (totalHeight <= 20) return;
    if (!force && previous.width === fixedWidth && previous.height === totalHeight) return;
    lastReportedSizeRef.current = { width: fixedWidth, height: totalHeight };
    void bridgeRequest('tray.resize', { width: fixedWidth, height: totalHeight }).catch(() => {
      lastReportedSizeRef.current = { width: 0, height: 0 };
    });
  }, []);

  // ResizeObserver can fire again because the native host accepted the previous
  // resize. Coalesce to one measurement per frame and never echo the same size.
  const reportSize = useCallback(() => {
    if (sizeFrameRef.current !== null) return;
    sizeFrameRef.current = window.requestAnimationFrame(() => {
      sizeFrameRef.current = null;
      reportSizeDirect(false);
    });
  }, [reportSizeDirect]);

  useLayoutEffect(() => {
    reportSize();
    if (!menuRef.current || typeof ResizeObserver === 'undefined') return;
    const ro = new ResizeObserver(() => {
      reportSize();
    });
    ro.observe(menuRef.current);
    return () => {
      ro.disconnect();
      if (sizeFrameRef.current !== null) {
        window.cancelAnimationFrame(sizeFrameRef.current);
        sizeFrameRef.current = null;
      }
    };
  }, [reportSize, activePlugins, remoteBoostEnabled, pendingRestart, elevated]);

  const refreshState = useCallback(() => {
    if (refreshInFlightRef.current) return;
    refreshInFlightRef.current = true;
    const settingsRequest = bridgeRequest<{ elevated?: boolean }>('general.getSettings')
      .then((res) => setElevated(Boolean(res?.elevated)))
      .catch(() => {});
    const remoteRequest = bridgeRequest<{ enabled?: boolean }>('remote.getSettings')
      .then((res) => setRemoteBoostEnabled(Boolean(res?.enabled)))
      .catch(() => {});
    const pluginsRequest = bridgeRequest<Array<{ id: string; active: boolean; enabled?: boolean }>>('plugins.getAll')
      .then((plugins) => {
        const active = new Set(
          plugins
            .filter((plugin) => plugin.enabled !== false && plugin.active)
            .flatMap((plugin) => {
              if (plugin.id === 'dialogenhancer' || plugin.id === 'dialog_enhancer') {
                return ['dialogenhancer', 'dialog_enhancer'];
              }
              if (plugin.id === 'remote_boost' || plugin.id === 'remote') {
                return ['remote_boost', 'remote'];
              }
              return [plugin.id];
            })
        );
        setActivePlugins((previous) => {
          if (previous.size === active.size && [...previous].every((id) => active.has(id))) return previous;
          return active;
        });
        if (active.has('gesture')) {
          return bridgeRequest<{ paused: boolean }>('gesture.getState')
            .then((state) => setGesturePaused(Boolean(state?.paused)))
            .catch(() => {});
        }
      })
      .catch(() => {});
    void Promise.allSettled([settingsRequest, pluginsRequest, remoteRequest]).finally(() => {
      refreshInFlightRef.current = false;
    });
  }, []);

  useBridgeEvent('plugins:changed', (data: unknown) => {
    const payload = data && typeof data === 'object' ? (data as { id?: string; enabled?: boolean }) : null;
    if (!payload?.id) {
      refreshState();
      return;
    }
    const { id, enabled } = payload;
    const aliases = (id === 'dialogenhancer' || id === 'dialog_enhancer')
      ? ['dialogenhancer', 'dialog_enhancer']
      : (id === 'remote_boost' || id === 'remote')
        ? ['remote_boost', 'remote']
        : [id];
    setActivePlugins((prev) => {
      const next = new Set(prev);
      if (enabled) {
        aliases.forEach((k) => next.add(k));
      } else {
        aliases.forEach((k) => next.delete(k));
      }
      return next;
    });
    if (id === 'remote_boost' || id === 'remote') {
      setRemoteBoostEnabled(Boolean(enabled));
    }
  });

  useBridgeEvent('dialog:configChanged', (data: unknown) => {
    const payload = data && typeof data === 'object' ? (data as { enabled?: boolean }) : null;
    if (payload && typeof payload.enabled === 'boolean') {
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (payload.enabled) {
          next.add('dialogenhancer');
          next.add('dialog_enhancer');
        } else {
          next.delete('dialogenhancer');
          next.delete('dialog_enhancer');
        }
        return next;
      });
    }
  });

  useBridgeEvent('keycast:configChanged', (data: unknown) => {
    const payload = data && typeof data === 'object' ? (data as { enabled?: boolean }) : null;
    if (payload && typeof payload.enabled === 'boolean') {
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (payload.enabled) next.add('keycast');
        else next.delete('keycast');
        return next;
      });
    }
  });

  useBridgeEvent('spotlight:stateChanged', (data: unknown) => {
    const payload = data && typeof data === 'object' ? (data as { enabled?: boolean }) : null;
    if (payload && typeof payload.enabled === 'boolean') {
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (payload.enabled) next.add('spotlight');
        else next.delete('spotlight');
        return next;
      });
    }
  });

  useBridgeEvent('remote:stateChanged', (data: unknown) => {
    const payload = data && typeof data === 'object' ? (data as { enabled?: boolean }) : null;
    if (payload && typeof payload.enabled === 'boolean') {
      setRemoteBoostEnabled(payload.enabled);
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (payload.enabled) {
          next.add('remote_boost');
          next.add('remote');
        } else {
          next.delete('remote_boost');
          next.delete('remote');
        }
        return next;
      });
    }
  });

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
      reportSizeDirect(true);
    };
    const handleVisibility = () => {
      if (document.hidden && pendingRestartRef.current) {
        // 用户调整完多个插件后收起托盘，后台静默自动重启生效
        pendingRestartRef.current = false;
        setPendingRestart(false);
        void bridgeRequest('app.restart').catch(() => {});
      }
    };
    window.addEventListener('tray:show', handleShow);
    window.addEventListener('visibilitychange', handleVisibility);
    return () => {
      window.removeEventListener('tray:show', handleShow);
      window.removeEventListener('visibilitychange', handleVisibility);
      delete document.documentElement.dataset.surface;
    };
  }, [refreshState, reportSizeDirect]);

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
      return item.getCustomActive(activePlugins, { gesturePaused, remoteBoostEnabled });
    }
    if (activePlugins.has(item.pluginId)) return true;
    if (item.aliasPluginIds?.some((id) => activePlugins.has(id))) return true;
    return false;
  }, [activePlugins, gesturePaused, remoteBoostEnabled]);

  // 框架化分发点击切换
  const handleToggleItem = async (e: React.MouseEvent, item: TrayControlItem) => {
    e.stopPropagation();
    if (item.id === 'gesture') {
      await toggleGesture(e);
      return;
    }
    if (item.id === 'remote_boost') {
      const next = !remoteBoostEnabled;
      setRemoteBoostEnabled(next);
      setActivePlugins((prev) => {
        const set = new Set(prev);
        if (next) set.add('remote_boost');
        else set.delete('remote_boost');
        return set;
      });
      try {
        await bridgeRequest('remote.updateSettings', { enabled: next });
      } catch {
        setRemoteBoostEnabled(!next);
        setActivePlugins((prev) => {
          const set = new Set(prev);
          if (!next) set.add('remote_boost');
          else set.delete('remote_boost');
          return set;
        });
      }
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
        await bridgeRequest('tray.action', { action: 'restartDemoted' });
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

      {remoteBoostEnabled && (
        <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('emergencyFlush')}>
          <Zap size={15} className="tray-menu__icon" />
          <span className="tray-menu__label">{t('tray.emergencyFlush', 'Emergency Key Flush')}</span>
        </button>
      )}

      <div className="tray-menu__divider" />

      <button
        type="button"
        className={`tray-menu__item ${elevated ? 'tray-menu__item--admin-active' : ''} ${elevating ? 'tray-menu__item--elevating' : ''}`}
        disabled={busy || elevating}
        onClick={() => void handleToggleElevated()}
        title={elevated ? t('tray.adminActiveDesc', 'Running with highest privileges') : t('tray.restartElevated', 'Run as Administrator')}
      >
        {elevated ? (
          <ShieldCheck size={15} className="tray-menu__icon tray-menu__icon--admin" />
        ) : (
          <Shield size={15} className="tray-menu__icon" />
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
