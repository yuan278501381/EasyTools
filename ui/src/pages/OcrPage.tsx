/* ─────────────────────────────────────────────────────────────────────────────
 * OcrPage — OCR 识别设置页
 *
 * 引擎: Windows.Media.Ocr (系统内置、离线)。
 * 从 C++ ConfigManager 加载设置，修改后实时 IPC 保存；并展示引擎可用性。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Badge } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';

interface OcrSettings {
  engine: string;
  language: string;
  autoOcr: boolean;
  copyResult: boolean;
}

export const OcrPage: FC = () => {
  const [settings, setSettings] = useState<OcrSettings>({
    engine: 'windows',
    language: 'auto',
    autoOcr: false,
    copyResult: true,
  });
  const [available, setAvailable] = useState<boolean | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    bridgeRequest<OcrSettings>('ocr.getSettings').then(data => {
      setSettings(prev => ({ ...prev, ...data }));
      setLoading(false);
    });
    bridgeRequest<{ available: boolean }>('ocr.getStatus')
      .then(s => setAvailable(s.available))
      .catch(() => setAvailable(false));
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
      <SettingGroup title="OCR 文字识别" icon="📝">
        <Card>
          <SettingRow label="识别引擎" description="使用 Windows 系统内置的离线 OCR 引擎，无需联网">
            <Badge text="Windows OCR" variant="primary" />
          </SettingRow>
          <SettingRow
            label="引擎状态"
            description={
              available === false
                ? '未检测到 OCR 语言包，请在「系统设置 → 应用 → 可选功能」中为目标语言添加“光学字符识别”。'
                : '系统已安装可用的 OCR 语言包'
            }
          >
            {available === null
              ? <Badge text="检测中…" variant="muted" />
              : available
                ? <Badge text="可用" variant="success" />
                : <Badge text="不可用" variant="danger" />}
          </SettingRow>
          <SettingRow label="快捷键" description="框选屏幕区域并识别其中文字">
            <Badge text="Ctrl + Shift + O" variant="muted" />
          </SettingRow>
        </Card>
      </SettingGroup>

      <SettingGroup title="识别行为" icon="⚙️">
        <Card>
          <Toggle
            id="ocr-auto"
            label="截图后自动识别"
            description="截图完成后自动对选区进行 OCR 文字识别"
            checked={settings.autoOcr}
            onChange={(v) => updateSetting('autoOcr', v)}
          />
          <Toggle
            id="ocr-copy"
            label="识别后自动复制"
            description="OCR 识别完成后自动将结果复制到剪贴板"
            checked={settings.copyResult}
            onChange={(v) => updateSetting('copyResult', v)}
          />
        </Card>
      </SettingGroup>
    </div>
  );
};
