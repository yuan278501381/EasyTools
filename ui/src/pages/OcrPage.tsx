/* ─────────────────────────────────────────────────────────────────────────────
 * OcrPage — OCR 识别设置页
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select, Badge } from '../components/UIKit';

export const OcrPage: FC = () => {
  const [enabled, setEnabled] = useState(true);
  const [language, setLanguage] = useState('zh-en');
  const [autoCopy, setAutoCopy] = useState(true);

  return (
    <div className="ocr-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="OCR 文字识别" icon="📝">
        <Card>
          <Toggle
            id="ocr-enabled"
            label="启用 OCR 识别"
            description="截图时可选择识别图片中的文字"
            checked={enabled}
            onChange={setEnabled}
          />
          <SettingRow label="识别引擎" description="当前使用的 OCR 识别引擎">
            <Badge text="PaddleOCR Lite" variant="primary" />
          </SettingRow>
          <SettingRow label="识别语言" description="选择 OCR 识别的目标语言">
            <Select
              id="ocr-language"
              value={language}
              onChange={setLanguage}
              options={[
                { value: 'zh-en', label: '中英文混合' },
                { value: 'zh', label: '仅中文' },
                { value: 'en', label: '仅英文' },
                { value: 'ja', label: '日文' },
                { value: 'ko', label: '韩文' },
              ]}
            />
          </SettingRow>
          <Toggle
            id="ocr-auto-copy"
            label="识别后自动复制"
            description="OCR 识别完成后自动将结果复制到剪贴板"
            checked={autoCopy}
            onChange={setAutoCopy}
          />
        </Card>
      </SettingGroup>
    </div>
  );
};
