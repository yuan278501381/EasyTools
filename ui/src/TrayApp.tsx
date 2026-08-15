import { useState, useEffect } from 'react';
import { Settings, Camera, Video, Pause, Play, LogOut } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { useAppearance } from './hooks/useAppearance';
import './TrayApp.css';

export default function TrayApp() {
  useAppearance();
  const { t } = useTranslation();
  const [gesturePaused, setGesturePaused] = useState(false);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    bridgeRequest<{ paused: boolean }>('gesture.getState')
      .then(state => setGesturePaused(state.paused))
      .catch(() => {});
  }, []);

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
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('screenshot')}>
        <Camera size={16} />
        <span>{t('tray.capture', 'Capture')}</span>
      </button>
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('recording')}>
        <Video size={16} />
        <span>{t('tray.recording', 'Recording')}</span>
      </button>
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item" disabled={busy} onClick={() => void handleAction('pauseGesture')}>
        {gesturePaused ? <Play size={16} /> : <Pause size={16} />}
        <span>
          {gesturePaused ? t('tray.resumeGesture', 'Resume Gesture') : t('tray.pauseGesture', 'Pause Gesture')}
        </span>
      </button>
      <div className="tray-menu__divider" />
      <button type="button" className="tray-menu__item tray-menu__item--danger" disabled={busy} onClick={() => void handleAction('exit')}>
        <LogOut size={16} />
        <span>{t('tray.exit', 'Exit')}</span>
      </button>
    </div>
  );
}
