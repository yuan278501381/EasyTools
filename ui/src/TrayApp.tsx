import { useState, useEffect } from 'react';
import { Settings, Camera, Video, Search, Pause, Play, LogOut } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const [gesturePaused, setGesturePaused] = useState(false);
  const [busy, setBusy] = useState(false);
  const [activePlugins, setActivePlugins] = useState(() => new Set(['capture', 'search', 'gesture']));

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
    <div className="tray-menu">
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('openSettings')}>
        <Settings size={16} />
        <span>{t('tray.settings', 'Settings')}</span>
      </button>
      {(captureActive || searchActive || gestureActive) && <div className="tray-menu__divider" />}
      {captureActive && (
        <>
          <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('screenshot')}>
            <Camera size={16} />
            <span>{t('tray.capture', 'Capture')}</span>
          </button>
          <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('recording')}>
            <Video size={16} />
            <span>{t('tray.recording', 'Recording')}</span>
          </button>
        </>
      )}
      {searchActive && (
        <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('search')}>
          <Search size={16} />
          <span>{t('tray.search', 'File Search')}</span>
        </button>
      )}
      {((captureActive || searchActive) && gestureActive) && <div className="tray-menu__divider" />}
      {gestureActive && (
        <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('pauseGesture')}>
          {gesturePaused ? <Play size={16} /> : <Pause size={16} />}
          <span>
            {gesturePaused ? t('tray.resumeGesture', 'Resume Gesture') : t('tray.pauseGesture', 'Pause Gesture')}
          </span>
        </button>
      )}
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item tray-menu__item--danger" disabled={busy} onClick={() => void handleAction('exit')}>
        <LogOut size={16} />
        <span>{t('tray.exit', 'Exit')}</span>
      </button>
    </div>
  );
}
