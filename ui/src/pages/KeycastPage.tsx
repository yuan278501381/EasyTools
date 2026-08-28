/* ─────────────────────────────────────────────────────────────────────────────
 * KeycastPage.tsx — 按键回显与屏幕演示配置中心 (世界级 UX & 2D 时序流)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Toggle, SettingGroup, Button, NumberInput } from '../components/UIKit';
import { bridgeRequest } from '../hooks/useBridge';
import { toast } from 'sonner';
import {
  RotateCcw,
  Play,
  Sparkles,
  Palette,
  LayoutGrid,
  Filter,
  Layers,
  ArrowDownLeft,
  ArrowDown,
  ArrowDownRight,
  ArrowUpLeft,
  ArrowUpRight,
  Check,
} from 'lucide-react';
import './KeycastPage.css';

interface KeycastSettings {
  enabled: boolean;
  autoBypassFullscreen: boolean;
  showKeyboard: boolean;
  filterMode: 'smart_shortcuts' | 'with_single_modifiers' | 'all_keys';
  includeFunctionKeys: boolean;
  position: 'bottom_left' | 'bottom_center' | 'bottom_right' | 'top_left' | 'top_right';
  mergeRecentKeys: boolean;
  mergeTimeoutMs: number;
  displayDurationMs: number;
  fontSize: number;
  opacity: number;
  textColor: string;
  backgroundColor: string;
  modifierKeycapColor: string;
  modifierKeycapOpacity: number;
  modifierTextColor: string;
  firstKeyAnim: 'slide' | 'pop' | 'fade' | 'none';
  subsequentKeyAnim: 'pop' | 'slide' | 'fade' | 'none';
  rowCascadeAnim: boolean;
  exitDriftAnim: boolean;
}

const DEFAULT_SETTINGS: KeycastSettings = {
  enabled: true,
  autoBypassFullscreen: true,
  showKeyboard: true,
  filterMode: 'smart_shortcuts',
  includeFunctionKeys: false,
  position: 'top_left',
  mergeRecentKeys: true,
  mergeTimeoutMs: 1200,
  displayDurationMs: 2500,
  fontSize: 36,
  opacity: 85,
  textColor: '#ffffff',
  backgroundColor: '#1c1c22',
  modifierKeycapColor: 'auto',
  modifierKeycapOpacity: 40,
  modifierTextColor: '#000000',
  firstKeyAnim: 'slide',
  subsequentKeyAnim: 'fade',
  rowCascadeAnim: true,
  exitDriftAnim: true,
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
          <span>{t('keycast.followBrandAccent', 'Follow Theme Accent')}</span>
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
          <span>{t('keycast.customColor', 'Custom Color')}</span>
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
                title={t('keycast.restoreFollowBrandDesc', 'Switch back and link dynamically to EasyTools theme accent color')}
              >
                <RotateCcw size={11} />
                <span>{t('keycast.restoreFollowBrand', 'Follow Theme Accent')}</span>
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
      toast.error(t('keycast.saveFailed', 'Failed to save keycast settings'), { description: String(e) });
    }
  };

  const handleResetDefaults = async () => {
    try {
      await bridgeRequest('keycast.resetDefaults', {});
      setSettings(DEFAULT_SETTINGS);
      toast.success(t('keycast.resetSuccess', 'Restored default keycast settings'));
    } catch (e) {
      toast.error(t('keycast.saveFailed', 'Failed to save keycast settings'), { description: String(e) });
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
    return <div style={{ padding: '2rem', opacity: 0.5 }}>{t('common.loading', 'Loading...')}</div>;
  }

  const POSITIONS: Array<{ id: KeycastSettings['position']; labelKey: string; icon: typeof ArrowDownLeft }> = [
    { id: 'top_left',      labelKey: 'keycast.posTopLeft',      icon: ArrowUpLeft },
    { id: 'top_right',     labelKey: 'keycast.posTopRight',     icon: ArrowUpRight },
    { id: 'bottom_left',   labelKey: 'keycast.posBottomLeft',   icon: ArrowDownLeft },
    { id: 'bottom_center', labelKey: 'keycast.posBottomCenter', icon: ArrowDown },
    { id: 'bottom_right',  labelKey: 'keycast.posBottomRight',  icon: ArrowDownRight },
  ];

  const FILTER_MODES: Array<{ id: KeycastSettings['filterMode']; titleKey: string; descKey: string }> = [
    {
      id: 'smart_shortcuts',
      titleKey: 'keycast.filterSmartShortcuts',
      descKey: 'keycast.filterSmartShortcutsDesc',
    },
    {
      id: 'with_single_modifiers',
      titleKey: 'keycast.filterWithModifiers',
      descKey: 'keycast.filterWithModifiersDesc',
    },
    {
      id: 'all_keys',
      titleKey: 'keycast.filterAllKeys',
      descKey: 'keycast.filterAllKeysDesc',
    },
  ];

  return (
    <div className="keycast-page">
      {/* ── 顶部操作栏 ──────────────────────────────────────────────── */}
      <div className="keycast-page__header">
        <div className="keycast-page__title-wrap">
          <h2 className="keycast-page__title">{t('keycast.title', 'Keycast')}</h2>
        </div>
        <Button variant="ghost" onClick={handleResetDefaults} title={t('keycast.resetDefaults')}>
          <RotateCcw size={14} style={{ marginRight: 6 }} />
          <span>{t('keycast.resetDefaults')}</span>
        </Button>
      </div>

      {/* ── 1. 主开关与全屏免打扰 ────────────────────────────────────── */}
      <Card>
        <Toggle
          id="keycast-main-enabled"
          label={t('keycast.mainToggle', 'Keycast')}
          description={t('keycast.mainToggleDesc', 'Render crystal keycap capsules on screen in real time.')}
          checked={settings.enabled}
          onChange={(v) => saveSetting('enabled', v)}
        />
        <Toggle
          id="keycast-auto-bypass"
          label={t('keycast.autoBypassFullscreen', 'Auto Bypass on Fullscreen')}
          description={t('keycast.autoBypassFullscreenDesc', 'Automatically suppress keycast overlay when exclusive fullscreen apps (e.g. 3D games or videos) are active')}
          checked={settings.autoBypassFullscreen}
          onChange={(v) => saveSetting('autoBypassFullscreen', v)}
        />
      </Card>

      {/* ── 2. 屏幕显示位置 ─────────────────────────────────────────── */}
      <SettingGroup title={t('keycast.positionSection', 'Screen Position')} icon={<LayoutGrid size={18} />}>
        <div className="keycast-page__position-grid">
          {POSITIONS.map((pos) => {
            const isSelected = settings.position === pos.id;
            const Icon = pos.icon;
            return (
              <button
                key={pos.id}
                type="button"
                className={`keycast-page__pos-card ${isSelected ? 'active' : ''}`}
                onClick={() => saveSetting('position', pos.id)}
              >
                <div className="keycast-page__pos-icon-wrap">
                  <Icon size={18} />
                </div>
                <span className="keycast-page__pos-label">{t(pos.labelKey as unknown as TemplateStringsArray)}</span>
                {isSelected && <span className="keycast-page__pos-check"><Check size={12} strokeWidth={3} /></span>}
              </button>
            );
          })}
        </div>
      </SettingGroup>

      {/* ── 3. 按键过滤与回显策略 ────────────────────────────────────── */}
      <SettingGroup title={t('keycast.filterSection', 'Filtering & Keystroke Policy')} icon={<Filter size={18} />}>
        <div className="keycast-page__filter-grid">
          {FILTER_MODES.map((mode) => {
            const isSelected = settings.filterMode === mode.id;
            return (
              <div
                key={mode.id}
                className={`keycast-page__filter-card ${isSelected ? 'active' : ''}`}
                onClick={() => saveSetting('filterMode', mode.id)}
                role="button"
                tabIndex={0}
              >
                <div className="keycast-page__filter-header">
                  <span className="keycast-page__filter-title">{t(mode.titleKey as unknown as TemplateStringsArray)}</span>
                  <div className={`keycast-page__filter-radio ${isSelected ? 'active' : ''}`}>
                    {isSelected && <div className="keycast-page__filter-radio-inner" />}
                  </div>
                </div>
                <p className="keycast-page__filter-desc">{t(mode.descKey as unknown as TemplateStringsArray)}</p>
              </div>
            );
          })}
        </div>
        <Card>
          <Toggle
            id="keycast-include-func-keys"
            label={t('keycast.includeFunctionKeys', 'Include Standalone Functional Keys')}
            description={t('keycast.includeFunctionKeysDesc', 'Show standalone Space, Backspace, Delete, Enter, Tab, Arrow keys, and F1~F12; when disabled, only display them when combined with modifiers.')}
            checked={settings.includeFunctionKeys}
            onChange={(v) => saveSetting('includeFunctionKeys', v)}
          />
        </Card>
      </SettingGroup>

      {/* ── 4. 时序流与物理动效 ─────────────────────────────────────── */}
      <SettingGroup title={t('keycast.motionSection', 'Timeline Flow & Physics Easing')} icon={<Layers size={18} />}>
        <Card>
          <Toggle
            id="keycast-merge-recent"
            label={t('keycast.mergeRecentKeys', 'Horizontal In-Line Push')}
            description={t('keycast.mergeRecentKeysDesc', 'Push recent keystrokes in the same row from right to left with spring damping; push row upward on timeout')}
            checked={settings.mergeRecentKeys}
            onChange={(v) => saveSetting('mergeRecentKeys', v)}
          />
          <Toggle
            id="keycast-row-cascade"
            label={t('keycast.rowCascadeAnim')}
            description={t('keycast.rowCascadeAnimDesc')}
            checked={settings.rowCascadeAnim}
            onChange={(v) => saveSetting('rowCascadeAnim', v)}
          />
          <Toggle
            id="keycast-exit-drift"
            label={t('keycast.exitDriftAnim')}
            description={t('keycast.exitDriftAnimDesc')}
            checked={settings.exitDriftAnim}
            onChange={(v) => saveSetting('exitDriftAnim', v)}
          />
        </Card>

        {/* 属性网格：首键动效、后续按键动效、显示时长、合并间隔、文字大小、颜色 */}
        <div className="keycast-page__grid" style={{ marginTop: '16px' }}>
          {/* 首键进场动效 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.firstKeyAnim')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.firstKeyAnimDesc')}</span>
            </div>
            <div className="keycast-page__capsule-wrap" style={{ marginTop: 8 }}>
              {([
                { id: 'slide', labelKey: 'keycast.animSlide', defaultLabel: 'Slide' },
                { id: 'pop', labelKey: 'keycast.animPop', defaultLabel: 'Pop' },
                { id: 'fade', labelKey: 'keycast.animFade', defaultLabel: 'Fade' },
                { id: 'none', labelKey: 'keycast.animNone', defaultLabel: 'None' },
              ] as const).map((opt) => (
                <button
                  key={opt.id}
                  type="button"
                  className={`keycast-page__capsule-btn ${settings.firstKeyAnim === opt.id ? 'active' : ''}`}
                  onClick={() => saveSetting('firstKeyAnim', opt.id)}
                >
                  {t(opt.labelKey as unknown as 'keycast.firstKeyAnim', opt.defaultLabel)}
                </button>
              ))}
            </div>
          </div>

          {/* 同排后续按键动效 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.subsequentKeyAnim')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.subsequentKeyAnimDesc')}</span>
            </div>
            <div className="keycast-page__capsule-wrap" style={{ marginTop: 8 }}>
              {([
                { id: 'pop', labelKey: 'keycast.animPop', defaultLabel: 'Pop' },
                { id: 'slide', labelKey: 'keycast.animSlide', defaultLabel: 'Slide' },
                { id: 'fade', labelKey: 'keycast.animFade', defaultLabel: 'Fade' },
                { id: 'none', labelKey: 'keycast.animNone', defaultLabel: 'None' },
              ] as const).map((opt) => (
                <button
                  key={opt.id}
                  type="button"
                  className={`keycast-page__capsule-btn ${settings.subsequentKeyAnim === opt.id ? 'active' : ''}`}
                  onClick={() => saveSetting('subsequentKeyAnim', opt.id)}
                >
                  {t(opt.labelKey as unknown as 'keycast.subsequentKeyAnim', opt.defaultLabel)}
                </button>
              ))}
            </div>
          </div>

          {/* 同排连击合并间隔 */}
          {settings.mergeRecentKeys && (
            <div className="keycast-page__prop-card">
              <div className="keycast-page__prop-header">
                <span className="keycast-page__prop-title">{t('keycast.mergeTimeout', 'In-Line Push Timeout')}</span>
                <span className="keycast-page__prop-desc">{t('keycast.mergeTimeoutDesc', 'Time window (ms) to merge consecutive keystrokes into the same horizontal line.')}</span>
              </div>
              <div className="keycast-page__prop-body">
                <NumberInput
                  min={300}
                  max={5000}
                  step={100}
                  unit="ms"
                  value={settings.mergeTimeoutMs || 1200}
                  onChange={(v) => saveSetting('mergeTimeoutMs', v)}
                  ariaLabel={t('keycast.mergeTimeout', 'In-Line Push Timeout')}
                />
              </div>
            </div>
          )}

          {/* 显示时长 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.displayDuration', 'Hold Duration')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.displayDurationDesc', 'Duration each keystroke capsule stays on screen (ms).')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <NumberInput
                min={500}
                max={10000}
                step={500}
                unit="ms"
                value={settings.displayDurationMs}
                onChange={(v) => saveSetting('displayDurationMs', v)}
                ariaLabel={t('keycast.displayDuration', 'Hold Duration')}
              />
            </div>
          </div>

          
          {/* 整体不透明度 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.opacity', 'Overall Opacity')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.opacityDesc', 'Global opacity of the keycast capsule (20%~100%). Lower values provide a more translucent ambient overlay.')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <NumberInput
                min={20}
                max={100}
                step={5}
                unit="%"
                value={settings.opacity ?? 85}
                onChange={(v) => saveSetting('opacity', v)}
                ariaLabel={t('keycast.opacity', 'Overall Opacity')}
              />
            </div>
          </div>
          {/* 文字大小 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.fontSize', 'Keycap Font Size')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.fontSizeDesc', 'Base font size for keycast (px). Capsule trays, modifier keycaps, Windows logo and paddings scale dynamically in golden ratio.')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <NumberInput
                min={12}
                max={64}
                step={2}
                unit="px"
                value={settings.fontSize}
                onChange={(v) => saveSetting('fontSize', v)}
                ariaLabel={t('keycast.fontSize', 'Keycap Font Size')}
              />
            </div>
          </div>

          {/* 文字颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.textColor', 'Text Color')}
            desc={t('keycast.textColorDesc', 'Color of the key text.')}
            value={settings.textColor}
            defaultCustomFallback="#000000"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('textColor', val)}
          />

          {/* 背景颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.backgroundColor', 'Capsule Background')}
            desc={t('keycast.backgroundColorDesc', 'Color of the keycap capsule background.')}
            value={settings.backgroundColor}
            defaultCustomFallback="#1c1c22"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('backgroundColor', val)}
          />

          {/* 修饰键底色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.modifierKeycapColor', 'Modifier Key Background')}
            desc={t('keycast.modifierKeycapColorDesc', 'Base background color for Ctrl / Alt / Win keys (follows theme accent by default).')}
            value={settings.modifierKeycapColor || 'auto'}
            defaultCustomFallback="#3b82f6"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('modifierKeycapColor', val)}
          />

          {/* 修饰键底色不透明度 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.modifierKeycapOpacity', 'Modifier Base Opacity')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.modifierKeycapOpacityDesc', 'Background base opacity (0%~100%, only affects background, text remains crisp and clear).')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <NumberInput
                min={0}
                max={100}
                step={2}
                unit="%"
                value={settings.modifierKeycapOpacity ?? 65}
                onChange={(v) => saveSetting('modifierKeycapOpacity', v)}
                ariaLabel={t('keycast.modifierKeycapOpacity', 'Modifier Base Opacity')}
              />
            </div>
          </div>

          {/* 修饰键文字颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.modifierTextColor', 'Modifier Key Text Color')}
            desc={t('keycast.modifierTextColorDesc', 'Text and icon color for Ctrl / Alt / Win keys (smart adaptive contrast by default, white on dark, black on light).')}
            value={settings.modifierTextColor || 'auto'}
            defaultCustomFallback="#ffffff"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('modifierTextColor', val)}
          />
        </div>
      </SettingGroup>

      {/* ── 底部即刻体验栏 ──────────────────────────────────────────── */}
      <div className="keycast-page__action-footer">
        <Button variant="primary" onClick={handleTestKeycast}>
          <Play size={14} style={{ marginRight: 6 }} />
          <span>{t('keycast.testKeycast', 'Try Keycast Now')}</span>
        </Button>
      </div>
    </div>
  );
};
