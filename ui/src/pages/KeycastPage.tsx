/* ─────────────────────────────────────────────────────────────────────────────
 * KeycastPage.tsx — 按键回显与屏幕演示配置中心 (世界级 UX 双态色彩体系)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Toggle, SettingGroup, Button } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import {
  Keyboard,
  RotateCcw,
  Play,
  Sparkles,
  Palette,
} from 'lucide-react';
import './KeycastPage.css';

interface KeycastSettings {
  enabled: boolean;
  autoBypassFullscreen: boolean;
  showKeyboard: boolean;
  onlyShortcuts: boolean;
  displayDurationMs: number;
  fontSize: number;
  textColor: string;
  backgroundColor: string;
}

const DEFAULT_SETTINGS: KeycastSettings = {
  enabled: true,
  autoBypassFullscreen: true,
  showKeyboard: true,
  onlyShortcuts: false,
  displayDurationMs: 3000,
  fontSize: 20,
  textColor: '#ffffff',
  backgroundColor: '#202020',
};

const ACCENT_COLOR_MAP: Record<string, string> = {
  blue: '#3b82f6',
  cyan: '#06b6d4',
  amber: '#f59e0b',
  mint: '#10b981',
  coral: '#f43f5e',
  violet: '#8b5cf6',
};

interface ColorSegmentControlProps {
  label: string;
  desc?: string;
  value: string;
  defaultCustomFallback: string;
  brandAccentHex: string;
  onChange: (val: string) => void;
}

const ColorSegmentControl: FC<ColorSegmentControlProps> = ({
  label,
  desc,
  value,
  defaultCustomFallback,
  brandAccentHex,
  onChange,
}) => {
  const { t } = useTranslation();
  const isAuto = value === 'auto';
  const displayHex = isAuto ? brandAccentHex : value;

  return (
    <div className="keycast-page__prop-card keycast-page__color-card">
      <div className="keycast-page__prop-header">
        <span className="keycast-page__prop-title">{label}</span>
        {desc && <span className="keycast-page__prop-desc">{desc}</span>}
      </div>

      {/* 双态胶囊选择器 */}
      <div className="keycast-page__capsule-wrap">
        <button
          type="button"
          className={`keycast-page__capsule-btn ${isAuto ? 'active' : ''}`}
          onClick={() => onChange('auto')}
        >
          <Sparkles size={13} />
          <span>{t('keycast.followBrandAccent', '跟随品牌色')}</span>
          <span className="keycast-page__capsule-dot" style={{ backgroundColor: brandAccentHex }} />
        </button>

        <button
          type="button"
          className={`keycast-page__capsule-btn ${!isAuto ? 'active' : ''}`}
          onClick={() => {
            if (isAuto) {
              onChange(defaultCustomFallback);
            }
          }}
        >
          <Palette size={13} />
          <span>{t('keycast.customColor', '自定义颜色')}</span>
        </button>
      </div>

      {/* 色彩展示与调色盘操作 */}
      <div className="keycast-page__prop-body">
        <div className="keycast-page__color-row">
          <div className="keycast-page__color-info">
            <span className="keycast-page__color-hex">{displayHex.toUpperCase()}</span>
            {!isAuto && (
              <button
                type="button"
                className="keycast-page__restore-capsule"
                onClick={() => onChange('auto')}
                title={t('keycast.restoreFollowBrandDesc', '一键切回并实时联动 EasyTools 品牌主色')}
              >
                <RotateCcw size={11} />
                <span>{t('keycast.restoreFollowBrand', '恢复跟随品牌色')}</span>
              </button>
            )}
          </div>

          <div className="keycast-page__color-picker-wrap">
            <div
              className="keycast-page__color-swatch"
              style={{ backgroundColor: displayHex }}
              title={isAuto ? t('keycast.followBrandAccent') : displayHex}
            />
            {!isAuto && (
              <input
                type="color"
                className="keycast-page__color-input"
                value={displayHex}
                onChange={(e) => onChange(e.target.value)}
                aria-label={label}
              />
            )}
          </div>
        </div>
      </div>
    </div>
  );
};

export const KeycastPage: FC = () => {
  const { t } = useTranslation();
  const [settings, setSettings] = useState<KeycastSettings>(DEFAULT_SETTINGS);
  const [loading, setLoading] = useState(true);
  const [accent, setAccent] = useState<string>(() => {
    try {
      return localStorage.getItem('easytools:accent-color') || 'blue';
    } catch {
      return 'blue';
    }
  });

  useEffect(() => {
    const handleAccent = (e: Event) => {
      const detail = (e as CustomEvent<string>).detail;
      if (detail) setAccent(detail);
    };
    window.addEventListener('easytools:accent-changed', handleAccent);
    return () => window.removeEventListener('easytools:accent-changed', handleAccent);
  }, []);

  useEffect(() => {
    bridgeRequest<KeycastSettings>('keycast.getSettings')
      .then((res) => {
        if (res) {
          setSettings((prev) => ({ ...prev, ...res }));
        }
      })
      .catch((err) => {
        console.error('Failed to load keycast settings:', err);
      })
      .finally(() => setLoading(false));
  }, []);

  const saveSetting = async <K extends keyof KeycastSettings>(key: K, value: KeycastSettings[K]) => {
    const previous = settings[key];
    const updated = { ...settings, [key]: value };
    setSettings(updated);
    try {
      await bridgeRequest('keycast.updateSettings', { [key]: value });
    } catch (e) {
      setSettings((prev) => ({ ...prev, [key]: previous }));
      toast.error(t('keycast.saveFailed', '保存设置失败'), { description: String(e) });
    }
  };

  const handleResetDefaults = async () => {
    try {
      await bridgeRequest('keycast.resetDefaults', {});
      setSettings(DEFAULT_SETTINGS);
      toast.success(t('keycast.resetSuccess', '已恢复默认设置'));
    } catch (e) {
      toast.error(t('keycast.saveFailed', '恢复默认失败'), { description: String(e) });
    }
  };

  const handleTestKeycast = async () => {
    try {
      await bridgeRequest('keycast.trigger', {});
    } catch (e) {
      console.error('Failed to trigger keycast:', e);
    }
  };

  const currentBrandAccentHex = ACCENT_COLOR_MAP[accent] || '#3b82f6';

  if (loading) {
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading', '加载中...')}</div>;
  }

  return (
    <div className="keycast-page">
      {/* ── 顶部操作栏 ──────────────────────────────────────────────── */}
      <div className="keycast-page__header">
        <div className="keycast-page__title-wrap">
          <h2 className="keycast-page__title">{t('keycast.title', '按键回显')}</h2>
        </div>
        <Button variant="ghost" onClick={handleResetDefaults} title={t('keycast.resetDefaults', '恢复默认')}>
          <RotateCcw size={14} style={{ marginRight: 6 }} />
          <span>{t('keycast.resetDefaults', '恢复默认')}</span>
        </Button>
      </div>

      {/* ── 1. 主开关与全屏免打扰 ────────────────────────────────────── */}
      <Card>
        <Toggle
          id="keycast-main-enabled"
          label={t('keycast.mainToggle', '按键回显')}
          description={t('keycast.mainToggleDesc', '在屏幕上回显按键。')}
          checked={settings.enabled}
          onChange={(v) => saveSetting('enabled', v)}
        />
        <Toggle
          id="keycast-auto-bypass"
          label={t('keycast.autoBypassFullscreen', '全屏游戏/视频免打扰')}
          description={t('keycast.autoBypassFullscreenDesc', '前台处于全屏独占应用时自动免打扰暂停按键回显，防止 3D 游戏或观影时被干扰')}
          checked={settings.autoBypassFullscreen}
          onChange={(v) => saveSetting('autoBypassFullscreen', v)}
        />
      </Card>

      {/* ── 2. 键盘设置 ────────────────────────────────────────────── */}
      <SettingGroup title={t('keycast.keyboardSection', '键盘')} icon={<Keyboard size={18} />}>
        <Card>
          <Toggle
            id="keycast-show-keyboard"
            label={t('keycast.showKeyboard', '显示键盘输入')}
            description={t('keycast.showKeyboardDesc', '显示按下的键盘按键和快捷键组合。')}
            checked={settings.showKeyboard}
            onChange={(v) => saveSetting('showKeyboard', v)}
          />
          <Toggle
            id="keycast-only-shortcuts"
            label={t('keycast.onlyShortcuts', '仅显示快捷键')}
            description={t('keycast.onlyShortcutsDesc', '开启后，只回显包含 Ctrl/Alt/Shift/Win 的组合键。')}
            checked={settings.onlyShortcuts}
            onChange={(v) => saveSetting('onlyShortcuts', v)}
          />
        </Card>

        {/* 属性网格：显示时长、文字大小、文字颜色、背景颜色 */}
        <div className="keycast-page__grid" style={{ marginTop: '16px' }}>
          {/* 显示时长 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.displayDuration', '显示时长')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.displayDurationDesc', '每个按键在屏幕上的停留时间 (ms)。')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <input
                type="number"
                className="keycast-page__number-input"
                min={500}
                max={10000}
                step={500}
                value={settings.displayDurationMs}
                onChange={(e) => saveSetting('displayDurationMs', Number(e.target.value) || 3000)}
                aria-label={t('keycast.displayDuration', '显示时长')}
              />
            </div>
          </div>

          {/* 文字大小 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.fontSize', '文字大小')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.fontSizeDesc', '回显按键的字体大小 (px)。')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <input
                type="number"
                className="keycast-page__number-input"
                min={12}
                max={48}
                step={2}
                value={settings.fontSize}
                onChange={(e) => saveSetting('fontSize', Number(e.target.value) || 20)}
                aria-label={t('keycast.fontSize', '文字大小')}
              />
            </div>
          </div>

          {/* 文字颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.textColor', '文字颜色')}
            desc={t('keycast.textColorDesc', '按键文字的颜色。')}
            value={settings.textColor}
            defaultCustomFallback="#ffffff"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('textColor', val)}
          />

          {/* 背景颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.backgroundColor', '背景颜色')}
            desc={t('keycast.backgroundColorDesc', '按键背景方块的颜色。')}
            value={settings.backgroundColor}
            defaultCustomFallback="#202020"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('backgroundColor', val)}
          />
        </div>
      </SettingGroup>

      {/* ── 底部即刻体验栏 ──────────────────────────────────────────── */}
      <div className="keycast-page__action-footer">
        <Button variant="primary" onClick={handleTestKeycast}>
          <Play size={14} style={{ marginRight: 6 }} />
          <span>{t('keycast.testKeycast', '立即体验按键回显')}</span>
        </Button>
      </div>
    </div>
  );
};
