import { useState, useEffect, useRef, useLayoutEffect, useCallback } from 'react';
import { Settings, Camera, Video, Search, Shield, ShieldCheck, LogOut, Keyboard, MousePointer } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const menuRef = useRef<HTMLDivElement>(null);
  const [gesturePaused, setGesturePaused] = useState(false);
  const [elevated, setElevated] = useState(false);
  const [busy, setBusy] = useState(false);
  const [activePlugins, setActivePlugins] = useState<Set<string>>(() => new Set(['capture', 'search', 'gesture']));

  // 动态上报真实尺寸给 C++ 宿主窗口（宽度严格锁定 200px，仅高度自适应内容）
  const reportSize = useCallback(() => {
    if (!menuRef.current) return;
    const rect = menuRef.current.getBoundingClientRect();
    // 加上 #root padding (4px * 2 = 8px) 阴影容差
    const totalHeight = Math.ceil(rect.height + 8);
    const fixedWidth = 200;
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
  }, [reportSize, activePlugins, gesturePaused]);

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

  useEffect(() => {
    document.documentElement.dataset.surface = 'tray';
    refreshState();
    return () => { delete document.documentElement.dataset.surface; };
  }, [refreshState]);

  // 键盘快捷导航 (Esc 关闭，方向键切换焦点)
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        void bridgeRequest('tray.hide').catch(() => {});
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

  // 快捷胶囊开关（不收起托盘，即时响应并乐观更新）
  const toggleGesture = async (e: React.MouseEvent) => {
    e.stopPropagation();
    if (!activePlugins.has('gesture')) {
      setActivePlugins((prev) => new Set(prev).add('gesture'));
      setGesturePaused(false);
      await bridgeRequest('plugins.setEnabled', { id: 'gesture', enabled: true }).catch(() => {});
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

  const togglePlugin = async (e: React.MouseEvent, id: string) => {
    e.stopPropagation();
    const willEnable = !activePlugins.has(id);
    setActivePlugins((prev) => {
      const next = new Set(prev);
      if (willEnable) next.add(id);
      else next.delete(id);
      return next;
    });
    try {
      await bridgeRequest('plugins.setEnabled', { id, enabled: willEnable });
    } catch {
      setActivePlugins((prev) => {
        const next = new Set(prev);
        if (willEnable) next.delete(id);
        else next.add(id);
        return next;
      });
    }
  };

  const captureActive = activePlugins.has('capture');
  const searchActive = activePlugins.has('search');
  const gestureEffectiveActive = activePlugins.has('gesture') && !gesturePaused;
  const keycastActive = activePlugins.has('keycast');

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

  return (
    <div ref={menuRef} className="tray-menu" role="menu">
      {/* 顶部 Mini Control Center 快捷胶囊栏 */}
      <div className="tray-control-center" role="group" aria-label={t('tray.quickControls', 'Quick Controls')}>
        <button
          type="button"
          className={`tray-pill ${gestureEffectiveActive ? 'tray-pill--active' : ''}`}
          onClick={(e) => void toggleGesture(e)}
          title={`${t('tray.pillGesture', 'Gestures')}: ${gestureEffectiveActive ? t('tray.enabled', 'Enabled') : t('tray.disabled', 'Disabled')}`}
        >
          <MousePointer size={13} className="tray-pill__icon" />
          <span className="tray-pill__label">{t('tray.pillGesture', 'Gestures')}</span>
          <span className="tray-pill__dot" />
        </button>

        <button
          type="button"
          className={`tray-pill ${keycastActive ? 'tray-pill--active' : ''}`}
          onClick={(e) => void togglePlugin(e, 'keycast')}
          title={`${t('tray.pillKeycast', 'Keycast')}: ${keycastActive ? t('tray.enabled', 'Enabled') : t('tray.disabled', 'Disabled')}`}
        >
          <Keyboard size={13} className="tray-pill__icon" />
          <span className="tray-pill__label">{t('tray.pillKeycast', 'Keycast')}</span>
          <span className="tray-pill__dot" />
        </button>

        <button
          type="button"
          className={`tray-pill ${captureActive ? 'tray-pill--active' : ''}`}
          onClick={(e) => void togglePlugin(e, 'capture')}
          title={`${t('tray.pillCapture', 'Capture')}: ${captureActive ? t('tray.enabled', 'Enabled') : t('tray.disabled', 'Disabled')}`}
        >
          <Camera size={13} className="tray-pill__icon" />
          <span className="tray-pill__label">{t('tray.pillCapture', 'Capture')}</span>
          <span className="tray-pill__dot" />
        </button>

        <button
          type="button"
          className={`tray-pill ${searchActive ? 'tray-pill--active' : ''}`}
          onClick={(e) => void togglePlugin(e, 'search')}
          title={`${t('tray.pillSearch', 'Search')}: ${searchActive ? t('tray.enabled', 'Enabled') : t('tray.disabled', 'Disabled')}`}
        >
          <Search size={13} className="tray-pill__icon" />
          <span className="tray-pill__label">{t('tray.pillSearch', 'Search')}</span>
          <span className="tray-pill__dot" />
        </button>
      </div>

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

      {elevated ? (
        <button
          type="button"
          className="tray-menu__item tray-menu__item--admin-active"
          disabled
          title={t('tray.adminActiveDesc', 'Running with highest privileges')}
        >
          <ShieldCheck size={15} className="tray-menu__icon tray-menu__icon--admin" />
          <span className="tray-menu__label">{t('tray.adminActive', 'Running as Administrator')}</span>
        </button>
      ) : (
        <button
          type="button"
          className="tray-menu__item tray-menu__item--admin"
          disabled={busy}
          onClick={() => void handleAction('restartElevated')}
        >
          <Shield size={15} className="tray-menu__icon tray-menu__icon--admin" />
          <span className="tray-menu__label">{t('tray.restartElevated', 'Run as Administrator')}</span>
        </button>
      )}

      <div className="tray-menu__divider" />

      <button type="button" className="tray-menu__item tray-menu__item--danger" disabled={busy} onClick={() => void handleAction('exit')}>
        <LogOut size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.exit', 'Exit')}</span>
      </button>
    </div>
  );
}
