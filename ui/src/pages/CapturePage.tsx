/* ─────────────────────────────────────────────────────────────────────────────
 * CapturePage — 截图录屏设置页
 *
 * 从 C++ ConfigManager 加载截图/录屏设置，修改后实时 IPC 保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';

interface CaptureSettings {
  format: string;
  quality: number;
  saveToFile: boolean;
  copyToClipboard: boolean;
  showCrosshair: boolean;
  autoDetectWindow: boolean;
}

interface RecordingSettings {
  format: string;
  fps: number;
  bitrate: number;
  includeAudio: boolean;
}

export const CapturePage: FC = () => {
  const [capture, setCapture] = useState<CaptureSettings>({
    format: 'png', quality: 90, saveToFile: true, copyToClipboard: true,
    showCrosshair: true, autoDetectWindow: true,
  });
  const [recording, setRecording] = useState<RecordingSettings>({
    format: 'mp4_h264', fps: 30, bitrate: 8, includeAudio: false,
  });
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    Promise.all([
      bridgeRequest<CaptureSettings>('capture.getSettings'),
      bridgeRequest<RecordingSettings>('recording.getSettings'),
    ]).then(([capData, recData]) => {
      setCapture(prev => ({ ...prev, ...capData }));
      setRecording(prev => ({ ...prev, ...recData }));
      setLoading(false);
    });
  }, []);

  const updateCapture = useCallback((key: keyof CaptureSettings, value: unknown) => {
    setCapture(prev => ({ ...prev, [key]: value }));
    bridgeRequest('capture.updateSettings', { [key]: value });
  }, []);

  const updateRecording = useCallback((key: keyof RecordingSettings, value: unknown) => {
    setRecording(prev => ({ ...prev, [key]: value }));
    bridgeRequest('recording.updateSettings', { [key]: value });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>加载中...</div>;
  }

  return (
    <div className="capture-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="截图设置" icon="📷">
        <Card>
          <SettingRow label="截图快捷键" description="触发截图的全局快捷键">
            <kbd style={{
              padding: '4px 10px', borderRadius: '6px',
              background: 'var(--bg-elevated)', border: '1px solid var(--card-border)',
              fontFamily: 'Consolas, monospace', fontSize: '0.85rem', color: 'var(--text-secondary)'
            }}>Ctrl+Shift+A</kbd>
          </SettingRow>
          <SettingRow label="截图格式" description="截图保存的默认图片格式">
            <Select
              id="capture-format"
              value={capture.format}
              onChange={(v) => updateCapture('format', v)}
              options={[
                { value: 'png', label: 'PNG (无损)' },
                { value: 'jpg', label: 'JPEG (压缩)' },
                { value: 'webp', label: 'WebP (高效)' },
                { value: 'bmp', label: 'BMP (原始)' },
              ]}
            />
          </SettingRow>
          <SettingRow label="图片质量" description="JPEG/WebP 压缩质量 (1-100)">
            <Select
              id="capture-quality"
              value={String(capture.quality)}
              onChange={(v) => updateCapture('quality', parseInt(v))}
              options={[
                { value: '100', label: '100 (最高)' },
                { value: '95', label: '95 (推荐)' },
                { value: '85', label: '85 (平衡)' },
                { value: '70', label: '70 (压缩)' },
              ]}
            />
          </SettingRow>
          <Toggle
            id="capture-copy-clipboard"
            label="复制到剪贴板"
            description="截图完成后自动复制到系统剪贴板"
            checked={capture.copyToClipboard}
            onChange={(v) => updateCapture('copyToClipboard', v)}
          />
          <Toggle
            id="capture-auto-save"
            label="自动保存到文件"
            description="截图完成后自动保存到指定目录"
            checked={capture.saveToFile}
            onChange={(v) => updateCapture('saveToFile', v)}
          />
          <Toggle
            id="capture-crosshair"
            label="显示十字准星"
            description="截图时显示十字辅助线"
            checked={capture.showCrosshair}
            onChange={(v) => updateCapture('showCrosshair', v)}
          />
          <Toggle
            id="capture-detect-window"
            label="自动检测窗口"
            description="悬停时自动高亮窗口边界，点击可直接截取窗口"
            checked={capture.autoDetectWindow}
            onChange={(v) => updateCapture('autoDetectWindow', v)}
          />
        </Card>
      </SettingGroup>

      <SettingGroup title="录屏设置" icon="🎬">
        <Card>
          <SettingRow label="录屏快捷键" description="开始/停止录屏的全局快捷键">
            <kbd style={{
              padding: '4px 10px', borderRadius: '6px',
              background: 'var(--bg-elevated)', border: '1px solid var(--card-border)',
              fontFamily: 'Consolas, monospace', fontSize: '0.85rem', color: 'var(--text-secondary)'
            }}>Ctrl+Shift+R</kbd>
          </SettingRow>
          <SettingRow label="录制格式" description="录屏输出的视频格式">
            <Select
              id="record-format"
              value={recording.format}
              onChange={(v) => updateRecording('format', v)}
              options={[
                { value: 'mp4_h264', label: 'MP4 (H.264)' },
                { value: 'mp4_h265', label: 'MP4 (H.265/HEVC)' },
                { value: 'gif', label: 'GIF (动图)' },
                { value: 'webm_vp9', label: 'WebM (VP9)' },
              ]}
            />
          </SettingRow>
          <SettingRow label="帧率" description="录屏的帧率设置">
            <Select
              id="record-fps"
              value={String(recording.fps)}
              onChange={(v) => updateRecording('fps', parseInt(v))}
              options={[
                { value: '15', label: '15 fps (省空间)' },
                { value: '30', label: '30 fps (推荐)' },
                { value: '60', label: '60 fps (流畅)' },
              ]}
            />
          </SettingRow>
          <SettingRow label="码率" description="视频码率 (Mbps)">
            <Select
              id="record-bitrate"
              value={String(recording.bitrate)}
              onChange={(v) => updateRecording('bitrate', parseInt(v))}
              options={[
                { value: '4', label: '4 Mbps (轻量)' },
                { value: '8', label: '8 Mbps (推荐)' },
                { value: '16', label: '16 Mbps (高清)' },
                { value: '32', label: '32 Mbps (极清)' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};
