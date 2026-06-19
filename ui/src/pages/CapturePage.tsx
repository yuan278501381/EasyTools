/* ─────────────────────────────────────────────────────────────────────────────
 * CapturePage — 截图录屏设置页
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';

export const CapturePage: FC = () => {
  const [captureHotkey] = useState('Ctrl+Shift+A');
  const [recordHotkey] = useState('Ctrl+Shift+R');
  const [format, setFormat] = useState('png');
  const [quality, setQuality] = useState('95');
  const [autoNumber, setAutoNumber] = useState(false);
  const [autoSave, setAutoSave] = useState(false);
  const [recordFormat, setRecordFormat] = useState('mp4');
  const [recordFps, setRecordFps] = useState('30');

  return (
    <div className="capture-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="截图设置" icon="📷">
        <Card>
          <SettingRow label="截图快捷键" description="触发截图的全局快捷键">
            <kbd style={{
              padding: '4px 10px', borderRadius: '6px',
              background: 'var(--bg-elevated)', border: '1px solid var(--card-border)',
              fontFamily: 'Consolas, monospace', fontSize: '0.85rem', color: 'var(--text-secondary)'
            }}>{captureHotkey}</kbd>
          </SettingRow>
          <SettingRow label="截图格式" description="截图保存的默认图片格式">
            <Select
              id="capture-format"
              value={format}
              onChange={setFormat}
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
              value={quality}
              onChange={setQuality}
              options={[
                { value: '100', label: '100 (最高)' },
                { value: '95', label: '95 (推荐)' },
                { value: '85', label: '85 (平衡)' },
                { value: '70', label: '70 (压缩)' },
              ]}
            />
          </SettingRow>
          <Toggle
            id="capture-auto-number"
            label="截图自动编号"
            description="截图标注时自动在每个标记上添加递增序号"
            checked={autoNumber}
            onChange={setAutoNumber}
          />
          <Toggle
            id="capture-auto-save"
            label="自动保存到文件"
            description="截图完成后自动保存到指定目录（而非仅复制到剪贴板）"
            checked={autoSave}
            onChange={setAutoSave}
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
            }}>{recordHotkey}</kbd>
          </SettingRow>
          <SettingRow label="录制格式" description="录屏输出的视频格式">
            <Select
              id="record-format"
              value={recordFormat}
              onChange={setRecordFormat}
              options={[
                { value: 'mp4', label: 'MP4 (H.264)' },
                { value: 'mp4-h265', label: 'MP4 (H.265/HEVC)' },
                { value: 'gif', label: 'GIF (动图)' },
                { value: 'webm', label: 'WebM (VP9)' },
              ]}
            />
          </SettingRow>
          <SettingRow label="帧率" description="录屏的帧率设置">
            <Select
              id="record-fps"
              value={recordFps}
              onChange={setRecordFps}
              options={[
                { value: '15', label: '15 fps (省空间)' },
                { value: '30', label: '30 fps (推荐)' },
                { value: '60', label: '60 fps (流畅)' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};
