/* ─────────────────────────────────────────────────────────────────────────────
 * CapturePage — 截图录屏设置页
 *
 * 从 C++ ConfigManager 加载截图/录屏设置，修改后实时 IPC 保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, TextInput, Select, Button } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { useTranslation } from 'react-i18next';
import { Camera, Video } from 'lucide-react';
import { HotkeyRecorder } from '../components/HotkeyRecorder';
import { toast } from 'sonner';

interface CaptureSettings {
  format: string;
  quality: number;
  saveToFile: boolean;
  copyToClipboard: boolean;
  showCrosshair: boolean;
  autoDetectWindow: boolean;
  shortcut?: string;
  saveDirectory?: string;
}

interface RecordingSettings {
  format: string;
  fps: number;
  bitrate: number;
  saveDirectory?: string;
}

interface OperationResult { success: boolean; error?: string; shortcut?: string }
interface HotkeyEntry { name: string; shortcut: string }

export const CapturePage: FC = () => {
  const { t } = useTranslation();
  const [capture, setCapture] = useState<CaptureSettings>({
    format: 'png', quality: 90, saveToFile: true, copyToClipboard: true,
    showCrosshair: true, autoDetectWindow: true,
  });
  const [recording, setRecording] = useState<RecordingSettings>({
    format: 'mp4_h264', fps: 30, bitrate: 8,
  });
  const [loading, setLoading] = useState(true);
  const [screenshotHotkey, setScreenshotHotkey] = useState('Ctrl+Shift+A');
  const [recordHotkey, setRecordHotkey] = useState('Ctrl+Shift+R');

  useEffect(() => {
    Promise.all([
      bridgeRequest<CaptureSettings>('capture.getSettings'),
      bridgeRequest<RecordingSettings>('recording.getSettings'),
      bridgeRequest<HotkeyEntry[]>('hotkey.getAll'),
    ]).then(([capData, recData, hotkeyData]) => {
      setCapture(prev => ({ ...prev, ...capData }));
      setRecording(prev => ({ ...prev, ...recData }));
      setScreenshotHotkey(hotkeyData.find(item => item.name === 'Screenshot')?.shortcut || 'Ctrl+Shift+A');
      setRecordHotkey(hotkeyData.find(item => item.name === 'Record')?.shortcut || 'Ctrl+Shift+R');
    }).catch((error) => {
      console.error(error);
      toast.error(t('capture.loadFailed'));
    }).finally(() => setLoading(false));
  }, [t]);

  const updateCapture = useCallback((key: keyof CaptureSettings, value: unknown) => {
    setCapture(prev => ({ ...prev, [key]: value }));
    bridgeRequest<OperationResult>('capture.updateSettings', { [key]: value })
      .then(result => { if (!result.success) throw new Error(result.error || 'update failed'); })
      .catch(async (error) => {
        toast.error(t('capture.saveFailed'), { description: String(error) });
        try { setCapture(await bridgeRequest<CaptureSettings>('capture.getSettings')); } catch { /* keep usable */ }
      });
  }, [t]);

  const updateRecording = useCallback((key: keyof RecordingSettings, value: unknown) => {
    setRecording(prev => ({ ...prev, [key]: value }));
    bridgeRequest<OperationResult>('recording.updateSettings', { [key]: value })
      .then(result => { if (!result.success) throw new Error(result.error || 'update failed'); })
      .catch(async (error) => {
        toast.error(t('capture.saveFailed'), { description: String(error) });
        try { setRecording(await bridgeRequest<RecordingSettings>('recording.getSettings')); } catch { /* keep usable */ }
      });
  }, [t]);

  const rebindHotkey = async (name: 'Screenshot' | 'Record', value: string) => {
    try {
      const result = await bridgeRequest<OperationResult>('hotkey.rebind', { name, hotkey: value });
      if (!result.success) throw new Error(result.error || t('hotkey.bindFailed'));
      if (name === 'Screenshot') setScreenshotHotkey(result.shortcut || value);
      else setRecordHotkey(result.shortcut || value);
    } catch (error) {
      toast.error(t('hotkey.bindFailed'), { description: String(error) });
    }
  };

  const handleBrowseDir = async () => {
    try {
      const dir = await bridgeRequest<string | null>('capture.browseDirectory');
      if (dir) updateCapture('saveDirectory', dir);
    } catch (error) {
      toast.error(t('capture.browseFailed'), { description: String(error) });
    }
  };

  const handleBrowseRecordingDir = async () => {
    try {
      const dir = await bridgeRequest<string | null>('capture.browseDirectory');
      if (dir) updateRecording('saveDirectory', dir);
    } catch (error) {
      toast.error(t('capture.browseFailed'), { description: String(error) });
    }
  };

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading')}</div>;
  }

  return (
    <div className="capture-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('capture.title')} icon={<Camera size={20} strokeWidth={2.5} />}>
        <Card>
          <SettingRow label={t('capture.shortcut')} description={t('capture.shortcutDesc')}>
            <HotkeyRecorder
              id="capture-shortcut"
              value={screenshotHotkey}
              onChange={(v) => void rebindHotkey('Screenshot', v)}
            />
          </SettingRow>
          <Toggle
            id="capture-copy-clipboard"
            label={t('capture.copyToClipboard')}
            description={t('capture.copyToClipboardDesc')}
            checked={capture.copyToClipboard}
            onChange={(v) => updateCapture('copyToClipboard', v)}
          />
          <Toggle
            id="capture-auto-save"
            label={t('capture.saveToFile')}
            description={t('capture.saveToFileDesc')}
            checked={capture.saveToFile}
            onChange={(v) => updateCapture('saveToFile', v)}
          />
          {capture.saveToFile && (
            <SettingRow label={t('capture.saveDir')} description={t('capture.saveDirDesc')}>
              <div style={{ display: 'flex', gap: '8px' }}>
                <div>
                  <TextInput
                    id="saveDir"
                    value={capture.saveDirectory || ''}
                    disabled
                    onChange={() => {}}
                  />
                </div>
                <Button variant="ghost" onClick={handleBrowseDir}>{t('capture.browse')}</Button>
              </div>
            </SettingRow>
          )}
          <SettingRow label={t('capture.imageFormat')} description={t('capture.imageFormatDesc')}>
            <Select
              id="capture-format"
              value={capture.format}
              onChange={(v) => updateCapture('format', v)}
              options={[
                { value: 'png', label: t('capture.formatPng') },
                { value: 'jpg', label: t('capture.formatJpg') },
                { value: 'webp', label: t('capture.formatWebp') },
                { value: 'bmp', label: t('capture.formatBmp') },
              ]}
            />
          </SettingRow>
          {(capture.format === 'jpg' || capture.format === 'jpeg' || capture.format === 'webp') && (
            <SettingRow label={t('capture.quality')} description={t('capture.qualityDesc')}>
              <Select
                id="capture-quality"
                value={String(capture.quality)}
                onChange={(v) => updateCapture('quality', parseInt(v))}
                options={[75, 85, 90, 95, 100].map((value) => ({
                  value: String(value), label: `${value}%`,
                }))}
              />
            </SettingRow>
          )}
          <Toggle
            id="capture-crosshair"
            label={t('capture.showCrosshair')}
            description={t('capture.showCrosshairDesc')}
            checked={capture.showCrosshair}
            onChange={(v) => updateCapture('showCrosshair', v)}
          />
          <Toggle
            id="capture-detect-window"
            label={t('capture.autoDetectWindow')}
            description={t('capture.autoDetectWindowDesc')}
            checked={capture.autoDetectWindow}
            onChange={(v) => updateCapture('autoDetectWindow', v)}
          />
        </Card>
      </SettingGroup>

      <SettingGroup title={t('recording.title')} icon={<Video size={20} strokeWidth={2.5} />}>
        <Card>
          <SettingRow label={t('recording.shortcut')} description={t('recording.shortcutDesc')}>
            <HotkeyRecorder
              id="recording-shortcut"
              value={recordHotkey}
              onChange={(v) => void rebindHotkey('Record', v)}
            />
          </SettingRow>
          <SettingRow label={t('recording.saveDir')} description={t('recording.saveDirDesc')}>
            <div style={{ display: 'flex', gap: '8px' }}>
              <TextInput id="recordSaveDir" value={recording.saveDirectory || ''} disabled onChange={() => {}} />
              <Button variant="ghost" onClick={handleBrowseRecordingDir}>{t('capture.browse')}</Button>
            </div>
          </SettingRow>
          <SettingRow label={t('recording.format')} description={t('recording.formatDesc')}>
            <Select
              id="record-format"
              value={recording.format}
              onChange={(v) => updateRecording('format', v)}
              options={[
                { value: 'mp4_h264', label: t('recording.formatMp4H264') },
                { value: 'mp4_h265', label: t('recording.formatMp4H265') },
                { value: 'gif', label: t('recording.formatGif') },
                { value: 'webm_vp9', label: t('recording.formatWebmVp9') },
              ]}
            />
          </SettingRow>
          <SettingRow label={t('recording.fps')} description={t('recording.fpsDesc')}>
            <Select
              id="record-fps"
              value={String(recording.fps)}
              onChange={(v) => updateRecording('fps', parseInt(v))}
              options={[
                { value: '15', label: t('recording.fps15') },
                { value: '30', label: t('recording.fps30') },
                { value: '60', label: t('recording.fps60') },
              ]}
            />
          </SettingRow>
          <SettingRow label={t('recording.bitrate')} description={t('recording.bitrateDesc')}>
            <Select
              id="record-bitrate"
              value={String(recording.bitrate)}
              onChange={(v) => updateRecording('bitrate', parseInt(v))}
              options={[
                { value: '4', label: t('recording.bitrate4') },
                { value: '8', label: t('recording.bitrate8') },
                { value: '16', label: t('recording.bitrate16') },
                { value: '32', label: t('recording.bitrate32') },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};
