import { useState, useEffect, useRef, useLayoutEffect, useCallback } from 'react';
import { Settings, Camera, Video, Search, Pause, Play, Shield, LogOut } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const menuRef = useRef<HTMLDivElement>(null);
  const [gesturePaused, setGesturePaused] = useState(false);
  const [busy, setBusy] = useState(false);
  const [activePlugins, setActivePlugins] = useState(() => new Set(['capture', 'search', 'gesture']));

  // 动态上报真实尺寸给 C++ 宿主窗口，杜绝任何空白溢出
  const reportSize = useCallback(() => {
    if (!menuRef.current) return;
    const rect = menuRef.current.getBoundingClientRect();
    // 加上 html #root 边距与阴影容差 (5px * 2 = 10px)
    const totalHeight = Math.ceil(rect.height + 10);
    const totalWidth = Math.ceil(rect.width + 10);
    if (totalHeight > 20 && totalWidth > 20) {
      void bridgeRequest('tray.resize', { width: totalWidth, height: totalHeight }).catch(() => {});
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

  useEffect(() => {
    document.documentElement.dataset.surface = 'tray';
    void bridgeRequest<Array<{ id: string; active: boolean }>>('plugins.getAll')
      .then((plugins) => {
        const active = new Set(plugins.filter((plugin) => plugin.active).map((plugin) => plugin.id));
        setActivePlugins(active);
        if (active.has('gesture')) {
          void bridgeRequest<{ paused: boolean }>('gesture.getState')
            .then(state => setGesturePaused(state.paused))
            .catch(() => {});
        }
      })
      .catch(() => {});
    return () => { delete document.documentElement.dataset.surface; };
  }, []);

  // 键盘快捷导航 (Esc 关闭，上下键切换焦点)
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        e.preventDefault();
        void bridgeRequest('tray.hide').catch(() => {});
        return;
      }
      if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        e.preventDefault();
        const buttons = Array.from(menuRef.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? []);
        if (buttons.length === 0) return;
        const currentIndex = buttons.indexOf(document.activeElement as HTMLButtonElement);
        let nextIndex: number;
        if (currentIndex === -1) {
          nextIndex = e.key === 'ArrowDown' ? 0 : buttons.length - 1;
        } else {
          nextIndex = e.key === 'ArrowDown'
            ? (currentIndex + 1) % buttons.length
            : (currentIndex - 1 + buttons.length) % buttons.length;
        }
        buttons[nextIndex]?.focus();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, []);

  const captureActive = activePlugins.has('capture');
  const searchActive = activePlugins.has('search');
  const gestureActive = activePlugins.has('gesture');

  const handleAction = async (action: string) => {
    if (busy) return;
    const previousPaused = gesturePaused;
    if (action === 'pauseGesture') {
      setGesturePaused(!gesturePaused);
    }
    setBusy(true);
    // 立即乐观收起托盘菜单，带来零延迟即时交互体验
    void bridgeRequest('tray.hide').catch(() => {});
    try {
      const result = await bridgeRequest<{ success: boolean }>('tray.action', { action });
      if (!result.success) throw new Error('Tray action failed');
    } catch (error) {
      console.error(error);
      setGesturePaused(previousPaused);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div ref={menuRef} className="tray-menu" role="menu">
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('openSettings')}>
        <Settings size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.settings', 'Settings')}</span>
      </button>
      {(captureActive || searchActive || gestureActive) && <div className="tray-menu__divider" />}
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
      {((captureActive || searchActive) && gestureActive) && <div className="tray-menu__divider" />}
      {gestureActive && (
        <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('pauseGesture')}>
          {gesturePaused ? <Play size={15} className="tray-menu__icon" /> : <Pause size={15} className="tray-menu__icon" />}
          <span className="tray-menu__label">
            {gesturePaused ? t('tray.resumeGesture', 'Resume Gesture') : t('tray.pauseGesture', 'Pause Gesture')}
          </span>
        </button>
      )}
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('restartElevated')}>
        <Shield size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.restartElevated', 'Restart as Administrator')}</span>
      </button>
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item tray-menu__item--danger" disabled={busy} onClick={() => void handleAction('exit')}>
        <LogOut size={15} className="tray-menu__icon" />
        <span className="tray-menu__label">{t('tray.exit', 'Exit')}</span>
      </button>
    </div>
  );
}
