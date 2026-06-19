/* ─────────────────────────────────────────────────────────────────────────────
 * OcrPage — OCR 识别设置页
 *
 * 从 C++ ConfigManager 加载 OCR 设置，修改后实时 IPC 保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select, Badge } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';

interface OcrSettings {
  engine: string;
  language: string;
  autoOcr: boolean;
  copyResult: boolean;
}

export const OcrPage: FC = () => {
  const [settings, setSettings] = useState<OcrSettings>({
    engine: 'paddleocr',
    language: 'ch',
    autoOcr: false,
    copyResult: true,
  });
  const [loading, setLoading] = useState(true);

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
      <SettingGroup title="OCR 文字识别" icon="📝">
        <Card>
          <Toggle
            id="ocr-auto"
            label="截图后自动识别"
            description="截图完成后自动对选区进行 OCR 文字识别"
            checked={settings.autoOcr}
            onChange={(v) => updateSetting('autoOcr', v)}
          />
          <SettingRow label="识别引擎" description="当前使用的 OCR 识别引擎">
            <Badge text="PaddleOCR Lite" variant="primary" />
          </SettingRow>
          <SettingRow label="识别语言" description="选择 OCR 识别的目标语言">
            <Select
              id="ocr-language"
              value={settings.language}
              onChange={(v) => updateSetting('language', v)}
              options={[
                { value: 'ch', label: '中英文混合' },
                { value: 'en', label: '仅英文' },
                { value: 'japan', label: '日文' },
                { value: 'korean', label: '韩文' },
              ]}
            />
          </SettingRow>
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
