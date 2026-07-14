import { useState, useEffect } from 'react';
import { Settings, Camera, Video, Pause, LogOut } from 'lucide-react';
import { bridgeRequest } from './hooks/useBridge';
import { useTranslation } from 'react-i18next';
import './TrayApp.css';

export default function TrayApp() {
  const { t } = useTranslation();
  const [gesturePaused, setGesturePaused] = useState(false);

  useEffect(() => {
    // Initial fetch of gesture pause state if needed
    bridgeRequest<boolean>('config.get', { key: '/gesture/paused' })
      .then(val => setGesturePaused(!!val))
      .catch(() => {});
  }, []);

  const handleAction = (action: string) => {
    if (action === 'pauseGesture') {
      const nextState = !gesturePaused;
      setGesturePaused(nextState);
      bridgeRequest('config.set', { key: '/gesture/paused', value: nextState });
    }
    bridgeRequest('tray.action', { action });
  };

  return (
    <div className="tray-menu">
      <div className="tray-menu__item" onClick={() => handleAction('openSettings')}>
        <Settings size={16} />
        <span>{t('tray.settings', 'Settings')}</span>
      </div>
      <div className="tray-menu__divider" />
      <div className="tray-menu__item" onClick={() => handleAction('screenshot')}>
        <Camera size={16} />
        <span>{t('tray.capture', 'Capture')}</span>
      </div>
      <div className="tray-menu__item" onClick={() => handleAction('recording')}>
        <Video size={16} />
        <span>{t('tray.recording', 'Recording')}</span>
      </div>
      <div className="tray-menu__divider" />
      <div className="tray-menu__item" onClick={() => handleAction('pauseGesture')}>
        <Pause size={16} />
        <span>
          {gesturePaused ? t('tray.resumeGesture', 'Resume Gesture') : t('tray.pauseGesture', 'Pause Gesture')}
        </span>
      </div>
      <div className="tray-menu__divider" />
      <div className="tray-menu__item tray-menu__item--danger" onClick={() => handleAction('exit')}>
        <LogOut size={16} />
        <span>{t('tray.exit', 'Exit')}</span>
      </div>
    </div>
  );
}
