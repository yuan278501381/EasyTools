/* ─────────────────────────────────────────────────────────────────────────────
 * GeneralPage — 通用设置页
 *
 * 从 C++ ConfigManager 加载设置，修改后通过 IPC 实时保存。
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, useCallback, type FC } from 'react';
import { Card, Toggle, SettingRow, SettingGroup, Select, Button } from '../components/UIKit';
import { HotkeyRecorder } from '../components/HotkeyRecorder';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import { useTranslation } from 'react-i18next';
import { Settings, Zap, Globe, Database, Keyboard, Download, Upload, RotateCcw } from 'lucide-react';
import './GeneralPage.css';

interface GeneralSettings {
  autoStart: boolean;
  minimizeToTray: boolean;
  checkUpdates: boolean;
  keycastEnabled: boolean;
  keycastOnlyShortcuts: boolean;
  language: string;
  logLevel: string;
  theme: string;
}

interface HotkeyEntry {
  name: string;
  shortcut: string;
}

interface OperationResult {
  success: boolean;
  cancelled?: boolean;
  error?: string;
  shortcut?: string;
}

export const GeneralPage: FC = () => {
  const [settings, setSettings] = useState<GeneralSettings>({
    autoStart: false,
    minimizeToTray: true,
    checkUpdates: true,
    keycastEnabled: false,
    keycastOnlyShortcuts: true,
    language: 'auto',
    logLevel: 'info',
    theme: 'dark',
  });
  const [loading, setLoading] = useState(true);
  const [hotkeys, setHotkeys] = useState<HotkeyEntry[]>([]);

  const { t, i18n } = useTranslation();

  // 初始化获取设置
  useEffect(() => {
    Promise.all([
      bridgeRequest<GeneralSettings>('general.getSettings'),
      bridgeRequest<HotkeyEntry[]>('hotkey.getAll'),
    ]).then(([res, hotkeyData]) => {
      setSettings(prev => ({ ...prev, ...res }));
      const lang = res.language;
      if (lang && lang !== 'auto') {
        i18n.changeLanguage(lang);
      }
      setHotkeys(Array.isArray(hotkeyData) ? hotkeyData : []);
    })
    .catch((error) => {
      console.error(error);
      toast.error(t('general.loadFailed'));
    })
    .finally(() => setLoading(false));
  }, [i18n, t]);

  // 保存单个设置项
  const updateSetting = useCallback(async <K extends keyof GeneralSettings,>(key: K, value: GeneralSettings[K]) => {
    const previous = settings[key];
    setSettings(prev => ({ ...prev, [key]: value }));

    const applyLocalValue = (localValue: GeneralSettings[K]) => {
      if (key === 'language') {
        const langValue = String(localValue);
        if (langValue === 'auto') void i18n.changeLanguage(navigator.language);
        else void i18n.changeLanguage(langValue);
      } else if (key === 'theme') {
        window.dispatchEvent(new CustomEvent('easytools:theme-changed', { detail: localValue }));
      }
    };
    applyLocalValue(value);

    try {
      const result = await bridgeRequest<OperationResult>('general.updateSettings', { [key]: value });
      if (!result.success) throw new Error(result.error || t('general.toastSaveFailed'));
    } catch (error) {
      setSettings(prev => prev[key] === value ? { ...prev, [key]: previous } : prev);
      applyLocalValue(previous);
      toast.error(t('general.toastSaveFailed'), { description: String(error) });
    }
  }, [i18n, settings, t]);

  // ── 数据管理操作 ─────────────────────────────────────────────────────────
  const handleExportConfig = async () => {
    try {
      const result = await bridgeRequest<OperationResult>('config.export');
      if (result.cancelled) return;
      if (!result.success) throw new Error(result.error || t('general.exportFailed'));
      toast.success(t('general.exportSuccess'));
    } catch (e) {
      toast.error(t('general.exportFailed'), { description: String(e) });
    }
  };

  const handleImportConfig = async () => {
    try {
      const result = await bridgeRequest<OperationResult>('config.import');
      if (result.cancelled) return;
      if (!result.success) throw new Error(result.error || t('general.importFailed'));
      toast.success(t('general.importSuccess'));
      const refreshed = await bridgeRequest<GeneralSettings>('general.getSettings');
      setSettings(prev => ({ ...prev, ...refreshed }));
      if (refreshed.language === 'auto') void i18n.changeLanguage(navigator.language);
      else void i18n.changeLanguage(refreshed.language);
      window.dispatchEvent(new CustomEvent('easytools:theme-changed', { detail: refreshed.theme }));
    } catch (e) {
      toast.error(t('general.importFailed'), { description: String(e) });
    }
  };

  const handleResetConfig = async () => {
    if (!window.confirm(t('general.resetConfirmMsg'))) return;
    try {
      const result = await bridgeRequest<OperationResult>('config.reset');
      if (!result.success) throw new Error(result.error || t('general.resetFailed'));
      toast.success(t('general.resetSuccess'));
      // 重新加载设置
      const res = await bridgeRequest<GeneralSettings>('general.getSettings');
      setSettings(prev => ({ ...prev, ...res }));
      if (res.language === 'auto') void i18n.changeLanguage(navigator.language);
      else void i18n.changeLanguage(res.language);
      window.dispatchEvent(new CustomEvent('easytools:theme-changed', { detail: res.theme }));
    } catch (e) {
      toast.error(t('general.resetFailed'), { description: String(e) });
    }
  };

  // ── 快捷键名称映射 ──────────────────────────────────────────────────────
  const hotkeyNameMap: Record<string, string> = {
    Screenshot: t('onboarding.shortcutCapture'),
    Record: t('onboarding.shortcutRecord'),
    OCR: t('onboarding.shortcutOcr'),
    'Pause Gestures': t('onboarding.shortcutGesturePause'),
    'Toggle Search': t('search.title'),
    'Pin Toggle': t('general.shortcutPinToggle'),
    'Pin Paste': t('general.shortcutPinPaste'),
    'Pin Hide All': t('general.shortcutPinHideAll'),
    'Pin Arrange': t('general.shortcutPinArrange'),
    capture: t('onboarding.shortcutCapture'),
    recording: t('onboarding.shortcutRecord'),
    ocr: t('onboarding.shortcutOcr'),
    gesturePause: t('onboarding.shortcutGesturePause'),
  };

  const rebindHotkey = async (entry: HotkeyEntry, shortcut: string) => {
    const previous = entry.shortcut;
    setHotkeys(items => items.map(item =>
      item.name === entry.name ? { ...item, shortcut } : item));
    try {
      const result = await bridgeRequest<OperationResult>('hotkey.rebind', {
        name: entry.name,
        hotkey: shortcut,
      });
      if (!result.success) throw new Error(result.error || t('hotkey.bindFailed'));
      setHotkeys(items => items.map(item =>
        item.name === entry.name ? { ...item, shortcut: result.shortcut || shortcut } : item));
    } catch (error) {
      setHotkeys(items => items.map(item =>
        item.name === entry.name && item.shortcut === shortcut
          ? { ...item, shortcut: previous }
          : item));
      toast.error(t('hotkey.bindFailed'), { description: String(error) });
    }
  };

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading')}</div>;
  }

  return (
    <div className="general-page" style={{ animation: 'fadeIn 0.3s ease' }}>
      <SettingGroup title={t('general.behavior')} icon={<Zap size={20} strokeWidth={2.5} />}>
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

      <SettingGroup title={t('general.uiAndLang')} icon={<Globe size={20} strokeWidth={2.5} />}>
        <Card>
          <Toggle
            id="keycast"
            label={t('general.keycast')}
            description={t('general.keycastDesc')}
            checked={settings.keycastEnabled}
            onChange={(v) => updateSetting('keycastEnabled', v)}
          />
          {settings.keycastEnabled && (
            <Toggle
              id="keycastOnlyShortcuts"
              label={t('general.keycastOnlyShortcuts')}
              description={t('general.keycastOnlyShortcutsDesc')}
              checked={settings.keycastOnlyShortcuts}
              onChange={(v) => updateSetting('keycastOnlyShortcuts', v)}
            />
          )}
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
                { value: 'system', label: t('general.themeSystem') },
                { value: 'light', label: t('general.themeLight') },
                { value: 'dark', label: t('general.themeDark') }
              ]}
            />
          </SettingRow>
        </Card>
      </SettingGroup>

      <SettingGroup title={t('general.advanced')} icon={<Settings size={20} strokeWidth={2.5} />}>
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

      {/* ── 快捷键总览 ──────────────────────────────────────────── */}
      <SettingGroup title={t('general.keyboardShortcuts')} icon={<Keyboard size={20} strokeWidth={2.5} />}>
        <Card>
          {hotkeys.length === 0 ? (
            <div className="general-page__empty">{t('general.noShortcuts')}</div>
          ) : (
            <div className="general-page__hotkey-list">
              {hotkeys.map((hk) => (
                <div key={hk.name} className="general-page__hotkey-item">
                  <span className="general-page__hotkey-name">
                    {hotkeyNameMap[hk.name] ?? hk.name}
                  </span>
                  <div className="general-page__hotkey-control">
                    <HotkeyRecorder
                      id={`general-hotkey-${hk.name.replace(/\s+/g, '-').toLowerCase()}`}
                      value={hk.shortcut}
                      onChange={(value) => void rebindHotkey(hk, value)}
                    />
                  </div>
                </div>
              ))}
            </div>
          )}
        </Card>
      </SettingGroup>

      {/* ── 数据管理 ────────────────────────────────────────────── */}
      <SettingGroup title={t('general.dataManagement')} icon={<Database size={20} strokeWidth={2.5} />}>
        <Card>
          <SettingRow label={t('general.exportConfig')} description={t('general.exportConfigDesc')}>
            <Button variant="ghost" onClick={handleExportConfig}>
              <Download size={16} />
              <span>{t('common.export')}</span>
            </Button>
          </SettingRow>
          <SettingRow label={t('general.importConfig')} description={t('general.importConfigDesc')}>
            <Button variant="ghost" onClick={handleImportConfig}>
              <Upload size={16} />
              <span>{t('common.import')}</span>
            </Button>
          </SettingRow>
          <SettingRow label={t('general.resetConfig')} description={t('general.resetConfigDesc')}>
            <Button variant="danger" onClick={handleResetConfig}>
              <RotateCcw size={16} />
              <span>{t('common.reset')}</span>
            </Button>
          </SettingRow>
        </Card>
      </SettingGroup>
    </div>
  );
};
