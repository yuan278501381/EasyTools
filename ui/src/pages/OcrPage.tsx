/* ─────────────────────────────────────────────────────────────────────────────
 * OcrPage — OCR 识别设置页
 *
 * 引擎: Windows.Media.Ocr (系统内置、离线)。
 * 从 C++ ConfigManager 加载设置，修改后实时 IPC 保存；并展示引擎可用性。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';

interface OcrSettings {
  engine: string;
  language: string;
  autoOcr: boolean;
  copyResult: boolean;
  shortcut: string;
  copyToClipboard: boolean;
  showResultWindow: boolean;
}

export const OcrPage: FC = () => {
  const [settings, setSettings] = useState<OcrSettings>({
    engine: 'windows',
    language: 'auto',
    autoOcr: false,
    copyResult: true,
    shortcut: 'Ctrl+Shift+O',
    copyToClipboard: true,
    showResultWindow: true,
  });
  const [loading, setLoading] = useState(true);
  const { t } = useTranslation();

  useEffect(() => {
    bridgeRequest<OcrSettings>('ocr.getSettings').then(data => {
      setSettings(prev => ({ ...prev, ...data }));
      setLoading(false);
    });
  }, []);

  const updateSetting = useCallback((key: keyof OcrSettings, value: unknown) => {
    setSettings(prev => ({ ...prev, [key]: value }));
    bridgeRequest('ocr.updateSettings', { [key]: value });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>加载中...</div>;
  }

  return (
    <div className="ocr-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('ocr.title')} icon="📝">
        <Card>
          <SettingRow label={t('ocr.shortcut')} description={t('ocr.shortcutDesc')}>
            <kbd style={{
              padding: '4px 10px', borderRadius: '6px',
              background: 'var(--bg-elevated)', border: '1px solid var(--card-border)',
              fontFamily: 'Consolas, monospace', fontSize: '0.85rem', color: 'var(--text-secondary)'
            }}>Ctrl+Shift+O</kbd>
          </SettingRow>
          
          <SettingRow label={t('ocr.engine')} description={t('ocr.engineDesc')}>
            <Select
              id="engine"
              value={settings.engine}
              onChange={(v) => updateSetting('engine', v)}
              options={[
                { value: 'tesseract', label: t('ocr.engineTesseract') },
                { value: 'windows', label: t('ocr.engineWindowsOCR') },
              ]}
            />
          </SettingRow>

          <Toggle
            id="copyToClipboard"
            label={t('ocr.copyToClipboard')}
            description={t('ocr.copyToClipboardDesc')}
            checked={settings.copyToClipboard}
            onChange={(v) => updateSetting('copyToClipboard', v)}
          />

          <Toggle
            id="showResultWindow"
            label={t('ocr.showResultWindow')}
            description={t('ocr.showResultWindowDesc')}
            checked={settings.showResultWindow}
            onChange={(v) => updateSetting('showResultWindow', v)}
          />
        </Card>
      </SettingGroup>
    </div>
  );
};
