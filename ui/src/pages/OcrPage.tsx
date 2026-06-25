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
import { FileText } from 'lucide-react';
import { HotkeyRecorder } from '../components/HotkeyRecorder';

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
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading')}</div>;
  }

  return (
    <div className="ocr-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('ocr.title')} icon={<FileText size={20} strokeWidth={2.5} />}>
        <Card>
          <SettingRow label={t('ocr.shortcut')} description={t('ocr.shortcutDesc')}>
            <HotkeyRecorder
              id="ocr-shortcut"
              value={settings.shortcut || 'Ctrl+Shift+O'}
              onChange={(v) => {
                updateSetting('shortcut', v);
                bridgeRequest('hotkey.rebind', { name: 'OCR', hotkey: v });
              }}
            />
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
