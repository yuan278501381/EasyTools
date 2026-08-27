/* ─────────────────────────────────────────────────────────────────────────────
 * KeycastPage.tsx — 按键回显与屏幕演示配置中心 (世界级 UX & 2D 时序流)
 * ───────────────────────────────────────────────────────────────────────────── */

import { useState, useEffect, type FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Toggle, SettingGroup, Button } from '../components/UIKit';
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
  includeFunctionKeys: true,
  position: 'bottom_left',
  mergeRecentKeys: true,
  mergeTimeoutMs: 1200,
  displayDurationMs: 2500,
  fontSize: 20,
  textColor: '#ffffff',
  backgroundColor: '#1c1c22',
  modifierKeycapColor: 'auto',
  modifierKeycapOpacity: 22,
  modifierTextColor: 'auto',
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
          description={t('keycast.mainToggleDesc', '在屏幕上呈现高质感晶体按键胶囊回显。')}
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

      {/* ── 2. 屏幕显示位置 ─────────────────────────────────────────── */}
      <SettingGroup title={t('keycast.positionSection', '屏幕出现位置')} icon={<LayoutGrid size={18} />}>
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
      <SettingGroup title={t('keycast.filterSection', '按键过滤与回显策略')} icon={<Filter size={18} />}>
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
            label={t("keycast.includeFunctionKeys" as any, "包含独立功能键")}
            description={t("keycast.includeFunctionKeysDesc" as any, "包含单独按下的 Space (空格)、Backspace (退格)、Delete (删除)、Enter、Tab、方向键及 F1~F12 等；关闭后仅在作为组合快捷键时回显")}
            checked={settings.includeFunctionKeys}
            onChange={(v) => saveSetting("includeFunctionKeys", v)}
          />
        </Card>
      </SettingGroup>

      {/* ── 4. 时序流与物理动效 ─────────────────────────────────────── */}
      <SettingGroup title={t('keycast.motionSection', '时序流与物理动效')} icon={<Layers size={18} />}>
        <Card>
          <Toggle
            id="keycast-merge-recent"
            label={t('keycast.mergeRecentKeys', '连续按键同排横向追加')}
            description={t('keycast.mergeRecentKeysDesc', '短时间内连续按键在同一排依次推入/冒出；达到屏幕中线或停顿后自动换行。')}
            checked={settings.mergeRecentKeys}
            onChange={(v) => saveSetting('mergeRecentKeys', v)}
          />
          <Toggle
            id="keycast-row-cascade"
            label={t('keycast.rowCascadeAnim', '新行换行时旧行级联上推')}
            description={t('keycast.rowCascadeAnimDesc', '开启后新行从下方跃入，上方旧行伴随物理推力优雅上浮，呈现层级机械质感')}
            checked={settings.rowCascadeAnim}
            onChange={(v) => saveSetting('rowCascadeAnim', v)}
          />
          <Toggle
            id="keycast-exit-drift"
            label={t('keycast.exitDriftAnim', '按键消融时轻盈飘升')}
            description={t('keycast.exitDriftAnimDesc', '停留寿命结束时伴随微幅轻盈向上飘升消融，消除生硬的瞬间闪退')}
            checked={settings.exitDriftAnim}
            onChange={(v) => saveSetting('exitDriftAnim', v)}
          />
        </Card>

        {/* 属性网格：首键动效、后续按键动效、显示时长、合并间隔、文字大小、颜色 */}
        <div className="keycast-page__grid" style={{ marginTop: '16px' }}>
          {/* 首键进场动效 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.firstKeyAnim', '每排首键进场')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.firstKeyAnimDesc', '新排首个按键的物理进场方式')}</span>
            </div>
            <div className="keycast-page__capsule-wrap" style={{ marginTop: 8 }}>
              {([
                { id: 'slide', label: '阻尼推入' },
                { id: 'pop', label: '弹性冒出' },
                { id: 'fade', label: '渐现' },
                { id: 'none', label: '静态' },
              ] as const).map((opt) => (
                <button
                  key={opt.id}
                  type="button"
                  className={`keycast-page__capsule-btn ${settings.firstKeyAnim === opt.id ? 'active' : ''}`}
                  onClick={() => saveSetting('firstKeyAnim', opt.id)}
                >
                  {opt.label}
                </button>
              ))}
            </div>
          </div>

          {/* 同排后续按键动效 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.subsequentKeyAnim', '同排后续按键')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.subsequentKeyAnimDesc', '同行连续输入的按键出现方式')}</span>
            </div>
            <div className="keycast-page__capsule-wrap" style={{ marginTop: 8 }}>
              {([
                { id: 'pop', label: '气泡冒出' },
                { id: 'slide', label: '阻尼推入' },
                { id: 'fade', label: '渐现' },
                { id: 'none', label: '静态' },
              ] as const).map((opt) => (
                <button
                  key={opt.id}
                  type="button"
                  className={`keycast-page__capsule-btn ${settings.subsequentKeyAnim === opt.id ? 'active' : ''}`}
                  onClick={() => saveSetting('subsequentKeyAnim', opt.id)}
                >
                  {opt.label}
                </button>
              ))}
            </div>
          </div>

          {/* 同排连击合并间隔 */}
          {settings.mergeRecentKeys && (
            <div className="keycast-page__prop-card">
              <div className="keycast-page__prop-header">
                <span className="keycast-page__prop-title">{t('keycast.mergeTimeout', '同排合并间隔')}</span>
                <span className="keycast-page__prop-desc">{t('keycast.mergeTimeoutDesc', '判定连续击键在同一排推入的时间窗口 (ms)。')}</span>
              </div>
              <div className="keycast-page__prop-body">
                <input
                  type="number"
                  className="keycast-page__number-input"
                  min={300}
                  max={5000}
                  step={100}
                  value={settings.mergeTimeoutMs || 1200}
                  onChange={(e) => saveSetting('mergeTimeoutMs', Number(e.target.value) || 1200)}
                  aria-label={t('keycast.mergeTimeout', '同排合并间隔')}
                />
              </div>
            </div>
          )}

          {/* 显示时长 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.displayDuration', '停留时长')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.displayDurationDesc', '按键胶囊在屏幕上的停留时间 (ms)。')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <input
                type="number"
                className="keycast-page__number-input"
                min={500}
                max={10000}
                step={500}
                value={settings.displayDurationMs}
                onChange={(e) => saveSetting('displayDurationMs', Number(e.target.value) || 2500)}
                aria-label={t('keycast.displayDuration', '停留时长')}
              />
            </div>
          </div>

          {/* 文字大小 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.fontSize', '键帽字号')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.fontSizeDesc', '回显按键的字体大小 (px)。')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <input
                type="number"
                className="keycast-page__number-input"
                min={12}
                max={36}
                step={2}
                value={settings.fontSize}
                onChange={(e) => saveSetting('fontSize', Number(e.target.value) || 20)}
                aria-label={t('keycast.fontSize', '键帽字号')}
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
            label={t('keycast.backgroundColor', '胶囊背景色')}
            desc={t('keycast.backgroundColorDesc', '按键胶囊底座的颜色。')}
            value={settings.backgroundColor}
            defaultCustomFallback="#1c1c22"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('backgroundColor', val)}
          />

          {/* 修饰键底色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.modifierKeycapColor', '修饰键底色')}
            desc={t('keycast.modifierKeycapColorDesc', 'Ctrl / Alt / Win 等按键底座背景颜色（默认跟随品牌色）。')}
            value={settings.modifierKeycapColor || 'auto'}
            defaultCustomFallback="#3b82f6"
            brandAccentHex={currentBrandAccentHex}
            onChange={(val) => saveSetting('modifierKeycapColor', val)}
          />

          {/* 修饰键底色不透明度 */}
          <div className="keycast-page__prop-card">
            <div className="keycast-page__prop-header">
              <span className="keycast-page__prop-title">{t('keycast.modifierKeycapOpacity', '修饰键底色不透明度')}</span>
              <span className="keycast-page__prop-desc">{t('keycast.modifierKeycapOpacityDesc', '按键底色不透明度 (0%~100%，仅影响底色，按键文字始终清晰显示)。')}</span>
            </div>
            <div className="keycast-page__prop-body">
              <input
                type="number"
                className="keycast-page__number-input"
                min={0}
                max={100}
                step={2}
                value={settings.modifierKeycapOpacity ?? 22}
                onChange={(e) => saveSetting('modifierKeycapOpacity', Math.max(0, Math.min(100, Number(e.target.value) || 22)))}
                aria-label={t('keycast.modifierKeycapOpacity', '修饰键底色不透明度')}
              />
            </div>
          </div>

          {/* 修饰键文字颜色 (双态胶囊) */}
          <ColorSegmentControl
            label={t('keycast.modifierTextColor', '修饰键文字颜色')}
            desc={t('keycast.modifierTextColorDesc', 'Ctrl / Alt / Win 等按键文字与徽标颜色（默认智能自适应底色明暗，暗底纯白、亮底深黑）。')}
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
          <span>{t('keycast.testKeycast', '立即体验按键回显')}</span>
        </Button>
      </div>
    </div>
  );
};
