/* ─────────────────────────────────────────────────────────────────────────────
 * GeneralPage — 通用设置页
 *
 * 从 C++ ConfigManager 加载设置，修改后通过 IPC 实时保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';

interface GeneralSettings {
  autoStart: boolean;
  minimizeToTray: boolean;
  checkUpdates: boolean;
  keycastEnabled: boolean;
  language: string;
  logLevel: string;
  theme: string;
}

export const GeneralPage: FC = () => {
  const [settings, setSettings] = useState<GeneralSettings>({
    autoStart: false,
    minimizeToTray: true,
    checkUpdates: true,
    keycastEnabled: false,
    language: 'auto',
    logLevel: 'info',
    theme: 'dark',
  });
  const [loading, setLoading] = useState(true);

  const { t, i18n } = useTranslation();

  // 初始化获取设置
  useEffect(() => {
    bridgeRequest<GeneralSettings>('general.getSettings')
      .then(res => {
        setSettings(prev => ({ ...prev, ...res }));
        const lang = res.language;
        if (lang && lang !== 'auto') {
          i18n.changeLanguage(lang);
        }
      })
      .catch(console.error)
      .finally(() => setLoading(false));
  }, [i18n]);

  // 保存单个设置项
  const updateSetting = useCallback((key: keyof GeneralSettings, value: unknown) => {
    setSettings(prev => ({ ...prev, [key]: value }));

    if (key === 'language') {
      const langValue = value as string;
      if (langValue === 'auto') {
        const browserLang = navigator.language;
        i18n.changeLanguage(browserLang);
      } else {
        i18n.changeLanguage(langValue);
      }
    }

    bridgeRequest('general.updateSettings', { [key]: value }).then(() => {
      toast.success(t('general.toastSaveSuccess'), {
        description: t('general.toastSaveDesc', { key }),
        duration: 2000,
      });
    }).catch(e => {
      toast.error(t('general.toastSaveFailed'), { description: String(e) });
    });
  }, [i18n, t]);

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>加载中...</div>;
  }

  return (
    <div className="general-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('general.behavior')} icon="⚙️">
        <Card>
          <Toggle
            id="autoStart"
            label={t('general.autoStart')}
            description={t('general.autoStartDesc')}
            checked={settings.autoStart}
            onChange={(v) => updateSetting('autoStart', v)}
          />
          <Toggle
            id="minimizeToTray"
            label={t('general.minimizeToTray')}
            description={t('general.minimizeToTrayDesc')}
            checked={settings.minimizeToTray}
            onChange={(v) => updateSetting('minimizeToTray', v)}
          />
          <Toggle
            id="checkUpdates"
            label={t('general.checkUpdates')}
            description={t('general.checkUpdatesDesc')}
            checked={settings.checkUpdates}
            onChange={(v) => updateSetting('checkUpdates', v)}
          />
        </Card>
      </SettingGroup>

      <SettingGroup title={t('general.uiAndLang')} icon="🌐">
        <Card>
          <Toggle
            id="keycast"
            label={t('general.keycast')}
            description={t('general.keycastDesc')}
            checked={settings.keycastEnabled}
            onChange={(v) => updateSetting('keycastEnabled', v)}
          />
          <SettingRow label={t('general.language')} description={t('general.languageDesc')}>
            <Select
              id="language"
              value={settings.language}
              onChange={(v) => updateSetting('language', v)}
              options={[
                { value: 'auto', label: t('general.langAuto') },
                { value: 'zh-CN', label: t('general.langZh') },
                { value: 'en-US', label: t('general.langEn') }
              ]}
            />
          </SettingRow>
          <SettingRow label={t('general.theme')} description={t('general.themeDesc')}>
            <Select
              id="theme"
              value={settings.theme}
              onChange={(v) => updateSetting('theme', v)}
              options={[
                { value: 'light', label: t('general.themeLight') },
                { value: 'dark', label: t('general.themeDark') }
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('general.advanced')} icon="🔧">
        <Card>
          <SettingRow label={t('general.logLevel')} description={t('general.logLevelDesc')}>
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
