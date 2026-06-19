/* ─────────────────────────────────────────────────────────────────────────────
 * GeneralPage — 通用设置页
 *
 * 从 C++ ConfigManager 加载设置，修改后通过 IPC 实时保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';

interface GeneralSettings {
  autoStart: boolean;
  minimizeToTray: boolean;
  checkUpdates: boolean;
  language: string;
  logLevel: string;
  theme: string;
}

export const GeneralPage: FC = () => {
  const [settings, setSettings] = useState<GeneralSettings>({
    autoStart: false,
    minimizeToTray: true,
    checkUpdates: true,
    language: 'zh-CN',
    logLevel: 'info',
    theme: 'light',
  });
  const [loading, setLoading] = useState(true);

  // 加载设置
  useEffect(() => {
    bridgeRequest<GeneralSettings>('general.getSettings').then(data => {
      setSettings(prev => ({ ...prev, ...data }));
      setLoading(false);
    });
  }, []);

  // 保存单个设置项
  const updateSetting = useCallback((key: keyof GeneralSettings, value: unknown) => {
    setSettings(prev => ({ ...prev, [key]: value }));
    bridgeRequest('general.updateSettings', { [key]: value });
  }, []);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>加载中...</div>;
  }

  return (
    <div className="general-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title="启动与运行" icon="🚀">
        <Card>
          <Toggle
            id="auto-start"
            label="开机自动启动"
            description="设置 EasyTools 在系统启动时自动运行"
            checked={settings.autoStart}
            onChange={(v) => updateSetting('autoStart', v)}
          />
          <Toggle
            id="minimize-to-tray"
            label="关闭窗口时最小化到托盘"
            description="关闭设置窗口不会退出程序，而是最小化到系统托盘"
            checked={settings.minimizeToTray}
            onChange={(v) => updateSetting('minimizeToTray', v)}
          />
          <Toggle
            id="check-updates"
            label="自动检查更新"
            description="启动时自动检查是否有新版本"
            checked={settings.checkUpdates}
            onChange={(v) => updateSetting('checkUpdates', v)}
          />
        </Card>
      </SettingGroup>

      <SettingGroup title="界面与语言" icon="🌐">
        <Card>
          <SettingRow label="界面语言" description="设置界面显示的语言">
            <Select
              id="language"
              value={settings.language}
              onChange={(v) => updateSetting('language', v)}
              options={[
                { value: 'zh-CN', label: '简体中文' },
                { value: 'en-US', label: 'English' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>

      <SettingGroup title="高级设置" icon="🔧">
        <Card>
          <SettingRow label="日志级别" description="控制日志输出的详细程度">
            <Select
              id="log-level"
              value={settings.logLevel}
              onChange={(v) => updateSetting('logLevel', v)}
              options={[
                { value: 'trace', label: 'Trace' },
                { value: 'debug', label: 'Debug' },
                { value: 'info', label: 'Info' },
                { value: 'warn', label: 'Warning' },
                { value: 'error', label: 'Error' },
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};
